#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    void InspectorPanel::EntityDrawContext::DrawCamera(const bool mixedComponentValues)
    {
        if (const auto camera = m_Entity.GetComponent<Keire::CameraComponent>())
        {
            m_Ui.Spacing();
            auto& cameraExpanded = Expansion("camera");
            if (auto card = m_Ui.BeginChild("CameraCard", {0.0F, cameraExpanded ? 620.0F : 38.0F}, true, false); card)
            {
                const auto registration = m_Scene->Components()->Find(camera->Type());
                const bool removed = registration && DrawComponentHeader(camera, *registration, cameraExpanded,
                                                                         "CAMERA", mixedComponentValues);
                if (cameraExpanded && !removed)
                {
                    m_Ui.TextColored(m_Theme.MutedText, "Game view | Priority-selected");
                    m_Ui.Separator();
                    auto enabled = camera->Enabled();
                    if (m_Ui.Checkbox("Enabled##Camera", enabled))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentEnabled(camera->Type(), enabled);
                    }
                    auto primary = camera->Primary();
                    if (m_Ui.Checkbox("Primary", primary))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(camera->Type(), "primary", primary);
                    }
                    const auto projection = camera->Projection();
                    if (auto combo = m_Ui.BeginCombo("Projection", projection == Keire::CameraProjection::Perspective
                                                                       ? "Perspective"
                                                                       : "Orthographic");
                        combo)
                    {
                        if (m_Ui.Selectable("Perspective", projection == Keire::CameraProjection::Perspective))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "projection", std::int64_t{0});
                        }
                        if (m_Ui.Selectable("Orthographic", projection == Keire::CameraProjection::Orthographic))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "projection", std::int64_t{1});
                        }
                    }
                    const auto clearMode = camera->ClearMode();
                    if (auto combo = m_Ui.BeginCombo(
                            "Background", clearMode == Keire::CameraClearMode::Skybox ? "Skybox" : "Solid Color");
                        combo)
                    {
                        if (m_Ui.Selectable("Skybox", clearMode == Keire::CameraClearMode::Skybox))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "clearMode", std::int64_t{0});
                        }
                        if (m_Ui.Selectable("Solid Color", clearMode == Keire::CameraClearMode::SolidColor))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "clearMode", std::int64_t{1});
                        }
                    }
                    auto priority = camera->Priority();
                    if (m_Ui.SliderInt("Priority", priority, -100, 100))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(camera->Type(), "priority", static_cast<std::int64_t>(priority));
                    }
                    if (camera->Projection() == Keire::CameraProjection::Perspective)
                    {
                        auto fieldOfView = camera->VerticalFieldOfViewDegrees();
                        if (m_Ui.SliderFloat("Vertical FOV", fieldOfView, 1.0F, 179.0F))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "fieldOfView", static_cast<double>(fieldOfView));
                        }
                    }
                    else
                    {
                        auto size = camera->OrthographicSize();
                        if (m_Ui.SliderFloat("Orthographic Size", size, 0.01F, 100.0F))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), "orthographicSize", static_cast<double>(size));
                        }
                    }
                    auto nearPlane = camera->NearPlane();
                    auto farPlane = camera->FarPlane();
                    const bool nearChanged =
                        m_Ui.SliderFloat("Near Plane", nearPlane, 0.01F, std::min(farPlane - 0.01F, 100.0F));
                    const bool farChanged =
                        m_Ui.SliderFloat("Far Plane", farPlane, std::max(nearPlane + 0.01F, 1.0F), 10000.0F);
                    if (nearChanged || farChanged)
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(camera->Type(), "nearPlane", static_cast<double>(nearPlane));
                        SetComponentProperty(camera->Type(), "farPlane", static_cast<double>(farPlane));
                    }
                    if (camera->ClearMode() == Keire::CameraClearMode::SolidColor)
                    {
                        auto clear = camera->ClearColor();
                        Keire::UiColor clearColor{clear.Red, clear.Green, clear.Blue, clear.Alpha};
                        if (m_Ui.ColorEdit("Clear Color", clearColor))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(
                                camera->Type(), "clearColor",
                                Keire::Color{clearColor.Red, clearColor.Green, clearColor.Blue, clearColor.Alpha});
                        }
                    }
                    m_Ui.Text("Fullscreen Effects");
                    InspectorPropertyEditor effectEditor(m_Ui, m_Records, m_Assets, m_Scene, *m_Panel.m_AssetPicker);
                    constexpr std::array effectKeys{"effectBeforeTonemapping", "effectAfterTonemapping",
                                                    "effectAfterUi"};
                    constexpr std::array effectLabels{"Before Tonemapping", "After Tonemapping", "After UI"};
                    for (std::size_t stage = 0; stage < effectKeys.size(); ++stage)
                    {
                        m_Ui.Text(effectLabels[stage]);
                        auto material = camera->FullscreenEffects()[stage];
                        if (effectEditor.EditAsset(std::string("##") + effectKeys[stage], material,
                                                   Keire::MaterialAsset::StaticType()))
                        {
                            m_Panel.m_Controller.RecordInspectorUndo();
                            SetComponentProperty(camera->Type(), effectKeys[stage], material);
                        }
                    }
                    if (m_Ui.Button("Reset Camera"))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        ResetComponent(camera->Type());
                    }
                }
            }
        }
    }

    void InspectorPanel::EntityDrawContext::DrawMeshRenderer(const bool mixedComponentValues)
    {
        const auto resolveManagedType = [this](const Keire::AssetId asset)
        { return m_Panel.m_Controller.InspectorManagedDataType(asset); };

        if (const auto renderer = m_Entity.GetComponent<Keire::MeshRendererComponent>())
        {
            m_Ui.Spacing();
            auto& rendererExpanded = Expansion("mesh-renderer");
            if (auto card = m_Ui.BeginChild("MeshRendererCard", {0.0F, rendererExpanded ? 426.0F : 38.0F}, true, false);
                card)
            {
                const auto registration = m_Scene->Components()->Find(renderer->Type());
                const bool removed = registration && DrawComponentHeader(renderer, *registration, rendererExpanded,
                                                                         "MESH RENDERER", mixedComponentValues);
                if (rendererExpanded && !removed)
                {
                    m_Ui.TextColored(m_Theme.MutedText, "Lit geometry submission");
                    m_Ui.Separator();
                    auto enabled = renderer->Enabled();
                    if (m_Ui.Checkbox("Enabled##MeshRenderer", enabled))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentEnabled(renderer->Type(), enabled);
                    }
                    auto visible = renderer->Visible();
                    if (m_Ui.Checkbox("Visible", visible))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        SetComponentProperty(renderer->Type(), "visible", visible);
                    }
                    auto alwaysVisible = renderer->AlwaysVisible();
                    if (m_Ui.Checkbox("Always Visible", alwaysVisible))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo("Change Frustum Culling");
                        SetComponentProperty(renderer->Type(), "alwaysVisible", alwaysVisible);
                    }
                    if (m_Ui.LastItemState().Hovered)
                    {
                        m_Ui.SetTooltip("Skip camera-frustum bounds culling for camera-relative or "
                                        "deforming geometry.",
                                        {.Delayed = true});
                    }
                    auto tint = renderer->Tint();
                    Keire::UiColor tintColor{tint.Red, tint.Green, tint.Blue, tint.Alpha};
                    if (m_Ui.ColorEdit("Tint", tintColor))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo("Change Tint",
                                                                 "mesh.tint." + m_Entity.Id().ToString() + "." +
                                                                     std::to_string(m_Panel.m_EditSerial));
                        SetComponentProperty(
                            renderer->Type(), "tint",
                            Keire::Color{tintColor.Red, tintColor.Green, tintColor.Blue, tintColor.Alpha});
                    }
                    if (m_Ui.LastItemState().DeactivatedAfterEdit)
                    {
                        ++m_Panel.m_EditSerial;
                    }
                    if (registration)
                    {
                        InspectorPropertyEditor propertyEditor(m_Ui, m_Records, m_Assets, m_Scene,
                                                               *m_Panel.m_AssetPicker, m_ManagedAssetTypes,
                                                               resolveManagedType);
                        for (const auto& property : registration->Properties)
                        {
                            if (property.Key != "mesh")
                            {
                                continue;
                            }
                            try
                            {
                                const auto values = registration->Serialize(*renderer);
                                const auto found = values.find(property.Key);
                                if (found == values.end())
                                {
                                    throw std::invalid_argument("The Mesh Renderer omitted a declared property.");
                                }
                                auto candidate = found->second;
                                if (m_PropertyDrawers.Draw(propertyEditor, registration->Type, property, candidate))
                                {
                                    m_Panel.m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                             "mesh-renderer." + property.Key + "." +
                                                                                 m_Entity.Id().ToString());
                                    SetComponentProperty(registration->Type, property.Key, std::move(candidate));
                                }
                            }
                            catch (const std::exception& error)
                            {
                                m_Ui.TextColored(m_Theme.Error, FormatManagedInspectorError(error));
                            }
                        }
                    }
                    InspectorPropertyEditor propertyEditor(m_Ui, m_Records, m_Assets, m_Scene, *m_Panel.m_AssetPicker,
                                                           m_ManagedAssetTypes, resolveManagedType);
                    Keire::Ref<const Keire::MeshAsset> mesh = renderer->Mesh()
                                                                  ? Keire::MeshAsset::ResolveBuiltin(renderer->Mesh())
                                                                  : Keire::MeshAsset::Cube();
                    if (!mesh && m_Assets)
                    {
                        mesh = m_Assets->Load<Keire::MeshAsset>(renderer->Mesh(), Keire::AssetPriority::High)
                                   .TryGetLoaded();
                    }
                    const bool commonMaterialLayout =
                        !m_MultiEditing || HaveCommonMeshMaterialLayout(m_Scene, m_EditTargets, renderer);
                    if (m_MultiEditing && !commonMaterialLayout)
                    {
                        m_Ui.TextColored(m_Theme.Warning,
                                         "Material slots require the same mesh on every selected entity.");
                    }
                    if (mesh && commonMaterialLayout)
                    {
                        m_Ui.TextColored(m_Theme.MutedText, "Material Slots");
                        for (std::size_t slot = 0; slot < mesh->MaterialSlots().size(); ++slot)
                        {
                            auto material = renderer->Material(slot);
                            if (!material)
                            {
                                material = mesh->MaterialSlots()[slot].DefaultMaterial;
                            }
                            const auto label =
                                mesh->MaterialSlots()[slot].Name + "##material-slot-" + std::to_string(slot);
                            if (propertyEditor.EditAsset(label, material, Keire::MaterialAsset::StaticType()))
                            {
                                m_Panel.m_Controller.RecordInspectorUndo(
                                    "Change Material Slot",
                                    "mesh-renderer.material." + std::to_string(slot) + "." + m_Entity.Id().ToString());
                                if (m_MultiEditing)
                                {
                                    m_Document.SetMeshRenderersMaterial(m_EditTargets, slot, material);
                                }
                                else
                                {
                                    m_Document.SetMeshRendererMaterial(m_Entity.Id(), slot, material);
                                }
                                m_Panel.m_Controller.NotifyInspectorMaterialAssigned(material);
                            }
                        }
                    }
                    auto castShadows = renderer->CastShadows();
                    if (m_Ui.Checkbox("Cast Shadows", castShadows))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo("Change Shadow Casting");
                        SetComponentProperty(renderer->Type(), "castShadows", castShadows);
                    }
                    auto receiveShadows = renderer->ReceiveShadows();
                    if (m_Ui.Checkbox("Receive Shadows", receiveShadows))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo("Change Shadow Receiving");
                        SetComponentProperty(renderer->Type(), "receiveShadows", receiveShadows);
                    }
                    m_Ui.Separator();
                    m_Ui.TextColored(m_Theme.MutedText, "Baked Lighting");
                    if (registration)
                    {
                        m_Panel.DrawMeshRendererBakedLightingProperties(m_Ui, renderer, *registration, m_Document,
                                                                        m_Scene, m_EditTargets, m_MultiEditing,
                                                                        m_EditingComponentOrdinal, m_Entity.Id());
                    }
                    if (m_Ui.Button("Reset Renderer"))
                    {
                        m_Panel.m_Controller.RecordInspectorUndo();
                        ResetComponent(renderer->Type());
                    }
                }
            }
        }
    }

} // namespace KeireEditor
