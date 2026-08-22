#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanelModel.h"
#include "KeireClient/Editor/VfxNodeCatalog.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace KeireEditor
{
    namespace
    {
        using Detail::BurstCount;
        using Detail::ContextColor;
        using Detail::ModuleName;
        using Detail::ModuleRunsInContext;
        using Detail::NewContextNode;
        using Detail::NewCustomHlslNode;
        using Detail::NewParameterNode;
        using Detail::NodeColor;
        using Detail::NodeLabel;
        using Detail::PinColor;
        using Detail::PreferredCanvasId;

        template <typename... Ts> struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

        template <typename Value> struct EnumEntry
        {
            Value Type;
            std::string_view Name;
        };

        constexpr std::array SimulationSpaces{
            EnumEntry{Keire::VfxSimulationSpace::Local, std::string_view("Local")},
            EnumEntry{Keire::VfxSimulationSpace::World, std::string_view("World")},
        };
        constexpr std::array ParticleDataTypes{
            EnumEntry{Keire::VfxParticleDataType::Particle, std::string_view("Particle")},
            EnumEntry{Keire::VfxParticleDataType::ParticleStrip, std::string_view("Particle Strip")},
        };
        constexpr std::array PreviewBackends{
            EnumEntry{Keire::VfxBackend::Cpu, std::string_view("CPU (Authoring)")},
            EnumEntry{Keire::VfxBackend::Gpu, std::string_view("GPU (Runtime)")},
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
        constexpr std::array GraphValueTypes{
            EnumEntry{Keire::VfxValueType::Boolean, std::string_view("Boolean")},
            EnumEntry{Keire::VfxValueType::Integer, std::string_view("Integer")},
            EnumEntry{Keire::VfxValueType::UnsignedInteger, std::string_view("Unsigned Integer")},
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Vector4, std::string_view("Vector 4")},
            EnumEntry{Keire::VfxValueType::Quaternion, std::string_view("Quaternion")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
            EnumEntry{Keire::VfxValueType::Matrix, std::string_view("Matrix")},
            EnumEntry{Keire::VfxValueType::Curve, std::string_view("Curve")},
            EnumEntry{Keire::VfxValueType::Gradient, std::string_view("Gradient")},
            EnumEntry{Keire::VfxValueType::ScalarRange, std::string_view("Scalar Range")},
            EnumEntry{Keire::VfxValueType::IntegerRange, std::string_view("Integer Range")},
            EnumEntry{Keire::VfxValueType::UnsignedIntegerRange, std::string_view("Unsigned Integer Range")},
            EnumEntry{Keire::VfxValueType::Vector2Range, std::string_view("Vector 2 Range")},
            EnumEntry{Keire::VfxValueType::Vector3Range, std::string_view("Vector 3 Range")},
            EnumEntry{Keire::VfxValueType::Vector4Range, std::string_view("Vector 4 Range")},
            EnumEntry{Keire::VfxValueType::ColorRange, std::string_view("Color Range")},
            EnumEntry{Keire::VfxValueType::Texture, std::string_view("Texture")},
            EnumEntry{Keire::VfxValueType::Texture2DArray, std::string_view("Texture 2D Array")},
            EnumEntry{Keire::VfxValueType::Texture3D, std::string_view("Texture 3D")},
            EnumEntry{Keire::VfxValueType::TextureCube, std::string_view("Texture Cube")},
            EnumEntry{Keire::VfxValueType::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxValueType::Buffer, std::string_view("Buffer")},
            EnumEntry{Keire::VfxValueType::PointCache, std::string_view("Point Cache")},
            EnumEntry{Keire::VfxValueType::SignedDistanceField, std::string_view("Signed Distance Field")},
            EnumEntry{Keire::VfxValueType::Asset, std::string_view("Asset")},
            EnumEntry{Keire::VfxValueType::ParticleStream, std::string_view("Particle Stream")},
        };
        constexpr std::array CustomHlslValueTypes{
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Vector4, std::string_view("Vector 4")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
        };
        template <typename Value, std::size_t Size>
        [[nodiscard]] std::string_view EnumName(const Value value, const std::array<EnumEntry<Value>, Size>& entries)
        {
            const auto found = std::ranges::find(entries, value, &EnumEntry<Value>::Type);
            return found == entries.end() ? std::string_view("Unsupported") : found->Name;
        }
        template <typename Value, std::size_t Size>
        [[nodiscard]] bool DrawEnum(Keire::UiFrame& ui, const std::string_view label, Value& value,
                                    const std::array<EnumEntry<Value>, Size>& entries)
        {
            bool changed = false;
            if (auto combo = ui.BeginCombo(label, EnumName(value, entries)); combo)
            {
                for (const auto& entry : entries)
                {
                    if (ui.Selectable(entry.Name, entry.Type == value))
                    {
                        value = entry.Type;
                        changed = true;
                    }
                }
            }
            return changed;
        }
        template <typename Module> [[nodiscard]] bool ContainsModule(const Keire::VfxEffectDefinition& definition)
        {
            return std::ranges::any_of(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                       { return std::holds_alternative<Module>(module.Payload); });
        }

        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueName(const Range& values, const std::string_view base, Projection projection)
        {
            std::string candidate(base);
            for (std::size_t suffix = 2; std::ranges::any_of(values, [&](const auto& value)
                                                             { return std::invoke(projection, value) == candidate; });
                 ++suffix)
            {
                candidate = std::string(base) + " " + std::to_string(suffix);
            }
            return candidate;
        }
    } // namespace

    void VfxEffectPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.vfx-effect", "VFX Effect", false});
    }

    bool VfxEffectPanel::ApplyEdit(const std::string_view name,
                                   const std::function<void(Keire::VfxEffectDefinition&)>& operation)
    {
        try
        {
            const bool changed = m_Controller.VfxEffectState().Edit(name, operation);
            if (changed)
                m_Message = std::string(name) + ".";
            return changed;
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
            m_Controller.ReportVfxEffectError(m_Message);
            return false;
        }
    }

    bool VfxEffectPanel::ApplyAction(const std::string_view name, const std::function<bool()>& operation)
    {
        try
        {
            const bool changed = operation();
            if (changed)
                m_Message = std::string(name) + ".";
            return changed;
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
            m_Controller.ReportVfxEffectError(m_Message);
            return false;
        }
    }

    void VfxEffectPanel::Draw(Keire::UiFrame& ui)
    {
        if (!m_Registration.Visible())
        {
            if (m_WasVisible)
                StopTransientPreview();
            return;
        }
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

    void VfxEffectPanel::DrawGraphCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        if (DrawGraphMultiSelectionInspector(ui))
            return;
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
        {
            ui.TextColored(theme.MutedText, "Create or select a system to begin authoring.");
            return;
        }
        const auto graphViewportSize = ui.ContentAvailable();
        ui.TextColored(theme.Accent, system->Name);
        ui.SameLine();
        bool nodePaletteMenuOpen = false;
        if (auto disabled = ui.BeginDisabled(definition.ExecutionSource != Keire::VfxExecutionSource::Graph); disabled)
        {
            if (auto add = ui.BeginCombo("Add Node", "Choose..."); add)
            {
                nodePaletteMenuOpen = true;
                if (!m_NodePaletteMenuOpen)
                {
                    m_NodePaletteSearch.clear();
                    m_NodeMenuSelection.Open();
                }
                if (m_NodeMenuSelection.ConsumeFocusRequest())
                    ui.RequestKeyboardFocus();
                (void)ui.InputTextWithHint("##VfxToolbarNodeSearch", "Search nodes...", m_NodePaletteSearch);
                ui.Separator();
                const auto zoom = m_GraphCanvas.Zoom();
                const Keire::Vector2 position{graphViewportSize.Width * 0.5F / zoom - m_GraphCanvas.Pan().X - 130.0F,
                                              graphViewportSize.Height * 0.4F / zoom - m_GraphCanvas.Pan().Y - 48.0F};
                if (DrawNodePaletteEntries(ui, system->Id, position, m_NodePaletteSearch))
                    return;
            }
        }
        m_NodePaletteMenuOpen = nodePaletteMenuOpen;
        if (definition.ExecutionSource != Keire::VfxExecutionSource::Graph)
        {
            ui.SameLine();
            ui.TextColored(theme.Warning, "Convert to Graph to add executable nodes.");
        }
        std::vector<NodeGraphNode> nodes;
        nodes.reserve(system->Nodes.size());
        std::vector<std::pair<StableNodeId, Keire::AssetId>> nodeIdentities;
        StableNodeGraphIdMap nodeIds;
        StableNodeGraphIdMap blockIds;
        StableNodeGraphIdMap pinIds;
        for (const auto& node : system->Nodes)
        {
            const auto inputs =
                static_cast<std::size_t>(std::ranges::count(node.Pins, true, &Keire::VfxGraphPin::Input));
            const auto outputs = node.Pins.size() - inputs;
            const auto rows = std::max(inputs, outputs);
            NodeGraphNode canvasNode{
                .Id = nodeIds.Assign(node.Id, PreferredCanvasId(node.Id, 0x5646584e4f444501ULL)),
                .Label = NodeLabel(definition, node),
                .Position = node.EditorPosition,
                .Size = {260.0F, std::max(98.0F, 70.0F + static_cast<float>(rows) * 24.0F)},
                .Color = NodeColor(node),
                .Subtitle = std::string(VfxGraphNodeKindLabel(node.Kind)) + "  |  " +
                            std::string(EnumName(node.Context, ContextTypes)),
            };
            nodeIdentities.emplace_back(canvasNode.Id, node.Id);
            canvasNode.Pins.reserve(node.Pins.size());
            for (const auto& pin : node.Pins)
            {
                canvasNode.Pins.push_back(
                    {.Id = pinIds.Assign(pin.Id, PreferredCanvasId(pin.Id, 0x56465850494e0001ULL)),
                     .Label = pin.Name + "  [" + std::string(EnumName(pin.Type, GraphValueTypes)) + "]",
                     .Direction = pin.Input ? NodeGraphPinDirection::Input : NodeGraphPinDirection::Output,
                     .Type = static_cast<StableNodeId>(pin.Type) + 1,
                     .Color = PinColor(pin.Type)});
            }
            canvasNode.Blocks.reserve(node.Blocks.size());
            canvasNode.Deletable = node.Kind != Keire::VfxGraphNodeKind::Context;
            for (const auto& block : node.Blocks)
            {
                NodeGraphBlockRow row{
                    .Id = blockIds.Assign(block.Id, PreferredCanvasId(block.Id, 0x564658424c4f434bULL)),
                    .Label = block.Type,
                    .Enabled = block.Enabled,
                    .Color = {0.12F, 0.16F, 0.22F, 1.0F},
                };
                row.Pins.reserve(block.Pins.size());
                for (const auto& pin : block.Pins)
                {
                    row.Pins.push_back(
                        {.Id = pinIds.Assign(pin.Id, PreferredCanvasId(pin.Id, 0x5646584250494e01ULL)),
                         .Label = pin.Name + "  [" + std::string(EnumName(pin.Type, GraphValueTypes)) + "]",
                         .Direction = pin.Input ? NodeGraphPinDirection::Input : NodeGraphPinDirection::Output,
                         .Type = static_cast<StableNodeId>(pin.Type) + 1,
                         .Color = PinColor(pin.Type)});
                }
                canvasNode.Blocks.push_back(std::move(row));
            }
            nodes.push_back(std::move(canvasNode));
        }
        std::vector<NodeGraphConnection> connections;
        connections.reserve(system->Connections.size());
        ApplyNodeGraphAnnotations(system->Authoring, nodeIdentities, nodes);
        auto comments = BuildNodeGraphCommentModel(system->Authoring, nodeIdentities);
        StableNodeGraphIdMap connectionIds;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> connectionIdentities;
        const auto findEndpointPin = [&](const Keire::VfxGraphEndpoint endpoint) -> const Keire::VfxGraphPin*
        {
            const auto node = std::ranges::find(system->Nodes, endpoint.Node, &Keire::VfxGraphNode::Id);
            if (node == system->Nodes.end())
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
        };
        for (const auto& connection : system->Connections)
        {
            const auto source = nodeIds.Find(connection.OutputNode);
            const auto target = nodeIds.Find(connection.InputNode);
            const auto sourceBlock =
                connection.OutputBlock ? blockIds.Find(connection.OutputBlock) : std::optional<StableNodeId>{0};
            const auto targetBlock =
                connection.InputBlock ? blockIds.Find(connection.InputBlock) : std::optional<StableNodeId>{0};
            const auto sourcePin = pinIds.Find(connection.OutputPin);
            const auto targetPin = pinIds.Find(connection.InputPin);
            if (!source || !target || !sourceBlock || !targetBlock || !sourcePin || !targetPin)
                continue;
            std::string label;
            const auto* graphPin = findEndpointPin(connection.OutputEndpoint());
            if (graphPin && graphPin->Type != Keire::VfxValueType::ParticleStream)
                label = std::string(EnumName(graphPin->Type, GraphValueTypes));
            connections.push_back({
                .Id = connectionIds.Assign(connection.Id, PreferredCanvasId(connection.Id, 0x5646584c494e4b01ULL)),
                .Source = *source,
                .Target = *target,
                .Label = std::move(label),
                .SourcePin = *sourcePin,
                .TargetPin = *targetPin,
                .SourceBlock = *sourceBlock,
                .TargetBlock = *targetBlock,
                .RoutingPoints = connection.RoutingPoints,
            });
            connectionIdentities.emplace_back(connections.back().Id, connection.Id);
        }
        if (m_SelectedNode && !nodeIds.Find(m_SelectedNode))
        {
            m_SelectedNode = {};
            m_SelectedBlock = {};
        }
        std::optional<NodeGraphBlockAddress> selectedBlock;
        if (m_SelectedBlock)
        {
            const auto owner =
                std::ranges::find_if(system->Nodes,
                                     [&](const Keire::VfxGraphNode& node)
                                     {
                                         return std::ranges::find(node.Blocks, m_SelectedBlock,
                                                                  &Keire::VfxGraphBlock::Id) != node.Blocks.end();
                                     });
            const auto canvasNode = owner == system->Nodes.end() ? std::nullopt : nodeIds.Find(owner->Id);
            const auto canvasBlock = blockIds.Find(m_SelectedBlock);
            if (!canvasNode || !canvasBlock)
                m_SelectedBlock = {};
            else
            {
                m_SelectedNode = owner->Id;
                selectedBlock = NodeGraphBlockAddress{*canvasNode, *canvasBlock};
            }
        }
        if (m_SelectedConnection && !connectionIds.Find(m_SelectedConnection))
            m_SelectedConnection = {};
        SynchronizeGraphSelection(m_GraphCanvas, nodeIdentities, m_SelectedNodes,
                                  m_SelectedNode ? std::optional(m_SelectedNode) : std::nullopt);
        m_GraphCanvas.SelectBlock(selectedBlock);
        m_GraphCanvas.SelectConnection(connectionIds.Find(m_SelectedConnection));
        if (ui.Button("Frame All"))
            m_GraphCanvas.Focus(nodes, ui.ContentAvailable());
        ui.SameLine();
        if (DrawGraphArrangeMenu(ui, nodes, connections, nodeIdentities, connectionIdentities))
            return;
        ui.SameLine();
        (void)DrawGraphBookmarkMenu(ui, m_GraphBookmarks, m_GraphCanvas);
        ui.SameLine();
        if (m_GraphCanvas.ConnectionDragActive())
            ui.TextColored(theme.Warning, "Release over a compatible pin  |  Escape cancels");
        else
            ui.TextColored(theme.MutedText,
                           "Right-click to add  |  drag pins to connect  |  double-click cables for routing points");
        const auto findGraphNode = [&](const StableNodeId canvasId) -> const Keire::VfxGraphNode*
        {
            const auto found = std::ranges::find_if(system->Nodes, [&](const Keire::VfxGraphNode& node)
                                                    { return nodeIds.Find(node.Id) == canvasId; });
            return found == system->Nodes.end() ? nullptr : std::addressof(*found);
        };
        const auto findGraphBlock = [&](const NodeGraphBlockAddress address)
            -> std::optional<std::pair<const Keire::VfxGraphNode*, const Keire::VfxGraphBlock*>>
        {
            const auto* node = findGraphNode(address.Node);
            if (!node)
                return std::nullopt;
            const auto block = std::ranges::find_if(node->Blocks, [&](const Keire::VfxGraphBlock& candidate)
                                                    { return blockIds.Find(candidate.Id) == address.Block; });
            if (block == node->Blocks.end())
                return std::nullopt;
            return std::pair{node, std::addressof(*block)};
        };
        const auto findGraphPin = [&](const StableNodeId canvasNode, const StableNodeId canvasBlock,
                                      const StableNodeId canvasPin) -> std::optional<Keire::VfxGraphEndpoint>
        {
            const auto* node = findGraphNode(canvasNode);
            if (!node)
                return std::nullopt;
            if (canvasBlock)
            {
                const auto block = std::ranges::find_if(node->Blocks, [&](const Keire::VfxGraphBlock& candidate)
                                                        { return blockIds.Find(candidate.Id) == canvasBlock; });
                if (block == node->Blocks.end())
                    return std::nullopt;
                const auto pin = std::ranges::find_if(block->Pins, [&](const Keire::VfxGraphPin& candidate)
                                                      { return pinIds.Find(candidate.Id) == canvasPin; });
                if (pin == block->Pins.end())
                    return std::nullopt;
                return Keire::VfxGraphEndpoint{node->Id, block->Id, pin->Id};
            }
            const auto pin = std::ranges::find_if(node->Pins, [&](const Keire::VfxGraphPin& candidate)
                                                  { return pinIds.Find(candidate.Id) == canvasPin; });
            if (pin == node->Pins.end())
                return std::nullopt;
            return Keire::VfxGraphEndpoint{node->Id, {}, pin->Id};
        };
        const auto findGraphConnection = [&](const StableNodeId canvasId) -> const Keire::VfxGraphConnection*
        {
            const auto found =
                std::ranges::find_if(system->Connections, [&](const Keire::VfxGraphConnection& connection)
                                     { return connectionIds.Find(connection.Id) == canvasId; });
            return found == system->Connections.end() ? nullptr : std::addressof(*found);
        };
        NodeGraphCanvasOptions options{
            .Editable = true,
            .ValidateConnection =
                [&](const NodeGraphConnectionRequest& request)
            {
                const auto source = findGraphPin(request.SourceNode, request.SourceBlock, request.SourcePin);
                const auto target = findGraphPin(request.TargetNode, request.TargetBlock, request.TargetPin);
                if (!source || !target)
                {
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                         "A connection endpoint is unavailable."};
                }
                const auto check = document.CheckConnection(system->Id, *source, *target);
                switch (check.Status)
                {
                case VfxGraphConnectionStatus::Accepted:
                    if (check.ReplacesInput)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                             "Replaces the cable currently driving this input."};
                    }
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Accept, {}};
                case VfxGraphConnectionStatus::AcceptedWithWarning:
                {
                    auto diagnostic = std::string("Connection is valid; the graph remains incomplete.");
                    if (!check.Diagnostic.empty())
                        diagnostic += " " + check.Diagnostic;
                    if (check.ReplacesInput)
                        diagnostic += " The existing input cable will be replaced.";
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                         std::move(diagnostic)};
                }
                case VfxGraphConnectionStatus::Rejected:
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject, check.Diagnostic};
                }
                return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                     "The connection could not be validated."};
            },
            .EditableReroutes = true,
            .MultiSelection = true,
            .Comments = comments.Comments,
        };
        const auto result = m_GraphCanvas.Draw(ui, "VfxNodeCanvas", nodes, connections, options);
        DrawGraphComments(ui, system->Id, nodeIdentities, nodes, comments, result);
        m_SelectedNodes = ResolveGraphSelection(result.SelectedNodes, nodeIdentities);
        m_SelectedNode = m_SelectedNodes.empty() ? Keire::AssetId{} : m_SelectedNodes.back();
        if (HandleGraphClipboard(result, nodeIdentities))
            return;
        if (!result.DuplicateNodesRequested.empty())
            return DuplicateGraphSelection(result.DuplicateNodesRequested, nodeIdentities);
        const auto renderedCanvasSize = ui.LastItemRect().Size();
        const auto setRouting = [&](const StableNodeId canvasConnection, std::vector<Keire::Vector2> routing) -> bool
        {
            const auto* connection = findGraphConnection(canvasConnection);
            if (!connection)
                return false;
            return ApplyAction("Routed VFX graph cable", [&document, graph = system->Id, connection = connection->Id,
                                                          routing = std::move(routing)]() mutable
                               { return document.SetConnectionRouting(graph, connection, std::move(routing)); });
        };
        if (result.AddRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.AddRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.AddRerouteRequested->Index <= connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.insert(routing.begin() + static_cast<std::ptrdiff_t>(result.AddRerouteRequested->Index),
                               result.AddRerouteRequested->GraphPosition);
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.MoveRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.MoveRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.MoveRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing[result.MoveRerouteRequested->Index] = result.MoveRerouteRequested->GraphPosition;
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.DeleteRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.DeleteRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.DeleteRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.erase(routing.begin() + static_cast<std::ptrdiff_t>(result.DeleteRerouteRequested->Index));
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.DeleteConnectionRequested)
        {
            if (const auto* connection = findGraphConnection(*result.DeleteConnectionRequested);
                connection &&
                ApplyAction("Unlinked VFX graph cable", [&document, graph = system->Id, cable = connection->Id]
                            { return document.RemoveConnection(graph, cable); }))
            {
                m_SelectedConnection = {};
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (result.DeleteBlockRequested)
        {
            if (const auto block = findGraphBlock(*result.DeleteBlockRequested);
                block &&
                ApplyAction("Removed VFX Context Block",
                            [&document, graph = system->Id, context = block->first->Id, blockId = block->second->Id]
                            { return document.RemoveBlock(graph, context, blockId); }))
            {
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.SelectBlock(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (!result.DeleteNodesRequested.empty())
        {
            std::vector<Keire::AssetId> selected;
            for (const auto canvasNode : result.DeleteNodesRequested)
                if (const auto* node = findGraphNode(canvasNode))
                    selected.push_back(node->Id);
            if (ApplyAction(selected.size() == 1 ? "Removed VFX graph node" : "Removed VFX graph nodes",
                            [&document, graph = system->Id, selected]
                            { return document.RemoveNodes(graph, selected); }))
            {
                m_SelectedNode = {};
                m_SelectedNodes.clear();
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectBlock(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (!result.ProtectedNodes.empty())
            m_Message = "Executable VFX Context nodes are protected and were not deleted.";
        if (result.ConnectionRequested)
        {
            const auto source =
                findGraphPin(result.ConnectionRequested->SourceNode, result.ConnectionRequested->SourceBlock,
                             result.ConnectionRequested->SourcePin);
            const auto target =
                findGraphPin(result.ConnectionRequested->TargetNode, result.ConnectionRequested->TargetBlock,
                             result.ConnectionRequested->TargetPin);
            if (source && target)
            {
                const auto check = document.CheckConnection(system->Id, *source, *target);
                if (check.Status != VfxGraphConnectionStatus::Rejected)
                {
                    Keire::VfxGraphConnection connection;
                    connection.Id = Keire::AssetId::Generate();
                    connection.OutputNode = source->Node;
                    connection.OutputPin = source->Pin;
                    connection.InputNode = target->Node;
                    connection.InputPin = target->Pin;
                    connection.OutputBlock = source->Block;
                    connection.InputBlock = target->Block;
                    const auto connectionId = connection.Id;
                    if (ApplyAction(check.ReplacesInput ? "Rewired VFX graph input" : "Connected VFX graph pins",
                                    [&document, graph = system->Id, connection = connection]() mutable
                                    { return document.AddConnection(graph, connection); }))
                    {
                        m_SelectedNode = {};
                        m_SelectedBlock = {};
                        m_SelectedConnection = connectionId;
                        m_GraphCanvas.Select(std::nullopt);
                        m_GraphCanvas.SelectBlock(std::nullopt);
                        return;
                    }
                }
                else
                {
                    m_Message = check.Diagnostic;
                }
            }
        }
        if (result.ActivatedNode)
        {
            if (const auto* node = findGraphNode(*result.ActivatedNode); node)
            {
                m_SelectedNode = node->Id;
                if (!result.ActivatedBlock)
                    m_SelectedBlock = {};
                m_SelectedConnection = {};
            }
        }
        if (result.ActivatedBlock)
        {
            if (const auto block = findGraphBlock(*result.ActivatedBlock); block)
            {
                m_SelectedNode = block->first->Id;
                m_SelectedBlock = block->second->Id;
                m_SelectedConnection = {};
            }
        }
        if (result.ActivatedConnection)
        {
            if (const auto* connection = findGraphConnection(*result.ActivatedConnection); connection)
            {
                m_SelectedConnection = connection->Id;
                m_SelectedNode = {};
                m_SelectedBlock = {};
            }
        }
        if (result.BackgroundActivated)
        {
            m_SelectedNode = {};
            m_SelectedBlock = {};
            m_SelectedConnection = {};
        }
        if (result.ContextRequested)
        {
            m_ContextNode = {};
            m_ContextBlock = {};
            m_ContextPin = {};
            m_ContextConnection = {};
            m_NodePalettePosition = result.ContextRequested->GraphPosition;
            switch (result.ContextRequested->Kind)
            {
            case NodeGraphContextTargetKind::Background:
                m_NodePaletteSearch.clear();
                m_NodeMenuSelection.Open();
                ui.SetNextWindowSize({380.0F, 440.0F}, true);
                ui.OpenPopup("VfxGraphNodePalette");
                break;
            case NodeGraphContextTargetKind::Node:
                if (const auto* node = findGraphNode(result.ContextRequested->Node); node)
                {
                    m_ContextNode = node->Id;
                    m_SelectedNode = node->Id;
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphNodeContext");
                }
                break;
            case NodeGraphContextTargetKind::Block:
                if (const auto block = findGraphBlock({result.ContextRequested->Node, result.ContextRequested->Block});
                    block)
                {
                    m_ContextNode = block->first->Id;
                    m_ContextBlock = block->second->Id;
                    m_SelectedNode = block->first->Id;
                    m_SelectedBlock = block->second->Id;
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphBlockContext");
                }
                break;
            case NodeGraphContextTargetKind::Pin:
                if (const auto pin = findGraphPin(result.ContextRequested->Node, result.ContextRequested->Block,
                                                  result.ContextRequested->Pin);
                    pin)
                {
                    m_ContextNode = pin->Node;
                    m_ContextBlock = pin->Block;
                    m_ContextPin = pin->Pin;
                    m_SelectedNode = pin->Node;
                    m_SelectedBlock = pin->Block;
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphPinContext");
                }
                break;
            case NodeGraphContextTargetKind::Connection:
                if (const auto* connection = findGraphConnection(result.ContextRequested->Connection); connection)
                {
                    m_ContextConnection = connection->Id;
                    m_SelectedConnection = connection->Id;
                    m_SelectedNode = {};
                    m_SelectedBlock = {};
                    ui.OpenPopup("VfxGraphConnectionContext");
                }
                break;
            case NodeGraphContextTargetKind::Comment:
                break;
            }
        }
        if (auto popup = ui.BeginPopup("VfxGraphNodePalette"); popup)
        {
            ui.TextColored(theme.Accent, "CREATE NODE");
            ui.TextColored(theme.MutedText, "Search contexts, Runtime Modules, Blackboard properties, and operators.");
            if (m_NodeMenuSelection.ConsumeFocusRequest())
                ui.RequestKeyboardFocus();
            (void)ui.InputTextWithHint("##VfxContextNodeSearch", "Search nodes...", m_NodePaletteSearch);
            ui.Separator();
            if (auto disabled = ui.BeginDisabled(definition.ExecutionSource != Keire::VfxExecutionSource::Graph);
                disabled)
            {
                if (DrawNodePaletteEntries(ui, system->Id, m_NodePalettePosition, m_NodePaletteSearch))
                    return;
            }
            if (definition.ExecutionSource != Keire::VfxExecutionSource::Graph)
                ui.TextColored(theme.Warning, "Convert Runtime Modules to Graph before adding executable nodes.");
            ui.Separator();
            if (ui.MenuItem("Create Empty Comment"))
                CreateGraphComment(ui, system->Id, nodeIdentities, nodes, m_NodePalettePosition, false);
            if (ui.MenuItem("Frame All Nodes"))
                m_GraphCanvas.Focus(nodes, renderedCanvasSize);
        }
        if (auto popup = ui.BeginPopup("VfxGraphNodeContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            if (node != system->Nodes.end())
            {
                ui.TextColored(NodeColor(*node), NodeLabel(definition, *node));
                ui.TextColored(theme.MutedText, std::string(VfxGraphNodeKindLabel(node->Kind)));
                ui.Separator();
                if (ui.MenuItem("Inspect Node"))
                {
                    m_SelectedNode = node->Id;
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                }
                if (node->Kind == Keire::VfxGraphNodeKind::Context)
                {
                    if (auto addBlockMenu = ui.BeginMenu("Add Compatible Block"); addBlockMenu)
                    {
                        (void)ui.InputTextWithHint("##VfxContextBlockSearch", "Search Blocks...", m_NodePaletteSearch);
                        ui.Separator();
                        if (DrawNodePaletteEntries(ui, system->Id, node->EditorPosition, m_NodePaletteSearch, node->Id))
                        {
                            return;
                        }
                    }
                    ui.Separator();
                }
                const bool connected = std::ranges::any_of(
                    system->Connections, [&](const Keire::VfxGraphConnection& connection)
                    { return connection.OutputNode == node->Id || connection.InputNode == node->Id; });
                if (ui.MenuItem("Unlink All Cables", false, connected))
                {
                    const auto nodeId = node->Id;
                    (void)ApplyEdit(
                        "Unlinked all VFX node cables",
                        [graph = system->Id, nodeId](Keire::VfxEffectDefinition& candidate)
                        {
                            auto graphSystem = std::ranges::find(candidate.Systems, graph, &Keire::VfxGraphSystem::Id);
                            if (graphSystem == candidate.Systems.end())
                                throw std::invalid_argument("VFX graph system is unavailable.");
                            std::erase_if(
                                graphSystem->Connections, [nodeId](const Keire::VfxGraphConnection& connection)
                                { return connection.OutputNode == nodeId || connection.InputNode == nodeId; });
                        });
                    return;
                }
                if (ui.MenuItem("Create Comment from Selection"))
                    CreateGraphComment(ui, system->Id, nodeIdentities, nodes, node->EditorPosition, true);
                if (ui.MenuItem("Delete Node", false, node->Kind != Keire::VfxGraphNodeKind::Context))
                {
                    (void)ApplyAction("Removed VFX graph node", [&document, graph = system->Id, nodeId = node->Id]
                                      { return document.RemoveNode(graph, nodeId); });
                    m_SelectedNode = {};
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                    return;
                }
                if (node->Kind == Keire::VfxGraphNodeKind::Context)
                    ui.TextColored(theme.MutedText, "Executable context nodes are fixed graph stages.");
            }
        }
        if (auto popup = ui.BeginPopup("VfxGraphBlockContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            if (node != system->Nodes.end())
            {
                const auto block = std::ranges::find(node->Blocks, m_ContextBlock, &Keire::VfxGraphBlock::Id);
                if (block != node->Blocks.end())
                {
                    ui.TextColored(NodeColor(*node), block->Type);
                    ui.TextColored(theme.MutedText,
                                   std::string(EnumName(node->Context, ContextTypes)) + " Context Block");
                    ui.Separator();
                    if (ui.MenuItem("Inspect Block"))
                    {
                        m_SelectedNode = node->Id;
                        m_SelectedBlock = block->Id;
                        m_SelectedConnection = {};
                    }
                    if (ui.MenuItem(block->Enabled ? "Disable Block" : "Enable Block") &&
                        ApplyAction(block->Enabled ? "Disabled VFX Context Block" : "Enabled VFX Context Block",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id,
                                     enabled = !block->Enabled]
                                    { return document.SetBlockEnabled(graph, context, blockId, enabled); }))
                    {
                        return;
                    }
                    const auto index = static_cast<std::size_t>(std::distance(node->Blocks.begin(), block));
                    if (ui.MenuItem("Move Up", false, index > 0) &&
                        ApplyAction("Moved VFX Context Block up",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id, index]
                                    { return document.MoveBlock(graph, context, blockId, index - 1); }))
                    {
                        return;
                    }
                    if (ui.MenuItem("Move Down", false, index + 1 < node->Blocks.size()) &&
                        ApplyAction("Moved VFX Context Block down",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id, index]
                                    { return document.MoveBlock(graph, context, blockId, index + 1); }))
                    {
                        return;
                    }
                    ui.Separator();
                    const bool connected = std::ranges::any_of(
                        system->Connections,
                        [&](const Keire::VfxGraphConnection& connection)
                        {
                            return (connection.OutputNode == node->Id && connection.OutputBlock == block->Id) ||
                                   (connection.InputNode == node->Id && connection.InputBlock == block->Id);
                        });
                    if (ui.MenuItem("Unlink Block Cables", false, connected))
                    {
                        const auto contextId = node->Id;
                        const auto blockId = block->Id;
                        (void)ApplyEdit("Unlinked VFX Context Block",
                                        [graph = system->Id, contextId, blockId](Keire::VfxEffectDefinition& candidate)
                                        {
                                            auto graphSystem =
                                                std::ranges::find(candidate.Systems, graph, &Keire::VfxGraphSystem::Id);
                                            if (graphSystem == candidate.Systems.end())
                                                throw std::invalid_argument("VFX graph system is unavailable.");
                                            std::erase_if(
                                                graphSystem->Connections,
                                                [contextId, blockId](const Keire::VfxGraphConnection& connection)
                                                {
                                                    return (connection.OutputNode == contextId &&
                                                            connection.OutputBlock == blockId) ||
                                                           (connection.InputNode == contextId &&
                                                            connection.InputBlock == blockId);
                                                });
                                        });
                        return;
                    }
                    if (ui.MenuItem("Remove Block") &&
                        ApplyAction("Removed VFX Context Block",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id]
                                    { return document.RemoveBlock(graph, context, blockId); }))
                    {
                        m_SelectedBlock = {};
                        m_SelectedConnection = {};
                        m_GraphCanvas.SelectBlock(std::nullopt);
                        m_GraphCanvas.SelectConnection(std::nullopt);
                        return;
                    }
                }
            }
        }

        if (auto popup = ui.BeginPopup("VfxGraphPinContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            const Keire::VfxGraphBlock* ownerBlock = nullptr;
            const Keire::VfxGraphPin* pin = nullptr;
            if (node != system->Nodes.end())
            {
                if (m_ContextBlock)
                {
                    const auto block = std::ranges::find(node->Blocks, m_ContextBlock, &Keire::VfxGraphBlock::Id);
                    if (block != node->Blocks.end())
                    {
                        ownerBlock = std::addressof(*block);
                        const auto found = std::ranges::find(block->Pins, m_ContextPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            pin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found = std::ranges::find(node->Pins, m_ContextPin, &Keire::VfxGraphPin::Id);
                    if (found != node->Pins.end())
                        pin = std::addressof(*found);
                }
            }
            if (pin)
            {
                ui.TextColored(PinColor(pin->Type), pin->Name);
                ui.TextColored(theme.MutedText, std::string(pin->Input ? "INPUT  |  " : "OUTPUT  |  ") +
                                                    std::string(EnumName(pin->Type, GraphValueTypes)));
                if (ownerBlock)
                    ui.TextColored(theme.MutedText, "Block: " + ownerBlock->Type);
                ui.Separator();
                const bool connected = std::ranges::any_of(
                    system->Connections,
                    [&](const Keire::VfxGraphConnection& connection)
                    {
                        return (connection.OutputNode == node->Id && connection.OutputBlock == m_ContextBlock &&
                                connection.OutputPin == pin->Id) ||
                               (connection.InputNode == node->Id && connection.InputBlock == m_ContextBlock &&
                                connection.InputPin == pin->Id);
                    });
                if (ui.MenuItem("Unlink Pin", false, connected))
                {
                    const auto nodeId = node->Id;
                    const auto blockId = m_ContextBlock;
                    const auto pinId = pin->Id;
                    (void)ApplyEdit(
                        "Unlinked VFX graph pin",
                        [graph = system->Id, nodeId, blockId, pinId](Keire::VfxEffectDefinition& candidate)
                        {
                            auto graphSystem = std::ranges::find(candidate.Systems, graph, &Keire::VfxGraphSystem::Id);
                            if (graphSystem == candidate.Systems.end())
                                throw std::invalid_argument("VFX graph system is unavailable.");
                            std::erase_if(graphSystem->Connections,
                                          [nodeId, blockId, pinId](const Keire::VfxGraphConnection& connection)
                                          {
                                              return (connection.OutputNode == nodeId &&
                                                      connection.OutputBlock == blockId &&
                                                      connection.OutputPin == pinId) ||
                                                     (connection.InputNode == nodeId &&
                                                      connection.InputBlock == blockId && connection.InputPin == pinId);
                                          });
                        });
                    return;
                }
                if (ownerBlock && ownerBlock->TypeId.View() == "keire.block.portable-hlsl" &&
                    ui.MenuItem("Remove Data Input") &&
                    ApplyAction("Removed Portable HLSL Block input",
                                [&document, graph = system->Id, context = node->Id, blockId = ownerBlock->Id,
                                 pinId = pin->Id] { return document.RemoveBlockPin(graph, context, blockId, pinId); }))
                {
                    m_ContextPin = {};
                    return;
                }
            }
        }

        if (auto popup = ui.BeginPopup("VfxGraphConnectionContext"); popup)
        {
            const auto connection =
                std::ranges::find(system->Connections, m_ContextConnection, &Keire::VfxGraphConnection::Id);
            if (connection != system->Connections.end())
            {
                ui.TextColored(theme.Accent, "GRAPH CABLE");
                ui.TextColored(theme.MutedText,
                               connection->OutputNode.ToString() + " -> " + connection->InputNode.ToString());
                ui.Separator();
                if (ui.MenuItem("Select Source Node"))
                {
                    m_SelectedNode = connection->OutputNode;
                    m_SelectedBlock = connection->OutputBlock;
                    m_SelectedConnection = {};
                }
                if (ui.MenuItem("Select Target Node"))
                {
                    m_SelectedNode = connection->InputNode;
                    m_SelectedBlock = connection->InputBlock;
                    m_SelectedConnection = {};
                }
                ui.Separator();
                if (ui.MenuItem("Unlink Cable"))
                {
                    (void)ApplyAction("Unlinked VFX graph cable",
                                      [&document, graph = system->Id, cable = connection->Id]
                                      { return document.RemoveConnection(graph, cable); });
                    m_SelectedConnection = {};
                    return;
                }
            }
        }

        if (result.BlockMoveRequested)
        {
            if (const auto block = findGraphBlock({result.BlockMoveRequested->Node, result.BlockMoveRequested->Block});
                block)
            {
                (void)ApplyAction("Reordered VFX Context Block",
                                  [&document, graph = system->Id, context = block->first->Id,
                                   blockId = block->second->Id, destination = result.BlockMoveRequested->Destination]
                                  { return document.MoveBlock(graph, context, blockId, destination); });
                return;
            }
        }

        if (!result.MoveCompletedNodes.empty())
        {
            std::vector<std::pair<Keire::AssetId, Keire::Vector2>> moves;
            for (const auto moved : result.MoveCompletedNodes)
                if (const auto* graphNode = findGraphNode(moved);
                    graphNode && std::ranges::find(nodes, moved, &NodeGraphNode::Id) != nodes.end())
                    moves.emplace_back(graphNode->Id, std::ranges::find(nodes, moved, &NodeGraphNode::Id)->Position);
            (void)ApplyAction(moves.size() == 1 ? "Moved VFX graph node" : "Moved VFX graph nodes",
                              [&document, graph = system->Id, moves] { return document.MoveNodes(graph, moves); });
        }
    }

    bool VfxEffectPanel::DrawGraphValueEditor(Keire::UiFrame& ui, const std::string_view label,
                                              const Keire::VfxValueType type, Keire::VfxParameterValue& value)
    {
        if (!Keire::VfxValueMatchesType(type, value))
            throw std::logic_error("VFX graph value editor received a mismatched value type.");

        const auto unsignedInteger = [&ui](const std::string_view field, std::uint64_t& candidate)
        {
            auto text = std::to_string(candidate);
            if (!ui.InputText(field, text))
                return false;
            std::uint64_t parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (error != std::errc{} || end != text.data() + text.size())
                return false;
            candidate = parsed;
            return true;
        };
        const auto editColor = [&ui](const std::string_view field, Keire::Color& candidate)
        {
            Keire::UiColor color{candidate.Red, candidate.Green, candidate.Blue, candidate.Alpha};
            if (!ui.ColorEdit(field, color))
                return false;
            candidate = {color.Red, color.Green, color.Blue, color.Alpha};
            return true;
        };

        switch (type)
        {
        case Keire::VfxValueType::Boolean:
            return ui.Checkbox(label, std::get<bool>(value));
        case Keire::VfxValueType::Integer:
            return ui.DragInteger(label, std::get<std::int64_t>(value));
        case Keire::VfxValueType::UnsignedInteger:
            return unsignedInteger(label, std::get<std::uint64_t>(value));
        case Keire::VfxValueType::Scalar:
        {
            double scalar = std::get<float>(value);
            if (!ui.DragScalar(label, scalar, 0.01))
                return false;
            value = static_cast<float>(scalar);
            return true;
        }
        case Keire::VfxValueType::Vector2:
            return ui.DragVector2(label, std::get<Keire::Vector2>(value), 0.01F);
        case Keire::VfxValueType::Vector3:
            return ui.DragVector3(label, std::get<Keire::Vector3>(value), 0.01F);
        case Keire::VfxValueType::Vector4:
            return ui.DragVector4(label, std::get<Keire::Vector4>(value), 0.01F);
        case Keire::VfxValueType::Quaternion:
        {
            auto& quaternion = std::get<Keire::Quaternion>(value);
            Keire::Vector4 components{quaternion.X, quaternion.Y, quaternion.Z, quaternion.W};
            if (!ui.DragVector4(label, components, 0.01F))
                return false;
            quaternion = {components.X, components.Y, components.Z, components.W};
            return true;
        }
        case Keire::VfxValueType::Color:
            return editColor(label, std::get<Keire::Color>(value));
        case Keire::VfxValueType::Matrix:
        {
            auto& matrix = std::get<Keire::Matrix4>(value);
            bool changed = false;
            for (std::size_t row = 0; row < 4; ++row)
            {
                Keire::Vector4 components{matrix.Elements[row * 4], matrix.Elements[row * 4 + 1],
                                          matrix.Elements[row * 4 + 2], matrix.Elements[row * 4 + 3]};
                if (!ui.DragVector4(std::string(label) + " Row " + std::to_string(row + 1), components, 0.01F))
                    continue;
                matrix.Elements[row * 4] = components.X;
                matrix.Elements[row * 4 + 1] = components.Y;
                matrix.Elements[row * 4 + 2] = components.Z;
                matrix.Elements[row * 4 + 3] = components.W;
                changed = true;
            }
            return changed;
        }
        case Keire::VfxValueType::Curve:
            return AuthoringValueEditors::Curve(ui, label, std::get<Keire::Curve1D>(value));
        case Keire::VfxValueType::Gradient:
            return AuthoringValueEditors::Gradient(ui, label, std::get<Keire::ColorGradient>(value));
        case Keire::VfxValueType::ScalarRange:
        {
            auto& range = std::get<Keire::VfxScalarRange>(value);
            double minimum = range.Minimum;
            double maximum = range.Maximum;
            const bool changed = static_cast<int>(ui.DragScalar(std::string(label) + " Min", minimum, 0.01)) |
                                 static_cast<int>(ui.DragScalar(std::string(label) + " Max", maximum, 0.01));
            if (!changed)
                return false;
            range.Minimum = static_cast<float>(std::min(minimum, maximum));
            range.Maximum = static_cast<float>(std::max(minimum, maximum));
            return true;
        }
        case Keire::VfxValueType::IntegerRange:
        {
            auto& range = std::get<Keire::VfxIntegerRange>(value);
            const bool changed = static_cast<int>(ui.DragInteger(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(ui.DragInteger(std::string(label) + " Max", range.Maximum));
            if (changed && range.Maximum < range.Minimum)
                std::swap(range.Minimum, range.Maximum);
            return changed;
        }
        case Keire::VfxValueType::UnsignedIntegerRange:
        {
            auto& range = std::get<Keire::VfxUnsignedIntegerRange>(value);
            const bool changed = static_cast<int>(unsignedInteger(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(unsignedInteger(std::string(label) + " Max", range.Maximum));
            if (changed && range.Maximum < range.Minimum)
                std::swap(range.Minimum, range.Maximum);
            return changed;
        }
        case Keire::VfxValueType::Vector2Range:
        {
            auto& range = std::get<Keire::VfxVector2Range>(value);
            const bool changed = static_cast<int>(ui.DragVector2(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector2(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y)};
            return true;
        }
        case Keire::VfxValueType::Vector3Range:
        {
            auto& range = std::get<Keire::VfxVector3Range>(value);
            const bool changed = static_cast<int>(ui.DragVector3(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector3(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y),
                             std::min(minimum.Z, maximum.Z)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y),
                             std::max(minimum.Z, maximum.Z)};
            return true;
        }
        case Keire::VfxValueType::Vector4Range:
        {
            auto& range = std::get<Keire::VfxVector4Range>(value);
            const bool changed = static_cast<int>(ui.DragVector4(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector4(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y),
                             std::min(minimum.Z, maximum.Z), std::min(minimum.W, maximum.W)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y),
                             std::max(minimum.Z, maximum.Z), std::max(minimum.W, maximum.W)};
            return true;
        }
        case Keire::VfxValueType::ColorRange:
        {
            auto& range = std::get<Keire::VfxColorRange>(value);
            const bool changed = static_cast<int>(editColor(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(editColor(std::string(label) + " Max", range.Maximum));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.Red, maximum.Red), std::min(minimum.Green, maximum.Green),
                             std::min(minimum.Blue, maximum.Blue), std::min(minimum.Alpha, maximum.Alpha)};
            range.Maximum = {std::max(minimum.Red, maximum.Red), std::max(minimum.Green, maximum.Green),
                             std::max(minimum.Blue, maximum.Blue), std::max(minimum.Alpha, maximum.Alpha)};
            return true;
        }
        case Keire::VfxValueType::Texture:
        case Keire::VfxValueType::Mesh:
        case Keire::VfxValueType::Asset:
        case Keire::VfxValueType::Texture2DArray:
        case Keire::VfxValueType::Texture3D:
        case Keire::VfxValueType::TextureCube:
        case Keire::VfxValueType::Buffer:
        case Keire::VfxValueType::PointCache:
        case Keire::VfxValueType::SignedDistanceField:
        {
            auto& asset = std::get<Keire::AssetId>(value);
            std::optional<Keire::AssetTypeId> expected;
            if (type == Keire::VfxValueType::Texture)
                expected = Keire::Texture2DAsset::StaticType();
            else if (type == Keire::VfxValueType::Mesh)
                expected = Keire::MeshAsset::StaticType();
            AssetPickerOptions options{
                .Label = label,
                .ExpectedType = expected,
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealVfxEffectAsset(selected); },
            };
            return m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), asset, options);
        }
        case Keire::VfxValueType::ParticleStream:
            return false;
        }
        return false;
    }

    bool VfxEffectPanel::DrawGraphPropertyEditor(Keire::UiFrame& ui, Keire::VfxGraphProperty& property)
    {
        if (property.Name == "Scope")
        {
            auto* scope = std::get_if<std::uint64_t>(&property.Value);
            if (!scope)
                throw std::logic_error("VFX Random Scope setting is malformed.");
            const auto current = *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerParticle)
                                     ? std::string_view("Per Particle")
                                 : *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent)
                                     ? std::string_view("Per VFX Component")
                                     : std::string_view("Per Particle Strip (Unavailable)");
            bool changed = false;
            if (auto combo = ui.BeginCombo(property.Name, current); combo)
            {
                if (ui.MenuItem("Per Particle", *scope == 0))
                {
                    *scope = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerParticle);
                    changed = true;
                }
                if (ui.MenuItem("Per VFX Component",
                                *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent)))
                {
                    *scope = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent);
                    changed = true;
                }
                (void)ui.MenuItem("Per Particle Strip (requires strip simulation)", false, false);
            }
            return changed;
        }
        if (property.Name == "Condition")
        {
            auto* condition = std::get_if<std::string>(&property.Value);
            if (!condition)
                throw std::logic_error("VFX Compare Condition setting is malformed.");
            static constexpr std::array conditions{
                std::string_view("Less"),      std::string_view("Less Or Equal"),    std::string_view("Equal"),
                std::string_view("Not Equal"), std::string_view("Greater Or Equal"), std::string_view("Greater")};
            bool changed = false;
            if (auto combo = ui.BeginCombo(property.Name, *condition); combo)
            {
                for (const auto candidate : conditions)
                {
                    if (ui.MenuItem(candidate, *condition == candidate))
                    {
                        *condition = candidate;
                        changed = true;
                    }
                }
            }
            return changed;
        }

        return std::visit(
            Overloaded{
                [&ui, &property](bool& candidate) { return ui.Checkbox(property.Name, candidate); },
                [&ui, &property](std::int64_t& candidate) { return ui.DragInteger(property.Name, candidate); },
                [&ui, &property](std::uint64_t& candidate)
                {
                    auto text = std::to_string(candidate);
                    if (!ui.InputText(property.Name, text))
                        return false;
                    std::uint64_t parsed = 0;
                    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                    if (error != std::errc{} || end != text.data() + text.size())
                        return false;
                    candidate = parsed;
                    return true;
                },
                [&ui, &property](float& candidate)
                {
                    double scalar = candidate;
                    if (!ui.DragScalar(property.Name, scalar, 0.01))
                        return false;
                    candidate = static_cast<float>(scalar);
                    return true;
                },
                [&ui, &property](std::string& candidate) { return ui.InputText(property.Name, candidate); },
                [&ui, &property](Keire::Vector2& candidate) { return ui.DragVector2(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Vector3& candidate) { return ui.DragVector3(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Vector4& candidate) { return ui.DragVector4(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Quaternion& candidate)
                {
                    Keire::Vector4 components{candidate.X, candidate.Y, candidate.Z, candidate.W};
                    if (!ui.DragVector4(property.Name, components, 0.01F))
                        return false;
                    candidate = {components.X, components.Y, components.Z, components.W};
                    return true;
                },
                [&ui, &property](Keire::Color& candidate)
                {
                    Keire::UiColor color{candidate.Red, candidate.Green, candidate.Blue, candidate.Alpha};
                    if (!ui.ColorEdit(property.Name, color))
                        return false;
                    candidate = {color.Red, color.Green, color.Blue, color.Alpha};
                    return true;
                },
                [&ui, &property](Keire::Matrix4& candidate)
                {
                    bool changed = false;
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        Keire::Vector4 components{candidate.Elements[row * 4], candidate.Elements[row * 4 + 1],
                                                  candidate.Elements[row * 4 + 2], candidate.Elements[row * 4 + 3]};
                        if (!ui.DragVector4(property.Name + " Row " + std::to_string(row + 1), components, 0.01F))
                            continue;
                        candidate.Elements[row * 4] = components.X;
                        candidate.Elements[row * 4 + 1] = components.Y;
                        candidate.Elements[row * 4 + 2] = components.Z;
                        candidate.Elements[row * 4 + 3] = components.W;
                        changed = true;
                    }
                    return changed;
                },
                [this, &ui, &property](Keire::AssetId& candidate)
                {
                    AssetPickerOptions options{
                        .Label = property.Name,
                        .Reveal = [this](const Keire::AssetId selected)
                        { m_Controller.RevealVfxEffectAsset(selected); },
                    };
                    return m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), candidate, options);
                },
            },
            property.Value);
    }

    void VfxEffectPanel::DrawGraphInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
        {
            ui.TextColored(theme.MutedText, "No graph system selected.");
            return;
        }

        auto rename = system->Name;
        if (ui.InputText("System Name", rename))
        {
            (void)ApplyAction("Renamed VFX graph system",
                              [&document, graph = system->Id, rename = std::move(rename)]
                              {
                                  return document.EditSystem(graph, [&rename](Keire::VfxGraphSystem& candidate)
                                                             { candidate.Name = rename; });
                              });
            return;
        }
        ui.Separator();

        auto dataType = system->DataType;
        auto particlesPerStrip = static_cast<std::int64_t>(system->ParticlesPerStrip);
        const auto dataTypeChanged = DrawEnum(ui, "Data Type", dataType, ParticleDataTypes);
        const auto stripCountChanged =
            dataType == Keire::VfxParticleDataType::ParticleStrip &&
            ui.DragInteger("Particles Per Strip", particlesPerStrip, 1.0, 1, definition.Capacity);
        if (dataTypeChanged || stripCountChanged)
        {
            particlesPerStrip = std::clamp<std::int64_t>(particlesPerStrip, 1, definition.Capacity);
            (void)ApplyAction("Configured VFX graph system",
                              [&document, graph = system->Id, dataType, particlesPerStrip]
                              {
                                  return document.EditSystem(
                                      graph,
                                      [dataType, particlesPerStrip](Keire::VfxGraphSystem& candidate)
                                      {
                                          candidate.DataType = dataType;
                                          candidate.ParticlesPerStrip = static_cast<std::uint32_t>(particlesPerStrip);
                                      });
                              });
            return;
        }
        ui.TextColored(theme.MutedText, dataType == Keire::VfxParticleDataType::ParticleStrip
                                            ? "Stable strip identity is available to Random and Ribbon output."
                                            : "Independent particle simulation.");
        ui.Separator();

        const auto selectedConnection =
            std::ranges::find(system->Connections, m_SelectedConnection, &Keire::VfxGraphConnection::Id);
        if (selectedConnection != system->Connections.end())
        {
            const auto outputNode =
                std::ranges::find(system->Nodes, selectedConnection->OutputNode, &Keire::VfxGraphNode::Id);
            const auto inputNode =
                std::ranges::find(system->Nodes, selectedConnection->InputNode, &Keire::VfxGraphNode::Id);
            const Keire::VfxGraphBlock* outputBlock = nullptr;
            const Keire::VfxGraphBlock* inputBlock = nullptr;
            const Keire::VfxGraphPin* outputPin = nullptr;
            const Keire::VfxGraphPin* inputPin = nullptr;
            if (outputNode != system->Nodes.end())
            {
                if (selectedConnection->OutputBlock)
                {
                    const auto block = std::ranges::find(outputNode->Blocks, selectedConnection->OutputBlock,
                                                         &Keire::VfxGraphBlock::Id);
                    if (block != outputNode->Blocks.end())
                    {
                        outputBlock = std::addressof(*block);
                        const auto found =
                            std::ranges::find(block->Pins, selectedConnection->OutputPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            outputPin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found =
                        std::ranges::find(outputNode->Pins, selectedConnection->OutputPin, &Keire::VfxGraphPin::Id);
                    if (found != outputNode->Pins.end())
                        outputPin = std::addressof(*found);
                }
            }
            if (inputNode != system->Nodes.end())
            {
                if (selectedConnection->InputBlock)
                {
                    const auto block =
                        std::ranges::find(inputNode->Blocks, selectedConnection->InputBlock, &Keire::VfxGraphBlock::Id);
                    if (block != inputNode->Blocks.end())
                    {
                        inputBlock = std::addressof(*block);
                        const auto found =
                            std::ranges::find(block->Pins, selectedConnection->InputPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            inputPin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found =
                        std::ranges::find(inputNode->Pins, selectedConnection->InputPin, &Keire::VfxGraphPin::Id);
                    if (found != inputNode->Pins.end())
                        inputPin = std::addressof(*found);
                }
            }
            const auto outputLabel =
                outputNode == system->Nodes.end() ? std::string("Missing node") : NodeLabel(definition, *outputNode);
            const auto inputLabel =
                inputNode == system->Nodes.end() ? std::string("Missing node") : NodeLabel(definition, *inputNode);
            const auto outputPinLabel = (outputBlock ? outputBlock->Type + "." : std::string{}) +
                                        (outputPin ? outputPin->Name : std::string("Missing pin"));
            const auto inputPinLabel = (inputBlock ? inputBlock->Type + "." : std::string{}) +
                                       (inputPin ? inputPin->Name : std::string("Missing pin"));

            ui.TextColored(theme.Accent, "CABLE INSPECTOR");
            ui.TextColored(theme.MutedText, "Stable ID: " + selectedConnection->Id.ToString());
            ui.Text(outputLabel + "." + outputPinLabel);
            ui.TextColored(theme.MutedText, "                 ->");
            ui.Text(inputLabel + "." + inputPinLabel);
            if (outputPin)
                ui.TextColored(PinColor(outputPin->Type),
                               "TYPE  |  " + std::string(EnumName(outputPin->Type, GraphValueTypes)));
            ui.Separator();
            if (ui.Button("Select Source"))
            {
                m_SelectedNode = selectedConnection->OutputNode;
                m_SelectedBlock = selectedConnection->OutputBlock;
                m_SelectedConnection = {};
                return;
            }
            ui.SameLine();
            if (ui.Button("Select Target"))
            {
                m_SelectedNode = selectedConnection->InputNode;
                m_SelectedBlock = selectedConnection->InputBlock;
                m_SelectedConnection = {};
                return;
            }
            if (ui.Button("Unlink Cable"))
            {
                (void)ApplyAction("Unlinked VFX graph cable",
                                  [&document, graph = system->Id, cable = selectedConnection->Id]
                                  { return document.RemoveConnection(graph, cable); });
                m_SelectedConnection = {};
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
            ui.TextColored(theme.MutedText, "Right-click the cable in the graph for the same actions.");
            return;
        }
        if (m_SelectedConnection)
            m_SelectedConnection = {};

        const auto selected = std::ranges::find(system->Nodes, m_SelectedNode, &Keire::VfxGraphNode::Id);
        if (selected == system->Nodes.end())
        {
            ui.TextColored(theme.Accent, "GRAPH INSPECTOR");
            ui.TextColored(theme.MutedText,
                           "Select a node or cable to inspect its executable references, typed pins, and routing.");
            return;
        }

        if (m_SelectedBlock)
        {
            const auto selectedBlock = std::ranges::find(selected->Blocks, m_SelectedBlock, &Keire::VfxGraphBlock::Id);
            if (selectedBlock == selected->Blocks.end())
            {
                m_SelectedBlock = {};
            }
            else
            {
                const auto blockIndex =
                    static_cast<std::size_t>(std::distance(selected->Blocks.begin(), selectedBlock));
                const bool portable = selectedBlock->TypeId.View() == "keire.block.portable-hlsl";
                ui.TextColored(NodeColor(*selected), "CONTEXT BLOCK");
                ui.TextColored(theme.MutedText, std::string(EnumName(selected->Context, ContextTypes)) + " / " +
                                                    std::to_string(blockIndex + 1) + " of " +
                                                    std::to_string(selected->Blocks.size()));
                ui.Text(selectedBlock->Type);
                ui.TextColored(theme.MutedText, "Stable ID: " + selectedBlock->Id.ToString());
                if (selectedBlock->Reference)
                    ui.TextColored(theme.MutedText, "Payload: " + selectedBlock->Reference.ToString());

                auto enabled = selectedBlock->Enabled;
                if (ui.Checkbox("Enabled", enabled))
                {
                    (void)ApplyAction(enabled ? "Enabled VFX Context Block" : "Disabled VFX Context Block",
                                      [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                       enabled] { return document.SetBlockEnabled(graph, context, block, enabled); });
                    return;
                }
                if (auto disabled = ui.BeginDisabled(blockIndex == 0); disabled)
                {
                    if (ui.Button("Move Up"))
                    {
                        (void)ApplyAction("Moved VFX Context Block up",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, blockIndex]
                                          { return document.MoveBlock(graph, context, block, blockIndex - 1); });
                        return;
                    }
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(blockIndex + 1 >= selected->Blocks.size()); disabled)
                {
                    if (ui.Button("Move Down"))
                    {
                        (void)ApplyAction("Moved VFX Context Block down",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, blockIndex]
                                          { return document.MoveBlock(graph, context, block, blockIndex + 1); });
                        return;
                    }
                }

                if (portable)
                {
                    ui.Separator();
                    ui.TextColored(theme.Accent, "PORTABLE CUSTOM HLSL");
                    const auto sourceProperty = std::ranges::find(selectedBlock->Properties, std::string_view("Source"),
                                                                  [](const Keire::VfxGraphProperty& property)
                                                                  { return std::string_view(property.Name); });
                    if (sourceProperty != selectedBlock->Properties.end())
                    {
                        if (const auto* source = std::get_if<std::string>(&sourceProperty->Value))
                        {
                            auto editedSource = *source;
                            if (ui.InputText("Source", editedSource))
                            {
                                (void)ApplyAction(
                                    "Edited Portable HLSL Block source",
                                    [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                     editedSource = std::move(editedSource)]() mutable
                                    {
                                        return document.EditBlock(
                                            graph, context, block,
                                            [&editedSource](Keire::VfxGraphBlock& candidate)
                                            {
                                                const auto property =
                                                    std::ranges::find(candidate.Properties, std::string_view("Source"),
                                                                      [](const Keire::VfxGraphProperty& value)
                                                                      { return std::string_view(value.Name); });
                                                if (property == candidate.Properties.end())
                                                    throw std::invalid_argument(
                                                        "Portable HLSL Block source is unavailable.");
                                                property->Value = std::move(editedSource);
                                            });
                                    });
                                return;
                            }
                        }
                        else
                        {
                            ui.TextColored(theme.Error, "Portable HLSL Block source is malformed.");
                        }
                    }
                    else
                    {
                        ui.TextColored(theme.Error, "Portable HLSL Block source is missing.");
                    }
                    ui.TextColored(theme.MutedText,
                                   "Statements may write Position, Velocity, Rotation, Tint, or Size. Typed inputs "
                                   "use their HLSL semantic identifiers.");
                }

                ui.Separator();
                ui.TextColored(theme.Accent, "TYPED BLOCK INPUTS");
                for (const auto& pin : selectedBlock->Pins)
                {
                    auto id = ui.PushId(pin.Id.ToString());
                    auto candidate = pin;
                    bool pinChanged = false;
                    if (portable)
                    {
                        pinChanged |= ui.InputText("Name", candidate.Name);
                        const auto previousType = candidate.Type;
                        pinChanged |= DrawEnum(ui, "Type", candidate.Type, CustomHlslValueTypes);
                        pinChanged |= ui.InputText("Semantic", candidate.Semantic);
                        if (candidate.Type != previousType || !candidate.DefaultValue)
                            candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);
                        pinChanged |= DrawGraphValueEditor(ui, "Fallback", candidate.Type, *candidate.DefaultValue);
                    }
                    else
                    {
                        ui.Text(pin.Name + " : " + std::string(EnumName(pin.Type, GraphValueTypes)));
                        if (!pin.Semantic.empty())
                            ui.TextColored(theme.MutedText, "Semantic: " + pin.Semantic);
                        if (candidate.Input && candidate.DefaultValue)
                        {
                            const bool connected =
                                std::ranges::any_of(system->Connections,
                                                    [&](const Keire::VfxGraphConnection& connection)
                                                    {
                                                        return connection.InputNode == selected->Id &&
                                                               connection.InputBlock == selectedBlock->Id &&
                                                               connection.InputPin == pin.Id;
                                                    });
                            pinChanged |= DrawGraphValueEditor(ui, connected ? "Inline Fallback" : "Inline Value",
                                                               candidate.Type, *candidate.DefaultValue);
                            if (connected)
                                ui.TextColored(theme.MutedText,
                                               "The incoming cable overrides this fallback while connected.");
                        }
                    }
                    if (pinChanged)
                    {
                        (void)ApplyAction("Edited VFX Context Block input",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, pin = pin.Id,
                                           candidate = std::move(candidate)]() mutable
                                          {
                                              return document.EditBlockPin(graph, context, block, pin,
                                                                           [&candidate](Keire::VfxGraphPin& value)
                                                                           { value = std::move(candidate); });
                                          });
                        return;
                    }
                    if (portable && ui.Button("Remove Input"))
                    {
                        (void)ApplyAction("Removed Portable HLSL Block input",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, pin = pin.Id]
                                          { return document.RemoveBlockPin(graph, context, block, pin); });
                        return;
                    }
                    ui.Separator();
                }
                if (selectedBlock->Pins.empty())
                    ui.TextColored(theme.MutedText, "No typed data inputs.");

                if (portable && ui.Button("+ Add Data Input"))
                {
                    std::size_t index = 1;
                    while (std::ranges::any_of(selectedBlock->Pins, [index](const Keire::VfxGraphPin& pin)
                                               { return pin.Semantic == "Input" + std::to_string(index); }))
                    {
                        ++index;
                    }
                    Keire::VfxGraphPin pin{Keire::AssetId::Generate(),      "Input " + std::to_string(index),
                                           Keire::VfxValueType::Scalar,     true,
                                           "Input" + std::to_string(index), 0.0F};
                    (void)ApplyAction("Added Portable HLSL Block input",
                                      [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                       pin = std::move(pin)]() mutable
                                      { return document.AddBlockPin(graph, context, block, std::move(pin)); });
                    return;
                }

                if (selectedBlock->Reference)
                {
                    ui.Separator();
                    const auto module = std::ranges::find(definition.Modules, selectedBlock->Reference,
                                                          &Keire::VfxModuleDefinition::Id);
                    if (module == definition.Modules.end())
                        ui.TextColored(theme.Error, "The referenced Runtime Module payload is missing.");
                    else if (ui.Selectable("Edit Payload: " + std::string(ModuleName(module->Payload)),
                                           module->Id == m_SelectedModule))
                        m_SelectedModule = module->Id;
                }

                ui.Separator();
                if (ui.Button("Remove Block") &&
                    ApplyAction("Removed VFX Context Block",
                                [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id]
                                { return document.RemoveBlock(graph, context, block); }))
                {
                    m_SelectedBlock = {};
                    m_GraphCanvas.SelectBlock(std::nullopt);
                    return;
                }
                ui.TextColored(theme.MutedText,
                               "Drag this row to reorder it. Right-click for enable, unlink, and remove actions.");
                return;
            }
        }

        auto node = *selected;
        bool changed = false;
        ui.TextColored(NodeColor(node), std::string(VfxGraphNodeKindLabel(node.Kind)));
        ui.TextColored(theme.MutedText, "Stable ID: " + node.Id.ToString());
        if (node.Reference)
            ui.TextColored(theme.MutedText, "Reference: " + node.Reference.ToString());
        if (node.Kind == Keire::VfxGraphNodeKind::Context || node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
            changed |= ui.InputText("Node Name", node.Type);
        else
            ui.Text("Source: " + NodeLabel(definition, node));
        const Keire::VfxNodeDescriptor* operatorDescriptor = nullptr;
        if (node.Kind == Keire::VfxGraphNodeKind::Operator)
            operatorDescriptor = Keire::FindVfxNodeDescriptor(node.TypeId.View());
        if (operatorDescriptor)
        {
            bool contextChanged = false;
            if (auto combo = ui.BeginCombo("Context", EnumName(node.Context, ContextTypes)); combo)
            {
                for (const auto& candidate : ContextTypes)
                {
                    if (std::ranges::find(operatorDescriptor->ValidContexts, candidate.Type) ==
                        operatorDescriptor->ValidContexts.end())
                    {
                        continue;
                    }
                    if (candidate.Type == Keire::VfxContextType::Event)
                    {
                        (void)ui.MenuItem("Event (requires Event context execution)", false, false);
                        continue;
                    }
                    if (ui.MenuItem(candidate.Name, node.Context == candidate.Type))
                    {
                        node.Context = candidate.Type;
                        contextChanged = true;
                    }
                }
            }
            changed |= contextChanged;
        }
        else if (node.Kind == Keire::VfxGraphNodeKind::Context || node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
            changed |= DrawEnum(ui, "Context", node.Context, ContextTypes);
        else
            ui.Text("Context: " + std::string(EnumName(node.Context, ContextTypes)));
        changed |= ui.DragVector2("Graph Position", node.EditorPosition, 1.0F);
        if (node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
        {
            changed |= ui.InputText("Custom HLSL", node.CustomHlsl);
            ui.TextColored(theme.MutedText,
                           "Portable statements write Position, Velocity, Rotation, Tint, or Size. Pin semantics are "
                           "the generated input names.");
        }
        if (node.Kind == Keire::VfxGraphNodeKind::Operator)
        {
            ui.Separator();
            ui.TextColored(theme.Accent, "OPERATOR SETTINGS");
            if (!operatorDescriptor)
            {
                ui.TextColored(theme.Error, "The compiler descriptor for this Operator is unavailable.");
            }
            else
            {
                const auto entry = BuildVfxNodeCatalogEntry(*operatorDescriptor);
                ui.TextColored(theme.MutedText, "Backend: " + VfxNodeCatalogSupportBadge(entry));
                for (auto& property : node.Properties)
                {
                    auto id = ui.PushId(property.Name);
                    changed |= DrawGraphPropertyEditor(ui, property);
                }
                if (node.Properties.empty())
                    ui.TextColored(theme.MutedText, "This Operator has no configurable settings.");
            }
        }
        if (changed)
        {
            const auto nodeId = node.Id;
            (void)ApplyAction("Edited VFX graph node",
                              [&document, graph = system->Id, nodeId, node = std::move(node)]() mutable
                              {
                                  return document.EditNode(
                                      graph, nodeId, [node = std::move(node)](Keire::VfxGraphNode& candidate) mutable
                                      { candidate = std::move(node); });
                              });
            return;
        }

        if (DrawGraphNodeComment(ui, system->Id, *selected))
            return;

        ui.Separator();
        ui.TextColored(theme.Accent, "TYPED PINS");
        const bool customPins = selected->Kind == Keire::VfxGraphNodeKind::CustomHlsl;
        for (const auto& pin : selected->Pins)
        {
            auto id = ui.PushId(pin.Id.ToString());
            auto candidate = pin;
            bool pinChanged = false;
            const bool customPinEditable = customPins && pin.Type != Keire::VfxValueType::ParticleStream;
            if (customPinEditable)
            {
                pinChanged |= ui.InputText("Name", candidate.Name);
                const auto previousType = candidate.Type;
                pinChanged |= DrawEnum(ui, "Type", candidate.Type, CustomHlslValueTypes);
                pinChanged |= ui.InputText("Semantic", candidate.Semantic);
                if (candidate.Type != previousType)
                    candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);

                if (candidate.Input)
                {
                    if (!candidate.DefaultValue)
                        candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);
                    pinChanged |= DrawGraphValueEditor(ui, "Fallback", candidate.Type, *candidate.DefaultValue);
                }
            }
            else
            {
                ui.Text(pin.Name + " : " + std::string(EnumName(pin.Type, GraphValueTypes)) +
                        (pin.Input ? " [Input]" : " [Output]"));
                if (!pin.Semantic.empty())
                    ui.TextColored(theme.MutedText, "Semantic: " + pin.Semantic);
                if (selected->Kind == Keire::VfxGraphNodeKind::Operator && candidate.Input && candidate.DefaultValue)
                {
                    const bool connected = std::ranges::any_of(
                        system->Connections, [&](const Keire::VfxGraphConnection& connection)
                        { return connection.InputNode == selected->Id && connection.InputPin == pin.Id; });
                    pinChanged |= DrawGraphValueEditor(ui, connected ? "Inline Fallback" : "Inline Value",
                                                       candidate.Type, *candidate.DefaultValue);
                    if (connected)
                        ui.TextColored(theme.MutedText, "The incoming cable overrides this fallback while connected.");
                }
            }
            if (pinChanged)
            {
                const bool topologyChanged = candidate.Type != pin.Type || candidate.Input != pin.Input;
                (void)ApplyEdit(
                    "Edit VFX graph pin",
                    [graph = system->Id, nodeId = selected->Id, candidate = std::move(candidate),
                     topologyChanged](Keire::VfxEffectDefinition& draft) mutable
                    {
                        auto graphSystem = std::ranges::find(draft.Systems, graph, &Keire::VfxGraphSystem::Id);
                        if (graphSystem == draft.Systems.end())
                            throw std::invalid_argument("VFX graph system is unavailable.");
                        auto graphNode = std::ranges::find(graphSystem->Nodes, nodeId, &Keire::VfxGraphNode::Id);
                        if (graphNode == graphSystem->Nodes.end())
                            throw std::invalid_argument("VFX graph node is unavailable.");
                        auto graphPin = std::ranges::find(graphNode->Pins, candidate.Id, &Keire::VfxGraphPin::Id);
                        if (graphPin == graphNode->Pins.end())
                            throw std::invalid_argument("VFX graph pin is unavailable.");
                        *graphPin = std::move(candidate);
                        if (topologyChanged)
                            std::erase_if(graphSystem->Connections,
                                          [pin = graphPin->Id](const Keire::VfxGraphConnection& connection)
                                          { return connection.OutputPin == pin || connection.InputPin == pin; });
                    });
                return;
            }

            if (customPinEditable)
            {
                if (ui.Button("Remove Pin"))
                {
                    (void)ApplyAction("Removed VFX graph pin",
                                      [&document, graph = system->Id, nodeId = selected->Id, pinId = pin.Id]
                                      { return document.RemovePin(graph, nodeId, pinId); });
                    return;
                }
            }
            ui.Separator();
        }

        const auto addPin = [&]
        {
            const auto dataInputs =
                std::ranges::count_if(selected->Pins, [](const Keire::VfxGraphPin& pin)
                                      { return pin.Input && pin.Type != Keire::VfxValueType::ParticleStream; });
            Keire::VfxGraphPin pin{Keire::AssetId::Generate(),
                                   "Input",
                                   Keire::VfxValueType::Scalar,
                                   true,
                                   "Input" + std::to_string(dataInputs + 1),
                                   0.0F};
            return ApplyAction("Added VFX graph pin",
                               [&document, graph = system->Id, nodeId = selected->Id, pin = std::move(pin)]() mutable
                               { return document.AddPin(graph, nodeId, std::move(pin)); });
        };
        if (customPins && ui.Button("+ Add Data Input") && addPin())
            return;

        ui.Separator();
        ui.TextColored(theme.Accent, "CONNECTIONS");
        bool hasConnections = false;
        for (const auto& connection : system->Connections)
        {
            if (connection.OutputNode != selected->Id && connection.InputNode != selected->Id)
                continue;
            hasConnections = true;
            auto id = ui.PushId(connection.Id.ToString());
            ui.TextColored(theme.MutedText,
                           connection.OutputNode.ToString() + "  ->  " + connection.InputNode.ToString());
            ui.SameLine();
            if (ui.Button("Remove Link"))
            {
                (void)ApplyAction("Removed VFX graph connection", [&document, graph = system->Id, link = connection.Id]
                                  { return document.RemoveConnection(graph, link); });
                return;
            }
        }
        if (!hasConnections)
            ui.TextColored(theme.MutedText, "No links on this context.");

        ui.Separator();
        if (selected->Kind == Keire::VfxGraphNodeKind::Context)
        {
            ui.TextColored(theme.Accent, "ORDERED CONTEXT BLOCKS");
            for (std::size_t index = 0; index < selected->Blocks.size(); ++index)
            {
                const auto& block = selected->Blocks[index];
                auto id = ui.PushId(block.Id.ToString());
                if (ui.Selectable(std::to_string(index + 1) + ". " + (block.Enabled ? "" : "[Disabled] ") + block.Type,
                                  block.Id == m_SelectedBlock))
                {
                    m_SelectedBlock = block.Id;
                    m_SelectedConnection = {};
                    return;
                }
            }
            if (selected->Blocks.empty())
                ui.TextColored(theme.MutedText, "This Context has no executable Blocks.");
            if (auto add = ui.BeginCombo("Add Block", "Choose..."); add)
            {
                (void)ui.InputTextWithHint("##VfxInspectorBlockSearch", "Search Blocks...", m_NodePaletteSearch);
                ui.Separator();
                if (DrawNodePaletteEntries(ui, system->Id, selected->EditorPosition, m_NodePaletteSearch, selected->Id))
                {
                    return;
                }
            }
        }
        else if (selected->Kind == Keire::VfxGraphNodeKind::Module)
        {
            ui.TextColored(theme.Accent, "REFERENCED RUNTIME MODULE");
            const auto module =
                std::ranges::find(definition.Modules, selected->Reference, &Keire::VfxModuleDefinition::Id);
            if (module == definition.Modules.end())
                ui.TextColored(theme.Error, "The referenced Runtime Module is missing.");
            else if (ui.Selectable(std::string(ModuleName(module->Payload)), module->Id == m_SelectedModule))
                m_SelectedModule = module->Id;
        }
        else if (selected->Kind == Keire::VfxGraphNodeKind::Parameter)
        {
            ui.TextColored(theme.Accent, "REFERENCED BLACKBOARD PROPERTY");
            const auto parameter =
                std::ranges::find(definition.Blackboard, selected->Reference, &Keire::VfxBlackboardParameter::Id);
            if (parameter == definition.Blackboard.end())
                ui.TextColored(theme.Error, "The referenced Blackboard property is missing.");
            else
                ui.Text(parameter->Name + " : " + std::string(EnumName(parameter->Type, ValueTypes)));
        }

        ui.Separator();
        if (auto disabled = ui.BeginDisabled(selected->Kind == Keire::VfxGraphNodeKind::Context); disabled)
        {
            if (ui.Button("Delete Node") &&
                ApplyAction("Removed VFX graph node", [&document, graph = system->Id, nodeId = selected->Id]
                            { return document.RemoveNode(graph, nodeId); }))
            {
                m_SelectedNode = {};
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.CancelInteractions();
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectBlock(std::nullopt);
                return;
            }
        }
        if (selected->Kind == Keire::VfxGraphNodeKind::Context)
            ui.TextColored(theme.MutedText, "Executable stage contexts cannot be deleted.");
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
