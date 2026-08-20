#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanelModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace KeireEditor
{
    namespace
    {
        using Detail::ModuleName;

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
        constexpr std::array KillShapes{
            EnumEntry{Keire::VfxShape::Box, std::string_view("Box")},
            EnumEntry{Keire::VfxShape::Sphere, std::string_view("Sphere")},
        };
        constexpr std::array KillShapeModes{
            EnumEntry{Keire::VfxKillShapeMode::Solid, std::string_view("Solid")},
            EnumEntry{Keire::VfxKillShapeMode::Inverted, std::string_view("Inverted")},
        };
        constexpr std::array RendererTypes{
            EnumEntry{Keire::VfxRendererType::Sprite, std::string_view("Sprite")},
            EnumEntry{Keire::VfxRendererType::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxRendererType::Ribbon, std::string_view("Ribbon")},
            EnumEntry{Keire::VfxRendererType::Volumetric, std::string_view("Volumetric")},
        };

        template <typename Value, std::size_t Size>
        [[nodiscard]] bool DrawEnum(Keire::UiFrame& ui, const std::string_view label, Value& value,
                                    const std::array<EnumEntry<Value>, Size>& entries)
        {
            const auto found = std::ranges::find(entries, value, &EnumEntry<Value>::Type);
            const auto preview = found == entries.end() ? std::string_view("Unsupported") : found->Name;
            bool changed = false;
            if (auto combo = ui.BeginCombo(label, preview); combo)
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
    } // namespace

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
        changed |= ui.Checkbox(definition.ExecutionSource == Keire::VfxExecutionSource::Graph
                                   ? "Default Enabled for New Blocks"
                                   : "Enabled",
                               module.Enabled);
        if (definition.ExecutionSource == Keire::VfxExecutionSource::Graph)
            ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                           "Existing Context Blocks have independent Enabled switches.");

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
                    asset("Volume Asset", value.Volume, Keire::VfxVolumeAsset::StaticType());
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
                    if (value.Type == Keire::VfxRendererType::Sprite || value.Type == Keire::VfxRendererType::Ribbon)
                        asset("Texture", value.Sprite, Keire::Texture2DAsset::StaticType());
                    if (value.Type == Keire::VfxRendererType::Mesh)
                    {
                        asset("Mesh", value.Mesh, Keire::MeshAsset::StaticType());
                        asset("Material", value.Material, Keire::MaterialAsset::StaticType());
                    }
                    if (value.Type == Keire::VfxRendererType::Ribbon)
                        ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                                       "Requires Particle Strip data and renders camera-facing trail segments.");
                    else if (value.Type == Keire::VfxRendererType::Volumetric)
                        ui.TextColored(m_Controller.VfxEffectTheme().MutedText,
                                       "Analytic density impostors provide CPU/GPU-matched volumetric particles.");
                },
                [&](Keire::VfxKillShapeModule& value)
                {
                    changed |= DrawEnum(ui, "Shape", value.Shape, KillShapes);
                    changed |= DrawEnum(ui, "Mode", value.Mode, KillShapeModes);
                    changed |= ui.DragVector3("Center", value.Center, 0.01F);
                    if (value.Shape == Keire::VfxShape::Box)
                        changed |= ui.DragVector3("Box Half Extent", value.BoxHalfExtent, 0.01F);
                    else
                        scalar("Radius", value.Radius, 0.01, 0.001, 1'000'000.0);
                },
            },
            module.Payload);

        if (!m_AssetPicker.Diagnostic().empty())
            ui.TextColored(m_Controller.VfxEffectTheme().Warning, m_AssetPicker.Diagnostic());
        if (changed)
        {
            const auto moduleId = module.Id;
            (void)ApplyAction("Edited VFX module",
                              [&document, moduleId, module = std::move(module)]() mutable
                              {
                                  return document.EditModule(
                                      moduleId,
                                      [module = std::move(module)](Keire::VfxModuleDefinition& candidate) mutable
                                      { candidate = std::move(module); });
                              });
        }
    }
} // namespace KeireEditor
