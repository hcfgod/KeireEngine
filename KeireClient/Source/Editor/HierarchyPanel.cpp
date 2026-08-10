#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SelectionRange.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"

namespace KeireEditor
{
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

        [[nodiscard]] std::string EncodeAssetPayload(const std::span<const Keire::AssetId> assets)
        {
            std::ostringstream stream;
            for (const auto asset : assets)
                stream << asset.ToString() << '\n';
            return stream.str();
        }
    } // namespace

    void HierarchyPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;

        auto& document = m_Controller.HierarchyDocument();
        const auto scene = m_Controller.ActiveHierarchyScene();
        const auto createText = [&](const Keire::EntityId parent, std::string name, std::string text)
        {
            const auto entity =
                document.CreateEntity(std::move(name), parent, Keire::RectTransformComponent::StaticType());
            (void)document.AddComponent(entity, Keire::UiTextComponent::StaticType());
            document.SetComponentProperty(entity, Keire::UiTextComponent::StaticType(), "text", std::move(text));
            return entity;
        };
        const auto createButton = [&](const Keire::EntityId parent, std::string name)
        {
            const auto button =
                document.CreateEntity(std::move(name), parent, Keire::RectTransformComponent::StaticType());
            (void)document.AddComponent(button, Keire::UiImageComponent::StaticType());
            (void)document.AddComponent(button, Keire::UiButtonComponent::StaticType());
            const auto label = createText(button, "Label", "CONTINUE");
            document.SetComponentProperty(label, Keire::RectTransformComponent::StaticType(), "anchorMinimum",
                                          Keire::Vector2{0.0F, 0.0F});
            document.SetComponentProperty(label, Keire::RectTransformComponent::StaticType(), "anchorMaximum",
                                          Keire::Vector2{1.0F, 1.0F});
            document.SetComponentProperty(label, Keire::RectTransformComponent::StaticType(), "sizeDelta",
                                          Keire::Vector2{});
            return button;
        };
        const auto createModernCanvas = [&](const Keire::EntityId parent)
        {
            const auto canvas = document.CreateEntity("Canvas", parent, Keire::CanvasComponent::StaticType());
            (void)document.AddComponent(canvas, Keire::RectTransformComponent::StaticType());
            document.SetComponentProperty(canvas, Keire::RectTransformComponent::StaticType(), "anchorMinimum",
                                          Keire::Vector2{0.0F, 0.0F});
            document.SetComponentProperty(canvas, Keire::RectTransformComponent::StaticType(), "anchorMaximum",
                                          Keire::Vector2{1.0F, 1.0F});
            document.SetComponentProperty(canvas, Keire::RectTransformComponent::StaticType(), "sizeDelta",
                                          Keire::Vector2{});

            const auto modernPanel =
                document.CreateEntity("Modern Panel", canvas, Keire::RectTransformComponent::StaticType());
            (void)document.AddComponent(modernPanel, Keire::UiImageComponent::StaticType());
            (void)document.AddComponent(modernPanel, Keire::UiLayoutComponent::StaticType());
            document.SetComponentProperty(modernPanel, Keire::RectTransformComponent::StaticType(), "sizeDelta",
                                          Keire::Vector2{560.0F, 360.0F});
            document.SetComponentProperty(modernPanel, Keire::UiImageComponent::StaticType(), "tint",
                                          Keire::Color{0.025F, 0.045F, 0.075F, 0.96F});
            document.SetComponentProperty(modernPanel, Keire::UiLayoutComponent::StaticType(), "spacing", 18.0);
            document.SetComponentProperty(modernPanel, Keire::UiLayoutComponent::StaticType(), "padding",
                                          Keire::Vector4{36.0F, 36.0F, 36.0F, 36.0F});

            const auto title = createText(modernPanel, "Title", "MISSION CONTROL");
            document.SetComponentProperty(title, Keire::UiTextComponent::StaticType(), "fontSize", 34.0);
            document.SetComponentProperty(title, Keire::UiTextComponent::StaticType(), "color",
                                          Keire::Color{0.86F, 0.94F, 1.0F, 1.0F});
            const auto subtitle = createText(modernPanel, "Subtitle", "SYSTEMS READY");
            document.SetComponentProperty(subtitle, Keire::UiTextComponent::StaticType(), "fontSize", 16.0);
            document.SetComponentProperty(subtitle, Keire::UiTextComponent::StaticType(), "color",
                                          Keire::Color{0.33F, 0.78F, 0.95F, 1.0F});
            (void)createButton(modernPanel, "Primary Action");
            return canvas;
        };
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
        const auto hierarchy = scene->HierarchySnapshot();
        const auto& objects = hierarchy.Objects;
        std::unordered_set<Keire::AssetId> prefabObjects;
        for (const auto& instance : hierarchy.PrefabInstances)
            for (const auto& mapping : instance.Objects)
                prefabObjects.insert(mapping.Instance);
        std::unordered_map<Keire::AssetId, std::vector<const Keire::SceneObjectDefinition*>> children;
        children.reserve(objects.size());
        for (const auto& object : objects)
            children[object.Parent].push_back(std::addressof(object));
        const auto isPrefabObject = [&](const Keire::AssetId object) { return prefabObjects.contains(object); };
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
        ui.TextColored({0.42F, 0.47F, 0.55F, 1.0F},
                       "Drop on a row to parent  |  drop on an edge to reorder  |  Ctrl/Shift multi-select");
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
                const bool prefabObject = isPrefabObject(object->Id);
                const auto label = prefabObject ? "[Prefab] " + object->Name : object->Name;
                if (ui.Selectable(label, document.IsSelected(object->Id)))
                    select(object->Id, matchOrder);
            }
            return;
        }
        const auto nextSibling = [&children](const Keire::AssetId id, const Keire::AssetId parent)
        {
            bool found = false;
            const auto siblings = children.find(parent);
            if (siblings == children.end())
                return Keire::AssetId{};
            for (const auto* candidate : siblings->second)
            {
                if (candidate->Id == id)
                {
                    found = true;
                    continue;
                }
                if (found)
                    return candidate->Id;
            }
            return Keire::AssetId{};
        };
        const auto moveEntities = [&](const std::span<const Keire::AssetId> entities, const Keire::AssetId parent,
                                      const Keire::AssetId beforeSibling)
        {
            if (entities.empty())
                return;
            try
            {
                std::vector<Keire::EntityId> requested;
                requested.reserve(entities.size());
                for (const auto entity : entities)
                    requested.emplace_back(entity);
                m_Controller.RecordHierarchyUndo();
                const auto moved =
                    document.MoveEntities(requested, Keire::EntityId(parent), Keire::EntityId(beforeSibling), true);
                for (const auto entity : moved)
                    m_Controller.MarkHierarchyEntity(entity.Value());
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportHierarchyError(error.what());
            }
        };
        const auto drawEntity = [&](const auto& self, const Keire::SceneObjectDefinition& object) -> void
        {
            auto id = ui.PushId(object.Id.ToString());
            const bool prefabObject = isPrefabObject(object.Id);
            const auto label = (object.Active ? std::string{} : std::string("[inactive] ")) +
                               (prefabObject ? std::string("[Prefab] ") : std::string{}) + object.Name + "##tree";
            auto node = ui.BeginTreeNode(label, document.IsSelected(object.Id));
            const auto state = ui.LastItemState();
            const auto row = ui.LastItemRect();
            if (state.Hovered && ui.PointerState().LeftPressed)
            {
                const bool preserveForPotentialDrag = document.IsSelected(object.Id) &&
                                                      document.Selections().size() > 1 && !ui.ControlDown() &&
                                                      !ui.ShiftDown();
                if (preserveForPotentialDrag)
                    m_PendingSelectionCollapse = object.Id;
                else
                {
                    m_PendingSelectionCollapse = {};
                    select(object.Id, hierarchyOrder);
                }
            }
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
                if (ui.MenuItem("UI Text Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(createText(Keire::EntityId(object.Id), "Text", "Text").Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("UI Button Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(createButton(Keire::EntityId(object.Id), "Button").Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Audio Source Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Audio Source", Keire::EntityId(object.Id),
                                                      Keire::AudioSourceComponent::StaticType())
                                        .Value());
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
                if (ui.MenuItem("Reflection Probe Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Reflection Probe", Keire::EntityId(object.Id),
                                                      Keire::ReflectionProbeComponent::StaticType())
                                        .Value());
                    m_Controller.MarkHierarchyEntity(document.Selection());
                }
                if (ui.MenuItem("Light Probe Volume Child"))
                {
                    m_Controller.RecordHierarchyUndo();
                    document.Select(document
                                        .CreateEntity("Light Probe Volume", Keire::EntityId(object.Id),
                                                      Keire::LightProbeVolumeComponent::StaticType())
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
                m_PendingSelectionCollapse = {};
                std::vector<Keire::AssetId> dragged;
                const auto selections = document.Selections();
                for (const auto entity : hierarchyOrder)
                    if (std::ranges::find(selections, entity) != selections.end())
                        dragged.push_back(entity);
                if (dragged.empty())
                    dragged.push_back(object.Id);
                const auto value = EncodeAssetPayload(dragged);
                ui.SetDragPayload("KEIRE_SCENE_OBJECT", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(dragged.size() == 1 ? object.Name : std::to_string(dragged.size()) + " selected entities");
                const auto previewCount = std::min<std::size_t>(dragged.size(), 4);
                for (std::size_t index = 0; index < previewCount; ++index)
                    if (const auto preview = scene->Find(dragged[index]).Snapshot(); preview)
                        ui.TextColored({0.62F, 0.67F, 0.75F, 1.0F}, preview->Name);
                if (dragged.size() > previewCount)
                    ui.TextColored({0.62F, 0.67F, 0.75F, 1.0F},
                                   "+ " + std::to_string(dragged.size() - previewCount) + " more");
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
                    ui.SetTooltip((insertBefore ? "Insert before " : "Insert after ") + object.Name);
                }
                else
                {
                    ui.DrawRectangle(row, accent, 2.0F, 3.0F);
                    ui.SetTooltip("Make child of " + object.Name);
                }
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                {
                    const auto dragged = DecodeAssetPayload(payload);
                    if (insertBefore)
                        moveEntities(dragged, object.Parent, object.Id);
                    else if (insertAfter)
                        moveEntities(dragged, object.Parent, nextSibling(object.Id, object.Parent));
                    else
                        moveEntities(dragged, object.Id, {});
                }
                payload.clear();
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        for (const auto script : DecodeAssetPayload(payload))
                            m_Controller.AddScriptToEntity(Keire::EntityId(object.Id), script);
                    }
                    catch (const std::exception& error)
                    {
                        m_Controller.ReportHierarchyError(error.what());
                    }
                }
            }
            if (m_PendingSelectionCollapse == object.Id && ui.PointerState().LeftReleased)
            {
                if (state.Hovered)
                {
                    document.Select(object.Id);
                    m_SelectionAnchor = object.Id;
                }
                m_PendingSelectionCollapse = {};
            }
            if (node)
            {
                const auto found = children.find(object.Id);
                if (found != children.end())
                    for (const auto* child : found->second)
                        self(self, *child);
            }
        };
        if (const auto roots = children.find({}); roots != children.end())
            for (const auto* object : roots->second)
                drawEntity(drawEntity, *object);
        const auto remaining = ui.ContentAvailable();
        (void)ui.InvisibleButton("HierarchyRootDrop",
                                 {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
        if (auto target = ui.BeginDragTarget(); target)
        {
            const auto area = ui.LastItemRect();
            ui.DrawRectangle(area, m_Controller.HierarchyAccent(), 2.0F, 3.0F);
            ui.SetTooltip("Move selection to the Scene root");
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                moveEntities(DecodeAssetPayload(payload), {}, {});
        }
        if (ui.PointerState().LeftReleased)
            m_PendingSelectionCollapse = {};
        if (auto context = ui.BeginItemContextMenu("HierarchyBlank"); context)
        {
            if (ui.MenuItem("Create Empty"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(document.CreateEntity().Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("UI / Modern Canvas"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(createModernCanvas({}).Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("UI / Text"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(createText({}, "Text", "Text").Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("UI / Button"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(createButton({}, "Button").Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Audio / Source"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Audio Source", {}, Keire::AudioSourceComponent::StaticType()).Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Audio / Listener"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Audio Listener", {}, Keire::AudioListenerComponent::StaticType()).Value());
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
            if (ui.MenuItem("Reflection Probe"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Reflection Probe", {}, Keire::ReflectionProbeComponent::StaticType())
                        .Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
            if (ui.MenuItem("Light Probe Volume"))
            {
                m_Controller.RecordHierarchyUndo();
                document.Select(
                    document.CreateEntity("Light Probe Volume", {}, Keire::LightProbeVolumeComponent::StaticType())
                        .Value());
                m_Controller.MarkHierarchyEntity(document.Selection());
            }
        }
    }
} // namespace KeireEditor
