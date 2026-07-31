#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
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
        constexpr std::array Shapes{
            EnumEntry{Keire::VfxShape::Point, std::string_view("Point")},
            EnumEntry{Keire::VfxShape::Box, std::string_view("Box")},
            EnumEntry{Keire::VfxShape::Sphere, std::string_view("Sphere")},
            EnumEntry{Keire::VfxShape::Cone, std::string_view("Cone")},
            EnumEntry{Keire::VfxShape::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxShape::Volume, std::string_view("Volume")},
        };
        constexpr std::array CollisionModes{
            EnumEntry{Keire::VfxCollisionMode::None, std::string_view("None")},
            EnumEntry{Keire::VfxCollisionMode::Cpu, std::string_view("CPU")},
            EnumEntry{Keire::VfxCollisionMode::GpuDepth, std::string_view("GPU Depth")},
            EnumEntry{Keire::VfxCollisionMode::ScenePhysics, std::string_view("Scene Physics")},
        };
        constexpr std::array RendererTypes{
            EnumEntry{Keire::VfxRendererType::Sprite, std::string_view("Sprite")},
            EnumEntry{Keire::VfxRendererType::Mesh, std::string_view("Mesh")},
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

        [[nodiscard]] std::string_view ModuleName(const Keire::VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const Keire::VfxEmissionRateModule&) -> std::string_view { return "Emission Rate"; },
                    [](const Keire::VfxBurstModule&) -> std::string_view { return "Burst"; },
                    [](const Keire::VfxShapeModule&) -> std::string_view { return "Shape"; },
                    [](const Keire::VfxInitializeModule&) -> std::string_view { return "Initialize"; },
                    [](const Keire::VfxForceModule&) -> std::string_view { return "Forces"; },
                    [](const Keire::VfxSizeOverLifetimeModule&) -> std::string_view { return "Size over Lifetime"; },
                    [](const Keire::VfxColorOverLifetimeModule&) -> std::string_view { return "Color over Lifetime"; },
                    [](const Keire::VfxCollisionModule&) -> std::string_view { return "Collision"; },
                    [](const Keire::VfxRendererModule&) -> std::string_view { return "Renderer"; },
                },
                payload);
        }

        template <typename Module> [[nodiscard]] bool ContainsModule(const Keire::VfxEffectDefinition& definition)
        {
            return std::ranges::any_of(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                       { return std::holds_alternative<Module>(module.Payload); });
        }

        [[nodiscard]] std::size_t BurstCount(const Keire::VfxEffectDefinition& definition)
        {
            return static_cast<std::size_t>(
                std::ranges::count_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                      { return std::holds_alternative<Keire::VfxBurstModule>(module.Payload); }));
        }

        [[nodiscard]] StableNodeId PreferredCanvasId(const Keire::AssetId id, const std::uint64_t salt) noexcept
        {
            return id.High() ^ std::rotl(id.Low(), 17) ^ salt;
        }

        [[nodiscard]] Keire::UiColor ContextColor(const Keire::VfxContextType context) noexcept
        {
            switch (context)
            {
            case Keire::VfxContextType::Spawn:
                return {0.10F, 0.48F, 0.48F, 1.0F};
            case Keire::VfxContextType::Initialize:
                return {0.42F, 0.27F, 0.62F, 1.0F};
            case Keire::VfxContextType::Update:
                return {0.18F, 0.38F, 0.68F, 1.0F};
            case Keire::VfxContextType::Output:
                return {0.72F, 0.38F, 0.14F, 1.0F};
            case Keire::VfxContextType::Event:
                return {0.64F, 0.20F, 0.30F, 1.0F};
            }
            return {0.2F, 0.24F, 0.3F, 1.0F};
        }

        [[nodiscard]] Keire::VfxParameterValue DefaultParameterValue(const Keire::VfxValueType type)
        {
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return false;
            case Keire::VfxValueType::Integer:
                return std::int64_t{0};
            case Keire::VfxValueType::Scalar:
                return 0.0F;
            case Keire::VfxValueType::Vector2:
                return Keire::Vector2{};
            case Keire::VfxValueType::Vector3:
                return Keire::Vector3{};
            case Keire::VfxValueType::Color:
                return Keire::Color{};
            case Keire::VfxValueType::Texture:
            case Keire::VfxValueType::Mesh:
            case Keire::VfxValueType::Asset:
                return Keire::AssetId{};
            }
            throw std::invalid_argument("VFX parameter type is unsupported.");
        }

        [[nodiscard]] Keire::VfxGraphNode NewContextNode(const Keire::VfxContextType context,
                                                         const Keire::Vector2 position)
        {
            Keire::VfxGraphNode result;
            result.Id = Keire::AssetId::Generate();
            result.Type = std::string(EnumName(context, ContextTypes)) + " Context";
            result.Context = context;
            result.EditorPosition = position;
            if (context != Keire::VfxContextType::Spawn && context != Keire::VfxContextType::Event)
                result.Pins.push_back({Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::Asset, true});
            if (context != Keire::VfxContextType::Output)
                result.Pins.push_back({Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::Asset, false});
            return result;
        }

        [[nodiscard]] bool ModuleRunsInContext(const Keire::VfxModulePayload& payload,
                                               const Keire::VfxContextType context)
        {
            return std::visit(
                Overloaded{
                    [context](const Keire::VfxEmissionRateModule&) { return context == Keire::VfxContextType::Spawn; },
                    [context](const Keire::VfxBurstModule&) { return context == Keire::VfxContextType::Spawn; },
                    [context](const Keire::VfxShapeModule&) { return context == Keire::VfxContextType::Initialize; },
                    [context](const Keire::VfxInitializeModule&)
                    { return context == Keire::VfxContextType::Initialize; },
                    [context](const Keire::VfxForceModule&) { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxSizeOverLifetimeModule&)
                    { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxColorOverLifetimeModule&)
                    { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxCollisionModule&) { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxRendererModule&) { return context == Keire::VfxContextType::Output; },
                },
                payload);
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
        if (ui.Shortcut({Keire::UiKey::S, true}) && document.Dirty())
            save();
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
                const auto compiled = Keire::CompileVfxEffect(document.Definition(), backend);
                const auto warningCount = static_cast<std::size_t>(
                    std::ranges::count(compiled.Diagnostics, Keire::VfxCompileDiagnosticSeverity::Warning,
                                       &Keire::VfxCompileDiagnostic::Severity));
                if (!compiled.Valid)
                {
                    m_Message = compiled.Diagnostics.empty()
                                    ? "Graph compilation failed."
                                    : "Graph compilation failed: " + compiled.Diagnostics.front().Message;
                    m_Controller.ReportVfxEffectError(m_Message);
                }
                else
                {
                    m_Message = "Compiled " + std::string(EnumName(backend, PreviewBackends)) + " program (" +
                                std::to_string(compiled.CanonicalIr.size()) + " bytes, " +
                                std::to_string(warningCount) + " warnings).";
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
        const auto& theme = m_Controller.VfxEffectTheme();
        ui.Separator();
        ui.TextColored(status.Active ? theme.Success : theme.Warning, status.Active ? "PREVIEW LIVE" : "PREVIEW IDLE");
        ui.SameLine();
        if (ui.IconButton("RestartVfxPreview", Keire::UiIcon::Refresh))
            (void)ApplyAction("Restarted VFX preview",
                              [this]
                              {
                                  m_Controller.RestartVfxEffectPreview();
                                  return true;
                              });
        ui.SetTooltip("Restart the transient authoring preview.");
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
        if (DrawEnum(ui, "Backend", backend, PreviewBackends))
            (void)ApplyAction("Changed VFX preview backend",
                              [this, backend]
                              {
                                  m_Controller.SetVfxEffectPreviewBackend(backend);
                                  return true;
                              });
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
            m_PendingOutput.reset();
        }

        ui.TextColored(theme.Accent, "SYSTEMS");
        ui.TextColored(theme.MutedText, "Schema v" + std::to_string(definition.SchemaVersion));
        for (const auto& system : definition.Systems)
        {
            auto id = ui.PushId(system.Id.ToString());
            const auto label = system.Name + "  [" + std::to_string(system.Nodes.size()) + "]";
            if (ui.Selectable(label, system.Id == m_SelectedSystem))
            {
                m_SelectedSystem = system.Id;
                m_SelectedNode = {};
                m_PendingOutput.reset();
                m_GraphCanvas.Select(std::nullopt);
            }
        }
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
                return;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_SelectedSystem); disabled)
        {
            if (ui.Button("Remove") && ApplyAction("Removed VFX graph system", [&document, system = m_SelectedSystem]
                                                   { return document.RemoveSystem(system); }))
            {
                m_SelectedSystem = {};
                m_SelectedNode = {};
                m_PendingOutput.reset();
                return;
            }
        }

        ui.Separator();
        ui.TextColored(theme.Accent, "BLACKBOARD");
        ui.TextColored(theme.MutedText, "Exposed graph inputs");
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
            }
        }

        ui.Separator();
        std::size_t nodes = 0;
        std::size_t links = 0;
        for (const auto& system : definition.Systems)
        {
            nodes += system.Nodes.size();
            links += system.Connections.size();
        }
        ui.TextColored(theme.MutedText, std::to_string(nodes) + " nodes  |  " + std::to_string(links) + " links");
        ui.TextColored(theme.Warning,
                       "Graph topology is persisted and validated. Runtime behavior is currently driven by the "
                       "Runtime Modules tab.");
    }

    void VfxEffectPanel::DrawGraphCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
        {
            ui.TextColored(theme.MutedText, "Create or select a system to begin authoring.");
            return;
        }

        ui.TextColored(theme.Accent, system->Name);
        ui.SameLine();
        if (auto add = ui.BeginCombo("Add Context", "Choose..."); add)
        {
            for (std::size_t contextIndex = 0; contextIndex < ContextTypes.size(); ++contextIndex)
            {
                const auto& context = ContextTypes[contextIndex];
                if (!ui.MenuItem(context.Name))
                    continue;
                const auto count =
                    static_cast<float>(std::ranges::count(system->Nodes, context.Type, &Keire::VfxGraphNode::Context));
                const auto column = static_cast<float>(contextIndex);
                auto node = NewContextNode(context.Type, {column * 280.0F, count * 128.0F});
                const auto id = node.Id;
                if (ApplyAction("Added VFX graph node",
                                [&document, graph = system->Id, node = std::move(node)]() mutable
                                { return document.AddNode(graph, std::move(node)); }))
                {
                    m_SelectedNode = id;
                    ui.CloseCurrentPopup();
                    return;
                }
                ui.CloseCurrentPopup();
                break;
            }
        }

        std::vector<NodeGraphNode> nodes;
        nodes.reserve(system->Nodes.size());
        StableNodeGraphIdMap nodeIds;
        for (const auto& node : system->Nodes)
        {
            const auto blockCount =
                std::ranges::count_if(definition.Modules, [&node](const Keire::VfxModuleDefinition& module)
                                      { return ModuleRunsInContext(module.Payload, node.Context); });
            nodes.push_back({.Id = nodeIds.Assign(node.Id, PreferredCanvasId(node.Id, 0x5646584e4f444501ULL)),
                             .Label = node.Type,
                             .Position = node.EditorPosition,
                             .Size = {210.0F, std::max(88.0F, 62.0F + static_cast<float>(node.Pins.size()) * 12.0F)},
                             .Color = ContextColor(node.Context),
                             .Subtitle = std::string(EnumName(node.Context, ContextTypes)) + "  |  " +
                                         std::to_string(node.Pins.size()) + " pins  |  " + std::to_string(blockCount) +
                                         " runtime blocks"});
        }
        std::vector<NodeGraphConnection> connections;
        connections.reserve(system->Connections.size());
        StableNodeGraphIdMap connectionIds;
        for (const auto& connection : system->Connections)
        {
            const auto source = nodeIds.Find(connection.OutputNode);
            const auto target = nodeIds.Find(connection.InputNode);
            if (!source || !target)
                continue;
            connections.push_back(
                {.Id = connectionIds.Assign(connection.Id, PreferredCanvasId(connection.Id, 0x5646584c494e4b01ULL)),
                 .Source = *source,
                 .Target = *target,
                 .Label = "Data"});
        }
        m_GraphCanvas.Select(nodeIds.Find(m_SelectedNode));

        if (ui.Button("Frame All"))
            m_GraphCanvas.Focus(nodes, ui.ContentAvailable());
        ui.SameLine();
        if (m_PendingOutput)
        {
            ui.TextColored(theme.Warning, "Choose a compatible input pin to complete the link.");
            ui.SameLine();
            if (ui.Button("Cancel Link"))
                m_PendingOutput.reset();
        }
        else
        {
            ui.TextColored(theme.MutedText, "Drag nodes • middle-drag to pan • wheel to zoom");
        }

        const auto result = m_GraphCanvas.Draw(ui, "VfxNodeCanvas", nodes, connections, true);
        if (result.ActivatedNode)
        {
            const auto found = std::ranges::find_if(system->Nodes, [&](const Keire::VfxGraphNode& node)
                                                    { return nodeIds.Find(node.Id) == result.ActivatedNode; });
            if (found != system->Nodes.end())
                m_SelectedNode = found->Id;
        }
        if (result.BackgroundActivated)
            m_SelectedNode = {};
        if (result.MoveCompletedNode)
        {
            const auto canvasNode = std::ranges::find(nodes, *result.MoveCompletedNode, &NodeGraphNode::Id);
            const auto graphNode = std::ranges::find_if(system->Nodes, [&](const Keire::VfxGraphNode& node)
                                                        { return nodeIds.Find(node.Id) == result.MoveCompletedNode; });
            if (canvasNode != nodes.end() && graphNode != system->Nodes.end())
            {
                (void)ApplyAction("Moved VFX graph node",
                                  [&document, graph = system->Id, node = graphNode->Id, position = canvasNode->Position]
                                  {
                                      return document.EditNode(graph, node, [position](Keire::VfxGraphNode& candidate)
                                                               { candidate.EditorPosition = position; });
                                  });
            }
        }
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
                                                             { candidate.Name = std::move(rename); });
                              });
            return;
        }
        ui.Separator();

        const auto selected = std::ranges::find(system->Nodes, m_SelectedNode, &Keire::VfxGraphNode::Id);
        if (selected == system->Nodes.end())
        {
            ui.TextColored(theme.Accent, "GRAPH INSPECTOR");
            ui.TextColored(theme.MutedText, "Select a context card to edit its properties and typed connections.");
            return;
        }

        auto node = *selected;
        bool changed = false;
        ui.TextColored(ContextColor(node.Context), std::string(EnumName(node.Context, ContextTypes)) + " CONTEXT");
        ui.TextColored(theme.MutedText, "Stable ID: " + node.Id.ToString());
        changed |= ui.InputText("Node Name", node.Type);
        changed |= DrawEnum(ui, "Context", node.Context, ContextTypes);
        changed |= ui.DragVector2("Graph Position", node.EditorPosition, 1.0F);
        changed |= ui.InputText("Custom HLSL", node.CustomHlsl);
        if (changed)
        {
            (void)ApplyAction("Edited VFX graph node",
                              [&document, graph = system->Id, node = std::move(node)]() mutable
                              {
                                  return document.EditNode(
                                      graph, node.Id, [node = std::move(node)](Keire::VfxGraphNode& candidate) mutable
                                      { candidate = std::move(node); });
                              });
            return;
        }

        ui.Separator();
        ui.TextColored(theme.Accent, "TYPED PINS");
        for (const auto& pin : selected->Pins)
        {
            auto id = ui.PushId(pin.Id.ToString());
            auto candidate = pin;
            bool pinChanged = false;
            pinChanged |= ui.InputText("Name", candidate.Name);
            pinChanged |= DrawEnum(ui, "Type", candidate.Type, ValueTypes);
            pinChanged |= ui.Checkbox("Input", candidate.Input);
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

            if (!pin.Input)
            {
                if (ui.Button("Start Link"))
                    m_PendingOutput = std::pair{selected->Id, pin.Id};
            }
            else if (m_PendingOutput)
            {
                const auto outputNode =
                    std::ranges::find(system->Nodes, m_PendingOutput->first, &Keire::VfxGraphNode::Id);
                const Keire::VfxGraphPin* outputPin = nullptr;
                if (outputNode != system->Nodes.end())
                {
                    const auto found =
                        std::ranges::find(outputNode->Pins, m_PendingOutput->second, &Keire::VfxGraphPin::Id);
                    if (found != outputNode->Pins.end())
                        outputPin = std::addressof(*found);
                }
                const bool duplicate = std::ranges::any_of(system->Connections,
                                                           [&](const Keire::VfxGraphConnection& connection)
                                                           {
                                                               return connection.OutputNode == m_PendingOutput->first &&
                                                                      connection.OutputPin == m_PendingOutput->second &&
                                                                      connection.InputNode == selected->Id &&
                                                                      connection.InputPin == pin.Id;
                                                           });
                const bool compatible = outputPin && !outputPin->Input && outputPin->Type == pin.Type && !duplicate;
                if (auto disabled = ui.BeginDisabled(!compatible); disabled)
                {
                    if (ui.Button("Connect Here"))
                    {
                        Keire::VfxGraphConnection connection{Keire::AssetId::Generate(), m_PendingOutput->first,
                                                             m_PendingOutput->second, selected->Id, pin.Id};
                        if (ApplyAction("Added VFX graph connection",
                                        [&document, graph = system->Id, connection = std::move(connection)]() mutable
                                        { return document.AddConnection(graph, std::move(connection)); }))
                        {
                            m_PendingOutput.reset();
                            return;
                        }
                    }
                }
            }
            ui.SameLine();
            if (ui.Button("Remove Pin"))
            {
                (void)ApplyAction("Removed VFX graph pin",
                                  [&document, graph = system->Id, nodeId = selected->Id, pinId = pin.Id]
                                  { return document.RemovePin(graph, nodeId, pinId); });
                m_PendingOutput.reset();
                return;
            }
            ui.Separator();
        }

        const auto addPin = [&](const bool input)
        {
            Keire::VfxGraphPin pin{Keire::AssetId::Generate(), input ? "Input" : "Output", Keire::VfxValueType::Scalar,
                                   input};
            return ApplyAction("Added VFX graph pin",
                               [&document, graph = system->Id, nodeId = selected->Id, pin = std::move(pin)]() mutable
                               { return document.AddPin(graph, nodeId, std::move(pin)); });
        };
        if (ui.Button("+ Add Input") && addPin(true))
            return;
        ui.SameLine();
        if (ui.Button("+ Add Output") && addPin(false))
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
                           connection.OutputNode.ToString() + "  →  " + connection.InputNode.ToString());
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
        ui.TextColored(theme.Accent, "EXECUTABLE RUNTIME BLOCKS");
        bool hasBlocks = false;
        for (const auto& module : definition.Modules)
        {
            if (!ModuleRunsInContext(module.Payload, selected->Context))
                continue;
            hasBlocks = true;
            auto id = ui.PushId(module.Id.ToString());
            if (ui.Selectable(std::string(module.Enabled ? "" : "[Disabled] ") +
                                  std::string(ModuleName(module.Payload)),
                              module.Id == m_SelectedModule))
            {
                m_SelectedModule = module.Id;
            }
        }
        if (!hasBlocks)
            ui.TextColored(theme.MutedText, "No runtime modules assigned to this context.");

        ui.Separator();
        if (ui.Button("Delete Context") &&
            ApplyAction("Removed VFX graph node", [&document, graph = system->Id, nodeId = selected->Id]
                        { return document.RemoveNode(graph, nodeId); }))
        {
            m_SelectedNode = {};
            m_PendingOutput.reset();
            m_GraphCanvas.Select(std::nullopt);
        }
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
            ui.TextColored(theme.MutedText, "Typed values available to graph operators and future runtime bindings.");
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
                parameter.DefaultValue = DefaultParameterValue(type);
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
                changed |= m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), value, std::move(options));
                break;
            }
            }

            if (changed)
            {
                (void)ApplyAction("Edited VFX blackboard parameter",
                                  [&document, parameter = std::move(parameter)]() mutable
                                  {
                                      return document.EditBlackboardParameter(
                                          parameter.Id, [parameter = std::move(parameter)](
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
            ui.TextColored(m_Controller.VfxEffectTheme().Accent, "MODULE STACK");
            for (const auto& module : definition.Modules)
            {
                auto id = ui.PushId(module.Id.ToString());
                const auto label =
                    std::string(module.Enabled ? "" : "[Disabled] ") + std::string(ModuleName(module.Payload));
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

    void VfxEffectPanel::DrawSelectedModule(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto found = std::ranges::find(definition.Modules, m_SelectedModule, &Keire::VfxModuleDefinition::Id);
        if (found == definition.Modules.end())
        {
            ui.Text("Select a VFX module.");
            return;
        }

        auto module = *found;
        bool changed = false;
        ui.TextColored(m_Controller.VfxEffectTheme().Accent, ModuleName(module.Payload));
        ui.TextColored(m_Controller.VfxEffectTheme().MutedText, "Stable ID: " + module.Id.ToString());
        changed |= ui.Checkbox("Enabled", module.Enabled);

        const auto scalar = [&ui, &changed](const std::string_view label, float& value, const double speed,
                                            const double minimum, const double maximum)
        {
            double candidate = value;
            if (ui.DragScalar(label, candidate, speed, minimum, maximum))
            {
                value = static_cast<float>(candidate);
                changed = true;
            }
        };
        const auto integer = [&ui, &changed](const std::string_view label, std::uint32_t& value,
                                             const std::int64_t minimum, const std::int64_t maximum)
        {
            std::int64_t candidate = value;
            if (ui.DragInteger(label, candidate, 1.0, minimum, maximum))
            {
                value = static_cast<std::uint32_t>(candidate);
                changed = true;
            }
        };
        const auto asset = [this, &ui, &changed](const std::string_view label, Keire::AssetId& value,
                                                 const std::optional<Keire::AssetTypeId> type)
        {
            AssetPickerOptions options{
                .Label = label,
                .ExpectedType = type,
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealVfxEffectAsset(selected); },
            };
            changed |= m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), value, options);
        };

        std::visit(
            Overloaded{
                [&](Keire::VfxEmissionRateModule& value)
                { scalar("Particles per Second", value.ParticlesPerSecond, 0.1, 0.0, 1'000'000.0); },
                [&](Keire::VfxBurstModule& value)
                {
                    scalar("Time (s)", value.Time, 0.01, 0.0,
                           std::nextafter(static_cast<double>(definition.Duration), 0.0));
                    integer("Count", value.Count, 1, 1'000'000);
                    integer("Cycles", value.Cycles, 1, 1024);
                    scalar("Interval (s)", value.Interval, 0.01, 0.0, definition.Duration);
                },
                [&](Keire::VfxShapeModule& value)
                {
                    changed |= DrawEnum(ui, "Shape", value.Shape, Shapes);
                    changed |= ui.DragVector3("Box Half Extent", value.BoxHalfExtent, 0.01F);
                    scalar("Radius", value.Radius, 0.01, 0.001, 1'000'000.0);
                    scalar("Cone Angle", value.ConeAngleDegrees, 0.1, 0.001, 89.999);
                    scalar("Cone Length", value.ConeLength, 0.01, 0.001, 1'000'000.0);
                    asset("Mesh", value.Mesh, Keire::MeshAsset::StaticType());
                    asset("Volume Asset", value.Volume, std::nullopt);
                },
                [&](Keire::VfxInitializeModule& value)
                {
                    scalar("Lifetime Minimum", value.LifetimeMinimum, 0.01, 0.001, 86'400.0);
                    scalar("Lifetime Maximum", value.LifetimeMaximum, 0.01, 0.001, 86'400.0);
                    changed |= ui.DragVector3("Velocity Minimum", value.VelocityMinimum, 0.01F);
                    changed |= ui.DragVector3("Velocity Maximum", value.VelocityMaximum, 0.01F);
                    changed |= ui.DragVector3("Rotation Minimum", value.RotationMinimum, 0.1F);
                    changed |= ui.DragVector3("Rotation Maximum", value.RotationMaximum, 0.1F);
                },
                [&](Keire::VfxForceModule& value)
                {
                    changed |= ui.DragVector3("Force", value.Force, 0.01F);
                    scalar("Gravity Multiplier", value.GravityMultiplier, 0.01, -1000.0, 1000.0);
                },
                [&](Keire::VfxSizeOverLifetimeModule& value)
                { changed |= AuthoringValueEditors::Curve(ui, "Size Curve", value.Size); },
                [&](Keire::VfxColorOverLifetimeModule& value)
                { changed |= AuthoringValueEditors::Gradient(ui, "Color Gradient", value.Color); },
                [&](Keire::VfxCollisionModule& value)
                {
                    changed |= DrawEnum(ui, "Mode", value.Mode, CollisionModes);
                    scalar("Restitution", value.Restitution, 0.01, 0.0, 1.0);
                    changed |= ui.Checkbox("Kill on Collision", value.KillOnCollision);
                },
                [&](Keire::VfxRendererModule& value)
                {
                    changed |= DrawEnum(ui, "Renderer", value.Type, RendererTypes);
                    asset("Sprite", value.Sprite, Keire::Texture2DAsset::StaticType());
                    asset("Mesh", value.Mesh, Keire::MeshAsset::StaticType());
                    ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                                   "Only the asset selected by the renderer type is used.");
                },
            },
            module.Payload);

        if (!m_AssetPicker.Diagnostic().empty())
            ui.TextColored(m_Controller.VfxEffectTheme().Warning, m_AssetPicker.Diagnostic());
        if (changed)
            (void)ApplyAction("Edited VFX module",
                              [&document, module = std::move(module)]() mutable
                              {
                                  return document.EditModule(
                                      module.Id,
                                      [module = std::move(module)](Keire::VfxModuleDefinition& candidate) mutable
                                      { candidate = std::move(module); });
                              });
    }

    void VfxEffectPanel::ResetTransientState() noexcept
    {
        m_SelectedModule = {};
        m_SelectedSystem = {};
        m_SelectedNode = {};
        m_SelectedParameter = {};
        m_PendingOutput.reset();
        m_GraphCanvas.Select(std::nullopt);
        m_AssetPicker.Clear();
        m_Message.clear();
    }

    void VfxEffectPanel::StopTransientPreview() noexcept
    {
        m_Controller.StopVfxEffectPreview();
        m_WasVisible = false;
    }
} // namespace KeireEditor
