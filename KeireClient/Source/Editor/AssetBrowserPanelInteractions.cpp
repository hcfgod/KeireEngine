#include "KeireClientInternal/Editor/AssetBrowserPanelInternal.h"

namespace KeireEditor
{
    void AssetBrowserPanel::Impl::DrawRenamePopups(Keire::UiFrame& ui, IAssetBrowserController& editor)
    {
        if (OpenNamedCreatePopup)
        {
            ui.OpenPopup("Create Asset");
            OpenNamedCreatePopup = false;
            FocusCreateName = true;
        }
        if (auto create = ui.BeginPopupModal("Create Asset"); create)
        {
            const auto type = NamedAssetCreationDisplayName(PendingCreateKind);
            ui.Text("Choose a name for the new " + std::string(type));
            if (FocusCreateName)
            {
                ui.RequestKeyboardFocus();
                FocusCreateName = false;
            }
            (void)ui.InputText("Name", CreateNameBuffer, true);
            const bool submitCreate = ui.Shortcut({.Key = Keire::UiKey::Enter, .Global = true});
            if (PendingCreateKind == NamedCreateKind::MaterialGraph)
                MaterialGraphCreation.Draw(ui, editor.AssetBrowserRecords(), editor.AssetBrowserTheme());
            const auto canCreate = PendingCreateKind != NamedCreateKind::MaterialGraph ||
                                   static_cast<bool>(MaterialGraphCreation.Shader());
            if (auto disabled = ui.BeginDisabled(!canCreate); disabled)
            {
                const bool createButton = ui.Button("Create");
                if (canCreate && (createButton || submitCreate))
                {
                    try
                    {
                        if (CreateNameBuffer.empty() || CreateNameBuffer == "." || CreateNameBuffer == ".." ||
                            CreateNameBuffer.find_first_of("/\\") != std::string::npos)
                            throw std::invalid_argument("Asset name must be one non-empty path component.");
                        const auto previousFolder = std::exchange(CurrentFolder, PendingCreateFolder);
                        bool created = false;
                        try
                        {
                            created = Detail::CreateNamedAsset(
                                editor, PendingCreateKind, CreateNameBuffer, PendingManagedType, PendingInputActions,
                                PendingVariantBase, MaterialGraphCreation.Shader(), PendingShaderGraphTemplate);
                        }
                        catch (...)
                        {
                            CurrentFolder = previousFolder;
                            throw;
                        }
                        CurrentFolder = previousFolder;
                        if (created)
                        {
                            PendingCreateKind = NamedCreateKind::None;
                            PendingCreateFolder.clear();
                            PendingManagedType = {};
                            PendingInputActions.reset();
                            PendingVariantBase = {};
                            MaterialGraphCreation.Reset();
                            CreateNameBuffer.clear();
                            ui.CloseCurrentPopup();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        editor.ReportAssetBrowserError(std::string("Asset creation failed: ") + error.what());
                    }
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel"))
            {
                PendingCreateKind = NamedCreateKind::None;
                PendingCreateFolder.clear();
                PendingManagedType = {};
                PendingInputActions.reset();
                PendingVariantBase = {};
                MaterialGraphCreation.Reset();
                CreateNameBuffer.clear();
                ui.CloseCurrentPopup();
            }
        }

        if (OpenRenamePopup)
        {
            ui.OpenPopup("Rename Asset");
            OpenRenamePopup = false;
            FocusRenameName = true;
        }
        if (auto rename = ui.BeginPopupModal("Rename Asset"); rename)
        {
            ui.Text("Asset name (the extension is preserved)");
            if (FocusRenameName)
            {
                ui.RequestKeyboardFocus();
                FocusRenameName = false;
            }
            (void)ui.InputText("Name", RenameBuffer, true);
            const bool submitRename = ui.Shortcut({.Key = Keire::UiKey::Enter, .Global = true});
            if (ui.Button("Rename") || submitRename)
            {
                try
                {
                    if (RenameBuffer.empty() || RenameBuffer == "." || RenameBuffer == ".." ||
                        RenameBuffer.find_first_of("/\\") != std::string::npos)
                        throw std::invalid_argument("Asset name must be one non-empty path component.");
                    const auto record = editor.AssetBrowserDatabase()->Find(Renaming);
                    if (!record)
                        throw std::runtime_error("Asset no longer exists.");
                    const auto destination =
                        record->RelativePath.parent_path() / (RenameBuffer + record->RelativePath.extension().string());
                    editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                               .Asset = Renaming,
                                               .Destination = destination},
                                              {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                               .Asset = Renaming,
                                               .Destination = record->RelativePath},
                                              "Rename Asset");
                    editor.SetAssetBrowserStatus("Renamed asset without changing its stable identity.");
                    Renaming = {};
                    RenameBuffer.clear();
                    ui.CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    editor.ReportAssetBrowserError(std::string("Asset rename failed: ") + error.what());
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel"))
            {
                Renaming = {};
                RenameBuffer.clear();
                ui.CloseCurrentPopup();
            }
        }

        if (OpenFolderRenamePopup)
        {
            ui.OpenPopup("Rename Folder");
            OpenFolderRenamePopup = false;
            FocusRenameName = true;
        }
        if (auto rename = ui.BeginPopupModal("Rename Folder"); rename)
        {
            ui.Text("Folder name");
            if (FocusRenameName)
            {
                ui.RequestKeyboardFocus();
                FocusRenameName = false;
            }
            (void)ui.InputText("Name", RenameBuffer, true);
            const bool submitRename = ui.Shortcut({.Key = Keire::UiKey::Enter, .Global = true});
            if (ui.Button("Rename") || submitRename)
            {
                try
                {
                    if (RenameBuffer.empty() || RenameBuffer == "." || RenameBuffer == ".." ||
                        RenameBuffer.find_first_of("/\\") != std::string::npos)
                        throw std::invalid_argument("Folder name must be one non-empty path component.");
                    const auto destination = RenamingFolder.parent_path() / RenameBuffer;
                    editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                               .Source = RenamingFolder,
                                               .Destination = destination},
                                              {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                               .Source = destination,
                                               .Destination = RenamingFolder},
                                              "Rename Folder");
                    if (SameOrChild(RenamingFolder, CurrentFolder))
                    {
                        const auto suffix = CurrentFolder.lexically_relative(RenamingFolder);
                        CurrentFolder = suffix.empty() ? destination : destination / suffix;
                    }
                    editor.SetAssetBrowserStatus("Renamed folder.");
                    RenamingFolder.clear();
                    RenameBuffer.clear();
                    ui.CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    editor.ReportAssetBrowserError(std::string("Folder rename failed: ") + error.what());
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel"))
            {
                RenamingFolder.clear();
                RenameBuffer.clear();
                ui.CloseCurrentPopup();
            }
        }

        if (OpenPackageCreatePopup)
        {
            ui.OpenPopup("Create Asset Package");
            OpenPackageCreatePopup = false;
        }
        if (auto package = ui.BeginPopupModal("Create Asset Package"); package)
        {
            ui.Text(PendingPackageSelection.Folder
                        ? "Package folder: Assets/" + PendingPackageSelection.Folder->generic_string()
                        : "Package " + std::to_string(PendingPackageSelection.Assets.size()) + " selected asset(s)");
            (void)ui.InputText("Display name", PendingPackageDraft.DisplayName);
            (void)ui.InputText("Package ID", PendingPackageDraft.PackageId);
            (void)ui.InputText("Version", PendingPackageDraft.Version);
            (void)ui.InputText("Publisher ID", PendingPackageDraft.PublisherId);
            (void)ui.InputText("Minimum Kéire version", PendingPackageDraft.MinimumEngineVersion);
            (void)ui.InputText("Summary", PendingPackageDraft.Summary);
            const bool complete = !PendingPackageDraft.DisplayName.empty() && !PendingPackageDraft.PackageId.empty() &&
                                  !PendingPackageDraft.Version.empty() && !PendingPackageDraft.PublisherId.empty() &&
                                  !PendingPackageDraft.MinimumEngineVersion.empty();
            if (auto disabled = ui.BeginDisabled(!complete); disabled)
            {
                if (ui.Button("Choose destination..."))
                {
                    editor.CreateAssetBrowserPackage(std::move(PendingPackageSelection),
                                                     std::move(PendingPackageDraft));
                    ResetPackageCreate();
                    ui.CloseCurrentPopup();
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel"))
            {
                ResetPackageCreate();
                ui.CloseCurrentPopup();
            }
        }
    }

    void AssetBrowserPanel::Impl::DrawAssetContext(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                                                   IAssetBrowserController& editor, const std::string_view id)
    {
        if (auto context = ui.BeginItemContextMenu(id); context)
        {
            SelectOnlyIfNeeded(record.Id, editor);
            if (ui.MenuItem("Open"))
                Open(record, editor);
            if (record.Type == Keire::PrefabAsset::StaticType() && ui.MenuItem("Create Variant..."))
            {
                PendingVariantBase = record.Id;
                RequestNamedCreate(NamedCreateKind::PrefabVariant, record.RelativePath.stem().string() + "Variant");
            }
            if (ui.MenuItem("Configure External Editor..."))
                editor.ConfigureAssetBrowserExternalEditor();
            if (ui.MenuItem("Rename", false, Selection.size() == 1))
                BeginAssetRename(record);
            if (ui.MenuItem("Duplicate"))
                DuplicateAssets(editor);
            if (ui.MenuItem("Cut"))
                SetClipboard(ClipboardMode::Cut, Selection);
            if (ui.MenuItem("Copy"))
                SetClipboard(ClipboardMode::Copy, Selection);
            if (ui.MenuItem("Create Asset Package..."))
            {
                const auto displayName = Selection.size() == 1 ? DisplayName(record.RelativePath) : "Selected Assets";
                RequestPackageCreate({.Assets = Selection}, displayName);
            }
            if (ui.MenuItem("Delete"))
                RequestDeleteAssets(editor);
            ui.Separator();
            if (ui.MenuItem("Reimport"))
                editor.ImportAssetBrowserAssets(Selection);
            if (record.Type == Keire::MeshAsset::StaticType() && !record.SubAssets.empty() &&
                ui.MenuItem("Extract Materials"))
                editor.ExtractAssetBrowserMaterials(record.Id);
            if (ui.MenuItem("Reveal in File Explorer"))
                Detail::RevealAssetBrowserPath(editor, AssetRoot / record.RelativePath);
            if (ui.MenuItem("Copy Relative Path"))
                Detail::CopyAssetBrowserText(editor,
                                             (std::filesystem::path("Assets") / record.RelativePath).generic_string());
            if (ui.MenuItem("Copy Asset ID"))
                Detail::CopyAssetBrowserText(editor, record.Id.ToString());
        }
    }

    void AssetBrowserPanel::Impl::DrawFolderContext(Keire::UiFrame& ui, const std::filesystem::path& folder,
                                                    IAssetBrowserController& editor, const std::string_view id)
    {
        if (auto context = ui.BeginItemContextMenu(id); context)
        {
            SelectFolderOnlyIfNeeded(folder, editor);
            if (ui.MenuItem("Open"))
                CurrentFolder = folder;
            if (auto create = ui.BeginMenu("Create"); create)
            {
                const auto previous = std::exchange(CurrentFolder, folder);
                DrawCreateItems(ui, editor);
                CurrentFolder = previous;
            }
            if (ui.MenuItem("Rename", false, FolderSelection.size() == 1))
                BeginFolderRename(folder);
            if (ui.MenuItem("Duplicate"))
                DuplicateFolders(editor);
            if (ui.MenuItem("Cut"))
                SetFolderClipboard(ClipboardMode::Cut, FolderSelection);
            if (ui.MenuItem("Copy"))
                SetFolderClipboard(ClipboardMode::Copy, FolderSelection);
            if (ui.MenuItem("Create Asset Package...", false, FolderSelection.size() == 1))
                RequestPackageCreate({.Folder = folder}, folder.filename().string());
            if (ui.MenuItem("Paste Into", false, ClipboardModeValue != ClipboardMode::Empty))
                Paste(folder, editor);
            if (ui.MenuItem("Delete"))
                RequestDeleteFolders(FolderSelection);
            ui.Separator();
            if (ui.MenuItem("Reimport Recursively"))
            {
                std::vector<Keire::AssetId> assets;
                for (const auto& asset : editor.AssetBrowserRecords())
                {
                    const bool selectedFolder =
                        std::ranges::any_of(FolderSelection,
                                            [&](const std::filesystem::path& selected)
                                            {
                                                if (selected.empty())
                                                    return true;
                                                const auto relative = asset.RelativePath.lexically_relative(selected);
                                                return !relative.empty() && *relative.begin() != "..";
                                            });
                    if (selectedFolder)
                        assets.push_back(asset.Id);
                }
                editor.ImportAssetBrowserAssets(assets);
            }
            if (ui.MenuItem("Reveal in File Explorer"))
                Detail::RevealAssetBrowserPath(editor, AssetRoot / folder);
            if (ui.MenuItem("Copy Relative Path"))
                Detail::CopyAssetBrowserText(editor, (std::filesystem::path("Assets") / folder).generic_string());
        }
    }
} // namespace KeireEditor
