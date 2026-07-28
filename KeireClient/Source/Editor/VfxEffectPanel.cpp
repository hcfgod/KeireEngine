#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
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

        const auto database = m_Controller.VfxEffectDatabase();
        const auto record = database ? database->Find(document.Asset()) : std::nullopt;
        ui.TextColored(theme.Accent, "VFX EFFECT");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (document.Dirty() ? " *" : "") : "Missing asset");
        ui.Separator();
        if (ui.Shortcut({Keire::UiKey::S, true}) && document.Dirty())
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
        }
        if (ui.Button("Save"))
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
        }
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
        if (ui.Button("Reload"))
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
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::ValidateVfxEffect(document.Definition());
                m_Message = "Validation passed.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportVfxEffectError(m_Message);
            }
        }
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        if (const auto preview = m_Controller.VfxEffectPreviewDiagnostic(); !preview.empty())
            ui.TextColored(theme.Warning, preview);
        ui.Separator();

        DrawEffectSettings(ui);
        ui.Separator();
        DrawModules(ui);
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
        m_AssetPicker.Clear();
        m_Message.clear();
    }

    void VfxEffectPanel::StopTransientPreview() noexcept
    {
        m_Controller.StopVfxEffectPreview();
        m_WasVisible = false;
    }
} // namespace KeireEditor
