#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanelModel.h"
#include "KeireClient/Editor/VfxNodeCatalog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        using Detail::ModuleName;
        using Detail::ModuleRunsInContext;
        using Detail::NewContextNode;
        using Detail::NewCustomHlslNode;
        using Detail::NewParameterNode;

        template <typename Value> struct EnumEntry
        {
            Value Type;
            std::string_view Name;
        };

        constexpr std::array ContextTypes{
            EnumEntry{Keire::VfxContextType::Spawn, std::string_view("Spawn")},
            EnumEntry{Keire::VfxContextType::Initialize, std::string_view("Initialize")},
            EnumEntry{Keire::VfxContextType::Update, std::string_view("Update")},
            EnumEntry{Keire::VfxContextType::Output, std::string_view("Output")},
            EnumEntry{Keire::VfxContextType::Event, std::string_view("Event")},
        };
        constexpr std::array ValueTypes{
            EnumEntry{Keire::VfxValueType::Boolean, std::string_view("Boolean")},
            EnumEntry{Keire::VfxValueType::Integer, std::string_view("Integer")},
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
            EnumEntry{Keire::VfxValueType::Texture, std::string_view("Texture")},
            EnumEntry{Keire::VfxValueType::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxValueType::Asset, std::string_view("Asset")},
        };
        constexpr std::array PreviewBackends{
            EnumEntry{Keire::VfxBackend::Cpu, std::string_view("CPU (Authoring)")},
            EnumEntry{Keire::VfxBackend::Gpu, std::string_view("GPU (Runtime)")},
        };

        template <typename Value, std::size_t Size>
        [[nodiscard]] std::string_view EnumName(const Value value, const std::array<EnumEntry<Value>, Size>& entries)
        {
            const auto found = std::ranges::find(entries, value, &EnumEntry<Value>::Type);
            return found == entries.end() ? std::string_view("Unsupported") : found->Name;
        }
    } // namespace

    bool VfxEffectPanel::DrawNodePaletteEntries(Keire::UiFrame& ui, const Keire::AssetId system,
                                                const Keire::Vector2 position, const std::string_view filter,
                                                const Keire::AssetId blockContext)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto graph = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
        if (graph == definition.Systems.end())
            return false;
        const Keire::VfxGraphNode* targetContext = nullptr;
        if (blockContext)
        {
            const auto found = std::ranges::find(graph->Nodes, blockContext, &Keire::VfxGraphNode::Id);
            if (found == graph->Nodes.end() || found->Kind != Keire::VfxGraphNodeKind::Context)
                return false;
            targetContext = std::addressof(*found);
        }
        const auto backend = m_Controller.VfxEffectPreviewState().Backend;

        enum class PaletteActionKind : std::uint8_t
        {
            Context,
            Operator,
            ContextBlock,
            PortableHlslBlock,
            RuntimeModule,
            BlackboardParameter,
            CustomHlsl
        };
        struct PaletteAction
        {
            PaletteActionKind Kind = PaletteActionKind::Context;
            Keire::AssetId Reference;
            Keire::AssetId ContextNode;
            Keire::VfxContextType Context = Keire::VfxContextType::Update;
            std::string TypeId;
        };

        VfxNodeSearchIndex catalog;
        std::vector<PaletteAction> actions;
        const auto addEntry = [&](VfxNodeCatalogEntry entry, const PaletteAction& action)
        {
            const auto index = catalog.Add(std::move(entry));
            if (index != actions.size())
                throw std::logic_error("VFX node palette catalog and creation actions lost alignment.");
            actions.push_back(action);
        };

        if (!targetContext)
        {
            for (const auto& context : ContextTypes)
            {
                const bool represented = std::ranges::any_of(
                    graph->Nodes, [&](const Keire::VfxGraphNode& node)
                    { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == context.Type; });
                VfxNodeCatalogEntry entry{
                    .Id = "keire.context." + std::to_string(static_cast<std::uint8_t>(context.Type)),
                    .Name = std::string(context.Name),
                    .Category = "Contexts",
                    .TypeName = "Context",
                    .Description = "Defines the " + std::string(context.Name) + " particle execution stage.",
                    .Aliases = {"Stage", std::string(context.Name) + " Stage"},
                    .Keywords = {"Flow", "Particle Stream"},
                    .Contexts = {context.Type},
                    .SortPriority = 100 - static_cast<int>(context.Type),
                };
                if (context.Type != Keire::VfxContextType::Spawn && context.Type != Keire::VfxContextType::Event)
                    entry.InputTypes.push_back(Keire::VfxValueType::ParticleStream);
                if (context.Type != Keire::VfxContextType::Output)
                    entry.OutputTypes.push_back(Keire::VfxValueType::ParticleStream);
                if (context.Type == Keire::VfxContextType::Event)
                {
                    entry.DisabledReason = represented ? "Already placed in this system." : std::string{};
                }
                else if (represented)
                    entry.DisabledReason = "Already placed in this system.";
                addEntry(std::move(entry), {.Kind = PaletteActionKind::Context, .Context = context.Type});
            }

            for (const auto& descriptor : Keire::VfxNodeCatalog())
            {
                if (descriptor.Class != Keire::VfxNodeClass::Operator)
                    continue;
                auto entry = BuildVfxNodeCatalogEntry(descriptor);
                entry.Description = "Creates the built-in " + descriptor.Label + " value operator.";
                entry.Keywords = {"Built-In", "Value", "Expression"};
                entry.SortPriority = 70;
                addEntry(std::move(entry), {.Kind = PaletteActionKind::Operator, .TypeId = descriptor.TypeId.Value});
            }
        }

        for (const auto& module : definition.Modules)
        {
            if (targetContext && !ModuleRunsInContext(module.Payload, targetContext->Context))
                continue;
            const auto name = ModuleName(module.Payload);
            const bool legacyRepresented = std::ranges::any_of(
                graph->Nodes, [&](const Keire::VfxGraphNode& node)
                { return node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == module.Id; });
            const auto blockOwner =
                std::ranges::find_if(graph->Nodes,
                                     [&](const Keire::VfxGraphNode& node)
                                     {
                                         return std::ranges::any_of(node.Blocks, [&](const Keire::VfxGraphBlock& block)
                                                                    { return block.Reference == module.Id; });
                                     });
            VfxNodeCatalogEntry entry{
                .Id = std::string(targetContext ? "keire.context-block." : "keire.runtime-module.") +
                      module.Id.ToString(),
                .Name = std::string(name),
                .Category = targetContext ? "Compatible Blocks" : "Legacy Module Nodes",
                .TypeName = targetContext ? "Context Block" : "Legacy Free-flow Module",
                .Description =
                    targetContext
                        ? "Adds the existing " + std::string(name) + " payload to this ordered Context."
                        : "Creates a legacy free-flow node for the existing " + std::string(name) + " payload.",
                .Aliases = {std::string(name) + " Module", std::string(name) + " Block"},
                .Keywords = {"Block", "Payload", "Particle Stream", "Ordered"},
                .InputTypes = targetContext ? std::vector<Keire::VfxValueType>{}
                                            : std::vector{Keire::VfxValueType::ParticleStream},
                .OutputTypes = targetContext ? std::vector<Keire::VfxValueType>{}
                                             : std::vector{Keire::VfxValueType::ParticleStream},
                .SortPriority = targetContext ? 95 : 25,
            };
            for (const auto& context : ContextTypes)
                if (ModuleRunsInContext(module.Payload, context.Type))
                    entry.Contexts.push_back(context.Type);
            if (legacyRepresented)
                entry.DisabledReason = "Already represented by a legacy Module node.";
            else if (blockOwner != graph->Nodes.end())
                entry.DisabledReason =
                    "Already present in the " + std::string(EnumName(blockOwner->Context, ContextTypes)) + " Context.";
            addEntry(std::move(entry),
                     {.Kind = targetContext ? PaletteActionKind::ContextBlock : PaletteActionKind::RuntimeModule,
                      .Reference = module.Id,
                      .ContextNode = blockContext});
        }

        if (targetContext)
        {
            addEntry(
                {
                    .Id = "keire.context-block.portable-hlsl",
                    .Name = "Portable Custom HLSL",
                    .Category = "Compatible Blocks",
                    .TypeName = "Context Block",
                    .Description = "Adds an ordered portable-code Block with typed data inputs to this Context.",
                    .Aliases = {"HLSL Block", "Custom Code Block", "Shader Block"},
                    .Keywords = {"Position", "Velocity", "Rotation", "Tint", "Size", "Ordered"},
                    .InputTypes = {Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector2,
                                   Keire::VfxValueType::Vector3, Keire::VfxValueType::Color},
                    .Contexts = {targetContext->Context},
                    .SortPriority = 90,
                },
                {.Kind = PaletteActionKind::PortableHlslBlock, .ContextNode = blockContext});
        }

        if (!targetContext)
        {
            for (const auto& parameter : definition.Blackboard)
            {
                addEntry(
                    {
                        .Id = "keire.blackboard." + parameter.Id.ToString(),
                        .Name = parameter.Name,
                        .Category = "Blackboard",
                        .TypeName = "Blackboard Parameter",
                        .Description = "Reads the stable Blackboard property named " + parameter.Name + ".",
                        .Aliases = {parameter.Name + " Parameter"},
                        .Keywords = {"Property", "Exposed", "Value"},
                        .OutputTypes = {parameter.Type},
                        .SortPriority = 60,
                    },
                    {.Kind = PaletteActionKind::BlackboardParameter, .Reference = parameter.Id});
            }

            addEntry(
                {
                    .Id = "keire.custom-hlsl",
                    .Name = "Custom HLSL (Legacy Node)",
                    .Category = "Legacy Free-flow Nodes",
                    .TypeName = "Legacy Custom HLSL Node",
                    .Description =
                        "Creates a readable legacy free-flow code node. Prefer Portable Custom HLSL Context Blocks.",
                    .Aliases = {"HLSL", "Custom Code", "Shader Code"},
                    .Keywords = {"Position", "Velocity", "Rotation", "Tint", "Size"},
                    .InputTypes = {Keire::VfxValueType::ParticleStream, Keire::VfxValueType::Scalar,
                                   Keire::VfxValueType::Vector2, Keire::VfxValueType::Vector3,
                                   Keire::VfxValueType::Color},
                    .OutputTypes = {Keire::VfxValueType::ParticleStream},
                    .Contexts = {Keire::VfxContextType::Spawn, Keire::VfxContextType::Initialize,
                                 Keire::VfxContextType::Update, Keire::VfxContextType::Output},
                    .SortPriority = 20,
                },
                {.Kind = PaletteActionKind::CustomHlsl});
        }

        const auto selected = [&](const Keire::AssetId node)
        {
            m_SelectedNode = node;
            m_SelectedBlock = {};
            m_SelectedConnection = {};
            m_GraphCanvas.SelectBlock(std::nullopt);
            m_GraphCanvas.SelectConnection(std::nullopt);
            m_NodePaletteSearch.clear();
            ui.CloseCurrentPopup();
        };
        const auto addNode = [&](const std::string_view message, Keire::VfxGraphNode node)
        {
            const auto id = node.Id;
            if (!ApplyAction(message, [&document, system, node = std::move(node)]() mutable
                             { return document.AddNode(system, std::move(node)); }))
            {
                return false;
            }
            selected(id);
            return true;
        };
        const auto addBlock = [&](const Keire::AssetId context, Keire::VfxGraphBlock block)
        {
            const auto id = block.Id;
            if (!ApplyAction("Added VFX Context Block", [&document, system, context, block = std::move(block)]() mutable
                             { return document.AddBlock(system, context, std::move(block)); }))
            {
                return false;
            }
            m_SelectedNode = context;
            m_SelectedBlock = id;
            m_SelectedConnection = {};
            m_GraphCanvas.SelectConnection(std::nullopt);
            m_NodePaletteSearch.clear();
            ui.CloseCurrentPopup();
            return true;
        };
        bool activateSelected = false;
        const auto drawEntry = [&](const std::size_t index, const bool qualified)
        {
            const auto entries = catalog.Entries();
            const auto& entry = entries[index];
            const auto& action = actions[index];
            const bool backendSupported = backend == Keire::VfxBackend::Cpu ? entry.CpuSupported : entry.GpuSupported;
            std::string label = qualified ? entry.Category + " / " + entry.Name : entry.Name;
            if (!qualified && action.Kind == PaletteActionKind::BlackboardParameter)
            {
                const auto parameter =
                    std::ranges::find(definition.Blackboard, action.Reference, &Keire::VfxBlackboardParameter::Id);
                if (parameter != definition.Blackboard.end())
                    label += "  :  " + std::string(EnumName(parameter->Type, ValueTypes));
            }
            if (qualified)
                label += "  |  " + entry.TypeName + "  |  " + VfxNodeCatalogSupportBadge(entry);
            else if (action.Kind == PaletteActionKind::Operator)
                label += "  |  " + VfxNodeCatalogSupportBadge(entry);
            if (!entry.DisabledReason.empty())
                label += "  (" + entry.DisabledReason + ")";
            else if (!backendSupported)
                label +=
                    "  (Unavailable on the selected " + std::string(EnumName(backend, PreviewBackends)) + " backend.)";
            label += "##VfxPaletteNode" + entry.Id;
            const bool available = entry.Enabled() && backendSupported;
            const bool activated = ui.MenuItem(label, m_NodeMenuSelection.IsSelected(entry.Id), available) ||
                                   (available && activateSelected && m_NodeMenuSelection.IsSelected(entry.Id));
            if (!activated)
                return false;

            bool created = false;
            switch (action.Kind)
            {
            case PaletteActionKind::Context:
                created = addNode("Added VFX context node", NewContextNode(action.Context, position));
                break;
            case PaletteActionKind::Operator:
                created =
                    addNode("Added VFX operator node", Keire::CreateVfxGraphOperatorNode(action.TypeId, position));
                break;
            case PaletteActionKind::ContextBlock:
            {
                const auto module =
                    std::ranges::find(definition.Modules, action.Reference, &Keire::VfxModuleDefinition::Id);
                created = module != definition.Modules.end() &&
                          addBlock(action.ContextNode, Keire::CreateVfxGraphBlock(*module));
                break;
            }
            case PaletteActionKind::PortableHlslBlock:
                created = addBlock(action.ContextNode, Keire::CreateVfxGraphPortableHlslBlock("Size *= 1.0;"));
                break;
            case PaletteActionKind::RuntimeModule:
            {
                const auto module =
                    std::ranges::find(definition.Modules, action.Reference, &Keire::VfxModuleDefinition::Id);
                created = module != definition.Modules.end() &&
                          addNode("Added VFX Runtime Module node", Keire::CreateVfxGraphModuleNode(*module, position));
                break;
            }
            case PaletteActionKind::BlackboardParameter:
            {
                const auto parameter =
                    std::ranges::find(definition.Blackboard, action.Reference, &Keire::VfxBlackboardParameter::Id);
                created = parameter != definition.Blackboard.end() &&
                          addNode("Added VFX Blackboard node", NewParameterNode(*parameter, position));
                break;
            }
            case PaletteActionKind::CustomHlsl:
                created = addNode("Added Custom HLSL node", NewCustomHlslNode(position));
                break;
            }
            if (created)
                m_NodeMenuSelection.Remember(entry.Id);
            return created;
        };
        const auto drawKind = [&](const PaletteActionKind kind)
        {
            for (std::size_t index = 0; index < actions.size(); ++index)
                if (actions[index].Kind == kind && drawEntry(index, false))
                    return true;
            return false;
        };

        const auto matches =
            catalog.Search({.Text = filter,
                            .Context = targetContext ? std::optional(targetContext->Context) : std::nullopt,
                            .Backend = backend,
                            .IncludeUnsupportedBackend = true});
        std::vector<std::size_t> keyboardEntries;
        if (filter.empty())
        {
            const auto append = [&](const std::size_t index)
            {
                if (std::ranges::find(keyboardEntries, index) == keyboardEntries.end())
                    keyboardEntries.push_back(index);
            };
            for (const auto& recent : m_NodeMenuSelection.Recent())
            {
                const auto found = std::ranges::find(catalog.Entries(), recent, &VfxNodeCatalogEntry::Id);
                if (found != catalog.Entries().end() && found->Enabled() &&
                    (backend == Keire::VfxBackend::Cpu ? found->CpuSupported : found->GpuSupported))
                {
                    append(static_cast<std::size_t>(found - catalog.Entries().begin()));
                }
            }
            for (const auto& match : matches)
            {
                if (match.BackendSupported)
                    append(match.EntryIndex);
                if (keyboardEntries.size() >= NodeMenuSelection::RecentCapacity)
                    break;
            }
        }
        else
        {
            for (const auto& match : matches)
                if (match.BackendSupported)
                    keyboardEntries.push_back(match.EntryIndex);
        }

        std::vector<std::string_view> keyboardIds;
        keyboardIds.reserve(keyboardEntries.size());
        for (const auto index : keyboardEntries)
            keyboardIds.push_back(catalog.Entries()[index].Id);
        m_NodeMenuSelection.Synchronize(keyboardIds);
        if (ui.Shortcut({.Key = Keire::UiKey::Up, .Global = true}))
            m_NodeMenuSelection.MovePrevious(keyboardIds);
        if (ui.Shortcut({.Key = Keire::UiKey::Down, .Global = true}))
            m_NodeMenuSelection.MoveNext(keyboardIds);
        activateSelected = ui.Shortcut({.Key = Keire::UiKey::Enter, .Global = true});

        if (filter.empty())
        {
            ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "RECENT & COMMON");
            for (const auto index : keyboardEntries)
                if (drawEntry(index, true))
                    return true;
            ui.Separator();
            if (targetContext)
            {
                ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "ALL COMPATIBLE BLOCKS");
                if (drawKind(PaletteActionKind::ContextBlock))
                    return true;
                if (drawKind(PaletteActionKind::PortableHlslBlock))
                    return true;
                if (actions.empty())
                    ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                                   "No compatible Runtime Modules are available for this Context.");
                return false;
            }
            if (auto contexts = ui.BeginMenu("Contexts"); contexts)
                if (drawKind(PaletteActionKind::Context))
                    return true;
            if (auto operators = ui.BeginMenu("Operators"); operators)
                if (drawKind(PaletteActionKind::Operator))
                    return true;
            if (auto modules = ui.BeginMenu("Legacy Module Nodes"); modules)
                if (drawKind(PaletteActionKind::RuntimeModule))
                    return true;
            if (auto parameters = ui.BeginMenu("Blackboard"); parameters)
            {
                if (definition.Blackboard.empty())
                    ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "No Blackboard properties.");
                else if (drawKind(PaletteActionKind::BlackboardParameter))
                    return true;
            }
            ui.Separator();
            if (drawKind(PaletteActionKind::CustomHlsl))
                return true;
        }
        else
        {
            for (const auto& match : matches)
                if (drawEntry(match.EntryIndex, true))
                    return true;
            if (matches.empty())
                ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "No nodes match this search.");
        }
        return false;
    }
} // namespace KeireEditor
