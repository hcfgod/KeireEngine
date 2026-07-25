#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SelectionRange.h"

#include <algorithm>
#include <cctype>
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
        if (ui.IconButton("HierarchyCreate", Keire::UiIcon::Create, false, {28.0F, 24.0F}))
        {
            if (scene)
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(document.CreateEntity().Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
        }
        ui.SameLine();
        (void)ui.InputTextWithHint("##HierarchySearch", "Search entities", m_Search);
        ui.Separator();
        if (!scene)
        {
            ui.Text("No scene entities.");
            return;
        }
        if (ui.Shortcut({Keire::UiKey::Delete}))
            m_Controller.DeleteHierarchySelection();
        if (ui.Shortcut({Keire::UiKey::F2}) && document.Selection())
        {
            const auto selected = scene->Find(document.Selection()).Snapshot();
            if (selected)
            {
                m_Controller.RecordHierarchyUndo();
                m_Controller.RequestHierarchyRename(selected->Id, selected->Name);
            }
        }
        const auto hierarchy = scene->Snapshot();
        const auto& objects = hierarchy.Objects;
        const auto prefabInstanceFor = [&](const Keire::AssetId object)
        {
            return std::ranges::find_if(hierarchy.PrefabInstances,
                                        [&](const Keire::PrefabInstanceDefinition& instance)
                                        {
                                            return std::ranges::any_of(instance.Objects,
                                                                       [&](const Keire::PrefabObjectMapping& mapping)
                                                                       { return mapping.Instance == object; });
                                        });
        };
        std::vector<Keire::AssetId> hierarchyOrder;
        hierarchyOrder.reserve(objects.size());
        for (const auto& object : objects)
            hierarchyOrder.push_back(object.Id);
        const auto select = [&](const Keire::AssetId entity, const std::span<const Keire::AssetId> order)
        {
            if (!ui.ShiftDown() || !m_SelectionAnchor)
            {
                document.Select(entity, ui.ControlDown());
                m_SelectionAnchor = entity;
                return;
            }
            const auto range =
                BuildRangeSelection(order, m_SelectionAnchor, entity, document.Selections(), ui.ControlDown());
            document.SetSelections(range);
        };
        ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F},
                       scene->Name() + "  •  " + std::to_string(objects.size()) + " entities");
        if (!m_Search.empty())
        {
            std::string search = m_Search;
            std::ranges::transform(search, search.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            std::vector<const Keire::SceneObjectDefinition*> matches;
            for (const auto& object : objects)
            {
                std::string name = object.Name;
                std::ranges::transform(name, name.begin(), [](const unsigned char character)
                                       { return static_cast<char>(std::tolower(character)); });
                if (name.find(search) == std::string::npos)
                    continue;
                matches.push_back(&object);
            }
            std::vector<Keire::AssetId> matchOrder;
            matchOrder.reserve(matches.size());
            for (const auto* object : matches)
                matchOrder.push_back(object->Id);
            for (const auto* object : matches)
            {
                auto id = ui.PushId(object->Id.ToString());
                const bool prefabObject = prefabInstanceFor(object->Id) != hierarchy.PrefabInstances.end();
                const auto label = prefabObject ? "[Prefab] " + object->Name : object->Name;
                if (ui.Selectable(label, document.IsSelected(object->Id)))
                    select(object->Id, matchOrder);
            }
            return;
        }
        const auto nextSibling = [&objects](const Keire::AssetId id, const Keire::AssetId parent)
        {
            bool found = false;
            for (const auto& candidate : objects)
            {
                if (candidate.Id == id)
                {
                    found = true;
                    continue;
                }
                if (found && candidate.Parent == parent)
                    return candidate.Id;
            }
            return Keire::AssetId{};
        };
        const auto moveEntity =
            [&](const Keire::AssetId entity, const Keire::AssetId parent, const Keire::AssetId beforeSibling)
        {
            if (!entity || entity == parent || entity == beforeSibling)
                return;
            try
            {
                m_Controller.RecordHierarchyUndo();
                document.MoveEntity(Keire::EntityId(entity), Keire::EntityId(parent), Keire::EntityId(beforeSibling),
                                    true);
                m_Controller.MarkHierarchyEntity(entity);
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportHierarchyError(error.what());
            }
        };
        const auto drawEntity = [&](const auto& self, const Keire::SceneObjectDefinition& object) -> void
        {
            auto id = ui.PushId(object.Id.ToString());
            const auto prefabInstance = prefabInstanceFor(object.Id);
            const bool prefabObject = prefabInstance != hierarchy.PrefabInstances.end();
            const auto label = (object.Active ? std::string{} : std::string("[inactive] ")) +
                               (prefabObject ? std::string("[Prefab] ") : std::string{}) + object.Name + "##tree";
            auto node = ui.BeginTreeNode(label, document.IsSelected(object.Id));
            const auto state = ui.LastItemState();
            const auto row = ui.LastItemRect();
            if (state.Hovered && ui.PointerState().LeftPressed)
                select(object.Id, hierarchyOrder);
            if (auto context = ui.BeginItemContextMenu(); context)
            {
                if (!document.IsSelected(object.Id))
                    document.Select(object.Id);
                if (ui.MenuItem("Create Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document.CreateEntity("GameObject", Keire::EntityId(object.Id)).Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Directional Light Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Directional Light", Keire::EntityId(object.Id),
                                                      Keire::DirectionalLightComponent::StaticType())
                                        .Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Point Light Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Point Light", Keire::EntityId(object.Id),
                                                      Keire::PointLightComponent::StaticType())
                                        .Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Spot Light Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Spot Light", Keire::EntityId(object.Id),
                                                      Keire::SpotLightComponent::StaticType())
                                        .Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                ui.Separator();
                if (ui.MenuItem("Duplicate"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document.DuplicateEntity(Keire::EntityId(object.Id)).Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Rename"))
                {
                    m_Controller.RecordHierarchyUndo();
                    m_Controller.RequestHierarchyRename(object.Id, object.Name);
                }
                if (prefabObject)
                {
                    ui.Separator();
                    if (ui.MenuItem("Unpack Prefab"))
                        m_Controller.UnpackHierarchyPrefab(object.Id, false);
                    if (ui.MenuItem("Unpack Prefab Completely"))
                        m_Controller.UnpackHierarchyPrefab(object.Id, true);
                }
                if (ui.MenuItem("Delete"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.DeleteEntity(Keire::EntityId(object.Id));
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
                const auto pointer = ui.PointerState().Position;
                const float rowHeight = std::max(row.Maximum.Y - row.Minimum.Y, 1.0F);
                const float relative = (pointer.Y - row.Minimum.Y) / rowHeight;
                const bool insertBefore = relative < 0.25F;
                const bool insertAfter = relative > 0.75F;
                const auto accent = m_Controller.HierarchyAccent();
                if (insertBefore || insertAfter)
                {
                    const float y = insertBefore ? row.Minimum.Y : row.Maximum.Y;
                    ui.DrawLine({row.Minimum.X, y}, {row.Maximum.X, y}, accent, 2.0F);
                }
                else
                    ui.DrawRectangle(row, accent, 2.0F, 3.0F);
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                {
                    const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto child = Keire::AssetId::Parse(value);
                    if (insertBefore)
                        moveEntity(child, object.Parent, object.Id);
                    else if (insertAfter)
                        moveEntity(child, object.Parent, nextSibling(object.Id, object.Parent));
                    else
                        moveEntity(child, object.Id, {});
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
        const auto remaining = ui.ContentAvailable();
        (void)ui.InvisibleButton("HierarchyRootDrop",
                                 {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
        if (auto target = ui.BeginDragTarget(); target)
        {
            const auto area = ui.LastItemRect();
            ui.DrawRectangle(area, m_Controller.HierarchyAccent(), 2.0F, 3.0F);
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
            {
                const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                moveEntity(Keire::AssetId::Parse(value), {}, {});
            }
        }
        if (auto context = ui.BeginWindowContextMenu("HierarchyBlank"); context)
        {
            if (ui.MenuItem("Create Empty"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(document.CreateEntity().Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Directional Light"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Directional Light", {}, Keire::DirectionalLightComponent::StaticType())
                        .Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Point Light"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Point Light", {}, Keire::PointLightComponent::StaticType()).Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Spot Light"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Spot Light", {}, Keire::SpotLightComponent::StaticType()).Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
        }
    }
} // namespace KeireEditor
