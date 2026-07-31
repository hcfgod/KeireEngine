#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
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

        [[nodiscard]] Keire::VfxGraphNode& RequireContext(Keire::VfxGraphSystem& system, const Keire::AssetId context)
        {
            auto& node = RequireNode(system, context);
            if (node.Kind != Keire::VfxGraphNodeKind::Context)
                throw std::invalid_argument("VFX Blocks can only be authored inside Context nodes.");
            return node;
        }

        [[nodiscard]] Keire::VfxGraphBlock& RequireBlock(Keire::VfxGraphNode& context, const Keire::AssetId block)
        {
            const auto found = std::ranges::find(context.Blocks, block, &Keire::VfxGraphBlock::Id);
            if (found == context.Blocks.end())
                throw std::invalid_argument("VFX Context Block is unavailable.");
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

        void AddEstimatedBytes(std::size_t& total, const std::size_t bytes) noexcept
        {
            total = bytes > std::numeric_limits<std::size_t>::max() - total ? std::numeric_limits<std::size_t>::max()
                                                                            : total + bytes;
        }

        template <typename Value> void AddEstimatedElements(std::size_t& total, const std::size_t count) noexcept
        {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value))
            {
                total = std::numeric_limits<std::size_t>::max();
                return;
            }
            AddEstimatedBytes(total, count * sizeof(Value));
        }

        void AddEstimatedProperties(std::size_t& total,
                                    const std::span<const Keire::VfxGraphProperty> properties) noexcept
        {
            AddEstimatedElements<Keire::VfxGraphProperty>(total, properties.size());
            for (const auto& property : properties)
            {
                AddEstimatedBytes(total, property.Name.size());
                if (const auto* text = std::get_if<std::string>(&property.Value))
                    AddEstimatedBytes(total, text->size());
            }
        }

        [[nodiscard]] std::size_t EstimateDefinitionSize(const Keire::VfxEffectDefinition& definition) noexcept
        {
            std::size_t result = sizeof(definition);
            AddEstimatedBytes(result, definition.Name.size());
            AddEstimatedElements<Keire::VfxModuleDefinition>(result, definition.Modules.size());
            for (const auto& module : definition.Modules)
            {
                std::visit(
                    [&result](const auto& payload)
                    {
                        using Payload = std::decay_t<decltype(payload)>;
                        if constexpr (std::is_same_v<Payload, Keire::VfxSizeOverLifetimeModule>)
                            AddEstimatedElements<Keire::CurveKey>(result, payload.Size.Keys().size());
                        else if constexpr (std::is_same_v<Payload, Keire::VfxColorOverLifetimeModule>)
                            AddEstimatedElements<Keire::ColorGradientKey>(result, payload.Color.Keys().size());
                    },
                    module.Payload);
            }
            AddEstimatedElements<Keire::VfxBlackboardParameter>(result, definition.Blackboard.size());
            for (const auto& parameter : definition.Blackboard)
                AddEstimatedBytes(result, parameter.Name.size());
            AddEstimatedElements<Keire::VfxGraphSystem>(result, definition.Systems.size());
            for (const auto& system : definition.Systems)
            {
                AddEstimatedBytes(result, system.Name.size());
                AddEstimatedElements<Keire::VfxGraphNode>(result, system.Nodes.size());
                AddEstimatedElements<Keire::VfxGraphConnection>(result, system.Connections.size());
                for (const auto& node : system.Nodes)
                {
                    AddEstimatedBytes(result, node.Type.size());
                    AddEstimatedBytes(result, node.TypeId.Value.size());
                    AddEstimatedBytes(result, node.CustomHlsl.size());
                    AddEstimatedProperties(result, node.Properties);
                    AddEstimatedElements<Keire::VfxGraphPin>(result, node.Pins.size());
                    for (const auto& pin : node.Pins)
                    {
                        AddEstimatedBytes(result, pin.Name.size());
                        AddEstimatedBytes(result, pin.Semantic.size());
                    }
                    AddEstimatedElements<Keire::VfxGraphBlock>(result, node.Blocks.size());
                    for (const auto& block : node.Blocks)
                    {
                        AddEstimatedBytes(result, block.Type.size());
                        AddEstimatedBytes(result, block.TypeId.Value.size());
                        AddEstimatedProperties(result, block.Properties);
                        AddEstimatedElements<Keire::VfxGraphPin>(result, block.Pins.size());
                        for (const auto& pin : block.Pins)
                        {
                            AddEstimatedBytes(result, pin.Name.size());
                            AddEstimatedBytes(result, pin.Semantic.size());
                        }
                    }
                }
            }
            return result;
        }

        [[nodiscard]] bool ContainsStableId(const Keire::VfxEffectDefinition& definition,
                                            const Keire::AssetId id) noexcept
        {
            if (definition.EmitterId == id ||
                std::ranges::any_of(definition.Modules, [id](const auto& module) { return module.Id == id; }) ||
                std::ranges::any_of(definition.Blackboard, [id](const auto& parameter) { return parameter.Id == id; }))
            {
                return true;
            }
            for (const auto& system : definition.Systems)
            {
                if (system.Id == id ||
                    std::ranges::any_of(system.Connections, [id](const Keire::VfxGraphConnection& connection)
                                        { return connection.Id == id; }))
                {
                    return true;
                }
                for (const auto& node : system.Nodes)
                {
                    if (node.Id == id ||
                        std::ranges::any_of(node.Pins, [id](const Keire::VfxGraphPin& pin) { return pin.Id == id; }))
                    {
                        return true;
                    }
                    for (const auto& block : node.Blocks)
                    {
                        if (block.Id == id || std::ranges::any_of(block.Pins, [id](const Keire::VfxGraphPin& pin)
                                                                  { return pin.Id == id; }))
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        [[nodiscard]] Keire::AssetId ConnectionProbeId(const Keire::VfxEffectDefinition& definition) noexcept
        {
            constexpr std::uint64_t probeNamespace = 0x5646584c494e4b50ULL;
            std::uint64_t value = 1;
            while (ContainsStableId(definition, Keire::AssetId(probeNamespace, value)))
                ++value;
            return Keire::AssetId(probeNamespace, value);
        }

        void RequireStableBlockIds(const Keire::VfxGraphBlock& before, const Keire::VfxGraphBlock& after)
        {
            if (after.Id != before.Id || SortedIds(after.Pins) != SortedIds(before.Pins))
                throw std::invalid_argument("VFX Block edits cannot replace stable Block or pin IDs.");
        }

        void RequireStableNodeIds(const Keire::VfxGraphNode& before, const Keire::VfxGraphNode& after)
        {
            if (after.Id != before.Id || SortedIds(after.Pins) != SortedIds(before.Pins) ||
                SortedIds(after.Blocks) != SortedIds(before.Blocks))
            {
                throw std::invalid_argument("VFX graph node edits cannot replace stable node or pin IDs.");
            }
            for (const auto& block : before.Blocks)
            {
                const auto found = std::ranges::find(after.Blocks, block.Id, &Keire::VfxGraphBlock::Id);
                if (found == after.Blocks.end())
                    throw std::invalid_argument("VFX graph node edits cannot replace stable Block IDs.");
                RequireStableBlockIds(block, *found);
            }
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

        [[nodiscard]] const Keire::VfxGraphPin* FindPin(const Keire::VfxGraphSystem& system,
                                                        const Keire::VfxGraphEndpoint endpoint) noexcept
        {
            const auto node = std::ranges::find(system.Nodes, endpoint.Node, &Keire::VfxGraphNode::Id);
            if (node == system.Nodes.end())
                return nullptr;
            if (endpoint.Block)
            {
                const auto block = std::ranges::find(node->Blocks, endpoint.Block, &Keire::VfxGraphBlock::Id);
                if (block == node->Blocks.end())
                    return nullptr;
                const auto pin = std::ranges::find(block->Pins, endpoint.Pin, &Keire::VfxGraphPin::Id);
                return pin == block->Pins.end() ? nullptr : std::addressof(*pin);
            }
            const auto pin = std::ranges::find(node->Pins, endpoint.Pin, &Keire::VfxGraphPin::Id);
            return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
        }

        [[nodiscard]] const Keire::VfxGraphPin* FindFlowPin(const Keire::VfxGraphNode& node, const bool input) noexcept
        {
            const auto found =
                std::ranges::find_if(node.Pins, [input](const Keire::VfxGraphPin& pin)
                                     { return pin.Input == input && pin.Type == Keire::VfxValueType::ParticleStream; });
            return found == node.Pins.end() ? nullptr : std::addressof(*found);
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
                if (input && output)
                {
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
                    if (incoming.size() == 1 && outgoing.size() == 1)
                    {
                        auto& bridge = system.Connections[incoming.front()];
                        const auto& destination = system.Connections[outgoing.front()];
                        bridge.InputNode = destination.InputNode;
                        bridge.InputBlock = destination.InputBlock;
                        bridge.InputPin = destination.InputPin;
                    }
                }
            }

            std::erase_if(system.Connections, [nodeId](const Keire::VfxGraphConnection& connection)
                          { return connection.OutputNode == nodeId || connection.InputNode == nodeId; });
            system.Nodes.erase(found);
        }

        void SynchronizeModuleNodes(Keire::VfxEffectDefinition& definition, const Keire::VfxModuleDefinition& module)
        {
            const auto canonicalNode = Keire::CreateVfxGraphModuleNode(module, {});
            const auto canonicalBlock = Keire::CreateVfxGraphBlock(module);
            for (auto& system : definition.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    if (node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == module.Id)
                    {
                        const bool topologyMatches =
                            node.Pins.size() == canonicalNode.Pins.size() &&
                            std::ranges::all_of(canonicalNode.Pins,
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
                            std::erase_if(
                                system.Connections, [nodeId = node.Id](const Keire::VfxGraphConnection& connection)
                                { return connection.OutputNode == nodeId || connection.InputNode == nodeId; });
                            auto replacement = Keire::CreateVfxGraphModuleNode(module, node.EditorPosition);
                            replacement.Id = node.Id;
                            node = std::move(replacement);
                        }
                        else
                        {
                            node.Type = canonicalNode.Type;
                            node.TypeId = canonicalNode.TypeId;
                            node.DefinitionVersion = canonicalNode.DefinitionVersion;
                            node.Context = canonicalNode.Context;
                            node.CustomHlsl.clear();
                            for (const auto& canonicalPin : canonicalNode.Pins)
                            {
                                const auto pin =
                                    std::ranges::find_if(node.Pins,
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

                    for (auto& block : node.Blocks)
                    {
                        if (block.Reference != module.Id)
                            continue;
                        const bool topologyMatches =
                            block.Pins.size() == canonicalBlock.Pins.size() &&
                            std::ranges::all_of(canonicalBlock.Pins,
                                                [&](const Keire::VfxGraphPin& canonicalPin)
                                                {
                                                    return std::ranges::count_if(
                                                               block.Pins,
                                                               [&](const Keire::VfxGraphPin& pin)
                                                               {
                                                                   return pin.Input == canonicalPin.Input &&
                                                                          pin.Type == canonicalPin.Type &&
                                                                          pin.Semantic == canonicalPin.Semantic;
                                                               }) == 1;
                                                });
                        if (!topologyMatches)
                        {
                            std::erase_if(
                                system.Connections,
                                [nodeId = node.Id, blockId = block.Id](const Keire::VfxGraphConnection& connection)
                                {
                                    return (connection.OutputNode == nodeId && connection.OutputBlock == blockId) ||
                                           (connection.InputNode == nodeId && connection.InputBlock == blockId);
                                });
                            auto replacement = Keire::CreateVfxGraphBlock(module);
                            replacement.Id = block.Id;
                            replacement.Enabled = block.Enabled;
                            block = std::move(replacement);
                            continue;
                        }

                        block.Type = canonicalBlock.Type;
                        block.TypeId = canonicalBlock.TypeId;
                        block.DefinitionVersion = canonicalBlock.DefinitionVersion;
                        for (const auto& canonicalPin : canonicalBlock.Pins)
                        {
                            const auto pin =
                                std::ranges::find_if(block.Pins,
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
        }

        void RemoveReferencedExecutables(Keire::VfxEffectDefinition& definition, const Keire::VfxGraphNodeKind kind,
                                         const Keire::AssetId reference)
        {
            for (auto& system : definition.Systems)
            {
                for (auto& context : system.Nodes)
                {
                    std::vector<Keire::AssetId> removedBlocks;
                    for (const auto& block : context.Blocks)
                        if (block.Reference == reference)
                            removedBlocks.push_back(block.Id);
                    if (removedBlocks.empty())
                        continue;
                    std::erase_if(
                        system.Connections,
                        [&](const Keire::VfxGraphConnection& connection)
                        {
                            return (connection.OutputNode == context.Id &&
                                    std::ranges::find(removedBlocks, connection.OutputBlock) != removedBlocks.end()) ||
                                   (connection.InputNode == context.Id &&
                                    std::ranges::find(removedBlocks, connection.InputBlock) != removedBlocks.end());
                        });
                    std::erase_if(context.Blocks, [reference](const Keire::VfxGraphBlock& block)
                                  { return block.Reference == reference; });
                }

                std::vector<Keire::AssetId> removedNodes;
                for (const auto& node : system.Nodes)
                    if (node.Kind == kind && node.Reference == reference)
                        removedNodes.push_back(node.Id);
                for (const auto node : removedNodes)
                    RemoveNodeAndReconnect(system, node,
                                           definition.ExecutionSource == Keire::VfxExecutionSource::Graph);
            }
        }
    } // namespace

    VfxEffectDocument::VfxEffectDocument(VfxEffectDocumentSpecification specification)
        : m_Host({.Validate = [](const Keire::VfxEffectDefinition& definition)
                  { Keire::ValidateVfxEffectAuthoring(definition); },
                  .Encode = [](const Keire::VfxEffectDefinition& definition)
                  { return Keire::VfxEffectAsset::Encode(definition); },
                  .EstimateSize = EstimateDefinitionSize,
                  .Preview =
                      [this, preview = std::move(specification.Preview)](const Keire::AssetId asset,
                                                                         const Keire::VfxEffectDefinition& definition)
                  {
                      try
                      {
                          Keire::ValidateVfxEffect(definition);
                      }
                      catch (const std::invalid_argument& error)
                      {
                          m_GraphDiagnostic = error.what();
                          return;
                      }
                      m_GraphDiagnostic.clear();
                      if (preview)
                          preview(asset, definition);
                  },
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
        Keire::ValidateVfxEffect(definition);
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
    }

    void VfxEffectDocument::Create(const Keire::AssetId asset, Keire::VfxEffectDefinition definition,
                                   Keire::Ref<Keire::UndoContext> undo)
    {
        Keire::ValidateVfxEffect(definition);
        m_Host.Create(asset, std::move(definition), std::move(undo));
    }

    void VfxEffectDocument::Save()
    {
        Keire::ValidateVfxEffect(m_Host.Draft());
        m_Host.Save();
    }

    void VfxEffectDocument::Discard()
    {
        m_Host.Discard();
        if (!m_Host.IsOpen())
            m_GraphDiagnostic.clear();
    }

    AssetDocumentReloadResult VfxEffectDocument::Reload(const std::span<const std::byte> bytes,
                                                        const std::uint64_t revision)
    {
        return Reload(Keire::VfxEffectAsset::Decode(bytes)->Definition(), revision);
    }

    AssetDocumentReloadResult VfxEffectDocument::Reload(Keire::VfxEffectDefinition definition,
                                                        const std::uint64_t revision)
    {
        Keire::ValidateVfxEffect(definition);
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        return m_Host.Reload(std::move(definition), revision);
    }

    bool VfxEffectDocument::Undo() { return m_Host.Undo(); }

    bool VfxEffectDocument::Redo() { return m_Host.Redo(); }

    void VfxEffectDocument::Close() noexcept
    {
        m_Host.Close();
        m_GraphDiagnostic.clear();
    }

    std::string_view VfxEffectDocument::Diagnostic() const noexcept
    {
        return m_Host.Diagnostic().empty() ? std::string_view(m_GraphDiagnostic) : m_Host.Diagnostic();
    }

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
                        RemoveReferencedExecutables(definition, Keire::VfxGraphNodeKind::Module, module);
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
                    { RequireSystem(definition, system).Nodes.push_back(std::move(node)); });
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

    bool VfxEffectDocument::AddBlock(const Keire::AssetId system, const Keire::AssetId context,
                                     Keire::VfxGraphBlock block)
    {
        return Edit("Add VFX Context Block",
                    [system, context, block = std::move(block)](Keire::VfxEffectDefinition& definition) mutable
                    {
                        auto& graph = RequireSystem(definition, system);
                        auto& blocks = RequireContext(graph, context).Blocks;
                        if (std::ranges::find(blocks, block.Id, &Keire::VfxGraphBlock::Id) != blocks.end())
                            throw std::invalid_argument("VFX Context Block stable ID is already in use.");
                        blocks.push_back(std::move(block));
                    });
    }

    bool VfxEffectDocument::EditBlock(const Keire::AssetId system, const Keire::AssetId context,
                                      const Keire::AssetId block,
                                      const std::function<void(Keire::VfxGraphBlock&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX Context Block edits require an operation.");
        return Edit("Edit VFX Context Block",
                    [system, context, block, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& found = RequireBlock(RequireContext(RequireSystem(definition, system), context), block);
                        const auto before = found;
                        operation(found);
                        RequireStableBlockIds(before, found);
                    });
    }

    bool VfxEffectDocument::SetBlockEnabled(const Keire::AssetId system, const Keire::AssetId context,
                                            const Keire::AssetId block, const bool enabled)
    {
        return Edit(
            "Set VFX Context Block state", [system, context, block, enabled](Keire::VfxEffectDefinition& definition)
            { RequireBlock(RequireContext(RequireSystem(definition, system), context), block).Enabled = enabled; });
    }

    bool VfxEffectDocument::RemoveBlock(const Keire::AssetId system, const Keire::AssetId context,
                                        const Keire::AssetId block)
    {
        return Edit("Remove VFX Context Block",
                    [system, context, block](Keire::VfxEffectDefinition& definition)
                    {
                        auto& graph = RequireSystem(definition, system);
                        auto& blocks = RequireContext(graph, context).Blocks;
                        const auto found = std::ranges::find(blocks, block, &Keire::VfxGraphBlock::Id);
                        if (found == blocks.end())
                            throw std::invalid_argument("VFX Context Block is unavailable.");
                        std::erase_if(graph.Connections,
                                      [context, block](const Keire::VfxGraphConnection& connection)
                                      {
                                          return (connection.OutputNode == context &&
                                                  connection.OutputBlock == block) ||
                                                 (connection.InputNode == context && connection.InputBlock == block);
                                      });
                        blocks.erase(found);
                    });
    }

    bool VfxEffectDocument::MoveBlock(const Keire::AssetId system, const Keire::AssetId context,
                                      const Keire::AssetId block, const std::size_t destination)
    {
        return Edit("Reorder VFX Context Block",
                    [system, context, block, destination](Keire::VfxEffectDefinition& definition)
                    {
                        auto& blocks = RequireContext(RequireSystem(definition, system), context).Blocks;
                        if (destination >= blocks.size())
                            throw std::invalid_argument("VFX Context Block destination is out of range.");
                        const auto found = std::ranges::find(blocks, block, &Keire::VfxGraphBlock::Id);
                        if (found == blocks.end())
                            throw std::invalid_argument("VFX Context Block is unavailable.");
                        const auto source = static_cast<std::size_t>(std::distance(blocks.begin(), found));
                        if (source == destination)
                            return;
                        auto moved = std::move(*found);
                        blocks.erase(found);
                        blocks.insert(std::next(blocks.begin(), static_cast<std::ptrdiff_t>(destination)),
                                      std::move(moved));
                    });
    }

    bool VfxEffectDocument::AddBlockPin(const Keire::AssetId system, const Keire::AssetId context,
                                        const Keire::AssetId block, Keire::VfxGraphPin pin)
    {
        return Edit("Add VFX Context Block pin",
                    [system, context, block, pin = std::move(pin)](Keire::VfxEffectDefinition& definition) mutable
                    {
                        auto& pins =
                            RequireBlock(RequireContext(RequireSystem(definition, system), context), block).Pins;
                        if (std::ranges::find(pins, pin.Id, &Keire::VfxGraphPin::Id) != pins.end())
                            throw std::invalid_argument("VFX Context Block pin stable ID is already in use.");
                        pins.push_back(std::move(pin));
                    });
    }

    bool VfxEffectDocument::EditBlockPin(const Keire::AssetId system, const Keire::AssetId context,
                                         const Keire::AssetId block, const Keire::AssetId pin,
                                         const std::function<void(Keire::VfxGraphPin&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX Context Block pin edits require an operation.");
        return Edit("Edit VFX Context Block pin",
                    [system, context, block, pin, operation](Keire::VfxEffectDefinition& definition)
                    {
                        auto& graph = RequireSystem(definition, system);
                        auto& blockValue = RequireBlock(RequireContext(graph, context), block);
                        auto found = std::ranges::find(blockValue.Pins, pin, &Keire::VfxGraphPin::Id);
                        if (found == blockValue.Pins.end())
                            throw std::invalid_argument("VFX Context Block pin is unavailable.");
                        const auto before = *found;
                        operation(*found);
                        if (found->Id != pin)
                            throw std::invalid_argument("VFX Context Block pin edits cannot replace the stable ID.");
                        if (found->Type != before.Type || found->Input != before.Input)
                        {
                            std::erase_if(graph.Connections,
                                          [context, block, pin](const Keire::VfxGraphConnection& connection)
                                          {
                                              return (connection.OutputNode == context &&
                                                      connection.OutputBlock == block && connection.OutputPin == pin) ||
                                                     (connection.InputNode == context &&
                                                      connection.InputBlock == block && connection.InputPin == pin);
                                          });
                        }
                    });
    }

    bool VfxEffectDocument::RemoveBlockPin(const Keire::AssetId system, const Keire::AssetId context,
                                           const Keire::AssetId block, const Keire::AssetId pin)
    {
        return Edit("Remove VFX Context Block pin",
                    [system, context, block, pin](Keire::VfxEffectDefinition& definition)
                    {
                        auto& graph = RequireSystem(definition, system);
                        auto& pins = RequireBlock(RequireContext(graph, context), block).Pins;
                        const auto found = std::ranges::find(pins, pin, &Keire::VfxGraphPin::Id);
                        if (found == pins.end())
                            throw std::invalid_argument("VFX Context Block pin is unavailable.");
                        std::erase_if(graph.Connections,
                                      [context, block, pin](const Keire::VfxGraphConnection& connection)
                                      {
                                          return (connection.OutputNode == context && connection.OutputBlock == block &&
                                                  connection.OutputPin == pin) ||
                                                 (connection.InputNode == context && connection.InputBlock == block &&
                                                  connection.InputPin == pin);
                                      });
                        pins.erase(found);
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
        return Edit("Add VFX graph connection",
                    [system, connection = std::move(connection)](Keire::VfxEffectDefinition& definition) mutable
                    {
                        auto& connections = RequireSystem(definition, system).Connections;
                        if (std::ranges::find(connections, connection.Id, &Keire::VfxGraphConnection::Id) !=
                            connections.end())
                            throw std::invalid_argument("VFX graph connection stable ID is already in use.");
                        std::erase_if(connections,
                                      [&](const Keire::VfxGraphConnection& existing)
                                      {
                                          return existing.InputNode == connection.InputNode &&
                                                 existing.InputBlock == connection.InputBlock &&
                                                 existing.InputPin == connection.InputPin;
                                      });
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
                        const auto inputBlock = found->InputBlock;
                        const auto inputPin = found->InputPin;
                        std::erase_if(
                            connections,
                            [connection, inputNode, inputBlock, inputPin](const Keire::VfxGraphConnection& candidate)
                            {
                                return candidate.Id != connection && candidate.InputNode == inputNode &&
                                       candidate.InputBlock == inputBlock && candidate.InputPin == inputPin;
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

    VfxGraphConnectionCheck VfxEffectDocument::CheckConnection(const Keire::AssetId system,
                                                               const Keire::AssetId outputNode,
                                                               const Keire::AssetId outputPin,
                                                               const Keire::AssetId inputNode,
                                                               const Keire::AssetId inputPin) const
    {
        return CheckConnection(system, {outputNode, {}, outputPin}, {inputNode, {}, inputPin});
    }

    VfxGraphConnectionCheck VfxEffectDocument::CheckConnection(const Keire::AssetId system,
                                                               const Keire::VfxGraphEndpoint outputEndpoint,
                                                               const Keire::VfxGraphEndpoint inputEndpoint) const
    {
        VfxGraphConnectionCheck result;
        const auto graph = std::ranges::find(m_Host.Draft().Systems, system, &Keire::VfxGraphSystem::Id);
        if (graph == m_Host.Draft().Systems.end())
        {
            result.Diagnostic = "VFX graph system is unavailable.";
            return result;
        }

        const auto* output = FindPin(*graph, outputEndpoint);
        const auto* input = FindPin(*graph, inputEndpoint);
        if (!output || !input)
        {
            result.Diagnostic = "VFX graph connection references an unavailable pin.";
            return result;
        }
        if (output->Input || !input->Input)
        {
            result.Diagnostic = "Connect an output pin to an input pin.";
            return result;
        }
        if (output->Type != input->Type)
        {
            result.Diagnostic = "VFX graph pin types do not match.";
            return result;
        }
        if (input->Type != Keire::VfxValueType::ParticleStream)
        {
            const auto outputNode = std::ranges::find(graph->Nodes, outputEndpoint.Node, &Keire::VfxGraphNode::Id);
            const auto inputNode = std::ranges::find(graph->Nodes, inputEndpoint.Node, &Keire::VfxGraphNode::Id);
            if (outputNode == graph->Nodes.end() || inputNode == graph->Nodes.end())
            {
                result.Diagnostic = "VFX graph connection references an unavailable node.";
                return result;
            }

            const Keire::VfxGraphBlock* inputBlock = nullptr;
            if (inputEndpoint.Block)
            {
                const auto block = std::ranges::find(inputNode->Blocks, inputEndpoint.Block, &Keire::VfxGraphBlock::Id);
                if (block == inputNode->Blocks.end())
                {
                    result.Diagnostic = "VFX graph connection references an unavailable Block.";
                    return result;
                }
                inputBlock = std::addressof(*block);
            }

            const bool portableInput = (inputBlock && inputBlock->TypeId.View() == "keire.block.portable-hlsl") ||
                                       (!inputBlock && inputNode->Kind == Keire::VfxGraphNodeKind::CustomHlsl);
            if (portableInput && outputNode->Kind != Keire::VfxGraphNodeKind::Parameter)
            {
                result.Diagnostic = "Portable Custom HLSL inputs may only bind Blackboard parameters.";
                return result;
            }

            const bool moduleInput = (inputBlock && inputBlock->TypeId.View() != "keire.block.portable-hlsl") ||
                                     (!inputBlock && inputNode->Kind == Keire::VfxGraphNodeKind::Module);
            if (moduleInput && outputNode->Kind != Keire::VfxGraphNodeKind::Parameter &&
                outputNode->Kind != Keire::VfxGraphNodeKind::Operator)
            {
                result.Diagnostic = "VFX module properties require a Blackboard or executable Operator source.";
                return result;
            }
        }
        if (std::ranges::any_of(graph->Connections,
                                [&](const Keire::VfxGraphConnection& connection)
                                {
                                    return connection.OutputEndpoint() == outputEndpoint &&
                                           connection.InputEndpoint() == inputEndpoint;
                                }))
        {
            result.Diagnostic = "This VFX graph cable already exists.";
            return result;
        }

        auto candidate = m_Host.Draft();
        auto& candidateGraph = RequireSystem(candidate, system);
        result.ReplacesInput =
            std::ranges::any_of(candidateGraph.Connections, [&](const Keire::VfxGraphConnection& connection)
                                { return connection.InputEndpoint() == inputEndpoint; });
        std::erase_if(candidateGraph.Connections, [&](const Keire::VfxGraphConnection& connection)
                      { return connection.InputEndpoint() == inputEndpoint; });
        Keire::VfxGraphConnection probe;
        probe.Id = ConnectionProbeId(candidate);
        probe.OutputNode = outputEndpoint.Node;
        probe.OutputPin = outputEndpoint.Pin;
        probe.InputNode = inputEndpoint.Node;
        probe.InputPin = inputEndpoint.Pin;
        probe.OutputBlock = outputEndpoint.Block;
        probe.InputBlock = inputEndpoint.Block;
        candidateGraph.Connections.push_back(std::move(probe));

        try
        {
            Keire::ValidateVfxEffectAuthoring(candidate);
        }
        catch (const std::invalid_argument& error)
        {
            result.Diagnostic = error.what();
            return result;
        }

        try
        {
            Keire::ValidateVfxEffect(candidate);
            result.Status = VfxGraphConnectionStatus::Accepted;
        }
        catch (const std::invalid_argument& error)
        {
            result.Status = VfxGraphConnectionStatus::AcceptedWithWarning;
            result.Diagnostic = error.what();
        }
        return result;
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
                                              const auto* output = FindPin(system, connection.OutputEndpoint());
                                              const auto* input = FindPin(system, connection.InputEndpoint());
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
                        RemoveReferencedExecutables(definition, Keire::VfxGraphNodeKind::Parameter, parameter);
                        definition.Blackboard.erase(found);
                    });
    }
} // namespace KeireEditor
