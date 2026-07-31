#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::VfxGraphSystem& RequireSystem(Keire::VfxEffectDefinition& definition,
                                                           const Keire::AssetId system)
        {
            const auto found = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
            if (found == definition.Systems.end())
                throw std::invalid_argument("VFX graph system is unavailable.");
            return *found;
        }

        [[nodiscard]] Keire::VfxGraphNode& RequireNode(Keire::VfxGraphSystem& system, const Keire::AssetId node)
        {
            const auto found = std::ranges::find(system.Nodes, node, &Keire::VfxGraphNode::Id);
            if (found == system.Nodes.end())
                throw std::invalid_argument("VFX graph node is unavailable.");
            return *found;
        }

        [[nodiscard]] Keire::VfxGraphPin& RequirePin(Keire::VfxGraphNode& node, const Keire::AssetId pin)
        {
            const auto found = std::ranges::find(node.Pins, pin, &Keire::VfxGraphPin::Id);
            if (found == node.Pins.end())
                throw std::invalid_argument("VFX graph pin is unavailable.");
            return *found;
        }

        template <typename Values> [[nodiscard]] std::vector<Keire::AssetId> SortedIds(const Values& values)
        {
            std::vector<Keire::AssetId> result;
            result.reserve(values.size());
            for (const auto& value : values)
                result.push_back(value.Id);
            std::ranges::sort(result);
            return result;
        }

        void RequireStableNodeIds(const Keire::VfxGraphNode& before, const Keire::VfxGraphNode& after)
        {
            if (after.Id != before.Id || SortedIds(after.Pins) != SortedIds(before.Pins))
                throw std::invalid_argument("VFX graph node edits cannot replace stable node or pin IDs.");
        }

        void RequireStableSystemIds(const Keire::VfxGraphSystem& before, const Keire::VfxGraphSystem& after)
        {
            if (after.Id != before.Id || SortedIds(after.Nodes) != SortedIds(before.Nodes) ||
                SortedIds(after.Connections) != SortedIds(before.Connections))
            {
                throw std::invalid_argument("VFX graph system edits cannot replace stable graph IDs.");
            }
            for (const auto& node : before.Nodes)
            {
                const auto found = std::ranges::find(after.Nodes, node.Id, &Keire::VfxGraphNode::Id);
                if (found == after.Nodes.end())
                    throw std::invalid_argument("VFX graph system edits cannot replace stable graph IDs.");
                RequireStableNodeIds(node, *found);
            }
        }

        [[nodiscard]] const Keire::VfxGraphPin*
        FindPin(const Keire::VfxGraphSystem& system, const Keire::AssetId nodeId, const Keire::AssetId pinId) noexcept
        {
            const auto node = std::ranges::find(system.Nodes, nodeId, &Keire::VfxGraphNode::Id);
            if (node == system.Nodes.end())
                return nullptr;
            const auto pin = std::ranges::find(node->Pins, pinId, &Keire::VfxGraphPin::Id);
            return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
        }

        [[nodiscard]] std::uint8_t ContextOrder(const Keire::VfxContextType context) noexcept
        {
            switch (context)
            {
            case Keire::VfxContextType::Spawn:
                return 0;
            case Keire::VfxContextType::Initialize:
                return 1;
            case Keire::VfxContextType::Update:
                return 2;
            case Keire::VfxContextType::Output:
                return 3;
            case Keire::VfxContextType::Event:
                return 4;
            }
            return 4;
        }

        [[nodiscard]] const Keire::VfxGraphNode* FindNode(const Keire::VfxGraphSystem& system,
                                                          const Keire::AssetId node) noexcept
        {
            const auto found = std::ranges::find(system.Nodes, node, &Keire::VfxGraphNode::Id);
            return found == system.Nodes.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const Keire::VfxGraphPin* FindFlowPin(const Keire::VfxGraphNode& node, const bool input) noexcept
        {
            const auto found =
                std::ranges::find_if(node.Pins, [input](const Keire::VfxGraphPin& pin)
                                     { return pin.Input == input && pin.Type == Keire::VfxValueType::ParticleStream; });
            return found == node.Pins.end() ? nullptr : std::addressof(*found);
        }

        void InsertExecutableNode(Keire::VfxGraphSystem& system, Keire::VfxGraphNode node)
        {
            const auto* nodeInput = FindFlowPin(node, true);
            const auto* nodeOutput = FindFlowPin(node, false);
            if (!nodeInput || !nodeOutput)
                throw std::invalid_argument("Executable VFX nodes require particle-stream input and output pins.");

            std::vector<std::size_t> insertionCandidates;
            for (std::size_t index = 0; index < system.Connections.size(); ++index)
            {
                const auto& connection = system.Connections[index];
                const auto* source = FindNode(system, connection.OutputNode);
                const auto* target = FindNode(system, connection.InputNode);
                const auto* sourcePin = FindPin(system, connection.OutputNode, connection.OutputPin);
                const auto* targetPin = FindPin(system, connection.InputNode, connection.InputPin);
                if (!source || !target || !sourcePin || !targetPin ||
                    sourcePin->Type != Keire::VfxValueType::ParticleStream ||
                    targetPin->Type != Keire::VfxValueType::ParticleStream)
                {
                    continue;
                }

                const bool crossesNextContext = ContextOrder(source->Context) == ContextOrder(node.Context) &&
                                                ContextOrder(target->Context) > ContextOrder(node.Context);
                const bool entersOutputContext = node.Context == Keire::VfxContextType::Output &&
                                                 target->Kind == Keire::VfxGraphNodeKind::Context &&
                                                 target->Context == Keire::VfxContextType::Output;
                if (crossesNextContext || entersOutputContext)
                    insertionCandidates.push_back(index);
            }
            if (insertionCandidates.size() != 1)
            {
                throw std::invalid_argument(
                    "VFX node insertion requires one unambiguous particle-stream cable at its context boundary.");
            }

            auto& insertion = system.Connections[insertionCandidates.front()];
            const auto targetNode = insertion.InputNode;
            const auto targetPin = insertion.InputPin;
            insertion.InputNode = node.Id;
            insertion.InputPin = nodeInput->Id;
            system.Connections.push_back({Keire::AssetId::Generate(), node.Id, nodeOutput->Id, targetNode, targetPin});
            system.Nodes.push_back(std::move(node));
        }

        void RemoveNodeAndReconnect(Keire::VfxGraphSystem& system, const Keire::AssetId nodeId,
                                    const bool requireExecutableGraph)
        {
            const auto found = std::ranges::find(system.Nodes, nodeId, &Keire::VfxGraphNode::Id);
            if (found == system.Nodes.end())
                throw std::invalid_argument("VFX graph node is unavailable.");

            const bool executable =
                found->Kind == Keire::VfxGraphNodeKind::Module || found->Kind == Keire::VfxGraphNodeKind::CustomHlsl;
            if (requireExecutableGraph && found->Kind == Keire::VfxGraphNodeKind::Context)
                throw std::invalid_argument("Executable VFX context nodes cannot be deleted.");

            if (requireExecutableGraph && executable)
            {
                const auto* input = FindFlowPin(*found, true);
                const auto* output = FindFlowPin(*found, false);
                if (!input || !output)
                    throw std::invalid_argument("Executable VFX node flow pins are malformed.");

                std::vector<std::size_t> incoming;
                std::vector<std::size_t> outgoing;
                for (std::size_t index = 0; index < system.Connections.size(); ++index)
                {
                    const auto& connection = system.Connections[index];
                    if (connection.InputNode == nodeId && connection.InputPin == input->Id)
                        incoming.push_back(index);
                    if (connection.OutputNode == nodeId && connection.OutputPin == output->Id)
                        outgoing.push_back(index);
                }
                if (incoming.size() != 1 || outgoing.size() != 1)
                {
                    throw std::invalid_argument(
                        "Deleting this VFX node requires one incoming and one outgoing particle-stream cable.");
                }

                auto& bridge = system.Connections[incoming.front()];
                const auto& destination = system.Connections[outgoing.front()];
                bridge.InputNode = destination.InputNode;
                bridge.InputPin = destination.InputPin;
            }

            std::erase_if(system.Connections, [nodeId](const Keire::VfxGraphConnection& connection)
                          { return connection.OutputNode == nodeId || connection.InputNode == nodeId; });
            system.Nodes.erase(found);
        }

        void SynchronizeModuleNodes(Keire::VfxEffectDefinition& definition, const Keire::VfxModuleDefinition& module)
        {
            const auto canonical = Keire::CreateVfxGraphModuleNode(module, {});
            for (auto& system : definition.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    if (node.Kind != Keire::VfxGraphNodeKind::Module || node.Reference != module.Id)
                        continue;

                    const bool topologyMatches =
                        node.Pins.size() == canonical.Pins.size() &&
                        std::ranges::all_of(canonical.Pins,
                                            [&](const Keire::VfxGraphPin& canonicalPin)
                                            {
                                                return std::ranges::count_if(
                                                           node.Pins,
                                                           [&](const Keire::VfxGraphPin& pin)
                                                           {
                                                               return pin.Input == canonicalPin.Input &&
                                                                      pin.Type == canonicalPin.Type &&
                                                                      pin.Semantic == canonicalPin.Semantic;
                                                           }) == 1;
                                            });
                    if (!topologyMatches)
                    {
                        std::erase_if(system.Connections,
                                      [nodeId = node.Id](const Keire::VfxGraphConnection& connection)
                                      { return connection.OutputNode == nodeId || connection.InputNode == nodeId; });
                        auto replacement = Keire::CreateVfxGraphModuleNode(module, node.EditorPosition);
                        replacement.Id = node.Id;
                        node = std::move(replacement);
                        continue;
                    }

                    node.Type = canonical.Type;
                    node.Context = canonical.Context;
                    node.CustomHlsl.clear();
                    for (const auto& canonicalPin : canonical.Pins)
                    {
                        const auto pin = std::ranges::find_if(node.Pins,
                                                              [&](const Keire::VfxGraphPin& candidate)
                                                              {
                                                                  return candidate.Input == canonicalPin.Input &&
                                                                         candidate.Type == canonicalPin.Type &&
                                                                         candidate.Semantic == canonicalPin.Semantic;
                                                              });
                        pin->Name = canonicalPin.Name;
                        pin->DefaultValue = canonicalPin.DefaultValue;
                    }
                }
            }
        }

        void RemoveReferencedNodes(Keire::VfxEffectDefinition& definition, const Keire::VfxGraphNodeKind kind,
                                   const Keire::AssetId reference)
        {
            for (auto& system : definition.Systems)
            {
                std::vector<Keire::AssetId> removed;
                for (const auto& node : system.Nodes)
                    if (node.Kind == kind && node.Reference == reference)
                        removed.push_back(node.Id);
                if (removed.empty())
                    continue;

                for (const auto node : removed)
                    RemoveNodeAndReconnect(system, node,
                                           definition.ExecutionSource == Keire::VfxExecutionSource::Graph);
            }
        }
    } // namespace

    VfxEffectDocument::VfxEffectDocument(VfxEffectDocumentSpecification specification)
        : m_Host(
              {.Validate = [](const Keire::VfxEffectDefinition& definition) { Keire::ValidateVfxEffect(definition); },
               .Encode = [](const Keire::VfxEffectDefinition& definition)
               { return Keire::VfxEffectAsset::Encode(definition); },
               .Preview = std::move(specification.Preview),
               .CancelPreview = std::move(specification.StopPreview),
               .Persist = std::move(specification.Persist)})
    {
    }

    void VfxEffectDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                 const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        Open(asset, Keire::VfxEffectAsset::Decode(bytes)->Definition(), revision, std::move(undo));
    }

    void VfxEffectDocument::Open(const Keire::AssetId asset, Keire::VfxEffectDefinition definition,
                                 const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
    }

    void VfxEffectDocument::Create(const Keire::AssetId asset, Keire::VfxEffectDefinition definition,
                                   Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Create(asset, std::move(definition), std::move(undo));
    }

    void VfxEffectDocument::Save() { m_Host.Save(); }

    void VfxEffectDocument::Discard() { m_Host.Discard(); }

    AssetDocumentReloadResult VfxEffectDocument::Reload(const std::span<const std::byte> bytes,
                                                        const std::uint64_t revision)
    {
        return Reload(Keire::VfxEffectAsset::Decode(bytes)->Definition(), revision);
    }

    AssetDocumentReloadResult VfxEffectDocument::Reload(Keire::VfxEffectDefinition definition,
                                                        const std::uint64_t revision)
    {
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        return m_Host.Reload(std::move(definition), revision);
    }

    bool VfxEffectDocument::Undo() { return m_Host.Undo(); }

    bool VfxEffectDocument::Redo() { return m_Host.Redo(); }

    void VfxEffectDocument::Close() noexcept { m_Host.Close(); }

    bool VfxEffectDocument::Edit(const std::string_view name,
                                 const std::function<void(Keire::VfxEffectDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX effect edits require an operation.");
        auto candidate = m_Host.Draft();
        operation(candidate);
        return m_Host.Edit(name, std::move(candidate));
    }

    bool VfxEffectDocument::ConvertToGraph()
    {
        if (m_Host.Draft().ExecutionSource == Keire::VfxExecutionSource::Graph)
            return false;
        return m_Host.Edit("Convert Runtime Modules to Graph", Keire::ConvertVfxEffectToGraph(m_Host.Draft()));
    }

    bool VfxEffectDocument::AddModule(Keire::VfxModuleDefinition module)
    {
        return Edit("Add VFX module", [module = std::move(module)](Keire::VfxEffectDefinition& definition) mutable
                    { definition.Modules.push_back(std::move(module)); });
    }

    bool VfxEffectDocument::EditModule(const Keire::AssetId module,
                                       const std::function<void(Keire::VfxModuleDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX module edits require an operation.");
        return Edit("Edit VFX module",
                    [module, operation](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                        if (found == definition.Modules.end())
                            throw std::invalid_argument("VFX module is unavailable.");
                        operation(*found);
                        if (found->Id != module)
                            throw std::invalid_argument("VFX module edits cannot replace the stable ID.");
                        SynchronizeModuleNodes(definition, *found);
                    });
    }

    bool VfxEffectDocument::RemoveModule(const Keire::AssetId module)
    {
        return Edit("Remove VFX module",
                    [module](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                        if (found == definition.Modules.end())
                            throw std::invalid_argument("VFX module is unavailable.");
                        definition.Modules.erase(found);
                        RemoveReferencedNodes(definition, Keire::VfxGraphNodeKind::Module, module);
                    });
    }

    bool VfxEffectDocument::MoveModule(const Keire::AssetId module, const std::size_t destination)
    {
        return Edit(
            "Reorder VFX module",
            [module, destination](Keire::VfxEffectDefinition& definition)
            {
                if (destination >= definition.Modules.size())
                    throw std::invalid_argument("VFX module destination is out of range.");
                const auto found = std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                if (found == definition.Modules.end())
                    throw std::invalid_argument("VFX module is unavailable.");
                const auto source = static_cast<std::size_t>(std::distance(definition.Modules.begin(), found));
                if (source == destination)
                    return;
                auto moved = std::move(*found);
                definition.Modules.erase(found);
                definition.Modules.insert(
                    std::next(definition.Modules.begin(), static_cast<std::ptrdiff_t>(destination)), std::move(moved));
            });
    }

    bool VfxEffectDocument::AddSystem(Keire::VfxGraphSystem system)
    {
        return Edit("Add VFX graph system", [system = std::move(system)](Keire::VfxEffectDefinition& definition) mutable
                    { definition.Systems.push_back(std::move(system)); });
    }

    bool VfxEffectDocument::EditSystem(const Keire::AssetId system,
                                       const std::function<void(Keire::VfxGraphSystem&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX graph system edits require an operation.");
        return Edit("Edit VFX graph system",
                    [system, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& found = RequireSystem(definition, system);
                        const auto before = found;
                        operation(found);
                        RequireStableSystemIds(before, found);
                    });
    }

    bool VfxEffectDocument::RemoveSystem(const Keire::AssetId system)
    {
        return Edit("Remove VFX graph system",
                    [system](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
                        if (found == definition.Systems.end())
                            throw std::invalid_argument("VFX graph system is unavailable.");
                        definition.Systems.erase(found);
                    });
    }

    bool VfxEffectDocument::AddNode(const Keire::AssetId system, Keire::VfxGraphNode node)
    {
        return Edit("Add VFX graph node",
                    [system, node = std::move(node)](Keire::VfxEffectDefinition& definition) mutable
                    {
                        auto& graph = RequireSystem(definition, system);
                        const bool executable = node.Kind == Keire::VfxGraphNodeKind::Module ||
                                                node.Kind == Keire::VfxGraphNodeKind::CustomHlsl;
                        if (definition.ExecutionSource == Keire::VfxExecutionSource::Graph && executable)
                            InsertExecutableNode(graph, std::move(node));
                        else
                            graph.Nodes.push_back(std::move(node));
                    });
    }

    bool VfxEffectDocument::EditNode(const Keire::AssetId system, const Keire::AssetId node,
                                     const std::function<void(Keire::VfxGraphNode&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX graph node edits require an operation.");
        return Edit("Edit VFX graph node",
                    [system, node, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& found = RequireNode(RequireSystem(definition, system), node);
                        const auto before = found;
                        operation(found);
                        RequireStableNodeIds(before, found);
                    });
    }

    bool VfxEffectDocument::RemoveNode(const Keire::AssetId system, const Keire::AssetId node)
    {
        return Edit("Remove VFX graph node",
                    [system, node](Keire::VfxEffectDefinition& definition)
                    {
                        auto& graph = RequireSystem(definition, system);
                        RemoveNodeAndReconnect(graph, node,
                                               definition.ExecutionSource == Keire::VfxExecutionSource::Graph);
                    });
    }

    bool VfxEffectDocument::AddPin(const Keire::AssetId system, const Keire::AssetId node, Keire::VfxGraphPin pin)
    {
        return Edit("Add VFX graph pin",
                    [system, node, pin = std::move(pin)](Keire::VfxEffectDefinition& definition) mutable
                    { RequireNode(RequireSystem(definition, system), node).Pins.push_back(std::move(pin)); });
    }

    bool VfxEffectDocument::EditPin(const Keire::AssetId system, const Keire::AssetId node, const Keire::AssetId pin,
                                    const std::function<void(Keire::VfxGraphPin&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX graph pin edits require an operation.");
        return Edit("Edit VFX graph pin",
                    [system, node, pin, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& found = RequirePin(RequireNode(RequireSystem(definition, system), node), pin);
                        operation(found);
                        if (found.Id != pin)
                            throw std::invalid_argument("VFX graph pin edits cannot replace the stable ID.");
                    });
    }

    bool VfxEffectDocument::RemovePin(const Keire::AssetId system, const Keire::AssetId node, const Keire::AssetId pin)
    {
        return Edit("Remove VFX graph pin",
                    [system, node, pin](Keire::VfxEffectDefinition& definition)
                    {
                        auto& graph = RequireSystem(definition, system);
                        auto& pins = RequireNode(graph, node).Pins;
                        const auto found = std::ranges::find(pins, pin, &Keire::VfxGraphPin::Id);
                        if (found == pins.end())
                            throw std::invalid_argument("VFX graph pin is unavailable.");
                        std::erase_if(graph.Connections,
                                      [node, pin](const Keire::VfxGraphConnection& connection)
                                      {
                                          return (connection.OutputNode == node && connection.OutputPin == pin) ||
                                                 (connection.InputNode == node && connection.InputPin == pin);
                                      });
                        pins.erase(found);
                    });
    }

    bool VfxEffectDocument::AddConnection(const Keire::AssetId system, Keire::VfxGraphConnection connection)
    {
        return Edit(
            "Add VFX graph connection",
            [system, connection = std::move(connection)](Keire::VfxEffectDefinition& definition) mutable
            {
                auto& connections = RequireSystem(definition, system).Connections;
                if (std::ranges::find(connections, connection.Id, &Keire::VfxGraphConnection::Id) != connections.end())
                    throw std::invalid_argument("VFX graph connection stable ID is already in use.");
                std::erase_if(
                    connections, [&](const Keire::VfxGraphConnection& existing)
                    { return existing.InputNode == connection.InputNode && existing.InputPin == connection.InputPin; });
                connections.push_back(std::move(connection));
            });
    }

    bool VfxEffectDocument::EditConnection(const Keire::AssetId system, const Keire::AssetId connection,
                                           const std::function<void(Keire::VfxGraphConnection&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX graph connection edits require an operation.");
        return Edit("Edit VFX graph connection",
                    [system, connection, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& connections = RequireSystem(definition, system).Connections;
                        const auto found = std::ranges::find(connections, connection, &Keire::VfxGraphConnection::Id);
                        if (found == connections.end())
                            throw std::invalid_argument("VFX graph connection is unavailable.");
                        operation(*found);
                        if (found->Id != connection)
                            throw std::invalid_argument("VFX graph connection edits cannot replace the stable ID.");
                        const auto inputNode = found->InputNode;
                        const auto inputPin = found->InputPin;
                        std::erase_if(connections,
                                      [connection, inputNode, inputPin](const Keire::VfxGraphConnection& candidate)
                                      {
                                          return candidate.Id != connection && candidate.InputNode == inputNode &&
                                                 candidate.InputPin == inputPin;
                                      });
                    });
    }

    bool VfxEffectDocument::RemoveConnection(const Keire::AssetId system, const Keire::AssetId connection)
    {
        return Edit("Remove VFX graph connection",
                    [system, connection](Keire::VfxEffectDefinition& definition)
                    {
                        auto& connections = RequireSystem(definition, system).Connections;
                        const auto found = std::ranges::find(connections, connection, &Keire::VfxGraphConnection::Id);
                        if (found == connections.end())
                            throw std::invalid_argument("VFX graph connection is unavailable.");
                        connections.erase(found);
                    });
    }

    bool VfxEffectDocument::AddBlackboardParameter(Keire::VfxBlackboardParameter parameter)
    {
        return Edit("Add VFX blackboard parameter",
                    [parameter = std::move(parameter)](Keire::VfxEffectDefinition& definition) mutable
                    { definition.Blackboard.push_back(std::move(parameter)); });
    }

    bool
    VfxEffectDocument::EditBlackboardParameter(const Keire::AssetId parameter,
                                               const std::function<void(Keire::VfxBlackboardParameter&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX blackboard parameter edits require an operation.");
        return Edit("Edit VFX blackboard parameter",
                    [parameter, operation](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Blackboard, parameter, &Keire::VfxBlackboardParameter::Id);
                        if (found == definition.Blackboard.end())
                            throw std::invalid_argument("VFX blackboard parameter is unavailable.");
                        const auto previousType = found->Type;
                        const auto previousName = found->Name;
                        operation(*found);
                        if (found->Id != parameter)
                            throw std::invalid_argument("VFX blackboard parameter edits cannot replace the stable ID.");
                        const bool typeChanged = found->Type != previousType;
                        if (!typeChanged && found->Name == previousName)
                            return;

                        for (auto& system : definition.Systems)
                        {
                            std::vector<Keire::AssetId> parameterNodes;
                            for (auto& node : system.Nodes)
                            {
                                if (node.Kind != Keire::VfxGraphNodeKind::Parameter || node.Reference != parameter)
                                    continue;
                                parameterNodes.push_back(node.Id);
                                for (auto& pin : node.Pins)
                                    if (!pin.Input)
                                    {
                                        pin.Type = found->Type;
                                        pin.Name = found->Name;
                                    }
                            }
                            if (parameterNodes.empty() || !typeChanged)
                                continue;

                            std::erase_if(system.Connections,
                                          [&](const Keire::VfxGraphConnection& connection)
                                          {
                                              if (std::ranges::find(parameterNodes, connection.OutputNode) ==
                                                  parameterNodes.end())
                                                  return false;
                                              const auto* output =
                                                  FindPin(system, connection.OutputNode, connection.OutputPin);
                                              const auto* input =
                                                  FindPin(system, connection.InputNode, connection.InputPin);
                                              return !output || !input || output->Type != input->Type;
                                          });
                        }
                    });
    }

    bool VfxEffectDocument::RemoveBlackboardParameter(const Keire::AssetId parameter)
    {
        return Edit("Remove VFX blackboard parameter",
                    [parameter](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Blackboard, parameter, &Keire::VfxBlackboardParameter::Id);
                        if (found == definition.Blackboard.end())
                            throw std::invalid_argument("VFX blackboard parameter is unavailable.");
                        RemoveReferencedNodes(definition, Keire::VfxGraphNodeKind::Parameter, parameter);
                        definition.Blackboard.erase(found);
                    });
    }
} // namespace KeireEditor
