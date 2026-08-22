#include "Keire/Vfx/VfxSubgraph.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool IsSubgraphBlock(const VfxGraphBlock& block) noexcept
        {
            return block.TypeId.View() == "keire.block.subgraph";
        }

        [[nodiscard]] bool IsSubgraphNode(const VfxGraphNode& node) noexcept
        {
            return node.Kind == VfxGraphNodeKind::Subgraph;
        }

        [[nodiscard]] std::uint64_t Mix64(std::uint64_t value) noexcept
        {
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31U);
        }

        void CollectIds(const VfxEffectDefinition& definition, std::set<AssetId>& used)
        {
            used.insert(definition.EmitterId);
            for (const auto& module : definition.Modules)
                used.insert(module.Id);
            for (const auto& parameter : definition.Blackboard)
                used.insert(parameter.Id);
            for (const auto& system : definition.Systems)
            {
                used.insert(system.Id);
                for (const auto& node : system.Nodes)
                {
                    used.insert(node.Id);
                    for (const auto& pin : node.Pins)
                        used.insert(pin.Id);
                    for (const auto& block : node.Blocks)
                    {
                        used.insert(block.Id);
                        for (const auto& pin : block.Pins)
                            used.insert(pin.Id);
                    }
                }
                for (const auto& connection : system.Connections)
                    used.insert(connection.Id);
            }
            used.erase(AssetId{});
        }

        class Expansion final
        {
          public:
            Expansion(const VfxSubgraphResolver& resolver, const std::size_t maximumDepth)
                : m_Resolver(resolver), m_MaximumDepth(maximumDepth)
            {
                if (!m_Resolver)
                    throw std::invalid_argument("VFX Subgraph expansion requires a resolver.");
                if (m_MaximumDepth == 0 || m_MaximumDepth > 64)
                    throw std::invalid_argument("VFX Subgraph expansion depth must be in the range 1..64.");
            }

            [[nodiscard]] VfxEffectDefinition Run(const VfxEffectDefinition& source)
            {
                auto result = source;
                CollectIds(result, m_Used);
                ExpandEffect(result);
                if (HasVfxSubgraphCalls(result))
                    throw std::logic_error("VFX Subgraph expansion left an unresolved call.");
                ValidateVfxEffect(result);
                return result;
            }

          private:
            struct CloneMap final
            {
                std::map<AssetId, AssetId> Ids;

                [[nodiscard]] AssetId Get(const AssetId value) const
                {
                    if (!value)
                        return {};
                    const auto found = Ids.find(value);
                    return found == Ids.end() ? value : found->second;
                }
            };

            [[nodiscard]] AssetId Allocate(const AssetId call, const AssetId source, std::uint64_t salt)
            {
                for (;; ++salt)
                {
                    const AssetId candidate(Mix64(call.High() ^ source.High() ^ salt),
                                            Mix64(call.Low() ^ source.Low() ^ (salt << 1U)));
                    if (candidate && m_Used.insert(candidate).second)
                        return candidate;
                }
            }

            [[nodiscard]] Ref<const VfxSubgraphAsset> Resolve(const AssetId reference)
            {
                if (!reference)
                    throw std::invalid_argument("VFX Subgraph call has no asset reference.");
                if (std::ranges::find(m_Stack, reference) != m_Stack.end())
                    throw std::invalid_argument("VFX Subgraph dependency graph contains an indirect cycle at " +
                                                reference.ToString() + ".");
                if (m_Stack.size() >= m_MaximumDepth)
                    throw std::invalid_argument("VFX Subgraph expansion exceeds its configured nesting depth.");
                auto asset = m_Resolver(reference);
                if (!asset)
                    throw std::invalid_argument("VFX Subgraph dependency is unavailable: " + reference.ToString());
                if (asset->Definition().Id != reference)
                    throw std::invalid_argument("VFX Subgraph resolver returned an asset with the wrong identity.");
                return asset;
            }

            template <typename Callback> void WithResolved(const AssetId reference, Callback&& callback)
            {
                const auto asset = Resolve(reference);
                m_Stack.push_back(reference);
                try
                {
                    callback(asset->Definition());
                    m_Stack.pop_back();
                }
                catch (...)
                {
                    m_Stack.pop_back();
                    throw;
                }
            }

            [[nodiscard]] VfxEffectDefinition Body(const VfxSubgraphDefinition& definition)
            {
                VfxEffectDefinition body;
                body.SchemaVersion = CurrentVfxSchemaVersion;
                body.EmitterId = definition.Id;
                body.Name = definition.Name;
                body.ExecutionSource = VfxExecutionSource::Graph;
                body.CompatibilityMode = VfxCompatibilityMode::NativeSchema4;
                body.Modules = definition.Modules;
                body.Blackboard = definition.Parameters;
                body.Systems = {definition.Graph};
                CollectIds(body, m_Used);
                ExpandEffect(body);
                return body;
            }

            [[nodiscard]] std::string ParameterName(const std::string& source, const AssetId call) const
            {
                auto result = source + " @" + call.ToString().substr(24);
                if (result.size() > 256)
                    result.resize(256);
                return result;
            }

            void ClonePayload(const VfxEffectDefinition& source, VfxEffectDefinition& destination, const AssetId call,
                              CloneMap& map)
            {
                std::uint64_t salt = 1;
                for (const auto& module : source.Modules)
                    map.Ids.emplace(module.Id, Allocate(call, module.Id, salt++));
                for (const auto& parameter : source.Blackboard)
                    map.Ids.emplace(parameter.Id, Allocate(call, parameter.Id, salt++));
                for (const auto& module : source.Modules)
                {
                    auto clone = module;
                    clone.Id = map.Get(module.Id);
                    destination.Modules.push_back(std::move(clone));
                }
                for (const auto& parameter : source.Blackboard)
                {
                    auto clone = parameter;
                    clone.Id = map.Get(parameter.Id);
                    clone.Name = ParameterName(parameter.Name, call);
                    clone.Exposed = false;
                    destination.Blackboard.push_back(std::move(clone));
                }
            }

            void AllocateGraphIds(const VfxGraphSystem& source, const AssetId call, CloneMap& map,
                                  const VfxContextType* selectedContext = nullptr)
            {
                std::uint64_t salt = 0x1000;
                map.Ids.emplace(source.Id, Allocate(call, source.Id, salt++));
                for (const auto& node : source.Nodes)
                {
                    if (selectedContext && node.Kind == VfxGraphNodeKind::Context && node.Context != *selectedContext)
                        continue;
                    map.Ids.emplace(node.Id, Allocate(call, node.Id, salt++));
                    for (const auto& pin : node.Pins)
                        map.Ids.emplace(pin.Id, Allocate(call, pin.Id, salt++));
                    for (const auto& block : node.Blocks)
                    {
                        map.Ids.emplace(block.Id, Allocate(call, block.Id, salt++));
                        for (const auto& pin : block.Pins)
                            map.Ids.emplace(pin.Id, Allocate(call, pin.Id, salt++));
                    }
                }
                for (const auto& connection : source.Connections)
                    map.Ids.emplace(connection.Id, Allocate(call, connection.Id, salt++));
            }

            [[nodiscard]] VfxGraphNode CloneNode(const VfxGraphNode& source, const CloneMap& map) const
            {
                auto result = source;
                result.Id = map.Get(source.Id);
                result.Reference = map.Get(source.Reference);
                for (auto& pin : result.Pins)
                    pin.Id = map.Get(pin.Id);
                for (auto& pin : result.DynamicPinOrder)
                    pin = map.Get(pin);
                for (auto& block : result.Blocks)
                {
                    block.Id = map.Get(block.Id);
                    block.Reference = map.Get(block.Reference);
                    for (auto& pin : block.Pins)
                        pin.Id = map.Get(pin.Id);
                }
                return result;
            }

            [[nodiscard]] VfxGraphConnection CloneConnection(const VfxGraphConnection& source,
                                                             const CloneMap& map) const
            {
                auto result = source;
                result.Id = map.Get(source.Id);
                result.OutputNode = map.Get(source.OutputNode);
                result.OutputBlock = map.Get(source.OutputBlock);
                result.OutputPin = map.Get(source.OutputPin);
                result.InputNode = map.Get(source.InputNode);
                result.InputBlock = map.Get(source.InputBlock);
                result.InputPin = map.Get(source.InputPin);
                return result;
            }

            void RequireNodeSignature(const VfxGraphNode& call, const VfxSubgraphDefinition& definition) const
            {
                if (call.TypeId.View() != "keire.subgraph" || call.Reference != definition.Id ||
                    call.DefinitionVersion != 1 || !call.CustomHlsl.empty() || !call.Properties.empty() ||
                    !call.Blocks.empty() || call.Pins.size() != definition.Ports.size())
                {
                    throw std::invalid_argument("VFX Subgraph call node is not canonical.");
                }
                for (std::size_t index = 0; index < call.Pins.size(); ++index)
                {
                    const auto& pin = call.Pins[index];
                    const auto& port = definition.Ports[index];
                    if (pin.Name != port.Name || pin.Type != port.Type || pin.Input != port.Input)
                        throw std::invalid_argument("VFX Subgraph call node has a stale boundary signature.");
                }
            }

            void RequireBlockSignature(const VfxGraphBlock& call, const VfxSubgraphDefinition& definition) const
            {
                if (!IsSubgraphBlock(call) || call.Reference != definition.Id || call.DefinitionVersion != 1 ||
                    !call.Properties.empty() || call.Pins.size() != definition.Ports.size() ||
                    std::ranges::any_of(definition.Ports, [](const VfxSubgraphPort& port) { return !port.Input; }))
                {
                    throw std::invalid_argument("VFX Subgraph call Block is not canonical.");
                }
                for (std::size_t index = 0; index < call.Pins.size(); ++index)
                {
                    const auto& pin = call.Pins[index];
                    const auto& port = definition.Ports[index];
                    if (pin.Name != port.Name || pin.Type != port.Type || !pin.Input)
                        throw std::invalid_argument("VFX Subgraph call Block has a stale boundary signature.");
                }
            }

            [[nodiscard]] VfxGraphEndpoint PortEndpoint(const VfxSubgraphPort& port, const CloneMap& map) const
            {
                return {map.Get(port.Node), map.Get(port.Block), map.Get(port.Pin)};
            }

            void RemoveNodeAuthoring(VfxGraphSystem& system, const AssetId node)
            {
                std::erase_if(system.Authoring.NodeAnnotations,
                              [node](const GraphNodeAnnotation& annotation) { return annotation.Node == node; });
                for (auto& comment : system.Authoring.Comments)
                    std::erase(comment.Members, node);
            }

            void ExpandOperatorCall(VfxEffectDefinition& effect, VfxGraphSystem& system, const std::size_t nodeIndex,
                                    const VfxSubgraphDefinition& definition)
            {
                if (definition.Purpose != VfxSubgraphPurpose::Operator)
                    throw std::invalid_argument("A free-standing value call requires an Operator VFX Subgraph.");
                const auto call = system.Nodes[nodeIndex];
                RequireNodeSignature(call, definition);
                auto body = Body(definition);
                if (body.Systems.size() != 1)
                    throw std::invalid_argument("Operator VFX Subgraphs cannot expand additional systems.");
                CloneMap map;
                ClonePayload(body, effect, call.Id, map);
                AllocateGraphIds(body.Systems.front(), call.Id, map);

                for (auto& connection : system.Connections)
                {
                    if (connection.InputNode == call.Id)
                    {
                        const auto pin = std::ranges::find(call.Pins, connection.InputPin, &VfxGraphPin::Id);
                        if (pin == call.Pins.end())
                            throw std::logic_error("VFX Subgraph input call pin disappeared during expansion.");
                        const auto index = static_cast<std::size_t>(std::distance(call.Pins.begin(), pin));
                        const auto endpoint = PortEndpoint(definition.Ports[index], map);
                        connection.InputNode = endpoint.Node;
                        connection.InputBlock = endpoint.Block;
                        connection.InputPin = endpoint.Pin;
                    }
                    if (connection.OutputNode == call.Id)
                    {
                        const auto pin = std::ranges::find(call.Pins, connection.OutputPin, &VfxGraphPin::Id);
                        if (pin == call.Pins.end())
                            throw std::logic_error("VFX Subgraph output call pin disappeared during expansion.");
                        const auto index = static_cast<std::size_t>(std::distance(call.Pins.begin(), pin));
                        const auto endpoint = PortEndpoint(definition.Ports[index], map);
                        connection.OutputNode = endpoint.Node;
                        connection.OutputBlock = endpoint.Block;
                        connection.OutputPin = endpoint.Pin;
                    }
                }
                for (const auto& node : body.Systems.front().Nodes)
                    system.Nodes.push_back(CloneNode(node, map));
                for (const auto& connection : body.Systems.front().Connections)
                    system.Connections.push_back(CloneConnection(connection, map));
                RemoveNodeAuthoring(system, call.Id);
                system.Nodes.erase(system.Nodes.begin() + static_cast<std::ptrdiff_t>(nodeIndex));
            }

            void ExpandSystemCall(VfxEffectDefinition& effect, VfxGraphSystem& system, const std::size_t nodeIndex,
                                  const VfxSubgraphDefinition& definition,
                                  std::vector<VfxGraphSystem>& additionalSystems)
            {
                if (definition.Purpose != VfxSubgraphPurpose::System)
                    throw std::invalid_argument("System VFX Subgraph marker references the wrong purpose.");
                const auto call = system.Nodes[nodeIndex];
                RequireNodeSignature(call, definition);
                if (!call.Pins.empty() || !definition.Ports.empty() ||
                    std::ranges::any_of(
                        system.Connections, [&](const VfxGraphConnection& connection)
                        { return connection.InputNode == call.Id || connection.OutputNode == call.Id; }))
                {
                    throw std::invalid_argument("System VFX Subgraph markers cannot have boundary cables.");
                }
                auto body = Body(definition);
                CloneMap payloadMap;
                ClonePayload(body, effect, call.Id, payloadMap);
                for (const auto& sourceSystem : body.Systems)
                {
                    CloneMap map = payloadMap;
                    AllocateGraphIds(sourceSystem, call.Id, map);
                    VfxGraphSystem clone = sourceSystem;
                    clone.Id = map.Get(sourceSystem.Id);
                    clone.Authoring = {};
                    clone.Nodes.clear();
                    clone.Connections.clear();
                    for (const auto& node : sourceSystem.Nodes)
                        clone.Nodes.push_back(CloneNode(node, map));
                    for (const auto& connection : sourceSystem.Connections)
                        clone.Connections.push_back(CloneConnection(connection, map));
                    additionalSystems.push_back(std::move(clone));
                }
                RemoveNodeAuthoring(system, call.Id);
                system.Nodes.erase(system.Nodes.begin() + static_cast<std::ptrdiff_t>(nodeIndex));
            }

            void ExpandBlockCall(VfxEffectDefinition& effect, VfxGraphSystem& system, VfxGraphNode& context,
                                 const std::size_t blockIndex, const VfxSubgraphDefinition& definition)
            {
                if (definition.Purpose != VfxSubgraphPurpose::Block ||
                    std::ranges::find(definition.ValidContexts, context.Context) == definition.ValidContexts.end())
                {
                    throw std::invalid_argument("Block VFX Subgraph is incompatible with its owning Context.");
                }
                const auto call = context.Blocks[blockIndex];
                RequireBlockSignature(call, definition);
                auto body = Body(definition);
                if (body.Systems.size() != 1)
                    throw std::invalid_argument("Block VFX Subgraphs cannot expand additional systems.");
                const auto& sourceSystem = body.Systems.front();
                const auto sourceContext = std::ranges::find_if(
                    sourceSystem.Nodes, [&](const VfxGraphNode& node)
                    { return node.Kind == VfxGraphNodeKind::Context && node.Context == context.Context; });
                if (sourceContext == sourceSystem.Nodes.end())
                    throw std::invalid_argument("Block VFX Subgraph has no body for its owning Context.");

                CloneMap map;
                ClonePayload(body, effect, call.Id, map);
                AllocateGraphIds(sourceSystem, call.Id, map, &context.Context);
                map.Ids[sourceContext->Id] = context.Id;
                for (auto& connection : system.Connections)
                {
                    if (connection.InputNode == context.Id && connection.InputBlock == call.Id)
                    {
                        const auto pin = std::ranges::find(call.Pins, connection.InputPin, &VfxGraphPin::Id);
                        if (pin == call.Pins.end())
                            throw std::logic_error("VFX Subgraph Block input disappeared during expansion.");
                        const auto index = static_cast<std::size_t>(std::distance(call.Pins.begin(), pin));
                        const auto endpoint = PortEndpoint(definition.Ports[index], map);
                        connection.InputNode = endpoint.Node;
                        connection.InputBlock = endpoint.Block;
                        connection.InputPin = endpoint.Pin;
                    }
                }
                const auto clonedContext = CloneNode(*sourceContext, map);
                std::vector<VfxGraphNode> clonedNodes;
                for (const auto& sourceNode : sourceSystem.Nodes)
                    if (sourceNode.Kind != VfxGraphNodeKind::Context)
                        clonedNodes.push_back(CloneNode(sourceNode, map));
                for (const auto& connection : sourceSystem.Connections)
                {
                    const auto cloned = CloneConnection(connection, map);
                    if (map.Ids.contains(connection.OutputPin) && map.Ids.contains(connection.InputPin) &&
                        cloned.OutputPin && cloned.InputPin)
                    {
                        system.Connections.push_back(cloned);
                    }
                }
                auto insertion = context.Blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex);
                insertion = context.Blocks.erase(insertion);
                for (const auto& block : clonedContext.Blocks)
                {
                    insertion = context.Blocks.insert(insertion, block) + 1;
                }
                for (auto& node : clonedNodes)
                    system.Nodes.push_back(std::move(node));
            }

            void ExpandEffect(VfxEffectDefinition& effect)
            {
                std::vector<VfxGraphSystem> additionalSystems;
                for (auto& system : effect.Systems)
                {
                    for (std::size_t nodeIndex = 0; nodeIndex < system.Nodes.size();)
                    {
                        if (!IsSubgraphNode(system.Nodes[nodeIndex]))
                        {
                            ++nodeIndex;
                            continue;
                        }
                        const auto reference = system.Nodes[nodeIndex].Reference;
                        bool removed = false;
                        WithResolved(reference,
                                     [&](const VfxSubgraphDefinition& definition)
                                     {
                                         if (definition.Purpose == VfxSubgraphPurpose::System)
                                             ExpandSystemCall(effect, system, nodeIndex, definition, additionalSystems);
                                         else
                                             ExpandOperatorCall(effect, system, nodeIndex, definition);
                                         removed = true;
                                     });
                        if (!removed)
                            ++nodeIndex;
                    }
                    for (std::size_t nodeIndex = 0; nodeIndex < system.Nodes.size(); ++nodeIndex)
                    {
                        if (system.Nodes[nodeIndex].Kind != VfxGraphNodeKind::Context)
                            continue;
                        for (std::size_t blockIndex = 0; blockIndex < system.Nodes[nodeIndex].Blocks.size();)
                        {
                            if (!IsSubgraphBlock(system.Nodes[nodeIndex].Blocks[blockIndex]))
                            {
                                ++blockIndex;
                                continue;
                            }
                            const auto reference = system.Nodes[nodeIndex].Blocks[blockIndex].Reference;
                            WithResolved(
                                reference, [&](const VfxSubgraphDefinition& definition)
                                { ExpandBlockCall(effect, system, system.Nodes[nodeIndex], blockIndex, definition); });
                        }
                    }
                }
                for (auto& system : additionalSystems)
                    effect.Systems.push_back(std::move(system));
            }

            const VfxSubgraphResolver& m_Resolver;
            std::size_t m_MaximumDepth = MaximumVfxSubgraphExpansionDepth;
            std::vector<AssetId> m_Stack;
            std::set<AssetId> m_Used;
        };
    } // namespace

    bool HasVfxSubgraphCalls(const VfxEffectDefinition& definition) noexcept
    {
        return std::ranges::any_of(
            definition.Systems,
            [](const VfxGraphSystem& system)
            {
                return std::ranges::any_of(
                    system.Nodes, [](const VfxGraphNode& node)
                    { return IsSubgraphNode(node) || std::ranges::any_of(node.Blocks, IsSubgraphBlock); });
            });
    }

    VfxEffectDefinition ExpandVfxSubgraphs(const VfxEffectDefinition& definition, const VfxSubgraphResolver& resolver,
                                           const std::size_t maximumDepth)
    {
        if (!HasVfxSubgraphCalls(definition))
            return definition;
        return Expansion(resolver, maximumDepth).Run(definition);
    }
} // namespace Keire
