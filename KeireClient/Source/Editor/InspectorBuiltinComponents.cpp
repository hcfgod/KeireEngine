#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace KeireEditor
{
    void InspectorPanel::EntityDrawContext::DrawTransform(const bool mixedComponentValues)
    {
        auto& transformExpanded = Expansion("transform");
        const auto transform = m_Entity.GetComponent<Keire::TransformComponent>();
        const auto transformRegistration = m_Scene->Components()->Find(Keire::TransformComponent::StaticType());
        const float transformCardHeight =
            transformExpanded ? (m_Ui.ContentAvailable().Width < 325.0F ? 267.0F : 212.0F) : 38.0F;
        if (auto card = m_Ui.BeginChild("TransformCard", {0.0F, transformCardHeight}, true, false); card)
        {
            if (transformRegistration)
            {
                (void)DrawComponentHeader(transform, *transformRegistration, transformExpanded, "TRANSFORM",
                                          mixedComponentValues);
            }
            if (transformExpanded)
            {
                m_Ui.TextColored(m_Theme.MutedText, "Required | Local space");
                m_Ui.Separator();
                auto position = transform->LocalPositionPrecise();
                const auto previousPosition = position;
                const auto currentOrientation = transform->LocalRotation();
                auto rotation = m_Panel.m_RotationDisplay.Read(transform);
                auto scale = transform->LocalScale();
                const bool positionChanged = m_Ui.DragVector3d("Position", position, 0.05F);
                const auto positionState = m_Ui.LastItemState();
                const bool rotationChanged = m_Ui.DragVector3("Rotation", rotation, 0.25F);
                const auto rotationState = m_Ui.LastItemState();
                const auto previousScale = scale;
                const bool scaleChanged = m_Ui.DragVector3("Scale", scale, 0.01F);
                const auto scaleState = m_Ui.LastItemState();
                if (m_Panel.m_UniformScale && scaleChanged)
                {
                    const auto propagate = [](const float previous, const float current, const float otherPrevious)
                    {
                        if (std::abs(previous) > 0.0001F)
                        {
                            return otherPrevious * (current / previous);
                        }
                        return otherPrevious + (current - previous);
                    };
                    if (scale.X != previousScale.X)
                    {
                        scale.Y = propagate(previousScale.X, scale.X, previousScale.Y);
                        scale.Z = propagate(previousScale.X, scale.X, previousScale.Z);
                    }
                    else if (scale.Y != previousScale.Y)
                    {
                        scale.X = propagate(previousScale.Y, scale.Y, previousScale.X);
                        scale.Z = propagate(previousScale.Y, scale.Y, previousScale.Z);
                    }
                    else if (scale.Z != previousScale.Z)
                    {
                        scale.X = propagate(previousScale.Z, scale.Z, previousScale.X);
                        scale.Y = propagate(previousScale.Z, scale.Z, previousScale.Y);
                    }
                }
                const bool validScale = Keire::TransformComponent::IsValidLocalScale(scale);
                if (positionChanged)
                {
                    if (m_MultiEditing)
                    {
                        m_Panel.m_Controller.RecordInspectorUndo(
                            "Change Position", "transform.multi.position." + std::to_string(m_Panel.m_EditSerial));
                        m_Document.SetTransforms(m_EditTargets, {.Position = position});
                    }
                    else
                    {
                        m_Panel.m_Controller.ApplyInspectorTransformEdit(
                            MakeInspectorTransformEdit(m_Entity.Id(), InspectorTransformProperty::Position,
                                                       previousPosition, position, m_Panel.m_EditSerial));
                    }
                }
                if (rotationChanged)
                {
                    const auto orientation = Keire::Math::EulerDegreesToQuaternion(rotation);
                    if (m_MultiEditing)
                    {
                        m_Panel.m_Controller.RecordInspectorUndo(
                            "Change Rotation", "transform.multi.rotation." + std::to_string(m_Panel.m_EditSerial));
                        m_Document.SetTransforms(m_EditTargets, {.EulerDegrees = rotation});
                    }
                    else
                    {
                        m_Panel.m_Controller.ApplyInspectorTransformEdit(
                            MakeInspectorTransformEdit(m_Entity.Id(), InspectorTransformProperty::Rotation,
                                                       currentOrientation, orientation, m_Panel.m_EditSerial));
                    }
                    m_Panel.m_RotationDisplay.SetValue(rotation, orientation);
                }
                if (scaleChanged && validScale)
                {
                    if (m_MultiEditing)
                    {
                        m_Panel.m_Controller.RecordInspectorUndo(
                            "Change Scale", "transform.multi.scale." + std::to_string(m_Panel.m_EditSerial));
                        m_Document.SetTransforms(m_EditTargets, {.Scale = scale});
                    }
                    else
                    {
                        m_Panel.m_Controller.ApplyInspectorTransformEdit(
                            MakeInspectorTransformEdit(m_Entity.Id(), InspectorTransformProperty::Scale, previousScale,
                                                       scale, m_Panel.m_EditSerial));
                    }
                }
                if (scaleState.Active && !validScale)
                {
                    m_Ui.TextColored(m_Theme.Error,
                                     "Scale axes need magnitude 0.000001 or greater. Finish typing to apply.");
                }
                m_Panel.m_RotationDisplay.SetEditing(rotationState.Active);
                if (rotationState.DeactivatedAfterEdit)
                {
                    m_Panel.m_RotationDisplay.EndEdit(transform->LocalRotation());
                }
                if (positionState.DeactivatedAfterEdit || rotationState.DeactivatedAfterEdit ||
                    scaleState.DeactivatedAfterEdit)
                {
                    ++m_Panel.m_EditSerial;
                }
                m_Ui.Spacing();
                (void)m_Ui.Checkbox("Uniform scale", m_Panel.m_UniformScale);
                m_Ui.SameLine();
                if (m_Ui.Button("Reset"))
                {
                    m_Panel.m_Controller.RecordInspectorUndo();
                    const SceneDocument::TransformValues defaults{.Position = Keire::Vector3d{},
                                                                  .EulerDegrees = Keire::Vector3{},
                                                                  .Scale = Keire::Vector3{1.0F, 1.0F, 1.0F}};
                    if (m_MultiEditing)
                    {
                        m_Document.SetTransforms(m_EditTargets, defaults);
                    }
                    else
                    {
                        m_Document.SetTransform(m_Entity.Id(), defaults);
                    }
                    m_Panel.m_RotationDisplay.Reset();
                }
                if (m_Ui.LastItemState().Hovered)
                {
                    m_Ui.SetTooltip("Reset local position, rotation, and scale.");
                }
            }
        }
    }

    void InspectorPanel::EntityDrawContext::DrawDirectionalLight(const bool mixedComponentValues)
    {
        if (const auto light = m_Entity.GetComponent<Keire::DirectionalLightComponent>())
        {
            m_Ui.Spacing();
            auto& lightExpanded = Expansion("directional-light");
            if (auto card =
                    m_Ui.BeginChild("DirectionalLightCard", {0.0F, lightExpanded ? 665.0F : 38.0F}, true, false);
                card)
            {
                const auto registration = m_Scene->Components()->Find(light->Type());
                const bool removed = registration && DrawComponentHeader(light, *registration, lightExpanded,
                                                                         "DIRECTIONAL LIGHT", mixedComponentValues);
                if (lightExpanded && !removed)
                {
                    const auto prepareLightField = [this](const std::string_view label)
                    {
                        const auto layout = KeireEditor::Detail::ResolveInspectorFieldLayout(
                            m_Ui.ContentAvailable().Width, 0.0F, KeireEditor::Detail::InspectorInlineActionSpacing,
                            KeireEditor::Detail::InspectorDescriptiveFieldWidth);
                        if (layout.Stacked)
                        {
                            const auto visibleLabel = KeireEditor::Detail::InspectorVisibleLabel(label);
                            if (!visibleLabel.empty())
                            {
                                m_Ui.TextWrapped(visibleLabel);
                            }
                            m_Ui.SetNextItemWidth(layout.ControlWidth);
                        }
                        return KeireEditor::Detail::InspectorControlLabel(label, layout.Stacked);
                    };
                    auto enabled = light->Enabled();
                    if (m_Ui.Checkbox("Enabled", enabled))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentEnabled(light->Type(), enabled);
                    }
                    auto color = light->LightColor();
                    Keire::UiColor editorColor{color.Red, color.Green, color.Blue, color.Alpha};
                    if (m_Ui.ColorEdit(prepareLightField("Color"), editorColor))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(
                            light->Type(), "color",
                            Keire::Color{editorColor.Red, editorColor.Green, editorColor.Blue, editorColor.Alpha});
                    }
                    auto intensity = light->Intensity();
                    if (m_Ui.SliderFloat(prepareLightField("Intensity"), intensity, 0.0F, 100.0F))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(light->Type(), "intensity", static_cast<double>(intensity));
                    }
                    auto temperature = light->UseColorTemperature();
                    if (m_Ui.Checkbox("Use Color Temperature", temperature))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(light->Type(), "useTemperature", temperature);
                    }
                    auto kelvin = light->ColorTemperatureKelvin();
                    if (m_Ui.SliderFloat(prepareLightField("Temperature (K)"), kelvin, 1000.0F, 20000.0F))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(light->Type(), "temperature", static_cast<double>(kelvin));
                    }
                    const auto shadows = light->Shadows();
                    const auto shadowLabel = shadows == Keire::ShadowQuality::Disabled ? "Disabled"
                                             : shadows == Keire::ShadowQuality::Hard   ? "Hard"
                                                                                       : "Soft";
                    if (auto shadowMode = m_Ui.BeginCombo(prepareLightField("Shadows"), shadowLabel); shadowMode)
                    {
                        constexpr std::array modes{Keire::ShadowQuality::Disabled, Keire::ShadowQuality::Hard,
                                                   Keire::ShadowQuality::Soft};
                        constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                        for (std::size_t index = 0; index < modes.size(); ++index)
                        {
                            if (m_Ui.Selectable(labels[index], shadows == modes[index]))
                            {
                                m_Panel.m_Controller.RecordInspectorUndo();
                                SetComponentProperty(light->Type(), "shadows", static_cast<std::int64_t>(modes[index]));
                            }
                        }
                    }
                    auto shadowStrength = light->ShadowStrength();
                    if (m_Ui.SliderFloat(prepareLightField("Shadow Strength"), shadowStrength, 0.0F, 1.0F))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(light->Type(), "shadowStrength", static_cast<double>(shadowStrength));
                    }
                    auto bias = light->ShadowBias();
                    if (m_Ui.SliderFloat(prepareLightField("Shadow Bias"), bias, 0.0F, 1.0F))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(light->Type(), "shadowBias", static_cast<double>(bias));
                    }
                    m_Panel.DrawDirectionalLightAdvancedProperties(m_Ui, light, m_Document, m_EditTargets,
                                                                   m_MultiEditing, m_EditingComponentOrdinal);
                    if (m_Ui.Button("Reset Light"))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        ResetComponent(light->Type());
                    }
                    m_Ui.TextColored(m_Theme.MutedText, "Direct + baked lighting | Cookies | Contact shadows");
                }
            }
        }
    }

} // namespace KeireEditor
