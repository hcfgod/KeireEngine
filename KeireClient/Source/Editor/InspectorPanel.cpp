#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"

KeireEditor::InspectorPanel::InspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetInspector(std::make_unique<AssetInspectorPanel>(controller)),
      m_AssetPicker(std::make_unique<AssetPicker>())
{
}

KeireEditor::InspectorPanel::~InspectorPanel() = default;

void KeireEditor::InspectorPanel::Draw(Keire::UiFrame& ui)
{
    // Width-dependent previews must not toggle the scrollbar and resize themselves every frame.
    auto panel = ui.BeginPanel(m_Registration, {.AlwaysVerticalScrollbar = true});
    if (!panel)
    {
        return;
    }
    auto& sceneDocument = m_Controller.InspectorSceneDocument();
    const auto& theme = m_Controller.InspectorTheme();
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
        {
            m_LockedEntity = sceneDocument.Selection();
        }
        else
        {
            m_LockedAsset = selectedAsset;
        }
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
    {
        m_Controller.ActivateInspectorHistory();
    }
    ui.TextColored(theme.Accent, "INSPECTOR");
    ui.Separator();
    if (scene && inspectedEntity)
    {
        auto entity = scene->FindEntity(Keire::EntityId(inspectedEntity));
        if (entity)
        {
            EntityDrawContext(*this, ui, scene, entity).Draw();
            return;
        }
        sceneDocument.ClearSelection();
    }
    if (ui.WindowFocused() && inspectedAsset)
    {
        m_Controller.ActivateInspectorAssetHistory();
    }
    m_AssetInspector->Draw(ui, inspectedAsset, m_Registration.Locked());
}
