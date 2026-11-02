#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"

#include "Keire/ECS/Components/WaterBodyComponent.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClientInternal/Editor/WaterBodyInspectorEditing.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool WaterScalar(Keire::UiFrame& ui, const std::string_view label, double& value,
                                       const double step = 0.1, const std::optional<double> minimum = {},
                                       const std::optional<double> maximum = {})
        {
            ui.Text(label);
            ui.SetNextItemWidth(-1.0F);
            return ui.DragScalar("##Value", value, step, minimum, maximum);
        }
    } // namespace

    void InspectorPanel::EntityDrawContext::DrawWaterBody(const Keire::Ref<Keire::Component>& displayedComponent,
                                                          const bool mixedComponentValues)
    {
        const auto body = Keire::DynamicRefCast<Keire::WaterBodyComponent>(displayedComponent);
        const auto registration = m_Scene->Components()->Find(displayedComponent->Type());
        if (!body || !registration)
        {
            return;
        }
        auto scope = m_Ui.PushId("WaterBody." + std::to_string(m_EditingComponentOrdinal));
        auto& expanded = Expansion("waterBody." + std::to_string(m_EditingComponentOrdinal));
        m_Ui.Spacing();
        m_Ui.Separator();
        if (DrawComponentHeader(body, *registration, expanded, "WATER BODY", mixedComponentValues) || !expanded)
        {
            return;
        }
        try
        {
            const auto targets = Detail::ResolveWaterTargets(m_Scene, m_EditTargets, m_EditingComponentOrdinal);
            const auto settings = body->Settings();
            const auto apply = [&](const std::string_view name, auto&& edit)
            {
                Detail::ApplyWaterEdit(m_Document, targets, m_EditingComponentOrdinal, name, edit,
                                       [&](const std::string_view command)
                                       { m_Panel.m_Controller.RecordInspectorUndo(command); });
            };
            auto enabled = body->Enabled();
            if (m_Ui.Checkbox("Enabled", enabled))
            {
                m_Panel.m_Controller.RecordInspectorUndo("Enable Water Body");
                SetComponentEnabled(body->Type(), enabled);
                return;
            }
            if (mixedComponentValues)
            {
                m_Ui.TextColoredWrapped(
                    m_Theme.MutedText,
                    "Values differ. Each edit preserves the other settings of every selected water body.");
            }
            m_Ui.Text("Quick setup");
            m_Ui.SetNextItemWidth(-1.0F);
            if (auto combo = m_Ui.BeginCombo("##WaterSetup", "Set up sea, lake or pond..."); combo)
            {
                constexpr std::array names{"Sea", "Lake", "Pond"};
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    if (m_Ui.Selectable(names[index]))
                    {
                        apply("Set Up Water Body", [index](auto& candidate)
                              { Detail::ApplyWaterSetup(candidate, static_cast<Detail::WaterSetupPreset>(index)); });
                        return;
                    }
                }
            }
            m_Ui.TextColoredWrapped(
                m_Theme.MutedText,
                "One step sets shape, size, natural color and waves. Replaces water settings; Undo restores them.");
            m_Ui.SetNextItemWidth(-1.0F);
            if (auto combo = m_Ui.BeginCombo("##WaterLook", "Apply a surface look..."); combo)
            {
                constexpr std::array names{"Natural blue", "Stylized turquoise"};
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    if (m_Ui.Selectable(names[index]))
                    {
                        apply("Apply Water Look", [index](auto& candidate)
                              { Detail::ApplyWaterLook(candidate, static_cast<Detail::WaterLookPreset>(index)); });
                        return;
                    }
                }
            }
            m_Ui.TextColoredWrapped(m_Theme.MutedText, "Looks change color, gloss and foam without resizing water.");
            const bool anySphere =
                std::ranges::any_of(targets, [](const auto& target)
                                    { return target.Component->Settings().Shape == Keire::WaterSurfaceShape::Sphere; });
            if (!anySphere)
            {
                m_Ui.Spacing();
                m_Ui.Text("Surface motion");
                {
                    auto id = m_Ui.PushId("WaveHeight");
                    auto height = Detail::WaterWaveHeight(settings);
                    if (WaterScalar(m_Ui, "Wave height (m)", height, 0.01, 0.0, 100.0))
                    {
                        apply("Set Water Wave Height",
                              [height](auto& candidate) { Detail::SetWaterWaveHeight(candidate, height); });
                        return;
                    }
                }
                m_Ui.TextColoredWrapped(m_Theme.MutedText,
                                        "Maximum combined crest-to-trough height. Zero makes the surface calm.");
                {
                    auto disabled = m_Ui.BeginDisabled(settings.Waves.empty());
                    auto id = m_Ui.PushId("WaveDirection");
                    auto direction = settings.Waves.empty() ? 0.0 : settings.Waves.front().DirectionDegrees;
                    if (WaterScalar(m_Ui, "Wave direction (degrees)", direction, 1.0, -180.0, 180.0))
                    {
                        apply("Set Water Wave Direction",
                              [direction](auto& candidate) { Detail::SetWaterWaveDirection(candidate, direction); });
                        return;
                    }
                }
            }
            if (auto section = m_Ui.BeginSection("Appearance"); section)
            {
                auto color = settings.SurfaceColor;
                Keire::UiColor editedColor{color.Red, color.Green, color.Blue, color.Alpha};
                m_Ui.Text("Surface albedo / opacity");
                m_Ui.SetNextItemWidth(-1.0F);
                if (m_Ui.ColorEdit("##SurfaceColor", editedColor))
                {
                    apply("Edit Water Color",
                          [editedColor](auto& candidate)
                          {
                              candidate.SurfaceColor = {editedColor.Red, editedColor.Green, editedColor.Blue,
                                                        editedColor.Alpha};
                          });
                    return;
                }
                const std::array appearanceFields{
                    std::pair{"Roughness", &Keire::WaterBodySettings::Roughness},
                    std::pair{"Crest foam strength", &Keire::WaterBodySettings::CrestFoamStrength}};
                for (const auto& [label, member] : appearanceFields)
                {
                    auto id = m_Ui.PushId(label);
                    double value = settings.*member;
                    if (WaterScalar(m_Ui, label, value, 0.01, 0.0, 1.0))
                    {
                        apply("Edit Water Appearance",
                              [member, value](auto& candidate) { candidate.*member = static_cast<float>(value); });
                        return;
                    }
                }
                InspectorPropertyEditor editor(m_Ui, m_Records, m_Assets, m_Scene, *m_Panel.m_AssetPicker);
                auto material = settings.Material;
                m_Ui.Text("Custom material");
                m_Ui.TextColoredWrapped(
                    m_Theme.MutedText,
                    "Leave empty for built-in water. Custom materials are not supported by the water renderer yet.");
                m_Ui.SetNextItemWidth(-1.0F);
                if (editor.EditAsset("##WaterMaterial", material, Keire::MaterialAsset::StaticType()))
                {
                    apply("Set Water Material", [&](auto& candidate) { candidate.Material = material; });
                    return;
                }
            }
            if (auto section = m_Ui.BeginSection("Quality"); section)
            {
                const std::array qualityFields{
                    std::pair{"Target grid spacing (m)", &Keire::WaterBodySettings::GridSpacingMetres},
                    std::pair{"Render distance (m)", &Keire::WaterBodySettings::RenderDistanceMetres}};
                for (const auto& [label, member] : qualityFields)
                {
                    auto id = m_Ui.PushId(label);
                    auto value = settings.*member;
                    if (WaterScalar(m_Ui, label, value, 0.1, 0.001))
                    {
                        apply("Edit Water Quality", [member, value](auto& candidate) { candidate.*member = value; });
                        return;
                    }
                }
                m_Ui.TextColoredWrapped(
                    m_Theme.MutedText,
                    "Finer spacing adds detail. The renderer may increase spacing to stay within its geometry budget.");
            }
            if (auto section = m_Ui.BeginSection("Geometry"); section)
            {
                constexpr std::array labels{"Infinite plane", "Rectangle", "Ellipse", "Sphere"};
                m_Ui.Text("Surface shape");
                m_Ui.SetNextItemWidth(-1.0F);
                if (auto combo = m_Ui.BeginCombo("##Shape", labels[static_cast<std::size_t>(settings.Shape)]); combo)
                {
                    for (std::size_t index = 0; index < labels.size(); ++index)
                    {
                        if (m_Ui.Selectable(labels[index], static_cast<std::size_t>(settings.Shape) == index))
                        {
                            apply("Set Water Shape",
                                  [index](auto& candidate)
                                  {
                                      candidate.Shape = static_cast<Keire::WaterSurfaceShape>(index);
                                      if (candidate.Shape == Keire::WaterSurfaceShape::Sphere)
                                      {
                                          candidate.Waves.clear();
                                      }
                                  });
                            return;
                        }
                    }
                }
                m_Ui.TextColoredWrapped(m_Theme.MutedText,
                                        "Dimensions are metres. Transform supplies position and rotation.");
                if (settings.Shape == Keire::WaterSurfaceShape::Rectangle ||
                    settings.Shape == Keire::WaterSurfaceShape::Ellipse)
                {
                    const std::array fields{
                        std::pair{"Half width X (m)", &Keire::WaterBodySettings::HalfExtentXMetres},
                        std::pair{"Half length Z (m)", &Keire::WaterBodySettings::HalfExtentZMetres}};
                    for (const auto& [label, member] : fields)
                    {
                        auto id = m_Ui.PushId(label);
                        auto value = settings.*member;
                        if (WaterScalar(m_Ui, label, value, 0.1, 0.001))
                        {
                            apply("Resize Water Body", [member, value](auto& candidate) { candidate.*member = value; });
                            return;
                        }
                    }
                }
                else if (settings.Shape == Keire::WaterSurfaceShape::Sphere)
                {
                    m_Ui.TextColoredWrapped(m_Theme.MutedText,
                                            "Spherical surfaces are not rendered by the built-in water pass yet.");
                    auto id = m_Ui.PushId("Radius");
                    auto value = settings.RadiusMetres;
                    if (WaterScalar(m_Ui, "Radius (m)", value, 1.0, 0.001))
                    {
                        apply("Resize Water Sphere", [value](auto& candidate) { candidate.RadiusMetres = value; });
                        return;
                    }
                }
                m_Ui.TextColoredWrapped(m_Theme.MutedText, "Switching to Sphere clears planar waves.");
            }
            if (auto section = m_Ui.BeginSection("Fluid"); section)
            {
                const std::array fields{
                    std::pair{"Depth (m)", &Keire::WaterBodySettings::DepthMetres},
                    std::pair{"Density (kg/m3)", &Keire::WaterBodySettings::DensityKilogramsPerCubicMetre}};
                for (const auto& [label, member] : fields)
                {
                    auto id = m_Ui.PushId(label);
                    auto value = settings.*member;
                    if (WaterScalar(m_Ui, label, value, 0.1, 0.001))
                    {
                        apply("Edit Water Fluid", [member, value](auto& candidate) { candidate.*member = value; });
                        return;
                    }
                }
                const std::array axes{std::pair{"Local flow X (m/s)", &Keire::Vector3d::X},
                                      std::pair{"Local flow Y (m/s)", &Keire::Vector3d::Y},
                                      std::pair{"Local flow Z (m/s)", &Keire::Vector3d::Z}};
                for (const auto& [label, member] : axes)
                {
                    auto id = m_Ui.PushId(label);
                    auto value = settings.FlowVelocityLocal.*member;
                    if (WaterScalar(m_Ui, label, value))
                    {
                        apply("Edit Water Flow",
                              [member, value](auto& candidate) { candidate.FlowVelocityLocal.*member = value; });
                        return;
                    }
                }
            }
            if (auto section = m_Ui.BeginSection("Advanced - individual waves"); section)
            {
                if (anySphere)
                {
                    m_Ui.TextColoredWrapped(m_Theme.MutedText,
                                            "Directional waves require planar water on every selected object.");
                    return;
                }
                m_Ui.TextColoredWrapped(m_Theme.MutedText,
                                        "Presets replace only the wave list. Zero waves gives a still surface.");
                m_Ui.SetNextItemWidth(-1.0F);
                if (auto combo = m_Ui.BeginCombo("##WavePreset", "Choose wave preset..."); combo)
                {
                    constexpr std::array names{"Still", "Gentle ripples", "Ocean swell"};
                    for (std::size_t index = 0; index < names.size(); ++index)
                    {
                        if (m_Ui.Selectable(names[index]))
                        {
                            apply("Apply Water Wave Preset",
                                  [index](auto& candidate)
                                  {
                                      candidate.Waves =
                                          Detail::WaterPresetWaves(static_cast<Detail::WaterWavePreset>(index));
                                  });
                            return;
                        }
                    }
                }
                const auto smallest = std::ranges::min(targets, {}, [](const auto& target)
                                                       { return target.Component->Settings().Waves.size(); })
                                          .Component->Settings()
                                          .Waves.size();
                const auto largest = std::ranges::max(targets, {}, [](const auto& target)
                                                      { return target.Component->Settings().Waves.size(); })
                                         .Component->Settings()
                                         .Waves.size();
                m_Ui.Text(std::to_string(settings.Waves.size()) + " / 16 waves");
                if (smallest != largest)
                {
                    m_Ui.TextColoredWrapped(
                        m_Theme.MutedText,
                        "Wave counts differ. Only slots present on every selected object can be edited.");
                }
                {
                    auto disabled = m_Ui.BeginDisabled(largest >= Keire::WaterWaveField::MaximumWaves);
                    if (m_Ui.Button("Add wave", {-1.0F, 0.0F}))
                    {
                        apply("Add Water Wave", [](auto& candidate) { Detail::AddWaterWave(candidate); });
                        return;
                    }
                }
                for (std::size_t index = 0; index < settings.Waves.size(); ++index)
                {
                    auto id = m_Ui.PushId("Wave." + std::to_string(index));
                    auto waveSection = m_Ui.BeginSection("Wave " + std::to_string(index + 1));
                    if (!waveSection)
                    {
                        continue;
                    }
                    auto common = m_Ui.BeginDisabled(index >= smallest);
                    if (m_Ui.Button("Remove"))
                    {
                        apply("Remove Water Wave",
                              [index](auto& candidate) { Detail::RemoveWaterWave(candidate, index); });
                        return;
                    }
                    {
                        auto disabled = m_Ui.BeginDisabled(index == 0);
                        if (m_Ui.Button("Move earlier"))
                        {
                            apply("Reorder Water Waves",
                                  [index](auto& candidate) { Detail::MoveWaterWave(candidate, index, index - 1); });
                            return;
                        }
                    }
                    {
                        auto disabled = m_Ui.BeginDisabled(index + 1 >= smallest);
                        if (m_Ui.Button("Move later"))
                        {
                            apply("Reorder Water Waves",
                                  [index](auto& candidate) { Detail::MoveWaterWave(candidate, index, index + 1); });
                            return;
                        }
                    }
                    struct WaveField
                    {
                        const char* Label;
                        double Keire::WaterWaveDefinition::* Member;
                        std::optional<double> Minimum;
                        std::optional<double> Maximum;
                    };
                    const std::array fields{
                        WaveField{"Wavelength (m)", &Keire::WaterWaveDefinition::WavelengthMetres, 0.001, {}},
                        WaveField{"Amplitude (m)", &Keire::WaterWaveDefinition::AmplitudeMetres, 0.0, {}},
                        WaveField{"Speed (m/s)", &Keire::WaterWaveDefinition::SpeedMetresPerSecond, {}, {}},
                        WaveField{"Direction (degrees)", &Keire::WaterWaveDefinition::DirectionDegrees, {}, {}},
                        WaveField{"Phase (degrees)", &Keire::WaterWaveDefinition::PhaseDegrees, {}, {}},
                        WaveField{"Steepness", &Keire::WaterWaveDefinition::Steepness, 0.0, 1.0}};
                    for (const auto& field : fields)
                    {
                        auto fieldId = m_Ui.PushId(field.Label);
                        auto value = settings.Waves[index].*field.Member;
                        if (WaterScalar(m_Ui, field.Label, value, 0.01, field.Minimum, field.Maximum))
                        {
                            apply("Edit Water Wave", [index, member = field.Member, value](auto& candidate)
                                  { candidate.Waves.at(index).*member = value; });
                            return;
                        }
                    }
                }
            }
        }
        catch (const std::exception& error)
        {
            m_Ui.TextColoredWrapped(m_Theme.Error, error.what());
        }
    }
} // namespace KeireEditor
