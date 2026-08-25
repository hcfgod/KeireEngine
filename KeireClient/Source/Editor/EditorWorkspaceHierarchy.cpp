#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

Keire::Ref<Keire::Scene> EditorWorkspaceLayer::ActiveHierarchyScene() const noexcept { return ActiveScene(); }

KeireEditor::SceneDocument& EditorWorkspaceLayer::HierarchyDocument() noexcept { return *m_SceneDocument; }

void EditorWorkspaceLayer::ReportHierarchyError(std::string message) noexcept
{
    ReportError("Hierarchy", std::move(message));
}

void EditorWorkspaceLayer::UnpackHierarchyPrefab(const Keire::AssetId entity, const bool completely)
{
    if (m_SceneDocument->PlaySession())
        throw std::runtime_error("Exit Play mode before unpacking a prefab.");
    const auto scene = m_SceneDocument->EditingScene();
    if (!scene)
        throw std::runtime_error("Open a scene before unpacking a prefab.");
    auto replacement = scene->Snapshot();
    const auto instance = std::ranges::find_if(replacement.PrefabInstances,
                                               [&](const Keire::PrefabInstanceDefinition& candidate)
                                               {
                                                   return std::ranges::any_of(
                                                       candidate.Objects, [&](const Keire::PrefabObjectMapping& mapping)
                                                       { return mapping.Instance == entity; });
                                               });
    if (instance == replacement.PrefabInstances.end())
        throw std::invalid_argument("The selected GameObject is not part of a prefab instance.");
    const auto root = instance->Root;
    RecordSceneUndo(completely ? "Unpack Prefab Completely" : "Unpack Prefab");
    if (!KeireEditor::UnpackPrefab(replacement, root, completely))
        throw std::runtime_error("The prefab instance changed before it could be unpacked.");
    auto rebuilt = Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
    rebuilt->MarkDirty();
    m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
    m_SceneDocument->Select(root);
    m_SceneDocument->SetStatus(completely ? "Prefab instance hierarchy unpacked." : "Prefab instance unpacked.");
}

Keire::UiColor EditorWorkspaceLayer::HierarchyAccent() const noexcept { return m_Theme.Accent; }

void EditorWorkspaceLayer::ActivateHierarchyHistory() noexcept { m_ActiveUndoContext = m_SceneDocument->History(); }

void EditorWorkspaceLayer::DeleteHierarchySelection()
{
    (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::DeleteSelection);
}

void EditorWorkspaceLayer::RecordHierarchyUndo() { RecordSceneUndo(); }

void EditorWorkspaceLayer::MarkHierarchyEntity(const Keire::AssetId entity) { MarkPlayEditorEntity(entity); }

void EditorWorkspaceLayer::RequestHierarchyRename(const Keire::AssetId entity, std::string name)
{
    m_SceneDocument->Select(entity);
    m_ProfileName = std::move(name);
    OpenDialog(Dialog::RenameEntity);
}
