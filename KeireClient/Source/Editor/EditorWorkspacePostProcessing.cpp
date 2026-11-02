#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"
#include "KeireClient/Editor/PostProcessPreviewSession.h"
#include "KeireClientInternal/Editor/PostProcessCreation.h"
#include "KeireInternal/FileSystem.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace
{
    [[nodiscard]] std::filesystem::path ProfileSource(const Keire::Ref<Keire::AssetDatabase>& database,
                                                      const Keire::AssetId asset)
    {
        if (!database)
        {
            throw std::runtime_error("The Asset Database is unavailable.");
        }
        const auto record = database->Find(asset);
        if (!record || record->Type != Keire::PostProcessProfileAsset::StaticType() ||
            record->RelativePath.extension() != ".keirepostprocess")
        {
            throw std::runtime_error("The Post Processing profile source is unavailable.");
        }
        const auto& specification = database->Specification();
        return specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    }
} // namespace

KeireEditor::PostProcessDocument& EditorWorkspaceLayer::PostProcessState() noexcept { return *m_PostProcessDocument; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::PostProcessDatabase() const noexcept { return m_AssetDatabase; }

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::PostProcessAssets() const noexcept { return Owner().Assets(); }

void EditorWorkspaceLayer::ActivatePostProcessHistory() noexcept
{
    m_ActiveUndoContext = m_PostProcessDocument->UndoContext();
}

void EditorWorkspaceLayer::OpenPostProcessProfile(const Keire::AssetId asset)
{
    if (m_PostProcessDocument->Asset() != asset)
    {
        if (m_PostProcessDocument->Dirty())
        {
            throw std::runtime_error("Save or discard the current profile's changes before opening another profile.");
        }
        const auto source = ProfileSource(m_AssetDatabase, asset);
        auto bytes = KeireEditor::Detail::ReadBytes(source);
        (void)Keire::PostProcessProfileAsset::Decode(bytes);
        KeireEditor::DocumentSourcePersistence persistence;
        persistence.Bind(source, bytes);
        Keire::Ref<Keire::UndoContext> context;
        if (const auto undo = Owner().Undo())
        {
            context = undo->CreateContext({.Name = "Post Processing Profile", .MaximumCommands = 128});
        }
        const auto previousContext = m_PostProcessDocument->UndoContext();
        if (++m_PostProcessDocumentRevision == 0)
        {
            ++m_PostProcessDocumentRevision;
        }
        m_PostProcessDocument->Open(asset, bytes, m_PostProcessDocumentRevision, std::move(context));
        m_PostProcessPersistence = std::move(persistence);
        if (previousContext)
        {
            previousContext->Close();
        }
    }
    ActivatePostProcessHistory();
    m_PostProcessPanel->Registration().SetVisible(true);
    m_PostProcessPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::CreatePostProcessProfile(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
    {
        throw std::runtime_error("Asset creation services are unavailable.");
    }
    if (m_PostProcessDocument->Dirty())
    {
        throw std::runtime_error("Save or discard the current profile before creating another profile.");
    }
    if (const auto error = KeireEditor::Detail::PostProcessProfileNameError(name); !error.empty())
    {
        throw std::invalid_argument(std::string(error));
    }
    const auto folder = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
    const auto destination = folder / Keire::Detail::PathFromUtf8(std::string(name) + ".keirepostprocess");
    if (m_AssetDatabase->Find(destination))
    {
        throw std::runtime_error("A profile with that name already exists in this folder.");
    }
    if (m_AssetOperations->Busy())
    {
        (void)m_AssetOperations->PreemptBackgroundImports();
    }
    m_AssetOperations->QueueCreateAsset(destination, Keire::PostProcessProfileAsset::Encode({}), {},
                                        {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenPostProcessing,
                                         .UndoName = "Create Post Processing Profile"});
    m_AssetStatus = "Creating " + Keire::Detail::PathToUtf8(destination) + ".";
}

void EditorWorkspaceLayer::PersistPostProcessProfile(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    const auto source = ProfileSource(m_AssetDatabase, asset);
    std::vector<std::byte> savedBytes(bytes.begin(), bytes.end());
    const auto savedProfile = Keire::PostProcessProfileAsset::Decode(bytes);
    m_PostProcessPersistence.Relocate(source);
    m_PostProcessPersistence.Publish(std::move(savedBytes));
    if (m_PostProcessPreview)
    {
        m_PostProcessPreview->CommitSavedBaseline(asset, savedProfile);
    }
}

void EditorWorkspaceLayer::PreviewPostProcessProfile(const Keire::AssetId asset,
                                                     const Keire::PostProcessProfileDefinition& definition)
{
    if (!m_PostProcessPreview)
    {
        m_PostProcessPreview =
            std::make_unique<KeireEditor::PostProcessPreviewSession>(KeireEditor::PostProcessPreviewDependencies{
                .ReadSource =
                    [this](const Keire::AssetId source)
                {
                    return Keire::PostProcessProfileAsset::Decode(
                        KeireEditor::Detail::ReadBytes(ProfileSource(m_AssetDatabase, source)));
                },
                .Publish =
                    [this](const Keire::AssetId target, const Keire::Ref<Keire::PostProcessProfileAsset>& profile)
                {
                    const auto assets = Owner().Assets();
                    return assets && assets->PublishDevelopmentAsset(target, profile);
                }});
    }
    m_PostProcessPreview->Preview(asset, definition);
}

void EditorWorkspaceLayer::StopPostProcessPreview()
{
    if (m_PostProcessPreview)
    {
        m_PostProcessPreview->Stop();
    }
}

void EditorWorkspaceLayer::SavePostProcessProfile()
{
    if (!m_AssetOperations)
    {
        throw std::runtime_error("The asset worker is unavailable for Post Processing profile publication.");
    }
    const auto asset = m_PostProcessDocument->Asset();
    m_PostProcessDocument->Save();
    m_AssetOperations->QueueAssetImport(asset, KeireEditor::AssetOperationPriority::ExplicitAction,
                                        {.ReloadAsset = asset, .Reason = "post-process-profile-save"});
    m_AssetStatus = "Post Processing profile saved; refreshing its runtime asset.";
    // The live revision already matches the saved source. Reloading the old catalog here would briefly
    // replace it with pre-save settings while the asynchronous importer is still rebuilding the profile.
}

void EditorWorkspaceLayer::ReloadPostProcessProfile()
{
    const auto source = ProfileSource(m_AssetDatabase, m_PostProcessDocument->Asset());
    auto bytes = KeireEditor::Detail::ReadBytes(source);
    KeireEditor::DocumentSourcePersistence persistence;
    persistence.Bind(source, bytes);
    if (++m_PostProcessDocumentRevision == 0)
    {
        ++m_PostProcessDocumentRevision;
    }
    const auto result = m_PostProcessDocument->Reload(bytes, m_PostProcessDocumentRevision);
    if (result == KeireEditor::AssetDocumentReloadResult::LocalChanges)
    {
        throw std::runtime_error("Reload cancelled: save or discard your local profile changes first.");
    }
    m_PostProcessPersistence = std::move(persistence);
}

void EditorWorkspaceLayer::DrawDirtyPostProcessingDialog(Keire::UiFrame& ui)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Post Processing Changes"); popup)
    {
        ui.Text("Save the open Post Processing profile before closing the editor?");
        if (ui.Button("Save"))
        {
            try
            {
                SavePostProcessProfile();
                if (!m_PostProcessDocument->Dirty())
                {
                    ui.CloseCurrentPopup();
                    ExecutePendingSceneAction();
                }
            }
            catch (const std::exception& error)
            {
                ReportError("Post Processing", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            try
            {
                m_PostProcessDocument->Discard();
                ui.CloseCurrentPopup();
                ExecutePendingSceneAction();
            }
            catch (const std::exception& error)
            {
                ReportError("Post Processing", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_DocumentCoordinator->CancelPendingTransition();
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
    }
}
