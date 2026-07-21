#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireClient/EditorWorkspaceLayer.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string DisplayName(const std::filesystem::path& path)
        {
            const auto stem = path.stem().string();
            return stem.empty() ? path.filename().string() : stem;
        }

        [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            const auto normalizedParent = parent.lexically_normal();
            const auto normalizedCandidate = candidate.lexically_normal();
            if (normalizedParent == normalizedCandidate)
                return true;
            const auto relative = normalizedCandidate.lexically_relative(normalizedParent);
            return !relative.empty() && !relative.is_absolute() && !relative.generic_string().starts_with("..");
        }

        [[nodiscard]] std::string AssetTypeName(const Keire::AssetSourceRecord& record)
        {
            const auto extension = record.RelativePath.extension().string();
            if (extension == ".keirescene")
                return "Scene";
            if (extension == ".keireinput")
                return "Input Actions";
            if (extension == ".keireshader")
                return "Shader";
            if (extension == ".keirematerial")
                return "Material";
            if (extension == ".hlsl")
                return "HLSL Source";
            return "Asset";
        }

        [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(const std::span<const std::byte> bytes)
        {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::istringstream stream(text);
            std::vector<Keire::AssetId> result;
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.empty())
                    continue;
                const auto id = Keire::AssetId::Parse(line);
                if (!id || result.size() >= 1024)
                    throw std::invalid_argument("Asset drag payload is invalid or exceeds 1024 entries.");
                result.push_back(id);
            }
            if (result.empty())
                throw std::invalid_argument("Asset drag payload is empty.");
            return result;
        }

        [[nodiscard]] std::string EncodeAssetPayload(const std::span<const Keire::AssetId> assets)
        {
            std::string result;
            for (const auto asset : assets)
            {
                result += asset.ToString();
                result.push_back('\n');
            }
            return result;
        }
    } // namespace

    class AssetBrowserPanel::Impl final
    {
      public:
        enum class ViewMode : std::uint8_t
        {
            List,
            Grid
        };

        enum class ClipboardMode : std::uint8_t
        {
            Empty,
            Copy,
            Cut
        };

        struct ClipboardEntry final
        {
            Keire::AssetId Asset;
            std::filesystem::path Folder;
        };

        struct TrashCommandState final
        {
            Keire::AssetTrashId Trash;
            Keire::AssetId Asset;
            std::filesystem::path Folder;
            bool IsFolder = false;
        };

        struct ExternalDropTarget final
        {
            Keire::UiItemRect Rect;
            std::filesystem::path Folder;
        };

        [[nodiscard]] std::filesystem::path ResolveExternalDropFolder(const Keire::UiPosition position) const
        {
            for (auto iterator = ExternalDropTargets.rbegin(); iterator != ExternalDropTargets.rend(); ++iterator)
                if (position.X >= iterator->Rect.Minimum.X && position.X <= iterator->Rect.Maximum.X &&
                    position.Y >= iterator->Rect.Minimum.Y && position.Y <= iterator->Rect.Maximum.Y)
                    return iterator->Folder;
            return CurrentFolder;
        }

        void SetProjectRoot(const std::filesystem::path& root)
        {
            Close();
            if (root.empty())
                return;
            ProjectRoot = std::filesystem::absolute(root).lexically_normal();
            AssetRoot = ProjectRoot / "Assets";
            PreferencePath = ProjectRoot / "Library" / "Editor" / "asset-browser.settings";
            Thumbnails = std::make_unique<ThumbnailService>(ProjectRoot / "Library" / "Thumbnails");
            LoadPreferences();
        }

        void SetUndoContext(Keire::Ref<Keire::UndoContext> context) { Undo = std::move(context); }

        void RequestCreateMaterial()
        {
            MaterialNameBuffer = "Material";
            OpenMaterialCreatePopup = true;
        }

        void InvalidateThumbnail(const Keire::AssetId asset)
        {
            Images.erase(asset);
            ImageDigests.erase(asset);
        }

        void Close() noexcept
        {
            SavePreferences();
            if (Thumbnails)
                Thumbnails->CancelAll();
            Thumbnails.reset();
            Images.clear();
            ImageDigests.clear();
            FolderImage.Reset();
            Selection.clear();
            Clipboard.clear();
            Undo.Reset();
            Renaming = {};
            RenamingFolder.clear();
            RenameBuffer.clear();
            MaterialNameBuffer.clear();
            CurrentFolder.clear();
            ProjectRoot.clear();
            AssetRoot.clear();
        }

        void LoadPreferences()
        {
            std::ifstream input(PreferencePath);
            std::string line;
            while (std::getline(input, line))
            {
                if (line == "view=grid")
                    Mode = ViewMode::Grid;
                else if (line == "view=list")
                    Mode = ViewMode::List;
                else if (line.starts_with("size="))
                {
                    try
                    {
                        ThumbnailSize = std::clamp(std::stof(line.substr(5)), 48.0F, 160.0F);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }

        void SavePreferences() noexcept
        {
            if (PreferencePath.empty())
                return;
            try
            {
                const auto text = std::string("view=") + (Mode == ViewMode::Grid ? "grid\n" : "list\n") +
                                  "size=" + std::to_string(ThumbnailSize) + "\n";
                Keire::Detail::WriteTextFileAtomically(PreferencePath, text);
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] std::vector<std::filesystem::path> Folders() const
        {
            std::vector<std::filesystem::path> result;
            const auto absolute = AssetRoot / CurrentFolder;
            std::error_code error;
            for (std::filesystem::directory_iterator iterator(absolute, error), end; !error && iterator != end;
                 iterator.increment(error))
                if (iterator->is_directory(error))
                    result.push_back(std::filesystem::relative(iterator->path(), AssetRoot, error));
            std::ranges::sort(result);
            return result;
        }

        [[nodiscard]] std::filesystem::path UniqueFolder(std::filesystem::path desired) const
        {
            const auto parent = desired.parent_path();
            const auto base = desired.filename().string();
            for (std::size_t copy = 2; std::filesystem::exists(AssetRoot / desired); ++copy)
                desired = parent / (base + " " + std::to_string(copy));
            return desired;
        }

        [[nodiscard]] std::filesystem::path UniqueAsset(const Keire::AssetSourceRecord& source,
                                                        const std::filesystem::path& folder,
                                                        EditorWorkspaceLayer& editor) const
        {
            const auto stem = source.RelativePath.stem().string();
            const auto extension = source.RelativePath.extension().string();
            auto destination = folder / (stem + " Copy" + extension);
            for (std::size_t copy = 2; editor.m_AssetDatabase->Find(destination); ++copy)
                destination = folder / (stem + " Copy " + std::to_string(copy) + extension);
            return destination;
        }

        void Select(const Keire::AssetId asset, const bool additive, EditorWorkspaceLayer& editor)
        {
            if (!additive)
                Selection.clear();
            const auto found = std::ranges::find(Selection, asset);
            if (additive && found != Selection.end())
                Selection.erase(found);
            else if (found == Selection.end())
                Selection.push_back(asset);
            editor.m_SelectedAsset = Selection.empty() ? Keire::AssetId{} : Selection.back();
            editor.m_SelectedSceneObject = {};
        }

        void SelectOnlyIfNeeded(const Keire::AssetId asset, EditorWorkspaceLayer& editor)
        {
            if (std::ranges::find(Selection, asset) == Selection.end())
                Select(asset, false, editor);
        }

        void Reveal(const Keire::AssetId asset, EditorWorkspaceLayer& editor)
        {
            if (const auto record = editor.m_AssetDatabase ? editor.m_AssetDatabase->Find(asset) : std::nullopt)
            {
                CurrentFolder = record->RelativePath.parent_path();
                Select(asset, false, editor);
                RevealAsset = asset;
            }
        }

        void Open(const Keire::AssetSourceRecord& record, EditorWorkspaceLayer& editor)
        {
            try
            {
                if (record.RelativePath.extension() == ".keireinput")
                    editor.OpenInputActions(record.Id);
                else if (record.RelativePath.extension() == ".keirescene")
                    editor.RequestOpenScene(record.Id);
                else
                    editor.m_AssetStatus =
                        "No editor is registered for " + record.RelativePath.filename().string() + ".";
            }
            catch (const std::exception& error)
            {
                editor.SetAssetError(std::string("Asset open failed: ") + error.what());
            }
        }

        void RecordCreatedAsset(const Keire::Ref<Keire::AssetDatabase>& database, const Keire::AssetId asset,
                                std::string name)
        {
            if (!Undo || !database || !asset)
                return;
            auto state = std::make_shared<TrashCommandState>();
            state->Asset = asset;
            Undo->RecordApplied(Keire::CreateUndoCommand(
                std::move(name),
                [database, state]
                {
                    if (!state->Trash)
                        throw std::logic_error("Created asset has no recoverable trash entry.");
                    database->RestoreTrash(state->Trash);
                    state->Trash = {};
                },
                [database, state]
                {
                    const auto trashed = database->TrashAsset(state->Asset);
                    state->Trash = trashed.Id;
                }));
        }

        void RecordCreatedFolder(const Keire::Ref<Keire::AssetDatabase>& database, const std::filesystem::path& folder)
        {
            if (!Undo || !database)
                return;
            auto state = std::make_shared<TrashCommandState>();
            state->Folder = folder;
            state->IsFolder = true;
            Undo->RecordApplied(Keire::CreateUndoCommand(
                "Create Folder",
                [database, state]
                {
                    if (!state->Trash)
                        throw std::logic_error("Created folder has no recoverable trash entry.");
                    database->RestoreTrash(state->Trash);
                    state->Trash = {};
                },
                [database, state]
                {
                    const auto trashed = database->TrashFolder(state->Folder);
                    state->Trash = trashed.Id;
                }));
        }

        void RecordAssetMove(const Keire::Ref<Keire::AssetDatabase>& database, const Keire::AssetId asset,
                             std::filesystem::path before, std::filesystem::path after, std::string name)
        {
            if (!Undo)
                return;
            Undo->RecordApplied(Keire::CreateUndoCommand(
                std::move(name), [database, asset, after] { database->MoveAsset(asset, after); },
                [database, asset, before] { database->MoveAsset(asset, before); }));
        }

        void RecordFolderMove(const Keire::Ref<Keire::AssetDatabase>& database, std::filesystem::path before,
                              std::filesystem::path after, std::string name)
        {
            if (!Undo)
                return;
            Undo->RecordApplied(Keire::CreateUndoCommand(
                std::move(name), [database, before, after] { database->MoveFolder(before, after); },
                [database, before, after] { database->MoveFolder(after, before); }));
        }

        void RecordTrash(const Keire::Ref<Keire::AssetDatabase>& database, const Keire::AssetTrashRecord& trashed,
                         std::string name)
        {
            if (!Undo)
                return;
            auto state = std::make_shared<TrashCommandState>();
            state->Trash = trashed.Id;
            state->Asset = trashed.Assets.size() == 1 ? trashed.Assets.front() : Keire::AssetId{};
            state->Folder = trashed.OriginalPath;
            state->IsFolder = trashed.Folder;
            Undo->RecordApplied(Keire::CreateUndoCommand(
                std::move(name),
                [database, state]
                {
                    const auto retrash =
                        state->IsFolder ? database->TrashFolder(state->Folder) : database->TrashAsset(state->Asset);
                    state->Trash = retrash.Id;
                },
                [database, state]
                {
                    database->RestoreTrash(state->Trash);
                    state->Trash = {};
                }));
        }

        void CreateFolder(EditorWorkspaceLayer& editor)
        {
            try
            {
                const auto folder = UniqueFolder(CurrentFolder / "New Folder");
                editor.m_AssetDatabase->CreateFolder(folder);
                RecordCreatedFolder(editor.m_AssetDatabase, folder);
                RenamingFolder = folder;
                RenameBuffer = folder.filename().string();
                OpenFolderRenamePopup = true;
                editor.m_AssetStatus = "Created " + folder.generic_string() + ".";
            }
            catch (const std::exception& error)
            {
                editor.SetAssetError(std::string("Folder creation failed: ") + error.what());
            }
        }

        void DrawCreateItems(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (ui.MenuItem("Folder"))
                CreateFolder(editor);
            if (ui.MenuItem("Scene"))
                editor.RequestCreateScene();
            if (ui.MenuItem("Material"))
                RequestCreateMaterial();
            if (ui.MenuItem("Unlit Shader"))
                editor.CreateUnlitShader();
            if (auto input = ui.BeginMenu("Input Actions"); input)
            {
                if (ui.MenuItem("Empty"))
                    editor.CreateInputActions({.SchemaVersion = 1, .Name = "InputActions"}, "InputActions");
                if (ui.MenuItem("Default"))
                    editor.CreateInputActions(Keire::InputActionAsset::DefaultDefinition(), "DefaultInput");
                if (ui.MenuItem("3D Gameplay"))
                    editor.CreateInputActions(Keire::InputActionAsset::GameplayDefinition(), "GameplayInput");
                if (ui.MenuItem("UI Navigation"))
                    editor.CreateInputActions(Keire::InputActionAsset::UiDefinition(), "UiInput");
            }
        }

        void DuplicateAssets(EditorWorkspaceLayer& editor)
        {
            if (Selection.empty())
                return;
            try
            {
                auto transaction =
                    Undo ? Undo->BeginTransaction(Selection.size() == 1 ? "Duplicate Asset" : "Duplicate Assets")
                         : nullptr;
                std::vector<Keire::AssetId> created;
                for (const auto asset : Selection)
                {
                    const auto record = editor.m_AssetDatabase->Find(asset);
                    if (!record)
                        continue;
                    const auto destination = UniqueAsset(*record, record->RelativePath.parent_path(), editor);
                    const auto copy = editor.m_AssetDatabase->Duplicate(asset, destination);
                    RecordCreatedAsset(editor.m_AssetDatabase, copy, "Duplicate Asset");
                    created.push_back(copy);
                }
                if (transaction)
                    transaction->Commit();
                Selection = std::move(created);
                editor.m_SelectedAsset = Selection.empty() ? Keire::AssetId{} : Selection.back();
                editor.m_AssetStatus = "Duplicated " + std::to_string(Selection.size()) + " asset(s).";
            }
            catch (const std::exception& error)
            {
                editor.SetAssetError(std::string("Asset duplication failed: ") + error.what());
            }
        }

        void MoveAssets(const std::span<const Keire::AssetId> assets, const std::filesystem::path& folder,
                        EditorWorkspaceLayer& editor)
        {
            std::vector<std::pair<Keire::AssetSourceRecord, std::filesystem::path>> moves;
            std::set<std::string> destinations;
            for (const auto asset : assets)
            {
                const auto record = editor.m_AssetDatabase->Find(asset);
                if (!record)
                    throw std::invalid_argument("Cannot move an asset that no longer exists.");
                const auto destination = (folder / record->RelativePath.filename()).lexically_normal();
                if (destination == record->RelativePath)
                    continue;
                if (editor.m_AssetDatabase->Find(destination) ||
                    !destinations.insert(destination.generic_string()).second)
                    throw std::runtime_error("Asset move destination already exists: " + destination.generic_string());
                moves.emplace_back(*record, destination);
            }
            auto transaction =
                Undo ? Undo->BeginTransaction(moves.size() == 1 ? "Move Asset" : "Move Assets") : nullptr;
            for (const auto& [record, destination] : moves)
            {
                editor.m_AssetDatabase->MoveAsset(record.Id, destination);
                RecordAssetMove(editor.m_AssetDatabase, record.Id, record.RelativePath, destination, "Move Asset");
            }
            if (transaction)
                transaction->Commit();
            editor.m_AssetStatus = "Moved " + std::to_string(moves.size()) + " asset(s).";
        }

        void SetClipboard(const ClipboardMode mode, const std::span<const Keire::AssetId> assets)
        {
            ClipboardModeValue = mode;
            Clipboard.clear();
            for (const auto asset : assets)
                Clipboard.push_back({asset, {}});
        }

        void SetFolderClipboard(const ClipboardMode mode, const std::filesystem::path& folder)
        {
            ClipboardModeValue = mode;
            Clipboard = {{Keire::AssetId{}, folder}};
        }

        void Paste(const std::filesystem::path& folder, EditorWorkspaceLayer& editor)
        {
            if (ClipboardModeValue == ClipboardMode::Empty || Clipboard.empty())
                return;
            try
            {
                auto transaction = Undo ? Undo->BeginTransaction(
                                              ClipboardModeValue == ClipboardMode::Cut ? "Move Assets" : "Paste Assets")
                                        : nullptr;
                for (const auto& entry : Clipboard)
                {
                    if (entry.Asset)
                    {
                        const auto record = editor.m_AssetDatabase->Find(entry.Asset);
                        if (!record)
                            throw std::runtime_error("Clipboard asset no longer exists.");
                        if (ClipboardModeValue == ClipboardMode::Cut)
                        {
                            const auto destination = folder / record->RelativePath.filename();
                            editor.m_AssetDatabase->MoveAsset(entry.Asset, destination);
                            RecordAssetMove(editor.m_AssetDatabase, entry.Asset, record->RelativePath, destination,
                                            "Move Asset");
                        }
                        else
                        {
                            const auto destination = UniqueAsset(*record, folder, editor);
                            const auto copy = editor.m_AssetDatabase->Duplicate(entry.Asset, destination);
                            RecordCreatedAsset(editor.m_AssetDatabase, copy, "Paste Asset");
                        }
                    }
                    else
                    {
                        const auto destinationBase = folder / entry.Folder.filename();
                        if (ClipboardModeValue == ClipboardMode::Cut)
                        {
                            editor.m_AssetDatabase->MoveFolder(entry.Folder, destinationBase);
                            RecordFolderMove(editor.m_AssetDatabase, entry.Folder, destinationBase, "Move Folder");
                        }
                        else
                        {
                            const auto destination = UniqueFolder(destinationBase);
                            (void)editor.m_AssetDatabase->DuplicateFolder(entry.Folder, destination);
                            RecordCreatedFolder(editor.m_AssetDatabase, destination);
                        }
                    }
                }
                if (transaction)
                    transaction->Commit();
                if (ClipboardModeValue == ClipboardMode::Cut)
                {
                    Clipboard.clear();
                    ClipboardModeValue = ClipboardMode::Empty;
                }
                editor.m_AssetStatus = "Pasted asset selection into " +
                                       (folder.empty() ? std::string("Assets") : folder.generic_string()) + ".";
            }
            catch (const std::exception& error)
            {
                editor.SetAssetError(std::string("Asset paste failed: ") + error.what());
            }
        }

        void RequestDeleteAssets(EditorWorkspaceLayer& editor)
        {
            if (Selection.empty())
                return;
            if (std::ranges::find(Selection, editor.m_SceneAsset) != Selection.end() && editor.m_EditingScene &&
                editor.m_EditingScene->Dirty())
            {
                editor.m_AssetStatus = "Save or close the dirty scene before deleting its asset.";
                return;
            }
            PendingDeleteAssets = Selection;
            PendingDeleteFolder.clear();
            OpenDeletePopup = true;
        }

        void RequestDeleteFolder(const std::filesystem::path& folder)
        {
            PendingDeleteAssets.clear();
            PendingDeleteFolder = folder;
            OpenDeletePopup = true;
        }

        void DrawDeletePopup(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (OpenDeletePopup)
            {
                ui.OpenPopup("Delete Assets");
                OpenDeletePopup = false;
            }
            if (auto popup = ui.BeginPopupModal("Delete Assets"); popup)
            {
                ui.TextColored(editor.m_Theme.Warning, "Move the selected content to recoverable project trash?");
                ui.Text(PendingDeleteFolder.empty() ? std::to_string(PendingDeleteAssets.size()) + " asset(s) selected"
                                                    : PendingDeleteFolder.generic_string());
                if (ui.Button("Move to Trash"))
                {
                    try
                    {
                        auto transaction = Undo ? Undo->BeginTransaction("Delete Assets") : nullptr;
                        if (!PendingDeleteFolder.empty())
                        {
                            const auto trashed = editor.m_AssetDatabase->TrashFolder(PendingDeleteFolder);
                            RecordTrash(editor.m_AssetDatabase, trashed, "Delete Folder");
                        }
                        else
                        {
                            for (const auto asset : PendingDeleteAssets)
                            {
                                const auto trashed = editor.m_AssetDatabase->TrashAsset(asset);
                                RecordTrash(editor.m_AssetDatabase, trashed, "Delete Asset");
                            }
                        }
                        if (transaction)
                            transaction->Commit();
                        Selection.clear();
                        editor.m_SelectedAsset = {};
                        editor.m_AssetStatus = "Moved selection to recoverable project trash.";
                        PendingDeleteAssets.clear();
                        PendingDeleteFolder.clear();
                        ui.CloseCurrentPopup();
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Asset delete failed: ") + error.what());
                    }
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                {
                    PendingDeleteAssets.clear();
                    PendingDeleteFolder.clear();
                    ui.CloseCurrentPopup();
                }
            }
        }

        void DrawTrashPopup(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (OpenTrashPopup)
            {
                ui.OpenPopup("Asset Trash");
                OpenTrashPopup = false;
            }
            if (auto popup = ui.BeginPopupModal("Asset Trash"); popup)
            {
                ui.TextColored(editor.m_Theme.Accent, "RECOVERABLE ASSET TRASH");
                ui.Separator();
                try
                {
                    const auto records = editor.m_AssetDatabase->TrashRecords();
                    if (records.empty())
                        ui.TextColored(editor.m_Theme.MutedText, "Trash is empty.");
                    for (const auto& record : records)
                    {
                        auto id = ui.PushId(record.Id.ToString());
                        ui.Text(record.OriginalPath.generic_string());
                        ui.SameLine();
                        if (ui.Button("Restore"))
                        {
                            editor.m_AssetDatabase->RestoreTrash(record.Id);
                            editor.m_AssetStatus = "Restored " + record.OriginalPath.generic_string() + ".";
                        }
                        ui.SameLine();
                        if (ui.Button("Delete Permanently"))
                        {
                            editor.m_AssetDatabase->PermanentlyDeleteTrash(record.Id);
                            editor.m_AssetStatus = "Permanently removed trash entry.";
                        }
                    }
                }
                catch (const std::exception& error)
                {
                    ui.TextColored(editor.m_Theme.Error, error.what());
                    editor.SetAssetError(std::string("Asset trash failed: ") + error.what());
                    ui.CloseCurrentPopup();
                }
                ui.Separator();
                if (ui.Button("Close"))
                    ui.CloseCurrentPopup();
            }
        }

        void BeginAssetRename(const Keire::AssetSourceRecord& record)
        {
            Renaming = record.Id;
            RenameBuffer = DisplayName(record.RelativePath);
            OpenRenamePopup = true;
        }

        void BeginFolderRename(const std::filesystem::path& folder)
        {
            RenamingFolder = folder;
            RenameBuffer = folder.filename().string();
            OpenFolderRenamePopup = true;
        }

        void DrawRenamePopups(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (OpenMaterialCreatePopup)
            {
                ui.OpenPopup("Create Material");
                OpenMaterialCreatePopup = false;
            }
            if (auto create = ui.BeginPopupModal("Create Material"); create)
            {
                ui.Text("Choose a name for the new material");
                (void)ui.InputText("Name", MaterialNameBuffer);
                if (ui.Button("Create"))
                {
                    try
                    {
                        if (MaterialNameBuffer.empty() || MaterialNameBuffer == "." || MaterialNameBuffer == ".." ||
                            MaterialNameBuffer.find_first_of("/\\") != std::string::npos)
                            throw std::invalid_argument("Material name must be one non-empty path component.");
                        if (editor.CreateMaterial(MaterialNameBuffer))
                        {
                            MaterialNameBuffer.clear();
                            ui.CloseCurrentPopup();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Material creation failed: ") + error.what());
                    }
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                {
                    MaterialNameBuffer.clear();
                    ui.CloseCurrentPopup();
                }
            }

            if (OpenRenamePopup)
            {
                ui.OpenPopup("Rename Asset");
                OpenRenamePopup = false;
            }
            if (auto rename = ui.BeginPopupModal("Rename Asset"); rename)
            {
                ui.Text("Asset name (the extension is preserved)");
                (void)ui.InputText("Name", RenameBuffer);
                if (ui.Button("Rename"))
                {
                    try
                    {
                        if (RenameBuffer.empty() || RenameBuffer == "." || RenameBuffer == ".." ||
                            RenameBuffer.find_first_of("/\\") != std::string::npos)
                            throw std::invalid_argument("Asset name must be one non-empty path component.");
                        const auto record = editor.m_AssetDatabase->Find(Renaming);
                        if (!record)
                            throw std::runtime_error("Asset no longer exists.");
                        const auto destination = record->RelativePath.parent_path() /
                                                 (RenameBuffer + record->RelativePath.extension().string());
                        editor.m_AssetDatabase->MoveAsset(Renaming, destination);
                        RecordAssetMove(editor.m_AssetDatabase, Renaming, record->RelativePath, destination,
                                        "Rename Asset");
                        editor.m_AssetStatus = "Renamed asset without changing its stable identity.";
                        Renaming = {};
                        RenameBuffer.clear();
                        ui.CloseCurrentPopup();
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Asset rename failed: ") + error.what());
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
            }
            if (auto rename = ui.BeginPopupModal("Rename Folder"); rename)
            {
                ui.Text("Folder name");
                (void)ui.InputText("Name", RenameBuffer);
                if (ui.Button("Rename"))
                {
                    try
                    {
                        if (RenameBuffer.empty() || RenameBuffer == "." || RenameBuffer == ".." ||
                            RenameBuffer.find_first_of("/\\") != std::string::npos)
                            throw std::invalid_argument("Folder name must be one non-empty path component.");
                        const auto destination = RenamingFolder.parent_path() / RenameBuffer;
                        editor.m_AssetDatabase->MoveFolder(RenamingFolder, destination);
                        RecordFolderMove(editor.m_AssetDatabase, RenamingFolder, destination, "Rename Folder");
                        if (SameOrChild(RenamingFolder, CurrentFolder))
                        {
                            const auto suffix = CurrentFolder.lexically_relative(RenamingFolder);
                            CurrentFolder = suffix.empty() ? destination : destination / suffix;
                        }
                        editor.m_AssetStatus = "Renamed folder.";
                        RenamingFolder.clear();
                        RenameBuffer.clear();
                        ui.CloseCurrentPopup();
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Folder rename failed: ") + error.what());
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
        }

        void CopyText(EditorWorkspaceLayer& editor, const std::string_view value)
        {
            try
            {
                editor.Owner().Windows()->SetClipboardText(value);
                editor.m_AssetStatus = "Copied to clipboard.";
            }
            catch (const std::exception& error)
            {
                editor.SetAssetError(std::string("Clipboard operation failed: ") + error.what());
            }
        }

        void RevealPath(EditorWorkspaceLayer& editor, const std::filesystem::path& path)
        {
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(path, diagnostic))
                editor.SetAssetError("Reveal failed: " + diagnostic);
        }

        void DrawAssetContext(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, EditorWorkspaceLayer& editor,
                              const std::string_view id)
        {
            if (auto context = ui.BeginItemContextMenu(id); context)
            {
                SelectOnlyIfNeeded(record.Id, editor);
                if (ui.MenuItem("Open"))
                    Open(record, editor);
                if (ui.MenuItem("Rename", false, Selection.size() == 1))
                    BeginAssetRename(record);
                if (ui.MenuItem("Duplicate"))
                    DuplicateAssets(editor);
                if (ui.MenuItem("Cut"))
                    SetClipboard(ClipboardMode::Cut, Selection);
                if (ui.MenuItem("Copy"))
                    SetClipboard(ClipboardMode::Copy, Selection);
                if (ui.MenuItem("Delete"))
                    RequestDeleteAssets(editor);
                ui.Separator();
                if (ui.MenuItem("Reimport"))
                    editor.ImportAssets();
                if (ui.MenuItem("Reveal in File Explorer"))
                    RevealPath(editor, AssetRoot / record.RelativePath);
                if (ui.MenuItem("Copy Relative Path"))
                    CopyText(editor, (std::filesystem::path("Assets") / record.RelativePath).generic_string());
                if (ui.MenuItem("Copy Asset ID"))
                    CopyText(editor, record.Id.ToString());
            }
        }

        void DrawFolderContext(Keire::UiFrame& ui, const std::filesystem::path& folder, EditorWorkspaceLayer& editor,
                               const std::string_view id)
        {
            if (auto context = ui.BeginItemContextMenu(id); context)
            {
                if (ui.MenuItem("Open"))
                    CurrentFolder = folder;
                if (auto create = ui.BeginMenu("Create"); create)
                {
                    const auto previous = std::exchange(CurrentFolder, folder);
                    DrawCreateItems(ui, editor);
                    CurrentFolder = previous;
                }
                if (ui.MenuItem("Rename"))
                    BeginFolderRename(folder);
                if (ui.MenuItem("Duplicate"))
                {
                    try
                    {
                        const auto destination =
                            UniqueFolder(folder.parent_path() / (folder.filename().string() + " Copy"));
                        (void)editor.m_AssetDatabase->DuplicateFolder(folder, destination);
                        RecordCreatedFolder(editor.m_AssetDatabase, destination);
                        editor.m_AssetStatus = "Duplicated folder.";
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Folder duplication failed: ") + error.what());
                    }
                }
                if (ui.MenuItem("Cut"))
                    SetFolderClipboard(ClipboardMode::Cut, folder);
                if (ui.MenuItem("Copy"))
                    SetFolderClipboard(ClipboardMode::Copy, folder);
                if (ui.MenuItem("Paste Into", false, ClipboardModeValue != ClipboardMode::Empty))
                    Paste(folder, editor);
                if (ui.MenuItem("Delete"))
                    RequestDeleteFolder(folder);
                ui.Separator();
                if (ui.MenuItem("Reimport Recursively"))
                    editor.ImportAssets();
                if (ui.MenuItem("Reveal in File Explorer"))
                    RevealPath(editor, AssetRoot / folder);
                if (ui.MenuItem("Copy Relative Path"))
                    CopyText(editor, (std::filesystem::path("Assets") / folder).generic_string());
            }
        }

        void DrawAssetTooltip(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, EditorWorkspaceLayer& editor)
        {
            std::error_code error;
            const auto bytes = std::filesystem::file_size(AssetRoot / record.RelativePath, error);
            const auto status = editor.m_AssetDatabase->ImportStatus(record.Id);
            std::ostringstream text;
            text << record.RelativePath.filename().string() << '\n'
                 << AssetTypeName(record) << '\n'
                 << "Assets/" << record.RelativePath.generic_string() << '\n';
            if (!error)
                text << bytes << " bytes\n";
            text << "ID: " << record.Id.ToString() << '\n' << "Importer: " << record.Importer;
            if (status.State == Keire::AssetImportState::Failed && !status.Diagnostics.empty())
                text << "\nImport failed: " << status.Diagnostics.front().Message;
            ui.SetTooltip(text.str(), {.Delayed = true});
        }

        void DrawAssetDragSource(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record)
        {
            if (auto source = ui.BeginDragSource(); source)
            {
                const bool selected = std::ranges::find(Selection, record.Id) != Selection.end();
                const auto payloadAssets = selected ? Selection : std::vector<Keire::AssetId>{record.Id};
                const auto value = EncodeAssetPayload(payloadAssets);
                ui.SetDragPayload("KEIRE_ASSETS", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(payloadAssets.size() == 1 ? DisplayName(record.RelativePath)
                                                  : std::to_string(payloadAssets.size()) + " assets");
            }
        }

        void DrawAsset(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, EditorWorkspaceLayer& editor,
                       const bool grid)
        {
            auto id = ui.PushId(record.Id.ToString());
            const auto image = Images.contains(record.Id) ? Images.at(record.Id) : FolderImage;
            const bool selected = std::ranges::find(Selection, record.Id) != Selection.end();
            bool open = false;
            if (grid)
            {
                if (ui.ImageButton("Thumbnail", image, {ThumbnailSize, ThumbnailSize}))
                    Select(record.Id, ui.ControlDown(), editor);
                open = ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "ThumbnailContext");
                if (ui.Selectable(DisplayName(record.RelativePath), selected))
                    Select(record.Id, ui.ControlDown(), editor);
                open |= ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "LabelContext");
                DrawAssetTooltip(ui, record, editor);
            }
            else
            {
                if (ui.ImageButton("Thumbnail", image, {32.0F, 32.0F}))
                    Select(record.Id, ui.ControlDown(), editor);
                open = ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "ThumbnailContext");
                ui.SameLine();
                if (ui.Selectable(DisplayName(record.RelativePath), selected))
                    Select(record.Id, ui.ControlDown(), editor);
                open |= ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "LabelContext");
                DrawAssetTooltip(ui, record, editor);
            }
            if (editor.m_AssetDatabase->ImportStatus(record.Id).State == Keire::AssetImportState::Failed)
                ui.TextColored(editor.m_Theme.Error, "Import error");
            if (open)
                Open(record, editor);
            if (RevealAsset == record.Id)
                RevealAsset = {};
        }

        void AcceptFolderDrop(Keire::UiFrame& ui, const std::filesystem::path& folder, EditorWorkspaceLayer& editor)
        {
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        MoveAssets(DecodeAssetPayload(payload), folder, editor);
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Asset move failed: ") + error.what());
                    }
                }
                payload.clear();
                if (ui.AcceptDragPayload("KEIRE_FOLDER", payload))
                {
                    try
                    {
                        const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
                        const std::filesystem::path source(text);
                        const auto destination = folder / source.filename();
                        editor.m_AssetDatabase->MoveFolder(source, destination);
                        RecordFolderMove(editor.m_AssetDatabase, source, destination, "Move Folder");
                        editor.m_AssetStatus = "Moved folder.";
                    }
                    catch (const std::exception& error)
                    {
                        editor.SetAssetError(std::string("Folder move failed: ") + error.what());
                    }
                }
            }
        }

        void DrawFolder(Keire::UiFrame& ui, const std::filesystem::path& folder, EditorWorkspaceLayer& editor,
                        const bool grid)
        {
            auto id = ui.PushId(folder.generic_string());
            const auto drawDragSource = [&]
            {
                if (auto source = ui.BeginDragSource(); source)
                {
                    const auto value = folder.generic_string();
                    ui.SetDragPayload("KEIRE_FOLDER", std::as_bytes(std::span(value.data(), value.size())));
                    ui.Text(folder.filename().string());
                }
            };
            if (grid)
            {
                if (ui.ImageButton("Folder", FolderImage, {ThumbnailSize, ThumbnailSize}))
                    CurrentFolder = folder;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderImageContext");
                if (ui.Selectable(folder.filename().string()))
                    CurrentFolder = folder;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderLabelContext");
                ExternalDropTargets.push_back({ui.LastItemRect(), folder});
                AcceptFolderDrop(ui, folder, editor);
                ui.SetTooltip((std::filesystem::path("Assets") / folder).generic_string(), {.Delayed = true});
            }
            else
            {
                if (ui.ImageButton("Folder", FolderImage, {32.0F, 32.0F}))
                    CurrentFolder = folder;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderImageContext");
                ui.SameLine();
                if (ui.Selectable(folder.filename().string()))
                    CurrentFolder = folder;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderLabelContext");
                ExternalDropTargets.push_back({ui.LastItemRect(), folder});
                AcceptFolderDrop(ui, folder, editor);
                ui.SetTooltip((std::filesystem::path("Assets") / folder).generic_string(), {.Delayed = true});
            }
        }

        void DrawFolderTree(Keire::UiFrame& ui, const std::filesystem::path& relative, EditorWorkspaceLayer& editor)
        {
            std::error_code error;
            std::vector<std::filesystem::path> children;
            for (std::filesystem::directory_iterator iterator(AssetRoot / relative, error), end;
                 !error && iterator != end; iterator.increment(error))
                if (iterator->is_directory(error))
                    children.push_back(std::filesystem::relative(iterator->path(), AssetRoot, error));
            std::ranges::sort(children);
            for (const auto& child : children)
            {
                auto id = ui.PushId(child.generic_string());
                auto node = ui.BeginTreeNode(child.filename().string());
                const auto item = ui.LastItemState();
                ExternalDropTargets.push_back({ui.LastItemRect(), child});
                DrawFolderContext(ui, child, editor, "TreeContext");
                AcceptFolderDrop(ui, child, editor);
                if (item.Activated || item.DoubleClicked)
                    CurrentFolder = child;
                if (node)
                    DrawFolderTree(ui, child, editor);
            }
        }

        void DrawFolderPane(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (ui.Selectable("Assets", CurrentFolder.empty()))
                CurrentFolder.clear();
            ExternalDropTargets.push_back({ui.LastItemRect(), {}});
            AcceptFolderDrop(ui, {}, editor);
            DrawFolderTree(ui, {}, editor);
        }

        void DrawBreadcrumbs(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            std::optional<std::filesystem::path> requestedFolder;
            if (ui.Button("Assets"))
                requestedFolder = std::filesystem::path{};
            AcceptFolderDrop(ui, {}, editor);

            const auto displayedFolder = CurrentFolder;
            std::filesystem::path breadcrumb;
            for (const auto& part : displayedFolder)
            {
                breadcrumb /= part;
                ui.SameLine();
                ui.Text(">");
                ui.SameLine();
                auto id = ui.PushId(breadcrumb.generic_string());
                const auto label = part.string();
                if (!label.empty() && ui.Button(label))
                    requestedFolder = breadcrumb;
                AcceptFolderDrop(ui, breadcrumb, editor);
            }
            if (requestedFolder)
                CurrentFolder = std::move(*requestedFolder);
            ui.Separator();
        }

        void DrawKeyboardCommands(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord* const> visible,
                                  EditorWorkspaceLayer& editor)
        {
            if (!ui.WindowFocused())
                return;
            if (ui.Shortcut({.Key = Keire::UiKey::A, .Primary = true}))
            {
                Selection.clear();
                for (const auto* record : visible)
                    Selection.push_back(record->Id);
                editor.m_SelectedAsset = Selection.empty() ? Keire::AssetId{} : Selection.back();
            }
            if (ui.Shortcut({.Key = Keire::UiKey::D, .Primary = true}))
                DuplicateAssets(editor);
            if (ui.Shortcut({.Key = Keire::UiKey::X, .Primary = true}))
                SetClipboard(ClipboardMode::Cut, Selection);
            if (ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
                SetClipboard(ClipboardMode::Copy, Selection);
            if (ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}))
                Paste(CurrentFolder, editor);
            if (ui.Shortcut({Keire::UiKey::Delete}))
                RequestDeleteAssets(editor);
            if (ui.Shortcut({Keire::UiKey::F2}) && Selection.size() == 1)
                if (const auto record = editor.m_AssetDatabase->Find(Selection.front()))
                    BeginAssetRename(*record);
            if (ui.Shortcut({Keire::UiKey::Enter}) && Selection.size() == 1)
                if (const auto record = editor.m_AssetDatabase->Find(Selection.front()))
                    Open(*record, editor);
            if (ui.Shortcut({Keire::UiKey::Backspace}) && !CurrentFolder.empty())
                CurrentFolder = CurrentFolder.parent_path();
        }

        void DrawBlankContext(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            if (auto context = ui.BeginWindowContextMenu("AssetBlankContext"); context)
            {
                if (auto create = ui.BeginMenu("Create"); create)
                    DrawCreateItems(ui, editor);
                if (ui.MenuItem("Paste", false, ClipboardModeValue != ClipboardMode::Empty))
                    Paste(CurrentFolder, editor);
                ui.Separator();
                if (ui.MenuItem("Refresh and Reimport"))
                    editor.ImportAssets();
                if (ui.MenuItem("Reveal in File Explorer"))
                    RevealPath(editor, AssetRoot / CurrentFolder);
                if (ui.MenuItem("Open Trash"))
                    OpenTrashPopup = true;
            }
        }

        void DrawContentPane(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            DrawBreadcrumbs(ui, editor);

            const auto folders = Folders();
            std::vector<const Keire::AssetSourceRecord*> assets;
            for (const auto& record : editor.m_AssetRecords)
                if (record.RelativePath.parent_path() == CurrentFolder &&
                    (Search.empty() || record.RelativePath.filename().string().find(Search) != std::string::npos))
                    assets.push_back(&record);
            std::ranges::sort(assets, {}, [](const auto* record) { return record->RelativePath.filename(); });

            if (Mode == ViewMode::List)
            {
                for (const auto& folder : folders)
                    if (Search.empty() || folder.filename().string().find(Search) != std::string::npos)
                        DrawFolder(ui, folder, editor, false);
                for (const auto* record : assets)
                    DrawAsset(ui, *record, editor, false);
                DrawKeyboardCommands(ui, assets, editor);
                DrawBlankContext(ui, editor);
                return;
            }

            const float availableWidth = std::max(ui.ContentAvailable().Width, 0.0F);
            if (availableWidth > 1.0F)
            {
                const float cellWidth = std::max(ThumbnailSize + 28.0F, 1.0F);
                const auto calculatedColumns = static_cast<std::size_t>(std::floor(availableWidth / cellWidth));
                const auto columns = std::clamp<std::size_t>(calculatedColumns, 1, 32);
                const Keire::UiTableOptions gridOptions{.Sizing = Keire::UiTableSizing::Equal,
                                                        .Borders = false,
                                                        .Resizable = false,
                                                        .RowBackground = false,
                                                        .PersistSettings = false};
                const auto tableId = "AssetContent-" + std::to_string(columns);
                if (auto table = ui.BeginTable(tableId, columns, gridOptions); table)
                {
                    std::size_t item = 0;
                    const auto nextCell = [&]
                    {
                        if (item % columns == 0)
                            ui.TableNextRow();
                        (void)ui.TableNextColumn();
                        ++item;
                    };
                    for (const auto& folder : folders)
                    {
                        if (!Search.empty() && folder.filename().string().find(Search) == std::string::npos)
                            continue;
                        nextCell();
                        DrawFolder(ui, folder, editor, true);
                    }
                    for (const auto* record : assets)
                    {
                        nextCell();
                        DrawAsset(ui, *record, editor, true);
                    }
                }
            }
            DrawKeyboardCommands(ui, assets, editor);
            DrawBlankContext(ui, editor);
        }

        void Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
        {
            Focused = false;
            ExternalDropTargets.clear();
            if (auto project = ui.BeginPanel(editor.m_Project); project)
            {
                Focused = ui.WindowFocused();
                ui.TextColored(editor.m_Theme.Accent, "ASSET BROWSER");
                ui.Separator();
                if (!editor.m_AssetDatabase || !Thumbnails)
                {
                    ui.TextColored(editor.m_Theme.Error, editor.m_AssetStatus.empty() ? "Asset database is unavailable."
                                                                                      : editor.m_AssetStatus);
                    return;
                }
                // External imports publish source files before their cooked catalog is ready. Keep displaying the
                // last published snapshot while that transaction is running so thumbnails cannot create failed
                // handles for asset IDs that are not mountable yet.
                if (!editor.m_ExternalAssetImport || !editor.m_ExternalAssetImport->Pending())
                    editor.m_AssetRecords = editor.m_AssetDatabase->Records();
                if (!FolderImage)
                {
                    const auto pixels = MakeFolderThumbnail(96, 96);
                    FolderImage = ui.CreateImage(96, 96, pixels);
                }
                for (auto& completed : Thumbnails->DrainCompleted())
                    Images[completed.Asset] = ui.CreateImage(completed.Width, completed.Height, completed.Pixels);
                for (const auto& record : editor.m_AssetRecords)
                {
                    std::string digest = record.SourceDigest + record.MetadataDigest;
                    for (const auto dependency : record.Dependencies)
                    {
                        digest += dependency.ToString();
                        const auto dependencyRecord =
                            std::ranges::find(editor.m_AssetRecords, dependency, &Keire::AssetSourceRecord::Id);
                        if (dependencyRecord != editor.m_AssetRecords.end())
                            digest += dependencyRecord->SourceDigest + dependencyRecord->MetadataDigest;
                    }
                    if (const auto found = ImageDigests.find(record.Id);
                        found != ImageDigests.end() && found->second != digest)
                    {
                        Images.erase(record.Id);
                        ImageDigests.erase(found);
                    }
                    if (Images.contains(record.Id) || ImageDigests.contains(record.Id))
                        continue;

                    ThumbnailRequest request;
                    request.Asset = record.Id;
                    request.Type = record.Type;
                    request.RelativePath = record.RelativePath;
                    request.Digest = digest;
                    const auto assets = editor.Owner().Assets();
                    bool ready = true;
                    if (assets && record.Type == Keire::Texture2DAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::Texture2DAsset>(record.Id, Keire::AssetPriority::Low);
                        request.PreviewAsset = handle.TryGetLoaded();
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                    }
                    else if (assets && record.Type == Keire::MeshAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::MeshAsset>(record.Id, Keire::AssetPriority::Low);
                        request.PreviewAsset = handle.TryGetLoaded();
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                    }
                    else if (assets && record.Type == Keire::MaterialAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::MaterialAsset>(record.Id, Keire::AssetPriority::Low);
                        const auto material = handle.TryGetLoaded();
                        request.PreviewAsset = material;
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                        if (material && material->Definition().Shader)
                        {
                            const auto shaderHandle = assets->Load<Keire::ShaderAsset>(material->Definition().Shader,
                                                                                       Keire::AssetPriority::Low);
                            request.PreviewShader = shaderHandle.TryGetLoaded();
                            if (!request.PreviewShader && shaderHandle.State() == Keire::AssetState::Failed)
                                request.PreviewShader = shaderHandle.Get();
                            ready = ready && static_cast<bool>(request.PreviewShader);
                        }
                        Keire::AssetId texture;
                        if (material)
                        {
                            if (const auto found = material->Definition().Properties.find("MainTexture");
                                found != material->Definition().Properties.end())
                                if (const auto* id = std::get_if<Keire::AssetId>(&found->second))
                                    texture = *id;
                            if (request.PreviewShader)
                            {
                                for (const auto& property : request.PreviewShader->Definition().Properties)
                                {
                                    if (property.TextureSemantic != Keire::ShaderTextureSemantic::BaseColor)
                                        continue;
                                    if (const auto found = material->Definition().Properties.find(property.Name);
                                        found != material->Definition().Properties.end())
                                        if (const auto* id = std::get_if<Keire::AssetId>(&found->second))
                                            texture = *id;
                                    break;
                                }
                            }
                        }
                        if (texture)
                        {
                            const auto textureHandle =
                                assets->Load<Keire::Texture2DAsset>(texture, Keire::AssetPriority::Low);
                            request.PreviewTexture = textureHandle.TryGetLoaded();
                            if (!request.PreviewTexture && textureHandle.State() == Keire::AssetState::Failed)
                                request.PreviewTexture = textureHandle.Get();
                            ready = ready && static_cast<bool>(request.PreviewTexture);
                        }
                    }
                    if (ready && Thumbnails->Request(std::move(request)))
                        ImageDigests.emplace(record.Id, std::move(digest));
                }

                if (ui.Button("Create"))
                    ui.OpenPopup("AssetCreateMenu");
                if (auto create = ui.BeginPopup("AssetCreateMenu"); create)
                    DrawCreateItems(ui, editor);
                ui.SameLine();
                if (ui.Button("Refresh"))
                    editor.ImportAssets();
                ui.SameLine();
                if (ui.Button(Mode == ViewMode::List ? "Grid" : "List"))
                {
                    Mode = Mode == ViewMode::List ? ViewMode::Grid : ViewMode::List;
                    SavePreferences();
                }
                ui.SameLine();
                if (ui.Button("Trash"))
                    OpenTrashPopup = true;
                (void)ui.InputText("Search Assets", Search);
                if (Mode == ViewMode::Grid && ui.SliderFloat("Thumbnail Size", ThumbnailSize, 48.0F, 160.0F) &&
                    ui.LastItemState().DeactivatedAfterEdit)
                    SavePreferences();

                ui.Separator();
                const auto browserSize = ui.ContentAvailable();
                constexpr float splitterThickness = 4.0F;
                constexpr float minimumFolderWidth = 150.0F;
                constexpr float minimumContentWidth = 260.0F;
                const float footerHeight = editor.m_AssetStatus.empty() ? 30.0F : 54.0F;
                const float paneHeight = std::max(browserSize.Height - footerHeight, 1.0F);
                const bool showFolderPane =
                    browserSize.Width >= minimumFolderWidth + minimumContentWidth + splitterThickness;
                if (showFolderPane)
                {
                    FolderPaneWidth = std::clamp(FolderPaneWidth, minimumFolderWidth,
                                                 browserSize.Width - minimumContentWidth - splitterThickness);
                    float contentPaneWidth = browserSize.Width - FolderPaneWidth - splitterThickness;
                    if (auto foldersPane = ui.BeginChild("Folders", {FolderPaneWidth, paneHeight}, true); foldersPane)
                        DrawFolderPane(ui, editor);
                    ui.SameLine();
                    (void)ui.Splitter(Keire::UiAxis::Horizontal, "AssetBrowserSplitter", FolderPaneWidth,
                                      contentPaneWidth, minimumFolderWidth, minimumContentWidth, splitterThickness);
                    ui.SameLine();
                }
                if (auto contentPane = ui.BeginChild("AssetContentPane", {0.0F, paneHeight}); contentPane)
                    DrawContentPane(ui, editor);

                DrawRenamePopups(ui, editor);
                DrawDeletePopup(ui, editor);
                DrawTrashPopup(ui, editor);
                if (!editor.m_AssetStatus.empty())
                    ui.TextColored(editor.m_Theme.MutedText, editor.m_AssetStatus);
                ui.TextColored(editor.m_Theme.MutedText, std::to_string(Selection.size()) + " selected  |  " +
                                                             std::to_string(Thumbnails->PendingCount()) +
                                                             " thumbnail request(s)");
            }
        }

        std::filesystem::path ProjectRoot;
        std::filesystem::path AssetRoot;
        std::filesystem::path CurrentFolder;
        std::filesystem::path PreferencePath;
        std::filesystem::path RenamingFolder;
        std::filesystem::path PendingDeleteFolder;
        std::unique_ptr<ThumbnailService> Thumbnails;
        std::unordered_map<Keire::AssetId, Keire::Ref<Keire::UiImage>> Images;
        std::unordered_map<Keire::AssetId, std::string> ImageDigests;
        Keire::Ref<Keire::UiImage> FolderImage;
        Keire::Ref<Keire::UndoContext> Undo;
        std::vector<Keire::AssetId> Selection;
        std::vector<Keire::AssetId> PendingDeleteAssets;
        std::vector<ClipboardEntry> Clipboard;
        std::vector<ExternalDropTarget> ExternalDropTargets;
        Keire::AssetId Renaming;
        Keire::AssetId RevealAsset;
        std::string Search;
        std::string RenameBuffer;
        std::string MaterialNameBuffer;
        ViewMode Mode = ViewMode::Grid;
        ClipboardMode ClipboardModeValue = ClipboardMode::Empty;
        float ThumbnailSize = 88.0F;
        float FolderPaneWidth = 210.0F;
        bool OpenRenamePopup = false;
        bool OpenMaterialCreatePopup = false;
        bool OpenFolderRenamePopup = false;
        bool OpenDeletePopup = false;
        bool OpenTrashPopup = false;
        bool Focused = false;
    };

    AssetBrowserPanel::AssetBrowserPanel() : m_Impl(std::make_unique<Impl>()) {}
    AssetBrowserPanel::~AssetBrowserPanel() { Close(); }
    void AssetBrowserPanel::SetProjectRoot(const std::filesystem::path& root) { m_Impl->SetProjectRoot(root); }
    void AssetBrowserPanel::SetUndoContext(Keire::Ref<Keire::UndoContext> context)
    {
        m_Impl->SetUndoContext(std::move(context));
    }
    Keire::Ref<Keire::UndoContext> AssetBrowserPanel::UndoContext() const { return m_Impl->Undo; }
    bool AssetBrowserPanel::Focused() const noexcept { return m_Impl->Focused; }
    std::filesystem::path AssetBrowserPanel::CurrentFolder() const { return m_Impl->CurrentFolder; }
    std::filesystem::path AssetBrowserPanel::ResolveExternalDropFolder(const Keire::UiPosition position) const
    {
        return m_Impl->ResolveExternalDropFolder(position);
    }

    std::vector<Keire::AssetId> AssetBrowserPanel::DecodeDragPayload(const std::span<const std::byte> bytes)
    {
        return DecodeAssetPayload(bytes);
    }
    void AssetBrowserPanel::InvalidateThumbnail(const Keire::AssetId asset) { m_Impl->InvalidateThumbnail(asset); }
    void AssetBrowserPanel::RevealAsset(const Keire::AssetId asset, EditorWorkspaceLayer& editor)
    {
        m_Impl->Reveal(asset, editor);
    }
    void AssetBrowserPanel::RequestCreateMaterial() { m_Impl->RequestCreateMaterial(); }
    void AssetBrowserPanel::RecordCreatedAsset(const Keire::Ref<Keire::AssetDatabase>& database,
                                               const Keire::AssetId asset, std::string name)
    {
        m_Impl->RecordCreatedAsset(database, asset, std::move(name));
    }
    void AssetBrowserPanel::Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor) { m_Impl->Draw(ui, editor); }
    void AssetBrowserPanel::Close() noexcept
    {
        if (m_Impl)
            m_Impl->Close();
    }
} // namespace KeireEditor
