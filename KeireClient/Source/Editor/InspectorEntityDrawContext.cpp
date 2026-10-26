#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include <utility>

namespace KeireEditor
{
    InspectorPanel::EntityDrawContext::EntityDrawContext(InspectorPanel& panel, Keire::UiFrame& ui,
                                                         const Keire::Ref<Keire::Scene>& scene,
                                                         const Keire::Entity& entity)
        : m_Panel(panel), m_Ui(ui), m_Document(panel.m_Controller.InspectorSceneDocument()), m_Scene(scene),
          m_Entity(entity), m_PropertyDrawers(panel.m_Controller.InspectorPropertyDrawers()),
          m_Theme(panel.m_Controller.InspectorTheme()), m_Records(panel.m_Controller.InspectorAssetRecords()),
          m_Assets(panel.m_Controller.InspectorAssetSystem()),
          m_ManagedAssetTypes(panel.m_Controller.InspectorManagedAssetTypes()),
          m_MultiEditing(!panel.m_Registration.Locked() && m_Document.Selections().size() > 1)
    {
        if (m_MultiEditing)
        {
            m_EditTargets.assign(m_Document.Selections().begin(), m_Document.Selections().end());
        }
        else
        {
            m_EditTargets.push_back(entity.Id().Value());
        }
    }

    void InspectorPanel::EntityDrawContext::Draw()
    {
        DrawMetadata();
        PasteComponent();
        m_ComponentOrder = m_Entity.GetComponents();
        for (const auto& displayedComponent : m_ComponentOrder)
        {
            if (!displayedComponent || !IsCommonComponent(displayedComponent))
            {
                continue;
            }
            m_EditingComponentOrdinal = ComponentOrdinal(m_ComponentOrder, displayedComponent);
            m_CurrentHeaderExpanded = nullptr;
            const auto componentStartY = m_Ui.CursorScreenPosition().Y;
            const auto displayedRegistration = m_Scene->Components()->Find(displayedComponent->Type());
            const bool mixedComponentValues =
                m_MultiEditing && displayedRegistration &&
                !HaveUniformComponentValues(m_Scene, m_EditTargets, *displayedRegistration, displayedComponent,
                                            m_EditingComponentOrdinal);

            if (displayedComponent->Type() == Keire::TransformComponent::StaticType())
            {
                DrawTransform(mixedComponentValues);
            }
            else if (displayedComponent->Type() == Keire::DirectionalLightComponent::StaticType())
            {
                DrawDirectionalLight(mixedComponentValues);
            }
            else if (displayedComponent->Type() == Keire::CameraComponent::StaticType())
            {
                DrawCamera(mixedComponentValues);
            }
            else if (displayedComponent->Type() == Keire::MeshRendererComponent::StaticType())
            {
                DrawMeshRenderer(mixedComponentValues);
            }
            DrawReflectedComponent(displayedComponent, mixedComponentValues);
            if (displayedRegistration && m_CurrentHeaderExpanded)
            {
                const auto headerTitle = m_CurrentHeaderTitle;
                auto* expanded = m_CurrentHeaderExpanded;
                const auto headerId = "PinnedComponent##" + displayedRegistration->Type.ToString() + "." +
                                      std::to_string(m_EditingComponentOrdinal);
                m_Ui.StickySectionHeader(headerId, componentStartY, m_Ui.CursorScreenPosition().Y,
                                         [&]
                                         {
                                             (void)DrawComponentHeader(displayedComponent, *displayedRegistration,
                                                                       *expanded, headerTitle, mixedComponentValues);
                                         });
            }
        }

        DrawFooter();
    }

} // namespace KeireEditor
