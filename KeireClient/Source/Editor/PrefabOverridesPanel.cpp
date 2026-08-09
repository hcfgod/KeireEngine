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
    [[nodiscard]] std::string PrefabOverrideName(const Keire::PrefabOverrideKind kind)
    {
        switch (kind)
        {
        case Keire::PrefabOverrideKind::RenameObject:
            return "Rename object";
        case Keire::PrefabOverrideKind::SetObjectActive:
            return "Set active";
        case Keire::PrefabOverrideKind::SetObjectTransform:
            return "Set transform";
        case Keire::PrefabOverrideKind::SetObjectLayer:
            return "Set layer";
        case Keire::PrefabOverrideKind::SetComponentProperty:
            return "Set component property";
        case Keire::PrefabOverrideKind::AddComponent:
            return "Add component";
        case Keire::PrefabOverrideKind::RemoveComponent:
            return "Remove component";
        case Keire::PrefabOverrideKind::AddObject:
            return "Add object";
        case Keire::PrefabOverrideKind::RemoveObject:
            return "Remove object";
        }
        return "Unknown override";
    }

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
    if (auto panel = ui.BeginPanel(m_PrefabOverrides); panel)
    {
        if (m_PrefabEditingStage)
        {
            ui.Text("Prefab Mode");
            ui.Text(m_PrefabEditingStage->RelativePath.generic_string());
            ui.TextColored(m_Theme.MutedText,
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
            ui.SameLine();
            if (ui.Button("Discard and Close"))
                ClosePrefabEditingStage();
            ui.Separator();
            ui.Text("Edit prefab objects in the Scene viewport and Inspector.");
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

        ui.Text("Prefab: " + instance->Prefab.ToString());
        ui.Text("Instance root: " + instance->Root.ToString());
        ui.Text(std::to_string(instance->Objects.size()) + " inherited objects");
        ui.Separator();
        ui.Text(std::to_string(instance->Overrides.size()) + " overrides");
        for (std::size_t index = 0; index < instance->Overrides.size(); ++index)
        {
            const auto& overrideValue = instance->Overrides[index];
            auto id = ui.PushId(std::to_string(index));
            std::string label = PrefabOverrideName(overrideValue.Kind);
            if (!overrideValue.Property.empty())
                label += "  " + overrideValue.Property;
            auto node = ui.BeginTreeNode(label);
            if (node)
            {
                ui.Text("Object: " + overrideValue.Object.ToString());
                if (overrideValue.Component)
                    ui.Text("Component: " + overrideValue.Component.ToString());
            }
        }

        ui.Separator();
        const auto instanceRoot = instance->Root;
        if (ui.Button("Apply to Prefab"))
        {
            try
            {
                ApplySelectedPrefabOverrides();
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
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
        ui.TextColored(
            m_Theme.MutedText,
            "Root placement is scene-owned. Nested roots require a variant so source ownership is preserved.");
    }
}
