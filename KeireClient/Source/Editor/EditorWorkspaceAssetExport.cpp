#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorPackageCoordinator.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

void EditorWorkspaceLayer::ExtractAssetBrowserMaterials(const Keire::AssetId model)
{
    if (!m_AssetDatabase)
        return;
    try
    {
        const auto record = m_AssetDatabase->Find(model);
        if (!record)
            throw std::runtime_error("The selected model no longer exists in the project database.");
        const auto directory =
            record->RelativePath.parent_path() / (record->RelativePath.stem().string() + " Materials");
        if (!m_AssetOperations)
            throw std::logic_error("The isolated asset worker is unavailable.");
        m_AssetOperations->QueueExtractMaterials(model, directory,
                                                 {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal});
        m_AssetStatus = "Extracting editable materials in the isolated worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material extraction failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CreateAssetBrowserPackage(KeireEditor::AssetPackageSelection selection,
                                                     KeireEditor::AssetPackageDraft draft)
{
    m_PackageCoordinator->CreateAssetPackage(std::move(selection), std::move(draft));
}

void EditorWorkspaceLayer::CompleteAssetBrowserPackage() { m_PackageCoordinator->Update(); }
