#include "KeireClientInternal/Editor/VfxEffectPanelInternal.h"

#include <algorithm>

namespace KeireEditor
{
    void VfxEffectPanel::Draw(Keire::UiFrame& ui)
    {
        if (!m_Registration.Visible())
        {
            if (m_WasVisible)
                StopTransientPreview();
            return;
        }
        ui.SetNextWindowSize({1040.0F, 640.0F});
        auto panel = ui.BeginPanel(m_Registration);
        if (!m_Registration.Visible())
        {
            StopTransientPreview();
            return;
        }
        m_WasVisible = true;
        if (!panel)
            return;

        auto& document = m_Controller.VfxEffectState();
        const auto& theme = m_Controller.VfxEffectTheme();
        if (ui.WindowFocused())
            m_Controller.ActivateVfxEffectHistory();
        if (!document.IsOpen())
        {
            ui.TextColored(theme.Accent, "VFX EFFECT");
            ui.Separator();
            ui.Text("No VFX Effect asset is open.");
            ui.TextColored(theme.MutedText, "Create or double-click a .keirevfx asset in the Project panel.");
            return;
        }

        DrawHeader(ui);
        DrawPreviewToolbar(ui);
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        if (const auto preview = m_Controller.VfxEffectPreviewDiagnostic(); !preview.empty())
            ui.TextColored(theme.Warning, preview);
        ui.Separator();

        if (auto tabs = ui.BeginTabBar("VfxEffectAuthoringTabs"); tabs)
        {
            if (auto graph = ui.BeginTabItem("Graph"); graph)
                DrawGraphEditor(ui);
            if (auto modules = ui.BeginTabItem("Runtime Modules"); modules)
                DrawModules(ui);
            if (auto blackboard = ui.BeginTabItem("Blackboard"); blackboard)
                DrawBlackboard(ui);
            if (auto settings = ui.BeginTabItem("Effect Settings"); settings)
                DrawEffectSettings(ui);
        }
    }

    void VfxEffectPanel::DrawHeader(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& theme = m_Controller.VfxEffectTheme();
        const auto database = m_Controller.VfxEffectDatabase();
        const auto record = database ? database->Find(document.Asset()) : std::nullopt;

        ui.TextColored(theme.Accent, "VFX GRAPH");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (document.Dirty() ? " *" : "") : "Missing asset");
        const bool legacy = document.Definition().ExecutionSource == Keire::VfxExecutionSource::LegacyModules;
        ui.TextColored(legacy ? theme.Warning : theme.Success,
                       legacy ? "EXECUTION: LEGACY RUNTIME MODULES" : "EXECUTION: GRAPH");
        if (!document.Publishable())
        {
            ui.TextColored(theme.Error, "GRAPH INCOMPLETE  |  PREVIEW FROZEN AT LAST VALID COMPILE");
            ui.TextColored(theme.Warning, document.GraphDiagnostic());
        }
        if (legacy)
        {
            ui.SameLine();
            if (ui.Button("Convert Runtime Modules to Graph"))
            {
                (void)ApplyAction("Converted Runtime Modules to executable graph",
                                  [&document] { return document.ConvertToGraph(); });
            }
            ui.TextColored(theme.MutedText,
                           "Conversion is undoable. It creates executable module/context nodes and typed cables.");
        }

        const auto save = [this]
        {
            try
            {
                m_Controller.SaveVfxEffectDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportVfxEffectError(m_Message);
            }
        };
        if (ui.Shortcut({Keire::UiKey::S, true}) && document.Dirty() && document.Publishable())
            save();
        if (auto disabled = ui.BeginDisabled(!document.Dirty() || !document.Publishable()); disabled)
            if (ui.Button("Save"))
                save();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.Dirty()); disabled)
        {
            if (ui.Button("Discard"))
            {
                try
                {
                    m_Controller.DiscardVfxEffectDocument();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportVfxEffectError(m_Message);
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Reload Source"))
        {
            try
            {
                m_Controller.ReloadVfxEffectDocument(document.Asset());
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportVfxEffectError(m_Message);
            }
        }
        ui.SameLine();
        const auto undo = document.UndoContext();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
                m_Controller.UndoVfxEffectEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
                m_Controller.RedoVfxEffectEdit();
        }
        ui.SameLine();
        if (ui.Button("Compile"))
        {
            try
            {
                const auto backend = m_Controller.VfxEffectPreviewState().Backend;
                const auto programs = Keire::CompileVfxEffectSystems(document.Definition(), backend);
                const auto warningCount = static_cast<std::size_t>(std::accumulate(
                    programs.begin(), programs.end(), std::size_t{0},
                    [](const std::size_t count, const auto& program)
                    {
                        return count + static_cast<std::size_t>(std::ranges::count(
                                           program.Diagnostics, Keire::VfxCompileDiagnosticSeverity::Warning,
                                           &Keire::VfxCompileDiagnostic::Severity));
                    }));
                const auto failed = std::ranges::find(programs, false, &Keire::VfxCompiledProgram::Valid);
                if (programs.empty() || failed != programs.end())
                {
                    m_Message = programs.empty() || failed->Diagnostics.empty()
                                    ? "Graph compilation failed."
                                    : "Graph compilation failed: " + failed->Diagnostics.front().Message;
                    m_Controller.ReportVfxEffectError(m_Message);
                }
                else
                {
                    const auto byteCount = std::accumulate(programs.begin(), programs.end(), std::size_t{0},
                                                           [](const std::size_t count, const auto& program)
                                                           { return count + program.CanonicalIr.size(); });
                    m_Message = "Compiled " + std::to_string(programs.size()) + " " +
                                std::string(EnumName(backend, PreviewBackends)) + " system program(s) (" +
                                std::to_string(byteCount) + " bytes, " + std::to_string(warningCount) + " warnings).";
                }
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportVfxEffectError(m_Message);
            }
        }
    }

    void VfxEffectPanel::DrawPreviewToolbar(Keire::UiFrame& ui)
    {
        auto status = m_Controller.VfxEffectPreviewState();
        const bool frozen = !m_Controller.VfxEffectState().Publishable();
        const auto& theme = m_Controller.VfxEffectTheme();
        ui.Separator();
        ui.TextColored(frozen ? theme.Warning : (status.Active ? theme.Success : theme.Warning),
                       frozen ? "PREVIEW FROZEN" : (status.Active ? "PREVIEW LIVE" : "PREVIEW IDLE"));
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(frozen); disabled)
        {
            if (ui.IconButton("RestartVfxPreview", Keire::UiIcon::Refresh))
                (void)ApplyAction("Restarted VFX preview",
                                  [this]
                                  {
                                      m_Controller.RestartVfxEffectPreview();
                                      return true;
                                  });
            ui.SetTooltip(frozen ? "Repair the graph before restarting preview."
                                 : "Restart the transient authoring preview.");
        }
        ui.SameLine();
        if (ui.IconButton("PauseVfxPreview", status.Paused ? Keire::UiIcon::Play : Keire::UiIcon::Pause, status.Paused))
        {
            m_Controller.SetVfxEffectPreviewPaused(!status.Paused);
            status.Paused = !status.Paused;
        }
        ui.SetTooltip(status.Paused ? "Resume the authoring preview." : "Pause the authoring preview.");
        ui.SameLine();

        auto autoRestart = status.AutoRestart;
        if (ui.Checkbox("Loop Preview", autoRestart))
            m_Controller.SetVfxEffectPreviewAutoRestart(autoRestart);
        ui.SameLine();
        auto backend = status.Backend;
        if (auto disabled = ui.BeginDisabled(frozen); disabled)
        {
            if (DrawEnum(ui, "Backend", backend, PreviewBackends))
                (void)ApplyAction("Changed VFX preview backend",
                                  [this, backend]
                                  {
                                      m_Controller.SetVfxEffectPreviewBackend(backend);
                                      return true;
                                  });
        }
        ui.SameLine();
        auto speed = status.Speed;
        if (ui.SliderFloat("Speed", speed, 0.05F, 4.0F))
            (void)ApplyAction("Changed VFX preview speed",
                              [this, speed]
                              {
                                  m_Controller.SetVfxEffectPreviewSpeed(speed);
                                  return true;
                              });
        ui.SameLine();
        const auto particleLabel = status.Backend == Keire::VfxBackend::Gpu ? " spawned (estimate)" : " active";
        ui.TextColored(theme.MutedText, std::to_string(status.ActiveParticles) + particleLabel + "  |  " +
                                            std::to_string(status.DroppedParticles) + " dropped");
    }

    void VfxEffectPanel::DrawEffectSettings(Keire::UiFrame& ui)
    {
        const auto& document = m_Controller.VfxEffectState();
        auto candidate = document.Definition();
        bool changed = false;

        ui.TextColored(m_Controller.VfxEffectTheme().Accent, "EFFECT SETTINGS");
        ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "Emitter ID: " + candidate.EmitterId.ToString());
        changed |= ui.InputText("Name", candidate.Name);
        changed |= ui.Checkbox("Loop", candidate.Loop);
        double duration = candidate.Duration;
        if (ui.DragScalar("Duration (s)", duration, 0.01, 0.001, 3600.0))
        {
            candidate.Duration = static_cast<float>(duration);
            changed = true;
        }
        changed |= DrawEnum(ui, "Simulation Space", candidate.Space, SimulationSpaces);
        std::int64_t seed = candidate.Seed;
        if (ui.DragInteger("Deterministic Seed", seed, 1.0, 0,
                           static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())))
        {
            candidate.Seed = static_cast<std::uint32_t>(seed);
            changed = true;
        }
        std::int64_t capacity = candidate.Capacity;
        if (ui.DragInteger("Capacity", capacity, 1.0, 1, 1'000'000))
        {
            candidate.Capacity = static_cast<std::uint32_t>(capacity);
            changed = true;
        }

        if (changed)
            (void)ApplyEdit("Edit VFX effect settings",
                            [candidate = std::move(candidate)](Keire::VfxEffectDefinition& definition) mutable
                            { definition = std::move(candidate); });
    }

    void VfxEffectPanel::DrawGraphEditor(Keire::UiFrame& ui)
    {
        const auto available = ui.ContentAvailable();
        if (available.Width < 780.0F)
        {
            if (auto tabs = ui.BeginTabBar("VfxCompactGraphTabs"); tabs)
            {
                if (auto canvas = ui.BeginTabItem("Canvas"); canvas)
                    DrawGraphCanvas(ui);
                if (auto systems = ui.BeginTabItem("Systems"); systems)
                    DrawGraphSystems(ui);
                if (auto inspector = ui.BeginTabItem("Inspector"); inspector)
                    DrawGraphInspector(ui);
            }
            return;
        }

        const float systemsWidth = std::clamp(available.Width * 0.2F, 190.0F, 240.0F);
        const float inspectorWidth = std::clamp(available.Width * 0.27F, 270.0F, 330.0F);
        constexpr float spacing = 14.0F;
        const float canvasWidth = available.Width - systemsWidth - inspectorWidth - spacing;

        if (auto systems = ui.BeginChild("VfxGraphSystems", {systemsWidth, 0.0F}, true); systems)
            DrawGraphSystems(ui);
        ui.SameLine();
        if (auto canvas = ui.BeginChild("VfxGraphCanvasHost", {canvasWidth, 0.0F}, true); canvas)
            DrawGraphCanvas(ui);
        ui.SameLine();
        if (auto inspector = ui.BeginChild("VfxGraphInspector", {inspectorWidth, 0.0F}, true); inspector)
            DrawGraphInspector(ui);
    }

    void VfxEffectPanel::DrawGraphSystems(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        if (!m_SelectedSystem || std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id) ==
                                     definition.Systems.end())
        {
            m_SelectedSystem = definition.Systems.empty() ? Keire::AssetId{} : definition.Systems.front().Id;
            m_SelectedNode = {};
            m_SelectedBlock = {};
            m_SelectedConnection = {};
            m_GraphCanvas.CancelInteractions();
        }

        ui.TextColored(theme.Accent, "SYSTEMS");
        ui.TextColored(theme.MutedText, "Schema v" + std::to_string(definition.SchemaVersion));
        ui.TextColored(definition.ExecutionSource == Keire::VfxExecutionSource::Graph ? theme.Success : theme.Warning,
                       definition.ExecutionSource == Keire::VfxExecutionSource::Graph
                           ? "Context Blocks, graph nodes, and typed cables are the executable program."
                           : "Runtime Modules remain authoritative until this asset is converted.");
        for (const auto& system : definition.Systems)
        {
            auto id = ui.PushId(system.Id.ToString());
            const auto label = system.Name + "  [" + std::to_string(system.Nodes.size()) + "]";
            if (ui.Selectable(label, system.Id == m_SelectedSystem))
            {
                m_SelectedSystem = system.Id;
                m_SelectedNode = {};
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.CancelInteractions();
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectBlock(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
            }
        }
        const bool executableGraph = definition.ExecutionSource == Keire::VfxExecutionSource::Graph;
        if (auto disabled = ui.BeginDisabled(executableGraph); disabled)
        {
            if (ui.Button("+ Add System"))
            {
                Keire::VfxGraphSystem system;
                system.Id = Keire::AssetId::Generate();
                system.Name = UniqueName(definition.Systems, "Particle System", &Keire::VfxGraphSystem::Name);
                const auto id = system.Id;
                if (ApplyAction("Added VFX graph system", [&document, system = std::move(system)]() mutable
                                { return document.AddSystem(std::move(system)); }))
                {
                    m_SelectedSystem = id;
                    m_SelectedNode = {};
                    m_SelectedBlock = {};
                    return;
                }
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_SelectedSystem || executableGraph); disabled)
        {
            if (ui.Button("Remove") && ApplyAction("Removed VFX graph system", [&document, system = m_SelectedSystem]
                                                   { return document.RemoveSystem(system); }))
            {
                m_SelectedSystem = {};
                m_SelectedNode = {};
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.CancelInteractions();
                return;
            }
        }
        if (executableGraph)
            ui.TextColored(theme.MutedText, "Executable Graph mode owns one particle system.");

        ui.Separator();
        ui.TextColored(theme.Accent, "BLACKBOARD");
        ui.TextColored(theme.MutedText, "Drag into the graph from Add Node / Blackboard");
        for (const auto& parameter : definition.Blackboard)
        {
            auto id = ui.PushId(parameter.Id.ToString());
            const auto label = parameter.Name + "  :  " + std::string(EnumName(parameter.Type, ValueTypes));
            if (ui.Selectable(label, parameter.Id == m_SelectedParameter))
                m_SelectedParameter = parameter.Id;
        }
        if (definition.Blackboard.empty())
            ui.TextColored(theme.MutedText, "No exposed properties.");
        if (ui.Button("+ Parameter"))
        {
            Keire::VfxBlackboardParameter parameter;
            parameter.Id = Keire::AssetId::Generate();
            parameter.Name = UniqueName(definition.Blackboard, "New Parameter", &Keire::VfxBlackboardParameter::Name);
            const auto id = parameter.Id;
            if (ApplyAction("Added VFX blackboard parameter", [&document, parameter = std::move(parameter)]() mutable
                            { return document.AddBlackboardParameter(std::move(parameter)); }))
            {
                m_SelectedParameter = id;
                return;
            }
        }

        ui.Separator();
        std::size_t nodes = 0;
        std::size_t blocks = 0;
        std::size_t links = 0;
        for (const auto& system : definition.Systems)
        {
            nodes += system.Nodes.size();
            for (const auto& node : system.Nodes)
                blocks += node.Blocks.size();
            links += system.Connections.size();
        }
        ui.TextColored(theme.MutedText, std::to_string(nodes) + " nodes  |  " + std::to_string(blocks) +
                                            " Blocks  |  " + std::to_string(links) + " cables");
        if (definition.ExecutionSource == Keire::VfxExecutionSource::Graph && document.Publishable())
            ui.TextColored(theme.Success, "Connected graph nodes execute on the selected preview backend.");
        else if (definition.ExecutionSource == Keire::VfxExecutionSource::Graph)
            ui.TextColored(theme.Warning, "Repair the graph to resume compilation, preview, and saving.");
        else
            ui.TextColored(theme.Warning, "Convert this legacy asset before graph edits affect simulation.");
    }

    void VfxEffectPanel::DrawBlackboard(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        if (!m_SelectedParameter ||
            std::ranges::find(definition.Blackboard, m_SelectedParameter, &Keire::VfxBlackboardParameter::Id) ==
                definition.Blackboard.end())
        {
            m_SelectedParameter = definition.Blackboard.empty() ? Keire::AssetId{} : definition.Blackboard.front().Id;
            m_AssetPicker.Clear();
        }

        if (auto list = ui.BeginChild("VfxBlackboardList", {260.0F, 0.0F}, true); list)
        {
            ui.TextColored(theme.Accent, "EXPOSED PROPERTIES");
            ui.TextColored(theme.MutedText,
                           "Drag properties into the graph as executable nodes. Exposed values support emitter "
                           "overrides.");
            for (const auto& parameter : definition.Blackboard)
            {
                auto id = ui.PushId(parameter.Id.ToString());
                if (ui.Selectable(parameter.Name + "  :  " + std::string(EnumName(parameter.Type, ValueTypes)),
                                  parameter.Id == m_SelectedParameter))
                {
                    m_SelectedParameter = parameter.Id;
                    m_AssetPicker.Clear();
                }
            }
            if (ui.Button("+ Add Property"))
            {
                Keire::VfxBlackboardParameter parameter;
                parameter.Id = Keire::AssetId::Generate();
                parameter.Name =
                    UniqueName(definition.Blackboard, "New Parameter", &Keire::VfxBlackboardParameter::Name);
                const auto id = parameter.Id;
                if (ApplyAction("Added VFX blackboard parameter",
                                [&document, parameter = std::move(parameter)]() mutable
                                { return document.AddBlackboardParameter(std::move(parameter)); }))
                {
                    m_SelectedParameter = id;
                    return;
                }
            }
        }
        ui.SameLine();
        if (auto inspector = ui.BeginChild("VfxBlackboardInspector", {}, true); inspector)
        {
            const auto selected =
                std::ranges::find(definition.Blackboard, m_SelectedParameter, &Keire::VfxBlackboardParameter::Id);
            if (selected == definition.Blackboard.end())
            {
                ui.TextColored(theme.MutedText, "Select or create a blackboard property.");
                return;
            }

            auto parameter = *selected;
            bool changed = false;
            ui.TextColored(theme.Accent, "PROPERTY INSPECTOR");
            ui.TextColored(theme.MutedText, "Stable ID: " + parameter.Id.ToString());
            changed |= ui.InputText("Name", parameter.Name);
            changed |= ui.Checkbox("Exposed", parameter.Exposed);
            auto type = parameter.Type;
            if (DrawEnum(ui, "Type", type, ValueTypes))
            {
                parameter.Type = type;
                parameter.DefaultValue = Keire::DefaultVfxValue(type);
                changed = true;
                m_AssetPicker.Clear();
            }

            switch (parameter.Type)
            {
            case Keire::VfxValueType::Boolean:
                changed |= ui.Checkbox("Default", std::get<bool>(parameter.DefaultValue));
                break;
            case Keire::VfxValueType::Integer:
                changed |= ui.DragInteger("Default", std::get<std::int64_t>(parameter.DefaultValue));
                break;
            case Keire::VfxValueType::Scalar:
            {
                double value = std::get<float>(parameter.DefaultValue);
                if (ui.DragScalar("Default", value, 0.01))
                {
                    parameter.DefaultValue = static_cast<float>(value);
                    changed = true;
                }
                break;
            }
            case Keire::VfxValueType::Vector2:
                changed |= ui.DragVector2("Default", std::get<Keire::Vector2>(parameter.DefaultValue), 0.01F);
                break;
            case Keire::VfxValueType::Vector3:
                changed |= ui.DragVector3("Default", std::get<Keire::Vector3>(parameter.DefaultValue), 0.01F);
                break;
            case Keire::VfxValueType::Color:
            {
                auto& value = std::get<Keire::Color>(parameter.DefaultValue);
                Keire::UiColor color{value.Red, value.Green, value.Blue, value.Alpha};
                if (ui.ColorEdit("Default", color))
                {
                    value = {color.Red, color.Green, color.Blue, color.Alpha};
                    changed = true;
                }
                break;
            }
            case Keire::VfxValueType::Texture:
            case Keire::VfxValueType::Mesh:
            case Keire::VfxValueType::Asset:
            {
                auto& value = std::get<Keire::AssetId>(parameter.DefaultValue);
                std::optional<Keire::AssetTypeId> expected;
                if (parameter.Type == Keire::VfxValueType::Texture)
                    expected = Keire::Texture2DAsset::StaticType();
                else if (parameter.Type == Keire::VfxValueType::Mesh)
                    expected = Keire::MeshAsset::StaticType();
                AssetPickerOptions options{
                    .Label = "Default",
                    .ExpectedType = expected,
                    .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealVfxEffectAsset(asset); },
                };
                changed |= m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), value, options);
                break;
            }
            default:
                break;
            }

            if (changed)
            {
                const auto parameterId = parameter.Id;
                (void)ApplyAction("Edited VFX blackboard parameter",
                                  [&document, parameterId, parameter = std::move(parameter)]() mutable
                                  {
                                      return document.EditBlackboardParameter(
                                          parameterId, [parameter = std::move(parameter)](
                                                           Keire::VfxBlackboardParameter& candidate) mutable
                                          { candidate = std::move(parameter); });
                                  });
                return;
            }
            if (ui.Button("Remove Property") &&
                ApplyAction("Removed VFX blackboard parameter",
                            [&document, id = selected->Id] { return document.RemoveBlackboardParameter(id); }))
            {
                m_SelectedParameter = {};
                m_AssetPicker.Clear();
            }
        }
    }

    void VfxEffectPanel::DrawModules(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        if (!m_SelectedModule || std::ranges::find(definition.Modules, m_SelectedModule,
                                                   &Keire::VfxModuleDefinition::Id) == definition.Modules.end())
        {
            m_SelectedModule = definition.Modules.empty() ? Keire::AssetId{} : definition.Modules.front().Id;
            m_AssetPicker.Clear();
        }

        if (auto stack = ui.BeginChild("VfxModuleStack", {270.0F, 0.0F}, true); stack)
        {
            ui.TextColored(m_Controller.VfxEffectTheme().Accent,
                           definition.ExecutionSource == Keire::VfxExecutionSource::Graph ? "MODULE PAYLOADS"
                                                                                          : "MODULE STACK");
            ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                           definition.ExecutionSource == Keire::VfxExecutionSource::Graph
                               ? "Graph mode executes payloads in Context Block order. Legacy Module nodes remain "
                                 "readable for migrated assets."
                               : "Legacy mode executes enabled modules directly in stack order.");
            for (const auto& module : definition.Modules)
            {
                auto id = ui.PushId(module.Id.ToString());
                const auto disabledLabel = definition.ExecutionSource == Keire::VfxExecutionSource::Graph
                                               ? std::string("[New Blocks Default Off] ")
                                               : std::string("[Disabled] ");
                const auto label =
                    std::string(module.Enabled ? "" : disabledLabel) + std::string(ModuleName(module.Payload));
                if (ui.Selectable(label, module.Id == m_SelectedModule))
                {
                    m_SelectedModule = module.Id;
                    m_AssetPicker.Clear();
                }
            }
            ui.Separator();

            if (auto add = ui.BeginCombo("Add Module", "Choose..."); add)
            {
                const auto addModule =
                    [&](const std::string_view label, Keire::VfxModulePayload payload, const bool available)
                {
                    if (!ui.MenuItem(label, false, available))
                        return false;
                    Keire::VfxModuleDefinition module{
                        .Id = Keire::AssetId::Generate(),
                        .Enabled = true,
                        .Payload = std::move(payload),
                    };
                    const auto id = module.Id;
                    if (ApplyAction("Added VFX module", [&document, module = std::move(module)]() mutable
                                    { return document.AddModule(std::move(module)); }))
                    {
                        m_SelectedModule = id;
                        m_AssetPicker.Clear();
                    }
                    ui.CloseCurrentPopup();
                    return true;
                };

                if (addModule("Emission Rate", Keire::VfxEmissionRateModule{},
                              !ContainsModule<Keire::VfxEmissionRateModule>(definition)) ||
                    addModule("Burst", Keire::VfxBurstModule{}, BurstCount(definition) < 32) ||
                    addModule("Shape", Keire::VfxShapeModule{}, !ContainsModule<Keire::VfxShapeModule>(definition)) ||
                    addModule("Initialize", Keire::VfxInitializeModule{},
                              !ContainsModule<Keire::VfxInitializeModule>(definition)) ||
                    addModule("Forces", Keire::VfxForceModule{}, !ContainsModule<Keire::VfxForceModule>(definition)) ||
                    addModule("Size over Lifetime", Keire::VfxSizeOverLifetimeModule{},
                              !ContainsModule<Keire::VfxSizeOverLifetimeModule>(definition)) ||
                    addModule("Color over Lifetime", Keire::VfxColorOverLifetimeModule{},
                              !ContainsModule<Keire::VfxColorOverLifetimeModule>(definition)) ||
                    addModule("Collision", Keire::VfxCollisionModule{},
                              !ContainsModule<Keire::VfxCollisionModule>(definition)) ||
                    addModule("Renderer", Keire::VfxRendererModule{},
                              !ContainsModule<Keire::VfxRendererModule>(definition)))
                {
                    return;
                }
            }

            const auto selected =
                std::ranges::find(definition.Modules, m_SelectedModule, &Keire::VfxModuleDefinition::Id);
            const auto selectedIndex =
                selected == definition.Modules.end()
                    ? definition.Modules.size()
                    : static_cast<std::size_t>(std::distance(definition.Modules.begin(), selected));
            if (auto disabled = ui.BeginDisabled(selected == definition.Modules.end()); disabled)
            {
                if (ui.Button("Remove") && ApplyAction("Removed VFX module", [&document, module = m_SelectedModule]
                                                       { return document.RemoveModule(module); }))
                {
                    m_SelectedModule = {};
                    m_AssetPicker.Clear();
                    return;
                }
                ui.SameLine();
                if (auto upDisabled = ui.BeginDisabled(selectedIndex == 0); upDisabled)
                {
                    if (ui.Button("Up") &&
                        ApplyAction("Reordered VFX module", [&document, module = m_SelectedModule, selectedIndex]
                                    { return document.MoveModule(module, selectedIndex - 1); }))
                    {
                        return;
                    }
                }
                ui.SameLine();
                if (auto downDisabled = ui.BeginDisabled(selectedIndex == definition.Modules.size() ||
                                                         selectedIndex + 1 >= definition.Modules.size());
                    downDisabled)
                {
                    if (ui.Button("Down") &&
                        ApplyAction("Reordered VFX module", [&document, module = m_SelectedModule, selectedIndex]
                                    { return document.MoveModule(module, selectedIndex + 1); }))
                    {
                        return;
                    }
                }
            }
        }
        ui.SameLine();
        if (auto inspector = ui.BeginChild("VfxModuleInspector", {}, true); inspector)
            DrawSelectedModule(ui);
    }

} // namespace KeireEditor
