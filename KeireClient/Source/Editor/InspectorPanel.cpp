#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/EulerEditContinuity.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/InspectorPropertyVisibility.h"
#include "KeireClient/Editor/ManagedDataInspectorPanel.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/UiBuilderInspector.h"
#include "KeireClient/Editor/VfxEmitterInspector.h"

#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
namespace
{
    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(const std::span<const std::byte> bytes)
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream stream(text);
        std::vector<Keire::AssetId> result;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty())
                result.push_back(Keire::AssetId::Parse(line));
        }
        return result;
    }

    [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view value, const std::string_view query)
    {
        if (query.empty())
            return true;
        const auto found = std::ranges::search(value, query,
                                               [](const char left, const char right)
                                               {
                                                   return std::tolower(static_cast<unsigned char>(left)) ==
                                                          std::tolower(static_cast<unsigned char>(right));
                                               });
        return !found.empty();
    }

    [[nodiscard]] std::string JoinEntityTags(const std::span<const std::string> tags)
    {
        std::string result;
        for (const auto& tag : tags)
        {
            if (!result.empty())
                result += ", ";
            result += tag;
        }
        return result;
    }

    [[nodiscard]] std::optional<std::vector<std::string>> ParseEntityTags(const std::string_view text)
    {
        std::vector<std::string> result;
        std::unordered_set<std::string> unique;
        std::size_t begin = 0;
        while (begin < text.size())
        {
            const auto separator = text.find(',', begin);
            auto value =
                text.substr(begin, separator == std::string_view::npos ? text.size() - begin : separator - begin);
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return std::nullopt;
            value.remove_prefix(first);
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(0, last + 1);
            if (!Keire::SceneAsset::IsValidEntityTag(value) || !unique.emplace(value).second ||
                result.size() >= Keire::MaximumEntityTagCount)
                return std::nullopt;
            result.emplace_back(value);
            if (separator == std::string_view::npos)
                break;
            begin = separator + 1;
            if (begin == text.size())
                return std::nullopt;
        }
        return result;
    }

} // namespace

KeireEditor::InspectorPanel::InspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetInspector(std::make_unique<AssetInspectorPanel>(controller)),
      m_AssetPicker(std::make_unique<AssetPicker>())
{
}

KeireEditor::InspectorPanel::~InspectorPanel() = default;

void KeireEditor::InspectorPanel::Draw(Keire::UiFrame& ui)
{
    auto panel = ui.BeginPanel(m_Registration);
    if (!panel)
        return;
    auto& sceneDocument = m_Controller.InspectorSceneDocument();
    auto& propertyDrawers = m_Controller.InspectorPropertyDrawers();
    const auto& theme = m_Controller.InspectorTheme();
    const auto records = m_Controller.InspectorAssetRecords();
    const auto assets = m_Controller.InspectorAssetSystem();
    const auto managedAssetTypes = m_Controller.InspectorManagedAssetTypes();
    const auto resolveManagedType = [this](const Keire::AssetId asset)
    { return m_Controller.InspectorManagedDataType(asset); };
    const auto database = m_Controller.InspectorAssetDatabase();
    const auto scene = sceneDocument.ActiveScene();
    const auto selectedAsset = m_Controller.InspectorSelectedAsset();
    if (!m_Registration.Locked())
    {
        m_LockedEntity = {};
        m_LockedAsset = {};
    }
    else if (!m_LockedEntity && !m_LockedAsset)
    {
        if (scene && sceneDocument.Selection())
            m_LockedEntity = sceneDocument.Selection();
        else
            m_LockedAsset = selectedAsset;
    }
    if (m_Registration.Locked() &&
        ((!m_LockedEntity && !m_LockedAsset) ||
         (m_LockedEntity && (!scene || !scene->FindEntity(Keire::EntityId(m_LockedEntity)))) ||
         (m_LockedAsset && (!database || !database->Find(m_LockedAsset)))))
    {
        m_Registration.SetLocked(false);
        m_LockedEntity = {};
        m_LockedAsset = {};
    }
    const auto inspectedEntity = m_Registration.Locked() ? m_LockedEntity : sceneDocument.Selection();
    const auto inspectedAsset = m_Registration.Locked() ? m_LockedAsset : selectedAsset;
    if (ui.WindowFocused() && scene && inspectedEntity)
        m_Controller.ActivateInspectorHistory();
    ui.TextColored(theme.Accent, "INSPECTOR");
    ui.Separator();
    if (scene && inspectedEntity)
    {
        auto entity = scene->FindEntity(Keire::EntityId(inspectedEntity));
        if (entity)
        {
            const bool multiEditing = !m_Registration.Locked() && sceneDocument.Selections().size() > 1;
            std::vector<Keire::AssetId> editTargets;
            if (multiEditing)
                editTargets.assign(sceneDocument.Selections().begin(), sceneDocument.Selections().end());
            else
                editTargets.push_back(entity.Id().Value());
            if (multiEditing)
            {
                ui.TextColored(theme.MutedText, std::to_string(editTargets.size()) +
                                                    " entities selected; common component changes apply to all.");
            }
            const auto currentName = entity.Name();
            if (m_EntityNameTarget != entity.Id().Value() || !m_EntityNameEditing)
            {
                m_EntityNameTarget = entity.Id().Value();
                m_EntityNameDraft = currentName;
            }
            (void)ui.InputText(multiEditing ? "Primary Entity Name" : "Entity Name", m_EntityNameDraft);
            const auto nameState = ui.LastItemState();
            const bool validEntityName = SceneDocument::IsValidEntityName(m_EntityNameDraft);
            if (nameState.DeactivatedAfterEdit)
            {
                if (validEntityName && m_EntityNameDraft != currentName)
                {
                    m_Controller.RecordInspectorUndo();
                    sceneDocument.RenameEntity(entity.Id(), m_EntityNameDraft);
                }
                else if (!validEntityName)
                    m_EntityNameDraft = currentName;
            }
            if (nameState.Active && !validEntityName)
                ui.TextColored(theme.Error, m_EntityNameDraft.empty() ? "Entity name cannot be empty."
                                                                      : "Entity name cannot exceed 256 UTF-8 bytes.");
            m_EntityNameEditing = nameState.Active;
            auto active = entity.ActiveSelf();
            const bool mixedActive =
                multiEditing && std::ranges::any_of(editTargets,
                                                    [&](const Keire::AssetId id)
                                                    {
                                                        const auto selected = scene->FindEntity(Keire::EntityId(id));
                                                        return selected && selected.ActiveSelf() != active;
                                                    });
            if (mixedActive)
                ui.TextColored(theme.MutedText, "Active: Mixed");
            if (ui.Checkbox("Active", active))
            {
                m_Controller.RecordInspectorUndo();
                if (multiEditing)
                    sceneDocument.SetEntitiesActive(editTargets, active);
                else
                    sceneDocument.SetEntityActive(entity.Id(), active);
            }
            const auto layerNames = m_Controller.InspectorLayerNames();
            auto layer = entity.Layer();
            bool mixedLayers = false;
            if (!m_Registration.Locked() && sceneDocument.Selections().size() > 1)
            {
                for (const auto selected : sceneDocument.Selections())
                {
                    const auto selectedEntity = scene->FindEntity(Keire::EntityId(selected));
                    if (selectedEntity && selectedEntity.Layer() != layer)
                    {
                        mixedLayers = true;
                        break;
                    }
                }
            }
            const auto layerPreview =
                mixedLayers ? std::string("Mixed")
                            : (layer < layerNames.size() ? layerNames[layer] : "Layer " + std::to_string(layer));
            if (auto layerCombo = ui.BeginCombo("Layer", layerPreview); layerCombo)
            {
                for (std::uint32_t candidate = 0; candidate < Keire::EntityLayerCount; ++candidate)
                {
                    const auto label = candidate < layerNames.size() && !layerNames[candidate].empty()
                                           ? layerNames[candidate]
                                           : "Layer " + std::to_string(candidate);
                    if (ui.Selectable(label, !mixedLayers && candidate == layer))
                    {
                        m_Controller.RecordInspectorUndo("Change Layer");
                        if (!m_Registration.Locked() && sceneDocument.Selections().size() > 1)
                            sceneDocument.SetEntitiesLayer(sceneDocument.Selections(), candidate);
                        else
                            sceneDocument.SetEntityLayer(entity.Id(), candidate);
                        layer = candidate;
                        mixedLayers = false;
                    }
                }
            }
            const auto currentTags = entity.Tags();
            if (m_EntityTagTarget != entity.Id().Value() || !m_EntityTagEditing)
            {
                m_EntityTagTarget = entity.Id().Value();
                m_EntityTagDraft = JoinEntityTags(currentTags);
            }
            (void)ui.InputText(multiEditing ? "Primary Entity Tags" : "Tags", m_EntityTagDraft);
            const auto tagState = ui.LastItemState();
            const auto parsedTags = ParseEntityTags(m_EntityTagDraft);
            if (tagState.DeactivatedAfterEdit)
            {
                if (parsedTags && *parsedTags != currentTags)
                {
                    m_Controller.RecordInspectorUndo("Change Tags");
                    sceneDocument.SetEntityTags(entity.Id(), *parsedTags);
                }
                else if (!parsedTags)
                    m_EntityTagDraft = JoinEntityTags(currentTags);
            }
            if (tagState.Active)
            {
                ui.TextColored(parsedTags ? theme.MutedText : theme.Error,
                               parsedTags ? "Comma-separated identifiers; supports letters, digits, _, -, and ."
                                          : "Use up to 16 unique tags beginning with a letter.");
            }
            m_EntityTagEditing = tagState.Active;
            ui.Separator();
            const auto copiedRegistration = m_ComponentClipboard && m_ComponentClipboard->WholeComponent
                                                ? scene->Components()->Find(m_ComponentClipboard->Type)
                                                : std::nullopt;
            const bool canPasteComponent =
                copiedRegistration && copiedRegistration->Removable &&
                (copiedRegistration->AllowMultiple || !entity.HasComponent(copiedRegistration->Type));
            if (ui.WindowFocused() && ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}) && canPasteComponent)
            {
                try
                {
                    m_Controller.RecordInspectorUndo("Paste " + copiedRegistration->Name);
                    const auto pasted = sceneDocument.AddComponent(entity.Id(), copiedRegistration->Type);
                    try
                    {
                        sceneDocument.SetComponentValues(entity.Id(), pasted, m_ComponentClipboard->Values);
                    }
                    catch (...)
                    {
                        sceneDocument.RemoveComponent(entity.Id(), pasted);
                        throw;
                    }
                }
                catch (const std::exception& error)
                {
                    m_Controller.ReportInspectorAssetError(std::string("Component operation failed: ") + error.what());
                }
            }
            const auto componentOrder = entity.GetComponents();
            std::size_t editingComponentOrdinal = 0;
            const auto commonComponent = [&](const Keire::Ref<Keire::Component>& component)
            {
                if (!multiEditing)
                    return true;
                const auto ordinal = ComponentOrdinal(componentOrder, component);
                for (const auto targetId : editTargets)
                {
                    const auto target = scene->FindEntity(Keire::EntityId(targetId));
                    if (!target)
                        return false;
                    std::size_t matches = 0;
                    for (const auto& candidate : target.GetComponents())
                        if (candidate && candidate->Type() == component->Type())
                            ++matches;
                    if (matches <= ordinal)
                        return false;
                }
                return true;
            };
            const auto setComponentEnabled = [&](const Keire::ComponentTypeId type, const bool enabled)
            {
                if (multiEditing)
                    sceneDocument.SetComponentsEnabled(editTargets, type, enabled, editingComponentOrdinal);
                else
                    sceneDocument.SetComponentEnabled(entity.Id(), type, enabled);
            };
            const auto setComponentProperty = [&](const Keire::ComponentTypeId type, const std::string_view property,
                                                  Keire::ComponentPropertyValue value)
            {
                if (multiEditing)
                    sceneDocument.SetComponentsProperty(editTargets, type, property, value, editingComponentOrdinal);
                else
                    sceneDocument.SetComponentProperty(entity.Id(), type, property, std::move(value));
            };
            const auto resetComponent = [&](const Keire::ComponentTypeId type)
            {
                if (multiEditing)
                    sceneDocument.ResetComponents(editTargets, type, editingComponentOrdinal);
                else
                    sceneDocument.ResetComponent(entity.Id(), type);
            };
            const auto drawComponentHeader = [&](const Keire::Ref<Keire::Component>& component,
                                                 const Keire::ComponentRegistration& registration, bool& expanded,
                                                 const std::string_view title, const bool mixedValues)
            {
                bool removed = false;
                const auto ordinal = ComponentOrdinal(componentOrder, component);
                const auto unique = registration.Type.ToString() + "." + std::to_string(ordinal);
                const auto popupId = "ComponentMenu##" + unique;
                const auto tableId = "ComponentHeader##" + unique;
                bool openMenu = false;
                if (auto table = ui.BeginTable(tableId, 2,
                                               {.Sizing = Keire::UiTableSizing::Proportional,
                                                .Borders = false,
                                                .Resizable = false,
                                                .RowBackground = false,
                                                .PersistSettings = false});
                    table)
                {
                    ui.TableSetupColumn("Component", Keire::UiTableColumnSizing::Stretch, 1.0F);
                    ui.TableSetupColumn("Menu", Keire::UiTableColumnSizing::Fixed, 28.0F);
                    ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    if (ui.Selectable((expanded ? "v  " : ">  ") + std::string(title)))
                        expanded = !expanded;
                    const bool hoveredComponent = ui.LastItemState().Hovered;
                    const auto headerRect = ui.LastItemRect();
                    if (!multiEditing)
                    {
                        if (auto source = ui.BeginDragSource(); source)
                        {
                            const auto payload = EncodeComponentOrderPayload({entity.Id(), registration.Type, ordinal});
                            ui.SetDragPayload("KEIRE_COMPONENT_ORDER",
                                              std::as_bytes(std::span(payload.data(), payload.size())));
                            ui.Text(entity.Name() + " / " + registration.Name);
                        }
                        if (auto target = ui.BeginDragTarget(headerRect, "ComponentDrop##" + unique); target)
                        {
                            std::vector<std::byte> bytes;
                            if (ui.AcceptDragPayload("KEIRE_COMPONENT_ORDER", bytes))
                            {
                                const auto payload = DecodeComponentOrderPayload(bytes);
                                const auto source =
                                    payload ? ResolveComponentOrderPayload(componentOrder, entity.Id(), *payload)
                                            : Keire::Ref<Keire::Component>{};
                                if (source && source != component)
                                {
                                    m_Controller.RecordInspectorUndo("Reorder Components");
                                    sceneDocument.MoveComponentBefore(entity.Id(), source, component);
                                }
                            }
                        }
                    }
                    if (hoveredComponent && ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
                    {
                        m_ComponentClipboard =
                            ComponentClipboard{registration.Type, registration.Serialize(*component), true};
                    }
                    if (!multiEditing)
                        if (auto context = ui.BeginItemContextMenu("ComponentContext##" + unique); context)
                            removed = DrawComponentMenu(ui, entity, component, registration, sceneDocument, scene);
                    (void)ui.TableNextColumn();
                    if (!multiEditing &&
                        ui.IconButton("ComponentMore##" + unique, Keire::UiIcon::More, false, {26.0F, 22.0F}))
                    {
                        openMenu = true;
                    }
                    if (!multiEditing && ui.LastItemState().Hovered)
                        ui.SetTooltip("Component actions", {.Delayed = true});
                    if (multiEditing)
                        ui.TextColored(theme.MutedText, mixedValues ? "Common | Mixed values" : "Common");
                }
                if (openMenu)
                    ui.OpenPopup(popupId);
                if (auto popup = ui.BeginPopup(popupId); popup)
                    removed = DrawComponentMenu(ui, entity, component, registration, sceneDocument, scene) || removed;
                return removed;
            };
            const auto expansion = [&](const std::string_view type) -> bool&
            {
                const auto key = entity.Id().ToString() + "." + std::string(type);
                return m_ComponentExpansion.try_emplace(key, true).first->second;
            };
            for (const auto& displayedComponent : componentOrder)
            {
                if (!displayedComponent || !commonComponent(displayedComponent))
                    continue;
                editingComponentOrdinal = ComponentOrdinal(componentOrder, displayedComponent);
                const auto displayedRegistration = scene->Components()->Find(displayedComponent->Type());
                const bool mixedComponentValues =
                    multiEditing && displayedRegistration &&
                    !HaveUniformComponentValues(scene, editTargets, *displayedRegistration, displayedComponent,
                                                editingComponentOrdinal);
                if (displayedComponent->Type() == Keire::TransformComponent::StaticType())
                {
                    auto& transformExpanded = expansion("transform");
                    const auto transform = entity.GetComponent<Keire::TransformComponent>();
                    const auto transformRegistration =
                        scene->Components()->Find(Keire::TransformComponent::StaticType());
                    const float transformCardHeight =
                        transformExpanded ? (ui.ContentAvailable().Width < 325.0F ? 267.0F : 212.0F) : 38.0F;
                    if (auto card = ui.BeginChild("TransformCard", {0.0F, transformCardHeight}, true); card)
                    {
                        if (transformRegistration)
                            (void)drawComponentHeader(transform, *transformRegistration, transformExpanded, "TRANSFORM",
                                                      mixedComponentValues);
                        if (transformExpanded)
                        {
                            ui.TextColored(theme.MutedText, "Required | Local space");
                            ui.Separator();
                            auto position = transform->LocalPosition();
                            const auto previousPosition = position;
                            const auto currentOrientation = transform->LocalRotation();
                            if (m_RotationTarget != entity.Id().Value() ||
                                (!m_RotationEditing && !SameRotation(currentOrientation, m_RotationOrientation)))
                            {
                                const auto reference = m_RotationTarget == entity.Id().Value()
                                                           ? m_RotationEuler
                                                           : transform->LocalEulerAngles();
                                m_RotationEuler = ContinuousEulerAngles(currentOrientation, reference);
                                m_RotationOrientation = currentOrientation;
                                m_RotationTarget = entity.Id().Value();
                            }
                            auto rotation = m_RotationEuler;
                            auto scale = transform->LocalScale();
                            const bool positionChanged = ui.DragVector3("Position", position, 0.05F);
                            const auto positionState = ui.LastItemState();
                            const bool rotationChanged = ui.DragVector3("Rotation", rotation, 0.25F);
                            const auto rotationState = ui.LastItemState();
                            const auto previousScale = scale;
                            const bool scaleChanged = ui.DragVector3("Scale", scale, 0.01F);
                            const auto scaleState = ui.LastItemState();
                            if (m_UniformScale && scaleChanged)
                            {
                                const auto propagate =
                                    [](const float previous, const float current, const float otherPrevious)
                                {
                                    if (std::abs(previous) > 0.0001F)
                                        return otherPrevious * (current / previous);
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
                                if (multiEditing)
                                {
                                    m_Controller.RecordInspectorUndo(
                                        "Change Position", "transform.multi.position." + std::to_string(m_EditSerial));
                                    sceneDocument.SetTransforms(editTargets, {.Position = position});
                                }
                                else
                                {
                                    m_Controller.ApplyInspectorTransformEdit(
                                        MakeInspectorTransformEdit(entity.Id(), InspectorTransformProperty::Position,
                                                                   previousPosition, position, m_EditSerial));
                                }
                            }
                            if (rotationChanged)
                            {
                                const auto orientation = Keire::Math::EulerDegreesToQuaternion(rotation);
                                if (multiEditing)
                                {
                                    m_Controller.RecordInspectorUndo(
                                        "Change Rotation", "transform.multi.rotation." + std::to_string(m_EditSerial));
                                    sceneDocument.SetTransforms(editTargets, {.EulerDegrees = rotation});
                                }
                                else
                                {
                                    m_Controller.ApplyInspectorTransformEdit(
                                        MakeInspectorTransformEdit(entity.Id(), InspectorTransformProperty::Rotation,
                                                                   currentOrientation, orientation, m_EditSerial));
                                }
                                m_RotationEuler = rotation;
                                m_RotationOrientation = orientation;
                            }
                            if (scaleChanged && validScale)
                            {
                                if (multiEditing)
                                {
                                    m_Controller.RecordInspectorUndo("Change Scale", "transform.multi.scale." +
                                                                                         std::to_string(m_EditSerial));
                                    sceneDocument.SetTransforms(editTargets, {.Scale = scale});
                                }
                                else
                                {
                                    m_Controller.ApplyInspectorTransformEdit(
                                        MakeInspectorTransformEdit(entity.Id(), InspectorTransformProperty::Scale,
                                                                   previousScale, scale, m_EditSerial));
                                }
                            }
                            if (scaleState.Active && !validScale)
                                ui.TextColored(
                                    theme.Error,
                                    "Scale axes need magnitude 0.000001 or greater. Finish typing to apply.");
                            m_RotationEditing = rotationState.Active;
                            if (rotationState.DeactivatedAfterEdit)
                            {
                                m_RotationEditing = false;
                                m_RotationOrientation = transform->LocalRotation();
                            }
                            if (positionState.DeactivatedAfterEdit || rotationState.DeactivatedAfterEdit ||
                                scaleState.DeactivatedAfterEdit)
                                ++m_EditSerial;
                            ui.Spacing();
                            (void)ui.Checkbox("Uniform scale", m_UniformScale);
                            ui.SameLine();
                            if (ui.Button("Reset"))
                            {
                                m_Controller.RecordInspectorUndo();
                                const SceneDocument::TransformValues defaults{.Position = Keire::Vector3{},
                                                                              .EulerDegrees = Keire::Vector3{},
                                                                              .Scale =
                                                                                  Keire::Vector3{1.0F, 1.0F, 1.0F}};
                                if (multiEditing)
                                    sceneDocument.SetTransforms(editTargets, defaults);
                                else
                                    sceneDocument.SetTransform(entity.Id(), defaults);
                                m_RotationTarget = entity.Id().Value();
                                m_RotationEuler = {};
                                m_RotationOrientation = {};
                                m_RotationEditing = false;
                            }
                            if (ui.LastItemState().Hovered)
                                ui.SetTooltip("Reset local position, rotation, and scale.");
                        }
                    }
                }
                if (displayedComponent->Type() == Keire::DirectionalLightComponent::StaticType())
                {
                    if (const auto light = entity.GetComponent<Keire::DirectionalLightComponent>())
                    {
                        ui.Spacing();
                        auto& lightExpanded = expansion("directional-light");
                        if (auto card =
                                ui.BeginChild("DirectionalLightCard", {0.0F, lightExpanded ? 360.0F : 38.0F}, true);
                            card)
                        {
                            const auto registration = scene->Components()->Find(light->Type());
                            const bool removed =
                                registration && drawComponentHeader(light, *registration, lightExpanded,
                                                                    "DIRECTIONAL LIGHT", mixedComponentValues);
                            if (lightExpanded && !removed)
                            {
                                auto enabled = light->Enabled();
                                if (ui.Checkbox("Enabled", enabled))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentEnabled(light->Type(), enabled);
                                }
                                auto color = light->LightColor();
                                Keire::UiColor editorColor{color.Red, color.Green, color.Blue, color.Alpha};
                                if (ui.ColorEdit("Color", editorColor))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "color",
                                                         Keire::Color{editorColor.Red, editorColor.Green,
                                                                      editorColor.Blue, editorColor.Alpha});
                                }
                                auto intensity = light->Intensity();
                                if (ui.SliderFloat("Intensity", intensity, 0.0F, 100.0F))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "intensity", static_cast<double>(intensity));
                                }
                                auto temperature = light->UseColorTemperature();
                                if (ui.Checkbox("Use Color Temperature", temperature))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "useTemperature", temperature);
                                }
                                auto kelvin = light->ColorTemperatureKelvin();
                                if (ui.SliderFloat("Temperature (K)", kelvin, 1000.0F, 20000.0F))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "temperature", static_cast<double>(kelvin));
                                }
                                const auto shadows = light->Shadows();
                                const auto shadowLabel = shadows == Keire::ShadowQuality::Disabled ? "Disabled"
                                                         : shadows == Keire::ShadowQuality::Hard   ? "Hard"
                                                                                                   : "Soft";
                                if (auto shadowMode = ui.BeginCombo("Shadows", shadowLabel); shadowMode)
                                {
                                    constexpr std::array modes{Keire::ShadowQuality::Disabled,
                                                               Keire::ShadowQuality::Hard, Keire::ShadowQuality::Soft};
                                    constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                                    for (std::size_t index = 0; index < modes.size(); ++index)
                                    {
                                        if (ui.Selectable(labels[index], shadows == modes[index]))
                                        {
                                            m_Controller.RecordInspectorUndo();
                                            setComponentProperty(light->Type(), "shadows",
                                                                 static_cast<std::int64_t>(modes[index]));
                                        }
                                    }
                                }
                                auto shadowStrength = light->ShadowStrength();
                                if (ui.SliderFloat("Shadow Strength", shadowStrength, 0.0F, 1.0F))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "shadowStrength",
                                                         static_cast<double>(shadowStrength));
                                }
                                auto bias = light->ShadowBias();
                                if (ui.SliderFloat("Shadow Bias", bias, 0.0F, 1.0F))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(light->Type(), "shadowBias", static_cast<double>(bias));
                                }
                                if (ui.Button("Reset Light"))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    resetComponent(light->Type());
                                }
                                ui.TextColored(theme.MutedText, "Direct lighting | Directional shadows");
                            }
                        }
                    }
                }
                if (displayedComponent->Type() == Keire::CameraComponent::StaticType())
                {
                    if (const auto camera = entity.GetComponent<Keire::CameraComponent>())
                    {
                        ui.Spacing();
                        auto& cameraExpanded = expansion("camera");
                        if (auto card = ui.BeginChild("CameraCard", {0.0F, cameraExpanded ? 440.0F : 38.0F}, true);
                            card)
                        {
                            const auto registration = scene->Components()->Find(camera->Type());
                            const bool removed =
                                registration && drawComponentHeader(camera, *registration, cameraExpanded, "CAMERA",
                                                                    mixedComponentValues);
                            if (cameraExpanded && !removed)
                            {
                                ui.TextColored(theme.MutedText, "Game view | Priority-selected");
                                ui.Separator();
                                auto enabled = camera->Enabled();
                                if (ui.Checkbox("Enabled##Camera", enabled))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentEnabled(camera->Type(), enabled);
                                }
                                auto primary = camera->Primary();
                                if (ui.Checkbox("Primary", primary))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(camera->Type(), "primary", primary);
                                }
                                const auto projection = camera->Projection();
                                if (auto combo =
                                        ui.BeginCombo("Projection", projection == Keire::CameraProjection::Perspective
                                                                        ? "Perspective"
                                                                        : "Orthographic");
                                    combo)
                                {
                                    if (ui.Selectable("Perspective",
                                                      projection == Keire::CameraProjection::Perspective))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "projection", std::int64_t{0});
                                    }
                                    if (ui.Selectable("Orthographic",
                                                      projection == Keire::CameraProjection::Orthographic))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "projection", std::int64_t{1});
                                    }
                                }
                                const auto clearMode = camera->ClearMode();
                                if (auto combo = ui.BeginCombo("Background", clearMode == Keire::CameraClearMode::Skybox
                                                                                 ? "Skybox"
                                                                                 : "Solid Color");
                                    combo)
                                {
                                    if (ui.Selectable("Skybox", clearMode == Keire::CameraClearMode::Skybox))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "clearMode", std::int64_t{0});
                                    }
                                    if (ui.Selectable("Solid Color", clearMode == Keire::CameraClearMode::SolidColor))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "clearMode", std::int64_t{1});
                                    }
                                }
                                auto priority = camera->Priority();
                                if (ui.SliderInt("Priority", priority, -100, 100))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(camera->Type(), "priority",
                                                         static_cast<std::int64_t>(priority));
                                }
                                if (camera->Projection() == Keire::CameraProjection::Perspective)
                                {
                                    auto fieldOfView = camera->VerticalFieldOfViewDegrees();
                                    if (ui.SliderFloat("Vertical FOV", fieldOfView, 1.0F, 179.0F))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "fieldOfView",
                                                             static_cast<double>(fieldOfView));
                                    }
                                }
                                else
                                {
                                    auto size = camera->OrthographicSize();
                                    if (ui.SliderFloat("Orthographic Size", size, 0.01F, 100.0F))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "orthographicSize",
                                                             static_cast<double>(size));
                                    }
                                }
                                auto nearPlane = camera->NearPlane();
                                auto farPlane = camera->FarPlane();
                                const bool nearChanged =
                                    ui.SliderFloat("Near Plane", nearPlane, 0.01F, std::min(farPlane - 0.01F, 100.0F));
                                const bool farChanged =
                                    ui.SliderFloat("Far Plane", farPlane, std::max(nearPlane + 0.01F, 1.0F), 10000.0F);
                                if (nearChanged || farChanged)
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(camera->Type(), "nearPlane", static_cast<double>(nearPlane));
                                    setComponentProperty(camera->Type(), "farPlane", static_cast<double>(farPlane));
                                }
                                if (camera->ClearMode() == Keire::CameraClearMode::SolidColor)
                                {
                                    auto clear = camera->ClearColor();
                                    Keire::UiColor clearColor{clear.Red, clear.Green, clear.Blue, clear.Alpha};
                                    if (ui.ColorEdit("Clear Color", clearColor))
                                    {
                                        m_Controller.RecordInspectorUndo();
                                        setComponentProperty(camera->Type(), "clearColor",
                                                             Keire::Color{clearColor.Red, clearColor.Green,
                                                                          clearColor.Blue, clearColor.Alpha});
                                    }
                                }
                                if (ui.Button("Reset Camera"))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    resetComponent(camera->Type());
                                }
                            }
                        }
                    }
                }
                if (displayedComponent->Type() == Keire::MeshRendererComponent::StaticType())
                {
                    if (const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>())
                    {
                        ui.Spacing();
                        auto& rendererExpanded = expansion("mesh-renderer");
                        if (auto card =
                                ui.BeginChild("MeshRendererCard", {0.0F, rendererExpanded ? 282.0F : 38.0F}, true);
                            card)
                        {
                            const auto registration = scene->Components()->Find(renderer->Type());
                            const bool removed =
                                registration && drawComponentHeader(renderer, *registration, rendererExpanded,
                                                                    "MESH RENDERER", mixedComponentValues);
                            if (rendererExpanded && !removed)
                            {
                                ui.TextColored(theme.MutedText, "Lit geometry submission");
                                ui.Separator();
                                auto enabled = renderer->Enabled();
                                if (ui.Checkbox("Enabled##MeshRenderer", enabled))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentEnabled(renderer->Type(), enabled);
                                }
                                auto visible = renderer->Visible();
                                if (ui.Checkbox("Visible", visible))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    setComponentProperty(renderer->Type(), "visible", visible);
                                }
                                auto alwaysVisible = renderer->AlwaysVisible();
                                if (ui.Checkbox("Always Visible", alwaysVisible))
                                {
                                    m_Controller.RecordInspectorUndo("Change Frustum Culling");
                                    setComponentProperty(renderer->Type(), "alwaysVisible", alwaysVisible);
                                }
                                if (ui.LastItemState().Hovered)
                                {
                                    ui.SetTooltip("Skip camera-frustum bounds culling for camera-relative or "
                                                  "deforming geometry.",
                                                  {.Delayed = true});
                                }
                                auto tint = renderer->Tint();
                                Keire::UiColor tintColor{tint.Red, tint.Green, tint.Blue, tint.Alpha};
                                if (ui.ColorEdit("Tint", tintColor))
                                {
                                    m_Controller.RecordInspectorUndo("Change Tint", "mesh.tint." +
                                                                                        entity.Id().ToString() + "." +
                                                                                        std::to_string(m_EditSerial));
                                    setComponentProperty(
                                        renderer->Type(), "tint",
                                        Keire::Color{tintColor.Red, tintColor.Green, tintColor.Blue, tintColor.Alpha});
                                }
                                if (ui.LastItemState().DeactivatedAfterEdit)
                                    ++m_EditSerial;
                                if (registration)
                                {
                                    InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker,
                                                                           managedAssetTypes, resolveManagedType);
                                    for (const auto& property : registration->Properties)
                                    {
                                        if (property.Key != "mesh")
                                            continue;
                                        try
                                        {
                                            const auto values = registration->Serialize(*renderer);
                                            const auto found = values.find(property.Key);
                                            if (found == values.end())
                                                throw std::invalid_argument(
                                                    "The Mesh Renderer omitted a declared property.");
                                            auto candidate = found->second;
                                            if (propertyDrawers.Draw(propertyEditor, registration->Type, property,
                                                                     candidate))
                                            {
                                                m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                                 "mesh-renderer." + property.Key + "." +
                                                                                     entity.Id().ToString());
                                                setComponentProperty(registration->Type, property.Key,
                                                                     std::move(candidate));
                                            }
                                        }
                                        catch (const std::exception& error)
                                        {
                                            ui.TextColored(theme.Error, FormatManagedInspectorError(error));
                                        }
                                    }
                                }
                                InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker,
                                                                       managedAssetTypes, resolveManagedType);
                                Keire::Ref<const Keire::MeshAsset> mesh =
                                    renderer->Mesh() ? Keire::MeshAsset::ResolveBuiltin(renderer->Mesh())
                                                     : Keire::MeshAsset::Cube();
                                if (!mesh && assets)
                                {
                                    mesh = assets->Load<Keire::MeshAsset>(renderer->Mesh(), Keire::AssetPriority::High)
                                               .TryGetLoaded();
                                }
                                const bool commonMaterialLayout =
                                    !multiEditing || HaveCommonMeshMaterialLayout(scene, editTargets, renderer);
                                if (multiEditing && !commonMaterialLayout)
                                    ui.TextColored(theme.Warning,
                                                   "Material slots require the same mesh on every selected entity.");
                                if (mesh && commonMaterialLayout)
                                {
                                    ui.TextColored(theme.MutedText, "Material Slots");
                                    for (std::size_t slot = 0; slot < mesh->MaterialSlots().size(); ++slot)
                                    {
                                        auto material = renderer->Material(slot);
                                        if (!material)
                                            material = mesh->MaterialSlots()[slot].DefaultMaterial;
                                        const auto label = mesh->MaterialSlots()[slot].Name + "##material-slot-" +
                                                           std::to_string(slot);
                                        if (propertyEditor.EditAsset(label, material,
                                                                     Keire::MaterialAsset::StaticType()))
                                        {
                                            m_Controller.RecordInspectorUndo("Change Material Slot",
                                                                             "mesh-renderer.material." +
                                                                                 std::to_string(slot) + "." +
                                                                                 entity.Id().ToString());
                                            if (multiEditing)
                                                sceneDocument.SetMeshRenderersMaterial(editTargets, slot, material);
                                            else
                                                sceneDocument.SetMeshRendererMaterial(entity.Id(), slot, material);
                                            m_Controller.NotifyInspectorMaterialAssigned(material);
                                        }
                                    }
                                }
                                auto castShadows = renderer->CastShadows();
                                if (ui.Checkbox("Cast Shadows", castShadows))
                                {
                                    m_Controller.RecordInspectorUndo("Change Shadow Casting");
                                    setComponentProperty(renderer->Type(), "castShadows", castShadows);
                                }
                                auto receiveShadows = renderer->ReceiveShadows();
                                if (ui.Checkbox("Receive Shadows", receiveShadows))
                                {
                                    m_Controller.RecordInspectorUndo("Change Shadow Receiving");
                                    setComponentProperty(renderer->Type(), "receiveShadows", receiveShadows);
                                }
                                if (ui.Button("Reset Renderer"))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    resetComponent(renderer->Type());
                                }
                            }
                        }
                    }
                }
                InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker, managedAssetTypes,
                                                       resolveManagedType, &m_ManagedGraphFocus);
                for (const auto& component : entity.GetComponents())
                {
                    if (component != displayedComponent)
                        continue;
                    if (!component || component->Type() == Keire::TransformComponent::StaticType() ||
                        component->Type() == Keire::CameraComponent::StaticType() ||
                        component->Type() == Keire::DirectionalLightComponent::StaticType() ||
                        component->Type() == Keire::MeshRendererComponent::StaticType())
                        continue;
                    const auto registration = scene->Components()->Find(component->Type());
                    if (!registration)
                        continue;
                    auto vfxEmitter = Keire::Ref<Keire::VfxEmitterComponent>{};
                    auto vfxEffect = Keire::Ref<const Keire::VfxEffectAsset>{};
                    std::string vfxEffectDiagnostic;
                    bool vfxEffectDiagnosticWarning = false;
                    if (registration->Type == Keire::VfxEmitterComponent::StaticType())
                    {
                        vfxEmitter = Keire::DynamicRefCast<Keire::VfxEmitterComponent>(component);
                        if (!vfxEmitter)
                        {
                            vfxEffectDiagnostic = "The VFX Emitter registration returned an incompatible component.";
                            vfxEffectDiagnosticWarning = true;
                        }
                        else if (!vfxEmitter->Effect())
                        {
                            vfxEffectDiagnostic = "Assign a VFX effect to edit exposed Blackboard parameters.";
                        }
                        else if (!assets)
                        {
                            vfxEffectDiagnostic =
                                "The asset system is unavailable; serialized overrides are preserved.";
                            vfxEffectDiagnosticWarning = true;
                        }
                        else
                        {
                            try
                            {
                                vfxEffect =
                                    assets
                                        ->Load<Keire::VfxEffectAsset>(vfxEmitter->Effect(), Keire::AssetPriority::High)
                                        .TryGetLoaded();
                                if (!vfxEffect)
                                    vfxEffectDiagnostic = "Loading VFX Blackboard metadata...";
                            }
                            catch (const std::exception& error)
                            {
                                vfxEffectDiagnostic =
                                    std::string("VFX Blackboard metadata is unavailable: ") + error.what();
                                vfxEffectDiagnosticWarning = true;
                            }
                        }
                    }
                    const auto audioSource = registration->Type == Keire::AudioSourceComponent::StaticType()
                                                 ? Keire::DynamicRefCast<Keire::AudioSourceComponent>(component)
                                                 : Keire::Ref<Keire::AudioSourceComponent>{};
                    const auto reverbZone = registration->Type == Keire::AudioReverbZoneComponent::StaticType()
                                                ? Keire::DynamicRefCast<Keire::AudioReverbZoneComponent>(component)
                                                : Keire::Ref<Keire::AudioReverbZoneComponent>{};
                    const auto collider = registration->Type == Keire::ColliderComponent::StaticType()
                                              ? Keire::DynamicRefCast<Keire::ColliderComponent>(component)
                                              : Keire::Ref<Keire::ColliderComponent>{};
                    const auto explicitMixer = audioSource  ? audioSource->Mixer()
                                               : reverbZone ? reverbZone->Mixer()
                                                            : Keire::AssetId{};
                    const auto resolvedMixer =
                        explicitMixer ? explicitMixer : m_Controller.InspectorDefaultAudioMixer();
                    auto mixerAsset = Keire::Ref<const Keire::AudioMixerAsset>{};
                    if (assets && resolvedMixer)
                    {
                        try
                        {
                            mixerAsset = assets->Load<Keire::AudioMixerAsset>(resolvedMixer, Keire::AssetPriority::High)
                                             .TryGetLoaded();
                        }
                        catch (...)
                        {
                            // The component card reports loading and validation state below without interrupting
                            // editing.
                        }
                    }
                    ui.Spacing();
                    const auto ordinal = ComponentOrdinal(componentOrder, component);
                    const auto componentKey = registration->Type.ToString() + "." + std::to_string(ordinal);
                    auto& expanded = expansion(componentKey);
                    const auto cardId = "ComponentCard##" + componentKey;
                    const float anchorPickerHeight = 0.0F;
                    std::size_t groupRows = 0;
                    std::size_t headerRows = 0;
                    std::size_t additionalTextRows = 0;
                    std::string_view previousGroup;
                    for (const auto& property : registration->Properties)
                    {
                        if (!IsInspectorPropertyVisible(registration->Type, property.Key))
                            continue;
                        if (!property.Group.empty() && property.Group != previousGroup)
                        {
                            ++groupRows;
                            previousGroup = property.Group;
                        }
                        if (!property.Header.empty())
                            ++headerRows;
                        if (property.TextLines > 1)
                            additionalTextRows += property.TextLines - 1;
                    }
                    const auto vfxEntries = vfxEffect ? KeireEditor::VfxEmitterInspector::VisibleEntryCount(
                                                            vfxEffect->Definition(), vfxEmitter->ParameterOverrides())
                                                      : std::size_t{0};
                    const float vfxInspectorHeight =
                        vfxEmitter ? std::min(720.0F,
                                              26.0F + static_cast<float>(std::max<std::size_t>(vfxEntries, 1U)) * 48.0F)
                                   : 0.0F;
                    const float audioSetupHeight =
                        registration->Type == Keire::AudioSourceComponent::StaticType() ||
                                registration->Type == Keire::AudioReverbZoneComponent::StaticType()
                            ? 72.0F
                            : 0.0F;
                    const float proceduralDiagnosticsHeight =
                        registration->Type == Keire::AnimatorComponent::StaticType() ? 138.0F : 0.0F;
                    const float uiDocumentActionsHeight = UiDocumentInspectorActionsHeight(component);
                    const float cardHeight =
                        expanded ? std::max(115.0F, 80.0F + anchorPickerHeight +
                                                        static_cast<float>(registration->Properties.size()) * 34.0F +
                                                        static_cast<float>(groupRows + headerRows) * 22.0F +
                                                        static_cast<float>(additionalTextRows) * 20.0F +
                                                        vfxInspectorHeight + audioSetupHeight +
                                                        proceduralDiagnosticsHeight + uiDocumentActionsHeight)
                                 : 38.0F;
                    if (auto card = ui.BeginChild(cardId, {0.0F, cardHeight}, true); card)
                    {
                        const bool removed = drawComponentHeader(component, *registration, expanded, registration->Name,
                                                                 mixedComponentValues);
                        if (!expanded || removed)
                            continue;
                        auto enabled = component->Enabled();
                        if (ui.Checkbox("Enabled##" + registration->Type.ToString(), enabled))
                        {
                            m_Controller.RecordInspectorUndo("Change " + registration->Name);
                            setComponentEnabled(registration->Type, enabled);
                        }
                        if (const auto animator = Keire::DynamicRefCast<Keire::AnimatorComponent>(component);
                            animator && animator->PoseSource() == Keire::AnimatorPoseSource::ProceduralHumanoid)
                        {
                            const auto& state = animator->ProceduralState();
                            ui.TextColored(theme.Accent, "PROCEDURAL RUNTIME");
                            ui.Text("State: " + std::string(Keire::ProceduralMotionStateName(state.State)));
                            ui.Text("Phase: " + std::to_string(state.GaitPhase) +
                                    " | Speed: " + std::to_string(state.Speed));
                            ui.Text(std::string("Contacts: L ") + (state.LeftFootPlanted ? "planted" : "free") +
                                    " | R " + (state.RightFootPlanted ? "planted" : "free"));
                            ui.Text("Quality: " +
                                    std::string(state.Quality == Keire::ProceduralMotionQuality::High     ? "High"
                                                : state.Quality == Keire::ProceduralMotionQuality::Medium ? "Medium"
                                                : state.Quality == Keire::ProceduralMotionQuality::Low    ? "Low"
                                                                                                          : "Auto"));
                            if (!animator->RuntimeDiagnostic().empty())
                                ui.TextColored(theme.Warning, animator->RuntimeDiagnostic());
                            ui.Separator();
                        }
                        DrawUiDocumentInspectorActions(ui, component, m_Controller, theme);
                        std::string activeGroup;
                        Keire::ComponentPropertyBag values;
                        bool serialized = false;
                        try
                        {
                            values = registration->Serialize(*component);
                            serialized = true;
                        }
                        catch (const std::exception& error)
                        {
                            ui.TextColored(theme.Error, error.what());
                        }
                        for (const auto& property : registration->Properties)
                        {
                            if (!serialized)
                                break;
                            if (!IsInspectorPropertyVisible(registration->Type, property.Key))
                                continue;
                            if (collider && !ColliderPropertyVisible(collider->Shape(), property.Key))
                                continue;
                            if (!property.Group.empty() && property.Group != activeGroup)
                            {
                                activeGroup = property.Group;
                                ui.TextColored(theme.MutedText, activeGroup);
                            }
                            if (!property.Header.empty())
                                ui.TextColored(theme.Accent, property.Header);
                            const auto propertyDisabled = ui.BeginDisabled(property.ReadOnly);
                            try
                            {
                                const auto found = values.find(property.Key);
                                if (found == values.end())
                                    throw std::invalid_argument("The component omitted a declared property.");
                                auto candidate = found->second;
                                std::optional<std::string> audioBusFallback;
                                bool changed = false;
                                bool editBoundary = false;
                                const bool localLight =
                                    registration->Type == Keire::PointLightComponent::StaticType() ||
                                    registration->Type == Keire::SpotLightComponent::StaticType();
                                const bool vfxOverrides =
                                    registration->Type == Keire::VfxEmitterComponent::StaticType() &&
                                    property.Key == "parameterOverrides";
                                if (collider && property.Key == "shape")
                                {
                                    const auto* current = std::get_if<std::int64_t>(&candidate);
                                    constexpr std::array<std::string_view, 5> labels{"Box", "Sphere", "Capsule",
                                                                                     "Convex Mesh", "Triangle Mesh"};
                                    if (!current || *current < 0 || static_cast<std::size_t>(*current) >= labels.size())
                                        throw std::invalid_argument("The collider shape is invalid.");
                                    if (auto shape = ui.BeginCombo("Shape##ColliderShape",
                                                                   labels[static_cast<std::size_t>(*current)]);
                                        shape)
                                    {
                                        for (std::int64_t index = 0; index < static_cast<std::int64_t>(labels.size());
                                             ++index)
                                        {
                                            if (ui.Selectable(labels[static_cast<std::size_t>(index)],
                                                              *current == index))
                                            {
                                                candidate = index;
                                                changed = true;
                                                editBoundary = true;
                                            }
                                        }
                                    }
                                }
                                else if (audioSource && property.Key == "busId")
                                {
                                    const auto current = audioSource->BusId();
                                    std::span<const Keire::AudioMixerBusDefinition> buses;
                                    std::string preview = !resolvedMixer ? "Select a Mixer" : "Loading mixer buses...";
                                    if (mixerAsset)
                                    {
                                        buses = mixerAsset->Definition().Buses;
                                        if (const auto selected =
                                                std::ranges::find(buses, current, &Keire::AudioMixerBusDefinition::Id);
                                            selected != buses.end())
                                        {
                                            preview = selected->Name;
                                        }
                                        else
                                            preview = "Missing Bus";
                                    }
                                    if (auto disabled = ui.BeginDisabled(!mixerAsset); disabled)
                                    {
                                        if (auto busCombo = ui.BeginCombo("Bus", preview); busCombo)
                                        {
                                            for (const auto& bus : buses)
                                            {
                                                if (ui.Selectable(bus.Name, bus.Id == current))
                                                {
                                                    candidate = bus.Id.ToString();
                                                    audioBusFallback = bus.Name;
                                                    changed = true;
                                                    editBoundary = true;
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (reverbZone && property.Key == "snapshotId")
                                {
                                    const auto current = reverbZone->SnapshotId();
                                    std::span<const Keire::AudioMixerSnapshotDefinition> snapshots;
                                    std::string preview = !current ? "None (wet blend only)" : "Missing Snapshot";
                                    if (mixerAsset && current)
                                    {
                                        snapshots = mixerAsset->Definition().Snapshots;
                                        if (const auto selected = std::ranges::find(
                                                snapshots, current, &Keire::AudioMixerSnapshotDefinition::Id);
                                            selected != snapshots.end())
                                        {
                                            preview = selected->Name;
                                        }
                                    }
                                    else if (mixerAsset)
                                        snapshots = mixerAsset->Definition().Snapshots;
                                    if (auto disabled = ui.BeginDisabled(!mixerAsset); disabled)
                                    {
                                        if (auto snapshotCombo = ui.BeginCombo("Snapshot", preview); snapshotCombo)
                                        {
                                            if (ui.Selectable("None (wet blend only)", !current))
                                            {
                                                candidate = std::string{};
                                                changed = true;
                                                editBoundary = true;
                                            }
                                            for (const auto& snapshot : snapshots)
                                            {
                                                if (ui.Selectable(snapshot.Name, snapshot.Id == current))
                                                {
                                                    candidate = snapshot.Id.ToString();
                                                    changed = true;
                                                    editBoundary = true;
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (vfxOverrides)
                                {
                                    if (!vfxEffect)
                                    {
                                        ui.TextColored(vfxEffectDiagnosticWarning ? theme.Warning : theme.MutedText,
                                                       vfxEffectDiagnostic);
                                    }
                                    else
                                    {
                                        auto overrides = std::vector<Keire::VfxParameterOverride>(
                                            vfxEmitter->ParameterOverrides().begin(),
                                            vfxEmitter->ParameterOverrides().end());
                                        if (KeireEditor::VfxEmitterInspector::VisibleEntryCount(vfxEffect->Definition(),
                                                                                                overrides) == 0)
                                        {
                                            ui.TextColored(theme.MutedText,
                                                           "This effect has no exposed Blackboard parameters.");
                                        }
                                        bool actionBoundary = false;
                                        KeireEditor::VfxEmitterInspectorCallbacks callbacks;
                                        callbacks.Status = [&ui, &theme](const Keire::AssetId,
                                                                         const std::string_view message,
                                                                         const bool warning)
                                        { ui.TextColored(warning ? theme.Warning : theme.MutedText, message); };
                                        callbacks.Reset = [&ui, &actionBoundary](const Keire::AssetId parameter)
                                        {
                                            ui.SameLine();
                                            auto id = ui.PushId("VfxBlackboardReset-" + parameter.ToString());
                                            if (!ui.Button("Reset"))
                                                return false;
                                            actionBoundary = true;
                                            return true;
                                        };
                                        callbacks.RemoveStale = [&ui, &actionBoundary](const Keire::AssetId parameter)
                                        {
                                            ui.SameLine();
                                            auto id = ui.PushId("VfxBlackboardRemoveStale-" + parameter.ToString());
                                            if (!ui.Button("Remove"))
                                                return false;
                                            actionBoundary = true;
                                            return true;
                                        };
                                        InspectorPropertyEditor vfxPropertyEditor(ui, records, assets, scene,
                                                                                  *m_AssetPicker);
                                        changed = KeireEditor::VfxEmitterInspector{}.Draw(
                                            vfxPropertyEditor, vfxEffect->Definition(), overrides, callbacks);
                                        editBoundary = actionBoundary || vfxPropertyEditor.EditBoundary();
                                        if (changed)
                                        {
                                            candidate = KeireEditor::VfxEmitterInspector::SerializeOverrides(
                                                *registration, values, overrides);
                                        }
                                    }
                                }
                                else if (localLight && property.Key == "shadows")
                                {
                                    const auto* current = std::get_if<std::int64_t>(&candidate);
                                    if (!current || *current < 0 || *current > 2)
                                        throw std::invalid_argument("The local-light shadow mode is invalid.");
                                    constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                                    if (auto shadowMode = ui.BeginCombo(property.DisplayName,
                                                                        labels[static_cast<std::size_t>(*current)]);
                                        shadowMode)
                                    {
                                        for (std::int64_t index = 0; index < static_cast<std::int64_t>(labels.size());
                                             ++index)
                                        {
                                            if (ui.Selectable(labels[static_cast<std::size_t>(index)],
                                                              *current == index))
                                            {
                                                candidate = index;
                                                changed = true;
                                            }
                                        }
                                    }
                                }
                                else
                                    changed =
                                        propertyDrawers.Draw(propertyEditor, registration->Type, property, candidate);
                                if (!vfxOverrides && !property.Tooltip.empty() && ui.LastItemState().Hovered)
                                    ui.SetTooltip(property.Tooltip, {.Delayed = true});
                                if (changed)
                                {
                                    m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                     registration->Type.ToString() + "." +
                                                                         property.Key + "." + entity.Id().ToString() +
                                                                         "." + std::to_string(m_EditSerial));
                                    setComponentProperty(registration->Type, property.Key, std::move(candidate));
                                    if (audioBusFallback)
                                    {
                                        setComponentProperty(registration->Type, "bus", std::move(*audioBusFallback));
                                    }
                                }
                                if (changed && (editBoundary || ui.LastItemState().DeactivatedAfterEdit))
                                    ++m_EditSerial;
                            }
                            catch (const std::exception& error)
                            {
                                ui.TextColored(theme.Error, FormatManagedInspectorError(error));
                            }
                        }
                        if (registration->Type == Keire::AudioReverbZoneComponent::StaticType())
                        {
                            const auto zone = Keire::DynamicRefCast<Keire::AudioReverbZoneComponent>(component);
                            if (!zone || !resolvedMixer)
                                ui.TextColored(theme.Warning,
                                               "Assign a Mixer here or configure the project's Default Mixer.");
                            else if (!assets)
                                ui.TextColored(theme.Warning, "Audio assets are unavailable for setup validation.");
                            else if (mixerAsset)
                            {
                                const auto& definition = mixerAsset->Definition();
                                const bool hasReverb = std::ranges::any_of(
                                    definition.Buses,
                                    [](const Keire::AudioMixerBusDefinition& bus)
                                    {
                                        return std::ranges::any_of(
                                            bus.Effects,
                                            [](const Keire::AudioMixerEffectDefinition& effect)
                                            {
                                                return effect.Type == Keire::AudioGraphNodeType::AlgorithmicReverb ||
                                                       effect.Type == Keire::AudioGraphNodeType::ConvolutionReverb;
                                            });
                                    });
                                const bool snapshotValid =
                                    !zone->SnapshotId() ||
                                    std::ranges::any_of(definition.Snapshots,
                                                        [&](const Keire::AudioMixerSnapshotDefinition& snapshot)
                                                        { return snapshot.Id == zone->SnapshotId(); });
                                ui.TextColored(hasReverb && snapshotValid ? theme.Success : theme.Warning,
                                               !hasReverb ? "Mixer has no reverb effect. Use Create Reverb Return."
                                               : !snapshotValid
                                                   ? "Snapshot Stable ID is not present in this mixer."
                                                   : "Zone setup valid: wet level follows listener overlap.");
                                ui.TextColored(
                                    theme.MutedText,
                                    "Snapshot is optional; Reverb Send controls wet intensity inside the zone.");
                                if (!zone->Mixer())
                                    ui.TextColored(theme.MutedText,
                                                   "Routing through the Project Settings Default Mixer.");
                            }
                            else
                                ui.TextColored(theme.MutedText, "Loading mixer setup validation...");
                        }
                        else if (registration->Type == Keire::AudioSourceComponent::StaticType())
                        {
                            const auto source = Keire::DynamicRefCast<Keire::AudioSourceComponent>(component);
                            if (source && resolvedMixer && assets)
                            {
                                if (mixerAsset)
                                {
                                    const auto& buses = mixerAsset->Definition().Buses;
                                    const bool busValid =
                                        source->BusId()
                                            ? std::ranges::any_of(buses, [&](const Keire::AudioMixerBusDefinition& bus)
                                                                  { return bus.Id == source->BusId(); })
                                            : std::ranges::any_of(buses, [&](const Keire::AudioMixerBusDefinition& bus)
                                                                  { return bus.Name == source->Bus(); });
                                    ui.TextColored(busValid ? theme.Success : theme.Warning,
                                                   busValid
                                                       ? "Mixer route is valid and active."
                                                       : "Selected bus is missing; audio will fall back to Master.");
                                    if (!source->Mixer())
                                        ui.TextColored(theme.MutedText,
                                                       "Routing through the Project Settings Default Mixer.");
                                }
                            }
                            else if (source && !resolvedMixer)
                                ui.TextColored(theme.Warning,
                                               "Assign a Mixer here or configure the project's Default Mixer.");
                        }
                    }
                }
            }
            ui.Spacing();
            if (auto add = ui.BeginCombo("Add Component", "Search components..."); add)
            {
                (void)ui.InputTextWithHint("##ComponentSearch", "Search scripts and components", m_ComponentSearch);
                for (const auto& registration : scene->Components()->Registrations())
                {
                    if (IsRetiredSceneUiComponent(registration.Type))
                        continue;
                    if (!ContainsCaseInsensitive(registration.Name, m_ComponentSearch) &&
                        !ContainsCaseInsensitive(registration.Category, m_ComponentSearch))
                        continue;
                    const bool canAdd = registration.Removable &&
                                        (registration.AllowMultiple || !entity.HasComponent(registration.Type));
                    if (ui.MenuItem(registration.Category + "/" + registration.Name, false, canAdd))
                    {
                        m_Controller.RecordInspectorUndo();
                        (void)sceneDocument.AddComponent(entity.Id(), registration.Type);
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Drop a C# script here to attach it");
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        for (const auto script : DecodeAssetPayload(payload))
                            m_Controller.AddScriptToEntity(entity.Id(), script);
                    }
                    catch (const std::exception& error)
                    {
                        m_Controller.ReportInspectorAssetError(error.what());
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Object ID");
            ui.Text(entity.Id().ToString());
            return;
        }
        sceneDocument.ClearSelection();
    }
    m_AssetInspector->Draw(ui, inspectedAsset, m_Registration.Locked());
}
