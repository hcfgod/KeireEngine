#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/SceneDocument.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace KeireEditor
{
    void HierarchyPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;

        auto& document = m_Controller.HierarchyDocument();
        const auto scene = m_Controller.ActiveHierarchyScene();
        if (ui.WindowFocused())
            m_Controller.ActivateHierarchyHistory();
        ui.TextColored(m_Controller.HierarchyAccent(), "HIERARCHY");
        ui.Separator();
        if (!scene)
        {
            ui.Text("No scene entities.");
            return;
        }
        if (ui.Shortcut({Keire::UiKey::Delete}))
            m_Controller.DeleteHierarchySelection();
        if (ui.Shortcut({Keire::UiKey::D, true}) && document.Selection())
        {
            m_Controller.RecordHierarchyUndo();
            const auto selected = document.Selections();
            const std::vector selection(selected.begin(), selected.end());
            std::vector<Keire::AssetId> duplicates;
            for (const auto entity : selection)
            {
                const auto duplicate = scene->DuplicateEntity(Keire::EntityId(entity)).Id().Value();
                duplicates.push_back(duplicate);
                m_Controller.MarkHierarchyEntity(duplicate);
            }
            document.SetSelections(duplicates);
        }
        if (ui.Shortcut({Keire::UiKey::F2}) && document.Selection())
        {
            const auto selected = scene->Find(document.Selection()).Snapshot();
            if (selected)
            {
                m_Controller.RecordHierarchyUndo();
                m_Controller.RequestHierarchyRename(selected->Id, selected->Name);
            }
        }
        const auto objects = scene->Objects();
        const auto drawEntity = [&](const auto& self, const Keire::SceneObjectDefinition& object) -> void
        {
            auto id = ui.PushId(object.Id.ToString());
            const auto label = (object.Active ? std::string{} : std::string("[inactive] ")) + object.Name + "##tree";
            auto node = ui.BeginTreeNode(label, document.IsSelected(object.Id));
            const auto state = ui.LastItemState();
            if (state.Hovered && ui.PointerState().LeftPressed)
                document.Select(object.Id, ui.ControlDown());
            if (auto context = ui.BeginItemContextMenu(); context)
            {
                if (!document.IsSelected(object.Id))
                    document.Select(object.Id);
                if (ui.MenuItem("Create Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    auto parent = scene->FindEntity(Keire::EntityId(object.Id));
                    document.Select(scene->CreateEntity("GameObject", parent).Id().Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Directional Light Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    auto parent = scene->FindEntity(Keire::EntityId(object.Id));
                    auto created = scene->CreateEntity("Directional Light", parent);
                    (void)created.AddComponent<Keire::DirectionalLightComponent>();
                    document.Select(created.Id().Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                ui.Separator();
                if (ui.MenuItem("Duplicate"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(scene->DuplicateEntity(Keire::EntityId(object.Id)).Id().Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Rename"))
                {
                    m_Controller.RecordHierarchyUndo();
                    m_Controller.RequestHierarchyRename(object.Id, object.Name);
                }
                if (ui.MenuItem("Delete"))
                {
                    m_Controller.RecordHierarchyUndo();
                    (void)scene->DestroyEntity(Keire::EntityId(object.Id));
                    document.ClearSelection();
                }
            }
            if (auto source = ui.BeginDragSource(); source)
            {
                const auto value = object.Id.ToString();
                ui.SetDragPayload("KEIRE_SCENE_OBJECT", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(object.Name);
            }
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                {
                    const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto child = Keire::AssetId::Parse(value);
                    if (child != object.Id)
                    {
                        m_Controller.RecordHierarchyUndo();
                        auto childEntity = scene->FindEntity(Keire::EntityId(child));
                        childEntity.SetParent(scene->FindEntity(Keire::EntityId(object.Id)), true);
                        m_Controller.MarkHierarchyEntity(child);
                    }
                }
            }
            if (node)
                for (const auto& child : objects)
                    if (child.Parent == object.Id)
                        self(self, child);
        };
        for (const auto& object : objects)
            if (!object.Parent)
                drawEntity(drawEntity, object);
        if (auto context = ui.BeginWindowContextMenu("HierarchyBlank"); context)
        {
            if (ui.MenuItem("Create Empty"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(scene->CreateEntity().Id().Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Directional Light"))
            {
                m_Controller.RecordHierarchyUndo();
                auto created = scene->CreateEntity("Directional Light");
                (void)created.AddComponent<Keire::DirectionalLightComponent>();
                document.Select(created.Id().Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
        }
    }
} // namespace KeireEditor
