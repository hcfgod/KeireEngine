#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/SceneDocument.h"

#include <exception>
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
} // namespace KeireEditor
