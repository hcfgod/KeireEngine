#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialSelectionEditing.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <vector>

namespace
{
    void PublishMaterialPreview(const Keire::Ref<Keire::AssetSystem>& assets,
                                const Keire::Ref<Keire::AssetDatabase>& database, Keire::AssetId asset,
                                const Keire::MaterialAssetDefinition& definition)
    {
        if (!assets || !database)
            return;
        const auto record = database->Find(asset);
        if (!record)
            return;
        if (record->Type != Keire::MaterialAsset::StaticType())
        {
            const auto generated =
                std::ranges::find_if(record->SubAssets, [&](Keire::AssetId id)
                                     { return assets->TryGetType(id) == Keire::MaterialAsset::StaticType(); });
            if (generated == record->SubAssets.end())
                return;
            asset = *generated;
        }
        (void)assets->PublishDevelopmentAsset(asset, Keire::CreateRef<Keire::MaterialAsset>(definition));
    }
} // namespace

std::span<const Keire::AssetId> EditorWorkspaceLayer::InspectorSelectedAssets() const noexcept
{
    if (!m_AssetBrowserPanel)
        return {};
    const auto selected = m_AssetBrowserPanel->SelectedAssets();
    return !selected.empty() && selected.back() == m_SelectedAsset ? selected : std::span<const Keire::AssetId>{};
}

void EditorWorkspaceLayer::CommitInspectorMaterialSelection(const std::span<const KeireEditor::MaterialDocument> before,
                                                            const std::span<const KeireEditor::MaterialDocument> after)
{
    if (!m_AssetDatabase || !m_AssetOperations || !m_AssetBrowserPanel)
        throw std::runtime_error("Material selection persistence services are unavailable.");
    if (m_AssetOperations->Busy())
        throw std::runtime_error("Wait for the current asset import before editing the material selection.");
    const auto root = m_AssetDatabase->Specification().ProjectRoot;
    const auto snapshot = [&](const std::span<const KeireEditor::MaterialDocument> documents, const bool original)
    {
        std::vector<KeireEditor::MaterialSourceSnapshot> result;
        for (const auto& document : documents)
        {
            const auto source =
                original ? std::vector<std::byte>(document.BaselineSource().begin(), document.BaselineSource().end())
                         : document.SaveSource();
            result.push_back({document.Asset(), document.SourcePath().lexically_relative(root), source});
        }
        return result;
    };
    const auto oldSources = snapshot(before, true);
    const auto newSources = snapshot(after, false);
    auto pending = m_PendingMaterialSelectionImports;
    for (const auto& source : newSources)
        if (std::ranges::find(pending, source.Asset) == pending.end())
            pending.push_back(source.Asset);
    const auto apply = [this, root](const std::span<const KeireEditor::MaterialSourceSnapshot> old,
                                    const std::span<const KeireEditor::MaterialSourceSnapshot> replacement)
    {
        if (!m_AssetOperations || m_AssetOperations->Busy())
            throw std::runtime_error("Wait for the current asset import before applying material history.");
        KeireEditor::PublishMaterialSelection(root, old, replacement);
        // Persistence is committed. A refresh failure must not make the caller retry an already-published edit.
        try
        {
            std::vector<Keire::AssetId> ids;
            for (const auto& source : replacement)
                ids.push_back(source.Asset);
            m_AssetOperations->QueueAssetImport(std::move(ids), KeireEditor::AssetOperationPriority::MaterialRefresh,
                                                {.Reason = "material-selection-edit"});
            for (const auto& source : replacement)
            {
                if (m_MaterialDocument->Asset() == source.Asset)
                    m_MaterialDocument->AcceptSavedSource(source.Source);
                const auto definition = KeireEditor::MaterialDocument::ResolveRuntimeRevision(
                    source.Source,
                    [this](const Keire::MaterialShaderReference& shader)
                        -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
                    {
                        const auto runtime = ResolveMaterialGraphShader(shader);
                        const auto assets = Owner().Assets();
                        if (!runtime || !assets)
                            return std::nullopt;
                        const auto loaded =
                            assets->Load<Keire::ShaderAsset>(runtime, Keire::AssetPriority::High).TryGetLoaded();
                        if (!loaded)
                            return std::nullopt;
                        return KeireEditor::MaterialDocument::ResolvedShader{runtime, loaded->Definition()};
                    });
                if (definition)
                    PublishMaterialPreview(Owner().Assets(), m_AssetDatabase, source.Asset, *definition);
            }
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Materials saved; reimport is required: ") + error.what());
        }
    };
    const auto available = [this, root, oldSources]
    {
        return m_AssetDatabase && m_AssetDatabase->Specification().ProjectRoot == root &&
               std::ranges::all_of(oldSources, [&](const auto& source)
                                   { return std::filesystem::is_regular_file(root / source.RelativePath); });
    };
    auto command = KeireEditor::CreateMaterialSelectionEdit(oldSources, newSources, m_InspectorPanel->EditSerial(),
                                                            apply, available);
    KeireEditor::PublishMaterialSelection(root, oldSources, newSources);
    try
    {
        const auto undo = m_AssetBrowserPanel->UndoContext();
        if (undo && undo->IsOpen())
        {
            undo->RecordApplied(std::move(command));
            m_ActiveUndoContext = undo;
        }
    }
    catch (...)
    {
        const auto failure = std::current_exception();
        try
        {
            KeireEditor::PublishMaterialSelection(root, newSources, oldSources);
        }
        catch (...)
        {
            SetAssetError("Material selection undo registration and source rollback both failed.");
        }
        std::rethrow_exception(failure);
    }
    m_PendingMaterialSelectionImports.swap(pending);
    try
    {
        for (const auto& source : newSources)
        {
            if (m_MaterialDocument->Asset() == source.Asset)
                m_MaterialDocument->AcceptSavedSource(source.Source);
        }
        for (const auto& document : after)
            PublishMaterialPreview(Owner().Assets(), m_AssetDatabase, document.Asset(), document.Definition());
        m_AssetStatus = "Saved selected materials together. Undo restores every selected source.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Materials saved; reimport is required: ") + error.what());
    }
}

void EditorWorkspaceLayer::FlushMaterialSelectionImports()
{
    if (m_PendingMaterialSelectionImports.empty() || !m_AssetOperations)
        return;
    m_AssetOperations->QueueAssetImport(m_PendingMaterialSelectionImports,
                                        KeireEditor::AssetOperationPriority::MaterialRefresh,
                                        {.Reason = "material-selection-edit"});
    m_PendingMaterialSelectionImports.clear();
}
