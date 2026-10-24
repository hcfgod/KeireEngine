#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> ReadPrefabBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw std::runtime_error("Could not open prefab source: " + path.string());
        const auto size = stream.tellg();
        if (size < 0 || size > static_cast<std::streamoff>(64U * 1024U * 1024U))
            throw std::runtime_error("Prefab source size is invalid.");
        std::vector<std::byte> result(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()), size))
            throw std::runtime_error("Could not read prefab source: " + path.string());
        return result;
    }
} // namespace

void EditorWorkspaceLayer::DrawPrefabOverrides(Keire::UiFrame& ui)
{
    if (!m_PrefabOverrides.Visible())
        return;
    ui.SetNextWindowSize({560.0F, 320.0F});
    if (auto panel = ui.BeginPanel(m_PrefabOverrides); panel)
    {
        const bool compactActions = ui.ContentAvailable().Width < 520.0F;
        if (m_PrefabEditingStage)
        {
            ui.Text("Prefab Mode");
            ui.TextWrapped(m_PrefabEditingStage->RelativePath.generic_string());
            ui.TextColoredWrapped(
                m_Theme.MutedText,
                "This isolated stage does not modify the open scene. Save publishes the prefab source; "
                "Discard restores the previous scene document.");
            if (ui.Button("Save Prefab"))
            {
                try
                {
                    SavePrefabEditingStage();
                }
                catch (const std::exception& error)
                {
                    ReportError("Prefab", error.what());
                }
            }
            if (!compactActions)
                ui.SameLine();
            if (ui.Button("Save and Close"))
            {
                try
                {
                    SavePrefabEditingStage();
                    ClosePrefabEditingStage();
                }
                catch (const std::exception& error)
                {
                    ReportError("Prefab", error.what());
                }
            }
            if (!compactActions)
                ui.SameLine();
            if (ui.Button("Discard and Close"))
                ClosePrefabEditingStage();
            ui.Separator();
            ui.TextWrapped("Edit prefab objects in the Scene viewport and Inspector.");
            return;
        }

        const auto scene = m_SceneDocument ? m_SceneDocument->EditingScene() : Keire::Ref<Keire::Scene>{};
        if (!scene || !m_SceneDocument->Selection())
        {
            DrawEmptyState(ui, "No prefab instance selected", "Select an object in a prefab instance.",
                           "Instance mappings and overrides will appear here.");
            return;
        }

        auto definition = scene->Snapshot();
        const auto selection = m_SceneDocument->Selection();
        const auto instance = std::ranges::find_if(
            definition.PrefabInstances,
            [selection](const Keire::PrefabInstanceDefinition& candidate)
            {
                return candidate.Root == selection ||
                       std::ranges::any_of(candidate.Objects, [selection](const Keire::PrefabObjectMapping& mapping)
                                           { return mapping.Instance == selection; });
            });
        if (instance == definition.PrefabInstances.end())
        {
            DrawEmptyState(ui, "Selection is not a prefab instance", "Select an inherited prefab object.",
                           "Regular scene objects do not have prefab overrides.");
            return;
        }

        if (!m_AssetDatabase || !Owner().GetProject())
        {
            ui.TextWrapped("The prefab asset database is unavailable. Overrides cannot be reviewed.");
            return;
        }
        const auto record = m_AssetDatabase->Find(instance->Prefab);
        ui.TextWrapped("Prefab: " + (record ? record->RelativePath.generic_string() : instance->Prefab.ToString()));
        const auto rootObject =
            std::ranges::find(definition.Objects, instance->Root, &Keire::SceneObjectDefinition::Id);
        ui.TextWrapped("Instance root: " +
                       (rootObject != definition.Objects.end() ? rootObject->Name : instance->Root.ToString()));
        ui.Text(std::to_string(instance->Objects.size()) + " inherited objects");
        ui.Separator();
        std::vector<KeireEditor::PrefabOverrideReviewEntry> review;
        bool reviewAvailable = false;
        try
        {
            const auto projectRoot = Owner().GetProject()->Root();
            const auto composed =
                Keire::ComposePrefab(instance->Prefab,
                                     [&](const Keire::AssetId prefab)
                                     {
                                         const auto dependency = m_AssetDatabase->Find(prefab);
                                         if (!dependency || dependency->Type != Keire::PrefabAsset::StaticType())
                                         {
                                             return Keire::Ref<Keire::PrefabAsset>{};
                                         }
                                         const auto path = projectRoot / "Assets" / dependency->RelativePath;
                                         const auto revision =
                                             dependency->SourceDigest + ":" + dependency->MetadataDigest;
                                         auto& cached = m_PrefabReviewSources[path.generic_string()];
                                         if (!cached.Asset || cached.Revision != revision)
                                         {
                                             cached.Asset = Keire::PrefabAsset::Decode(ReadPrefabBytes(path));
                                             cached.Revision = revision;
                                         }
                                         return cached.Asset;
                                     });
            review = KeireEditor::ReviewPrefabInstance(definition, instance->Root, composed);
            reviewAvailable = true;
        }
        catch (const std::exception& error)
        {
            ui.TextWrapped("Could not review prefab overrides: " + std::string(error.what()));
        }
        if (reviewAvailable)
        {
            ui.Text(std::to_string(review.size()) + " changed fields (excluding scene-owned root placement)");
        }
        for (std::size_t index = 0; index < review.size(); ++index)
        {
            const auto& change = review[index];
            auto id = ui.PushId(std::to_string(index));
            std::string label = change.ObjectName;
            if (change.Component)
            {
                const auto registration = scene->Components()->Find(change.Component);
                label += " / " + (registration ? registration->Name : change.Component.ToString());
            }
            label += " / " + change.Property;
            ui.TextWrapped(label);
            ui.TextWrapped("Before: " + change.Before);
            ui.TextWrapped("After: " + change.After);
            ui.Separator();
        }

        ui.Separator();
        const auto instanceRoot = instance->Root;
        if (auto applyDisabled = ui.BeginDisabled(!reviewAvailable); applyDisabled)
        {
            if (ui.Button("Apply to Prefab"))
            {
                try
                {
                    m_SceneDocument->Select(instanceRoot);
                    ApplySelectedPrefabOverrides();
                    m_PrefabReviewSources.clear();
                }
                catch (const std::exception& error)
                {
                    ReportError("Prefab", error.what());
                }
            }
        }
        if (!compactActions)
            ui.SameLine();
        if (ui.Button("Revert All Overrides"))
        {
            try
            {
                RecordSceneUndo("Revert Prefab Overrides");
                auto replacement = scene->Snapshot();
                const auto found = std::ranges::find(replacement.PrefabInstances, instanceRoot,
                                                     &Keire::PrefabInstanceDefinition::Root);
                if (found == replacement.PrefabInstances.end())
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                if (!m_AssetDatabase || !Owner().GetProject())
                    throw std::runtime_error("The prefab asset database is unavailable.");
                const auto projectRoot = Owner().GetProject()->Root();
                const auto composed =
                    Keire::ComposePrefab(found->Prefab,
                                         [&](const Keire::AssetId prefab)
                                         {
                                             const auto record = m_AssetDatabase->Find(prefab);
                                             if (!record || record->Type != Keire::PrefabAsset::StaticType())
                                                 return Keire::Ref<Keire::PrefabAsset>{};
                                             return Keire::PrefabAsset::Decode(
                                                 ReadPrefabBytes(projectRoot / "Assets" / record->RelativePath));
                                         });
                if (!KeireEditor::RevertPrefabInstance(replacement, instanceRoot, composed))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance overrides reverted.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        if (!compactActions)
            ui.SameLine();
        if (ui.Button("Unpack"))
        {
            try
            {
                RecordSceneUndo("Unpack Prefab");
                auto replacement = scene->Snapshot();
                if (!KeireEditor::UnpackPrefab(replacement, instanceRoot))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance unpacked.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        if (!compactActions)
            ui.SameLine();
        if (ui.Button("Unpack Completely"))
        {
            try
            {
                RecordSceneUndo("Unpack Prefab Completely");
                auto replacement = scene->Snapshot();
                if (!KeireEditor::UnpackPrefab(replacement, instanceRoot, true))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance hierarchy unpacked.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        ui.TextColoredWrapped(
            m_Theme.MutedText,
            "Root placement is scene-owned. Nested roots require a variant so source ownership is preserved.");
    }
}
