#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AssetPackageAuthoring.h"
#include "KeireInternal/Process.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    [[nodiscard]] std::string AssetPackageFileName(const std::string_view displayName)
    {
        std::string result(displayName);
        for (char& character : result)
            if (static_cast<unsigned char>(character) < 32U ||
                std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
                character = '-';
        while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
            result.pop_back();
        if (result.empty())
            result = "Asset Package";
        return result + ".keireassetpackage";
    }
} // namespace

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
    try
    {
        if (!m_AssetDatabase)
            throw std::logic_error("No project asset database is available.");
        if (m_PendingAssetPackageDialog || m_AssetPackageExport.valid())
            throw std::logic_error("An asset-package export is already active.");
        static_cast<void>(KeireEditor::ResolveAssetPackageRecords(m_AssetRecords, selection));
        Keire::SaveFileDialogSpecification dialog;
        dialog.Title = "Create Kéire Asset Package";
        dialog.DefaultLocation = m_AssetDatabase->Specification().ProjectRoot;
        dialog.DefaultName = AssetPackageFileName(draft.DisplayName);
        dialog.FilterName = "Kéire Asset Package";
        dialog.Extension = "keireassetpackage";
        auto operation = Owner().Windows()->ShowSaveFileDialog(Owner().MainWindow()->Id(), dialog);
        m_PendingAssetPackageDialog = {
            .Selection = std::move(selection), .Draft = std::move(draft), .Dialog = std::move(operation)};
        m_AssetStatus = "Choose a destination for the asset package.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset-package export failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CompleteAssetBrowserPackage()
{
    if (m_AssetPackageExport.valid())
    {
        if (m_AssetPackageExport.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try
        {
            const auto result = m_AssetPackageExport.get();
            m_AssetStatus = "Created " + m_AssetPackageOutput.filename().string() + " with " +
                            std::to_string(result.Manifest.Assets.size()) + " asset(s).";
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(m_AssetPackageOutput, diagnostic))
                m_AssetStatus += " Reveal failed: " + diagnostic;
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Asset-package export failed: ") + error.what());
        }
        m_AssetPackageOutput.clear();
    }
    if (!m_PendingAssetPackageDialog ||
        m_PendingAssetPackageDialog->Dialog->Status() == Keire::SaveFileDialogStatus::Pending)
        return;

    auto pending = std::move(*m_PendingAssetPackageDialog);
    m_PendingAssetPackageDialog.reset();
    if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Cancelled)
    {
        m_AssetStatus = "Asset-package export cancelled.";
        return;
    }
    if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Failed)
    {
        SetAssetError("Asset-package save dialog failed: " + pending.Dialog->Diagnostic());
        return;
    }
    try
    {
        auto output = pending.Dialog->SelectedPath();
        if (output.extension() != ".keireassetpackage")
            output += ".keireassetpackage";
        if (std::filesystem::exists(output))
            throw std::invalid_argument("Asset-package export will not overwrite an existing file.");
        if (!m_AssetDatabase)
            throw std::logic_error("The project closed before asset-package export began.");
        const auto& specification = m_AssetDatabase->Specification();
        KeireEditor::AssetPackageAuthoringRequest request{.ProjectRoot = specification.ProjectRoot,
                                                          .SourceDirectory = specification.SourceDirectory,
                                                          .StagingParent =
                                                              specification.ProjectRoot / "Library/AssetPackageExports",
                                                          .Output = output,
                                                          .Selection = std::move(pending.Selection),
                                                          .Draft = std::move(pending.Draft),
                                                          .Records = m_AssetRecords};
        m_AssetPackageOutput = std::move(output);
        m_AssetPackageExport = std::async(std::launch::async, [request = std::move(request)]
                                          { return KeireEditor::CreateAssetPackageArchive(request); });
        m_AssetStatus = "Creating the asset package in the background...";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset-package export failed: ") + error.what());
    }
}
