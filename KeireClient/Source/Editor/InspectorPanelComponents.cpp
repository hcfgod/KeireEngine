#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/InspectorPropertyVisibility.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <exception>
#include <optional>
#include <string>

namespace KeireEditor
{
    bool InspectorPanel::DrawComponentMenu(Keire::UiFrame& ui, const Keire::Entity& entity,
                                           const Keire::Ref<Keire::Component>& component,
                                           const Keire::ComponentRegistration& registration,
                                           SceneDocument& sceneDocument, const Keire::Ref<Keire::Scene>& scene)
    {
        bool removed = false;
        try
        {
            if (ui.MenuItem("Copy Component"))
                m_ComponentClipboard = ComponentClipboard{registration.Type, registration.Serialize(*component), true};
            const auto copiedRegistration = m_ComponentClipboard && m_ComponentClipboard->WholeComponent
                                                ? scene->Components()->Find(m_ComponentClipboard->Type)
                                                : std::nullopt;
            const bool canPasteComponent =
                copiedRegistration && copiedRegistration->Removable &&
                (copiedRegistration->AllowMultiple || !entity.HasComponent(copiedRegistration->Type));
            if (ui.MenuItem("Paste Component", false, canPasteComponent))
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
            ui.Separator();
            if (ui.MenuItem("Copy Values"))
                m_ComponentClipboard = ComponentClipboard{registration.Type, registration.Serialize(*component), false};
            const bool canPasteValues = m_ComponentClipboard && m_ComponentClipboard->Type == registration.Type;
            if (ui.MenuItem("Paste Values", false, canPasteValues))
            {
                m_Controller.RecordInspectorUndo("Paste " + registration.Name + " Values");
                sceneDocument.SetComponentValues(entity.Id(), component, m_ComponentClipboard->Values);
            }
            ui.Separator();
            if (ui.MenuItem("Remove", false, registration.Removable))
            {
                m_Controller.RecordInspectorUndo("Remove " + registration.Name);
                sceneDocument.RemoveComponent(entity.Id(), component);
                removed = true;
            }
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Component operation failed: ") + error.what());
        }
        return removed;
    }

    void InspectorPanel::DrawMeshRendererBakedLightingProperties(
        Keire::UiFrame& ui, const Keire::Ref<Keire::MeshRendererComponent>& renderer,
        const Keire::ComponentRegistration& registration, SceneDocument& sceneDocument,
        const Keire::Ref<Keire::Scene>& scene, const std::span<const Keire::AssetId> editTargets,
        const bool multiEditing, const std::size_t componentOrdinal, const Keire::EntityId entity)
    {
        auto& propertyDrawers = m_Controller.InspectorPropertyDrawers();
        const auto records = m_Controller.InspectorAssetRecords();
        const auto assets = m_Controller.InspectorAssetSystem();
        const auto managedAssetTypes = m_Controller.InspectorManagedAssetTypes();
        const auto resolveManagedType = [this](const Keire::AssetId asset)
        { return m_Controller.InspectorManagedDataType(asset); };
        InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker, managedAssetTypes,
                                               resolveManagedType);
        const auto values = registration.Serialize(*renderer);
        for (const auto& property : registration.Properties)
        {
            if (!IsMeshRendererBakedLightingProperty(property.Key))
                continue;
            const auto found = values.find(property.Key);
            if (found == values.end())
            {
                ui.TextColored(m_Controller.InspectorTheme().Error,
                               "The Mesh Renderer omitted a baked-lighting property.");
                continue;
            }
            auto candidate = found->second;
            if (!propertyDrawers.Draw(propertyEditor, registration.Type, property, candidate))
                continue;
            m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                             "mesh-renderer." + property.Key + "." + entity.ToString());
            if (multiEditing)
                sceneDocument.SetComponentsProperty(editTargets, registration.Type, property.Key, candidate,
                                                    componentOrdinal);
            else
                sceneDocument.SetComponentProperty(entity, registration.Type, property.Key, std::move(candidate));
        }
    }

} // namespace KeireEditor
