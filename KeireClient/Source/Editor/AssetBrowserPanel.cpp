#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireClient/Editor/AssetBrowserFolderCache.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SelectionRange.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <unordered_map>
#include <utility>

namespace KeireEditor
{
    class AssetBrowserPanel::Impl final
    {
      public:
        explicit Impl(IAssetBrowserController& controller) noexcept : Controller(controller) {}

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

        enum class NamedCreateKind : std::uint8_t
        {
            None,
            Scene,
            Material,
            AnimationGraph,
            Script,
            ManagedAssembly,
            ManagedData,
            AudioMixer,
            PhysicsMaterial,
            VfxEffect,
            MaterialGraph,
            MaterialGraphInstance,
            Prefab,
            PrefabVariant,
            Shader,
            InputActions
        };

        struct ClipboardEntry final
        {
            Keire::AssetId Asset;
            std::filesystem::path Folder;
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
            Thumbnails = std::make_unique<ThumbnailService>(ProjectRoot / "Library" / "Thumbnails", 256, Scheduler);
            LoadPreferences();
            RefreshFolderCache(true);
        }

        void SetUndoContext(Keire::Ref<Keire::UndoContext> context) { Undo = std::move(context); }

        void RequestCreateMaterial() { RequestNamedCreate(NamedCreateKind::Material, "Material"); }

        void RequestNamedCreate(const NamedCreateKind kind, const std::string_view defaultName)
        {
            PendingCreateKind = kind;
            PendingCreateFolder = CurrentFolder;
            CreateNameBuffer = defaultName;
            OpenNamedCreatePopup = true;
        }

        void RequestInputActionsCreate(Keire::InputActionAssetDefinition definition, const std::string_view defaultName)
        {
            PendingInputActions = std::move(definition);
            RequestNamedCreate(NamedCreateKind::InputActions, defaultName);
        }

        void RequestManagedDataCreate(const Keire::ManagedAssetTypeDescriptor& descriptor)
        {
            PendingManagedType = descriptor.StableTypeId;
            RequestNamedCreate(NamedCreateKind::ManagedData, descriptor.DefaultFileName);
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
            MaterialGraphFallbackImage.Reset();
            MaterialInstanceFallbackImage.Reset();
            VfxFallbackImage.Reset();
            Selection.clear();
            VisibleSelectionOrder.clear();
            SelectionAnchor = {};
            PendingVariantBase = {};
            Clipboard.clear();
            Undo.Reset();
            Renaming = {};
            RenamingFolder.clear();
            RenameBuffer.clear();
            CreateNameBuffer.clear();
            PendingCreateFolder.clear();
            PendingManagedType = {};
            PendingInputActions.reset();
            PendingCreateKind = NamedCreateKind::None;
            ExternalEditorBuffer.clear();
            ExternalEditor.clear();
            CurrentFolder.clear();
            ProjectRoot.clear();
            AssetRoot.clear();
            FolderCache.Clear();
            ObservedRecordRevision = 0;
            NextFolderRefresh = {};
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
                else if (line.starts_with("editor="))
                    ExternalEditor = Keire::Detail::PathFromUtf8(line.substr(7));
            }
        }

        void SavePreferences() noexcept
        {
            if (PreferencePath.empty())
                return;
            try
            {
                const auto text = std::string("view=") + (Mode == ViewMode::Grid ? "grid\n" : "list\n") +
                                  "size=" + std::to_string(ThumbnailSize) +
                                  "\neditor=" + Keire::Detail::PathToUtf8(ExternalEditor) + "\n";
                Keire::Detail::WriteTextFileAtomically(PreferencePath, text);
            }
            catch (...)
            {
            }
        }

        void RefreshFolderCache(const bool force)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!force && now < NextFolderRefresh)
                return;
            NextFolderRefresh = now + std::chrono::seconds(1);
            (void)FolderCache.Refresh(AssetRoot);
        }

        [[nodiscard]] std::vector<std::filesystem::path> Folders(const std::filesystem::path& parent) const
        {
            std::vector<std::filesystem::path> result;
            for (const auto& folder : FolderCache.Folders())
                if (folder.parent_path() == parent)
                    result.push_back(folder);
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
                                                        IAssetBrowserController& editor) const
        {
            const auto stem = source.RelativePath.stem().string();
            const auto extension = source.RelativePath.extension().string();
            auto copyName = stem;
            copyName.append(" Copy").append(extension);
            auto destination = folder / copyName;
            for (std::size_t copy = 2; editor.AssetBrowserDatabase()->Find(destination); ++copy)
            {
                copyName = stem;
                copyName.append(" Copy ").append(std::to_string(copy)).append(extension);
                destination = folder / copyName;
            }
            return destination;
        }

        void Select(const Keire::AssetId asset, const bool additive, IAssetBrowserController& editor)
        {
            if (!additive)
                Selection.clear();
            const auto found = std::ranges::find(Selection, asset);
            if (additive && found != Selection.end())
                Selection.erase(found);
            else if (found == Selection.end())
                Selection.push_back(asset);
            SelectionAnchor = asset;
            editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            editor.ClearAssetBrowserSceneSelection();
        }

        void SelectFromClick(const Keire::AssetId asset, Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (!ui.ShiftDown() || !SelectionAnchor)
            {
                Select(asset, ui.ControlDown(), editor);
                return;
            }
            Selection = BuildRangeSelection(VisibleSelectionOrder, SelectionAnchor, asset, Selection, ui.ControlDown());
            editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            editor.ClearAssetBrowserSceneSelection();
        }

        void SelectOnlyIfNeeded(const Keire::AssetId asset, IAssetBrowserController& editor)
        {
            if (std::ranges::find(Selection, asset) == Selection.end())
                Select(asset, false, editor);
        }

        void Reveal(const Keire::AssetId asset, IAssetBrowserController& editor)
        {
            if (const auto record =
                    editor.AssetBrowserDatabase() ? editor.AssetBrowserDatabase()->Find(asset) : std::nullopt)
            {
                CurrentFolder = record->RelativePath.parent_path();
                Select(asset, false, editor);
                RevealAsset = asset;
            }
        }

        void Open(const Keire::AssetSourceRecord& record, IAssetBrowserController& editor)
        {
            try
            {
                if (record.RelativePath.extension() == ".keireinput")
                    editor.OpenAssetBrowserInputActions(record.Id);
                else if (record.RelativePath.extension() == ".keireanimgraph")
                    editor.OpenAssetBrowserAnimationGraph(record.Id);
                else if (record.RelativePath.extension() == ".keiremixer")
                    editor.OpenAssetBrowserAudioMixer(record.Id);
                else if (record.RelativePath.extension() == ".keirevfx")
                    editor.OpenAssetBrowserVfxEffect(record.Id);
                else if (record.RelativePath.extension() == ".keirematerialgraph")
                    editor.OpenAssetBrowserMaterialGraph(record.Id);
                else if (record.RelativePath.extension() == ".keirescene")
                    editor.OpenAssetBrowserScene(record.Id);
                else if (record.RelativePath.extension() == ".keireprefab")
                    editor.OpenAssetBrowserPrefab(record.Id);
                else
                {
                    editor.PrepareAssetBrowserExternalOpen(record.Id);
                    std::string diagnostic;
                    if (!Keire::Detail::OpenInExternalEditor(AssetRoot / record.RelativePath, ExternalEditor,
                                                             ProjectRoot, diagnostic))
                        throw std::runtime_error(diagnostic);
                    editor.SetAssetBrowserStatus("Opened " + record.RelativePath.filename().string() +
                                                 " in an external editor.");
                }
            }
            catch (const std::exception& error)
            {
                editor.ReportAssetBrowserError(std::string("Asset open failed: ") + error.what());
            }
        }

        void CreateFolder(IAssetBrowserController& editor)
        {
            try
            {
                const auto folder = UniqueFolder(CurrentFolder / "New Folder");
                editor.MutateAssetBrowser(
                    {.Kind = Keire::Detail::AssetWorkerMutationKind::CreateFolder, .Destination = folder}, {},
                    "Create Folder");
                RenamingFolder = folder;
                RenameBuffer = folder.filename().string();
                OpenFolderRenamePopup = true;
                editor.SetAssetBrowserStatus("Created " + folder.generic_string() + ".");
            }
            catch (const std::exception& error)
            {
                editor.ReportAssetBrowserError(std::string("Folder creation failed: ") + error.what());
            }
        }

        void DrawCreateItems(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (ui.MenuItem("Folder"))
                CreateFolder(editor);
            if (ui.MenuItem("Scene"))
                RequestNamedCreate(NamedCreateKind::Scene, "NewScene");
            if (ui.MenuItem("Material"))
                RequestCreateMaterial();
            if (ui.MenuItem("Animator Controller"))
                RequestNamedCreate(NamedCreateKind::AnimationGraph, "NewAnimatorController");
            if (ui.MenuItem("C# Script"))
                RequestNamedCreate(NamedCreateKind::Script, "NewBehaviour");
            if (ui.MenuItem("Managed Assembly"))
                RequestNamedCreate(NamedCreateKind::ManagedAssembly, "Gameplay");
            if (ui.MenuItem("Audio Mixer"))
                RequestNamedCreate(NamedCreateKind::AudioMixer, "MainMixer");
            if (ui.MenuItem("Physics Material"))
                RequestNamedCreate(NamedCreateKind::PhysicsMaterial, "PhysicsMaterial");
            if (ui.MenuItem("VFX Effect"))
                RequestNamedCreate(NamedCreateKind::VfxEffect, "VfxEffect");
            if (ui.MenuItem("Material Graph"))
                RequestNamedCreate(NamedCreateKind::MaterialGraph, "PBRMaterial");
            if (ui.MenuItem("Material Instance"))
                RequestNamedCreate(NamedCreateKind::MaterialGraphInstance, "MaterialInstance");
            const auto managedTypes = editor.AssetBrowserManagedAssetTypes();
            if (std::ranges::any_of(managedTypes, [](const auto& type) { return !type.MenuPath.empty(); }))
            {
                if (auto managedData = ui.BeginMenu("Managed Data"); managedData)
                {
                    for (const auto& type : managedTypes)
                        if (!type.MenuPath.empty() && ui.MenuItem(type.MenuPath))
                            RequestManagedDataCreate(type);
                }
            }
            if (ui.MenuItem("Prefab from Selection"))
                RequestNamedCreate(NamedCreateKind::Prefab, "NewPrefab");
            if (ui.MenuItem("Unlit Shader"))
                RequestNamedCreate(NamedCreateKind::Shader, "UnlitShader");
            if (auto input = ui.BeginMenu("Input Actions"); input)
            {
                if (ui.MenuItem("Empty"))
                    RequestInputActionsCreate({.SchemaVersion = 1, .Name = "InputActions"}, "InputActions");
                if (ui.MenuItem("Default"))
                    RequestInputActionsCreate(Keire::InputActionAsset::DefaultDefinition(), "DefaultInput");
                if (ui.MenuItem("3D Gameplay"))
                    RequestInputActionsCreate(Keire::InputActionAsset::GameplayDefinition(), "GameplayInput");
                if (ui.MenuItem("UI Navigation"))
                    RequestInputActionsCreate(Keire::InputActionAsset::UiDefinition(), "UiInput");
            }
        }

        void DuplicateAssets(IAssetBrowserController& editor)
        {
            if (Selection.empty())
                return;
            try
            {
                std::size_t queued = 0;
                for (const auto asset : Selection)
                {
                    const auto record = editor.AssetBrowserDatabase()->Find(asset);
                    if (!record)
                        continue;
                    const auto destination = UniqueAsset(*record, record->RelativePath.parent_path(), editor);
                    editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateAsset,
                                               .Asset = asset,
                                               .Destination = destination},
                                              {}, "Duplicate Asset", true);
                    ++queued;
                }
                editor.SetAssetBrowserStatus("Queued " + std::to_string(queued) + " asset duplicate(s).");
            }
            catch (const std::exception& error)
            {
                editor.ReportAssetBrowserError(std::string("Asset duplication failed: ") + error.what());
            }
        }

        void MoveAssets(const std::span<const Keire::AssetId> assets, const std::filesystem::path& folder,
                        IAssetBrowserController& editor)
        {
            std::vector<std::pair<Keire::AssetSourceRecord, std::filesystem::path>> moves;
            std::set<std::string> destinations;
            for (const auto asset : assets)
            {
                const auto record = editor.AssetBrowserDatabase()->Find(asset);
                if (!record)
                    throw std::invalid_argument("Cannot move an asset that no longer exists.");
                const auto destination = (folder / record->RelativePath.filename()).lexically_normal();
                if (destination == record->RelativePath)
                    continue;
                if (editor.AssetBrowserDatabase()->Find(destination) ||
                    !destinations.insert(destination.generic_string()).second)
                    throw std::runtime_error("Asset move destination already exists: " + destination.generic_string());
                moves.emplace_back(*record, destination);
            }
            for (const auto& [record, destination] : moves)
            {
                editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                           .Asset = record.Id,
                                           .Destination = destination},
                                          {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                           .Asset = record.Id,
                                           .Destination = record.RelativePath},
                                          "Move Asset");
            }
            editor.SetAssetBrowserStatus("Queued " + std::to_string(moves.size()) + " asset move(s).");
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

        void Paste(const std::filesystem::path& folder, IAssetBrowserController& editor)
        {
            if (ClipboardModeValue == ClipboardMode::Empty || Clipboard.empty())
                return;
            try
            {
                for (const auto& entry : Clipboard)
                {
                    if (entry.Asset)
                    {
                        const auto record = editor.AssetBrowserDatabase()->Find(entry.Asset);
                        if (!record)
                            throw std::runtime_error("Clipboard asset no longer exists.");
                        if (ClipboardModeValue == ClipboardMode::Cut)
                        {
                            const auto destination = folder / record->RelativePath.filename();
                            editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = destination},
                                                      {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = record->RelativePath},
                                                      "Move Asset");
                        }
                        else
                        {
                            const auto destination = UniqueAsset(*record, folder, editor);
                            editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = destination},
                                                      {}, "Paste Asset", true);
                        }
                    }
                    else
                    {
                        const auto destinationBase = folder / entry.Folder.filename();
                        if (ClipboardModeValue == ClipboardMode::Cut)
                        {
                            editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                       .Source = entry.Folder,
                                                       .Destination = destinationBase},
                                                      {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                       .Source = destinationBase,
                                                       .Destination = entry.Folder},
                                                      "Move Folder");
                        }
                        else
                        {
                            const auto destination = UniqueFolder(destinationBase);
                            editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateFolder,
                                                       .Source = entry.Folder,
                                                       .Destination = destination},
                                                      {}, "Paste Folder");
                        }
                    }
                }
                if (ClipboardModeValue == ClipboardMode::Cut)
                {
                    Clipboard.clear();
                    ClipboardModeValue = ClipboardMode::Empty;
                }
                editor.SetAssetBrowserStatus("Pasted asset selection into " +
                                             (folder.empty() ? std::string("Assets") : folder.generic_string()) + ".");
            }
            catch (const std::exception& error)
            {
                editor.ReportAssetBrowserError(std::string("Asset paste failed: ") + error.what());
            }
        }

        void RequestDeleteAssets(IAssetBrowserController& editor)
        {
            if (Selection.empty())
                return;
            if (std::ranges::find(Selection, editor.AssetBrowserSceneAsset()) != Selection.end() &&
                editor.AssetBrowserSceneDirty())
            {
                editor.SetAssetBrowserStatus("Save or close the dirty scene before deleting its asset.");
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

        void DrawDeletePopup(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (OpenDeletePopup)
            {
                ui.OpenPopup("Delete Assets");
                OpenDeletePopup = false;
            }
            if (auto popup = ui.BeginPopupModal("Delete Assets"); popup)
            {
                ui.TextColored(editor.AssetBrowserTheme().Warning,
                               "Move the selected content to recoverable project trash?");
                ui.Text(PendingDeleteFolder.empty() ? std::to_string(PendingDeleteAssets.size()) + " asset(s) selected"
                                                    : PendingDeleteFolder.generic_string());
                if (ui.Button("Move to Trash"))
                {
                    try
                    {
                        if (!PendingDeleteFolder.empty())
                        {
                            editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::TrashFolder,
                                                       .Source = PendingDeleteFolder},
                                                      {}, "Delete Folder");
                        }
                        else
                        {
                            for (const auto asset : PendingDeleteAssets)
                            {
                                editor.MutateAssetBrowser(
                                    {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = asset}, {},
                                    "Delete Asset");
                            }
                        }
                        Selection.clear();
                        editor.SetAssetBrowserSelected({});
                        editor.SetAssetBrowserStatus("Moved selection to recoverable project trash.");
                        PendingDeleteAssets.clear();
                        PendingDeleteFolder.clear();
                        ui.CloseCurrentPopup();
                    }
                    catch (const std::exception& error)
                    {
                        editor.ReportAssetBrowserError(std::string("Asset delete failed: ") + error.what());
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

        void DrawTrashPopup(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (OpenTrashPopup)
            {
                try
                {
                    TrashEntries = editor.AssetBrowserDatabase()->TrashRecords();
                    TrashError.clear();
                }
                catch (const std::exception& error)
                {
                    TrashEntries.clear();
                    TrashError = error.what();
                }
                ui.OpenPopup("Asset Trash");
                OpenTrashPopup = false;
            }
            if (auto popup = ui.BeginPopupModal("Asset Trash"); popup)
            {
                ui.TextColored(editor.AssetBrowserTheme().Accent, "RECOVERABLE ASSET TRASH");
                ui.SameLine();
                if (ui.Button("Refresh"))
                {
                    try
                    {
                        TrashEntries = editor.AssetBrowserDatabase()->TrashRecords();
                        TrashError.clear();
                    }
                    catch (const std::exception& error)
                    {
                        TrashError = error.what();
                    }
                }
                ui.Separator();
                try
                {
                    if (!TrashError.empty())
                        ui.TextColored(editor.AssetBrowserTheme().Error, TrashError);
                    if (TrashEntries.empty() && TrashError.empty())
                        ui.TextColored(editor.AssetBrowserTheme().MutedText, "Trash is empty.");
                    Keire::AssetTrashId completed;
                    for (const auto& record : TrashEntries)
                    {
                        auto id = ui.PushId(record.Id.ToString());
                        ui.Text(record.OriginalPath.generic_string());
                        ui.SameLine();
                        if (ui.Button("Restore"))
                        {
                            editor.MutateAssetBrowser(
                                {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash, .Trash = record.Id}, {},
                                {});
                            editor.SetAssetBrowserStatus("Restored " + record.OriginalPath.generic_string() + ".");
                            completed = record.Id;
                        }
                        ui.SameLine();
                        if (ui.Button("Delete Permanently"))
                        {
                            editor.MutateAssetBrowser(
                                {.Kind = Keire::Detail::AssetWorkerMutationKind::PermanentlyDeleteTrash,
                                 .Trash = record.Id},
                                {}, {});
                            editor.SetAssetBrowserStatus("Permanently removed trash entry.");
                            completed = record.Id;
                        }
                    }
                    if (completed)
                        std::erase_if(TrashEntries,
                                      [&](const Keire::AssetTrashRecord& record) { return record.Id == completed; });
                }
                catch (const std::exception& error)
                {
                    ui.TextColored(editor.AssetBrowserTheme().Error, error.what());
                    editor.ReportAssetBrowserError(std::string("Asset trash failed: ") + error.what());
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

        void DrawRenamePopups(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (OpenNamedCreatePopup)
            {
                ui.OpenPopup("Create Asset");
                OpenNamedCreatePopup = false;
                FocusCreateName = true;
            }
            if (auto create = ui.BeginPopupModal("Create Asset"); create)
            {
                const std::string_view type =
                    PendingCreateKind == NamedCreateKind::Scene                   ? "scene"
                    : PendingCreateKind == NamedCreateKind::Material              ? "material"
                    : PendingCreateKind == NamedCreateKind::AnimationGraph        ? "Animator Controller"
                    : PendingCreateKind == NamedCreateKind::Script                ? "C# script"
                    : PendingCreateKind == NamedCreateKind::ManagedAssembly       ? "managed assembly"
                    : PendingCreateKind == NamedCreateKind::ManagedData           ? "managed data asset"
                    : PendingCreateKind == NamedCreateKind::AudioMixer            ? "audio mixer"
                    : PendingCreateKind == NamedCreateKind::PhysicsMaterial       ? "physics material"
                    : PendingCreateKind == NamedCreateKind::VfxEffect             ? "VFX effect"
                    : PendingCreateKind == NamedCreateKind::MaterialGraph         ? "material graph"
                    : PendingCreateKind == NamedCreateKind::MaterialGraphInstance ? "material instance"
                    : PendingCreateKind == NamedCreateKind::Prefab                ? "prefab"
                    : PendingCreateKind == NamedCreateKind::PrefabVariant         ? "prefab variant"
                    : PendingCreateKind == NamedCreateKind::Shader                ? "shader"
                                                                                  : "Input Actions asset";
                ui.Text("Choose a name for the new " + std::string(type));
                if (FocusCreateName)
                {
                    ui.RequestKeyboardFocus();
                    FocusCreateName = false;
                }
                (void)ui.InputText("Name", CreateNameBuffer, true);
                if (ui.Button("Create"))
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
                            created =
                                PendingCreateKind == NamedCreateKind::Scene
                                    ? editor.CreateAssetBrowserScene(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::Material
                                    ? editor.CreateAssetBrowserMaterial(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::AnimationGraph
                                    ? editor.CreateAssetBrowserAnimationGraph(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::Script
                                    ? editor.CreateAssetBrowserScript(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::ManagedAssembly
                                    ? editor.CreateAssetBrowserManagedAssembly(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::ManagedData
                                    ? editor.CreateAssetBrowserManagedData(PendingManagedType, CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::AudioMixer
                                    ? editor.CreateAssetBrowserAudioMixer(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::PhysicsMaterial
                                    ? editor.CreateAssetBrowserPhysicsMaterial(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::VfxEffect
                                    ? editor.CreateAssetBrowserVfxEffect(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::MaterialGraph
                                    ? editor.CreateAssetBrowserMaterialGraph(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::MaterialGraphInstance
                                    ? editor.CreateAssetBrowserMaterialGraphInstance(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::Prefab
                                    ? editor.CreateAssetBrowserPrefab(CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::PrefabVariant
                                    ? editor.CreateAssetBrowserPrefabVariant(PendingVariantBase, CreateNameBuffer)
                                : PendingCreateKind == NamedCreateKind::Shader
                                    ? editor.CreateAssetBrowserShader(CreateNameBuffer)
                                : PendingInputActions
                                    ? editor.CreateAssetBrowserInputActions(*PendingInputActions, CreateNameBuffer)
                                    : false;
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
                            CreateNameBuffer.clear();
                            ui.CloseCurrentPopup();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        editor.ReportAssetBrowserError(std::string("Asset creation failed: ") + error.what());
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
                    CreateNameBuffer.clear();
                    ui.CloseCurrentPopup();
                }
            }

            if (OpenExternalEditorPopup)
            {
                ExternalEditorBuffer = Keire::Detail::PathToUtf8(ExternalEditor);
                ui.OpenPopup("External Editor");
                OpenExternalEditorPopup = false;
            }
            if (auto editorPopup = ui.BeginPopupModal("External Editor"); editorPopup)
            {
                ui.Text("Executable path (leave empty to use the operating-system default)");
                (void)ui.InputText("Editor", ExternalEditorBuffer);
                if (ui.Button("Save"))
                {
                    const auto candidate = ExternalEditorBuffer.empty()
                                               ? std::filesystem::path{}
                                               : Keire::Detail::PathFromUtf8(ExternalEditorBuffer);
                    if (!candidate.empty() && !std::filesystem::is_regular_file(candidate))
                        editor.ReportAssetBrowserError("External editor executable does not exist.");
                    else
                    {
                        ExternalEditor =
                            candidate.empty() ? candidate : std::filesystem::absolute(candidate).lexically_normal();
                        SavePreferences();
                        ui.CloseCurrentPopup();
                    }
                }
                ui.SameLine();
                if (ui.Button("Use System Default"))
                {
                    ExternalEditor.clear();
                    ExternalEditorBuffer.clear();
                    SavePreferences();
                    ui.CloseCurrentPopup();
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                    ui.CloseCurrentPopup();
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
                        const auto record = editor.AssetBrowserDatabase()->Find(Renaming);
                        if (!record)
                            throw std::runtime_error("Asset no longer exists.");
                        const auto destination = record->RelativePath.parent_path() /
                                                 (RenameBuffer + record->RelativePath.extension().string());
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
        }

        void CopyText(IAssetBrowserController& editor, const std::string_view value)
        {
            try
            {
                editor.CopyAssetBrowserText(value);
                editor.SetAssetBrowserStatus("Copied to clipboard.");
            }
            catch (const std::exception& error)
            {
                editor.ReportAssetBrowserError(std::string("Clipboard operation failed: ") + error.what());
            }
        }

        void RevealPath(IAssetBrowserController& editor, const std::filesystem::path& path)
        {
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(path, diagnostic))
                editor.ReportAssetBrowserError("Reveal failed: " + diagnostic);
        }

        void DrawAssetContext(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
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
                    OpenExternalEditorPopup = true;
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
                    editor.ImportAssetBrowserAssets();
                if (record.Type == Keire::MeshAsset::StaticType() && !record.SubAssets.empty() &&
                    ui.MenuItem("Extract Materials"))
                    editor.ExtractAssetBrowserMaterials(record.Id);
                if (ui.MenuItem("Reveal in File Explorer"))
                    RevealPath(editor, AssetRoot / record.RelativePath);
                if (ui.MenuItem("Copy Relative Path"))
                    CopyText(editor, (std::filesystem::path("Assets") / record.RelativePath).generic_string());
                if (ui.MenuItem("Copy Asset ID"))
                    CopyText(editor, record.Id.ToString());
            }
        }

        void DrawFolderContext(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor,
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
                        editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateFolder,
                                                   .Source = folder,
                                                   .Destination = destination},
                                                  {}, "Duplicate Folder");
                        editor.SetAssetBrowserStatus("Duplicated folder.");
                    }
                    catch (const std::exception& error)
                    {
                        editor.ReportAssetBrowserError(std::string("Folder duplication failed: ") + error.what());
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
                    editor.ImportAssetBrowserAssets();
                if (ui.MenuItem("Reveal in File Explorer"))
                    RevealPath(editor, AssetRoot / folder);
                if (ui.MenuItem("Copy Relative Path"))
                    CopyText(editor, (std::filesystem::path("Assets") / folder).generic_string());
            }
        }

        void DrawAssetTooltip(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                              IAssetBrowserController& editor)
        {
            std::error_code error;
            const auto bytes = std::filesystem::file_size(AssetRoot / record.RelativePath, error);
            const auto status = editor.AssetBrowserDatabase()->ImportStatus(record.Id);
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

        void DrawAsset(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, IAssetBrowserController& editor,
                       const bool grid)
        {
            auto id = ui.PushId(record.Id.ToString());
            auto image = Images.contains(record.Id) ? Images.at(record.Id) : FolderImage;
            if (!Images.contains(record.Id))
            {
                if (record.Type == Keire::MaterialGraphAsset::StaticType())
                    image = MaterialGraphFallbackImage;
                else if (record.Type == Keire::MaterialGraphInstanceAsset::StaticType())
                    image = MaterialInstanceFallbackImage;
                else if (record.Type == Keire::VfxEffectAsset::StaticType())
                    image = VfxFallbackImage;
            }
            const bool selected = std::ranges::find(Selection, record.Id) != Selection.end();
            bool open = false;
            if (grid)
            {
                if (ui.ImageButton("Thumbnail", image, {ThumbnailSize, ThumbnailSize}))
                    SelectFromClick(record.Id, ui, editor);
                open = ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "ThumbnailContext");
                if (ui.Selectable(DisplayName(record.RelativePath), selected))
                    SelectFromClick(record.Id, ui, editor);
                open |= ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "LabelContext");
                DrawAssetTooltip(ui, record, editor);
            }
            else
            {
                if (ui.ImageButton("Thumbnail", image, {32.0F, 32.0F}))
                    SelectFromClick(record.Id, ui, editor);
                open = ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "ThumbnailContext");
                ui.SameLine();
                if (ui.Selectable(DisplayName(record.RelativePath), selected))
                    SelectFromClick(record.Id, ui, editor);
                open |= ui.LastItemState().DoubleClicked;
                DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "LabelContext");
                DrawAssetTooltip(ui, record, editor);
            }
            if (editor.AssetBrowserDatabase()->ImportStatus(record.Id).State == Keire::AssetImportState::Failed)
                ui.TextColored(editor.AssetBrowserTheme().Error, "Import error");
            if (open)
                Open(record, editor);
            if (RevealAsset == record.Id)
                RevealAsset = {};
        }

        void AcceptFolderPayloads(Keire::UiFrame& ui, const std::filesystem::path& folder,
                                  IAssetBrowserController& editor)
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
                    editor.ReportAssetBrowserError(std::string("Asset move failed: ") + error.what());
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
                    editor.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                               .Source = source,
                                               .Destination = destination},
                                              {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                               .Source = destination,
                                               .Destination = source},
                                              "Move Folder");
                    editor.SetAssetBrowserStatus("Moved folder.");
                }
                catch (const std::exception& error)
                {
                    editor.ReportAssetBrowserError(std::string("Folder move failed: ") + error.what());
                }
            }
            payload.clear();
            if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
            {
                try
                {
                    const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                    editor.CreateAssetBrowserPrefabFromObject(Keire::AssetId::Parse(value), folder);
                }
                catch (const std::exception& error)
                {
                    editor.ReportAssetBrowserError(std::string("Prefab creation failed: ") + error.what());
                }
            }
        }

        void AcceptFolderDrop(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor)
        {
            if (auto target = ui.BeginDragTarget(); target)
                AcceptFolderPayloads(ui, folder, editor);
        }

        void AcceptFolderDrop(Keire::UiFrame& ui, const Keire::UiItemRect area, const std::filesystem::path& folder,
                              IAssetBrowserController& editor)
        {
            if (auto target = ui.BeginDragTarget(area, "FolderCardDrop"); target)
                AcceptFolderPayloads(ui, folder, editor);
        }

        void DrawFolder(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor,
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
                auto area = ui.LastItemRect();
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderImageContext");
                if (ui.Selectable(folder.filename().string()))
                    CurrentFolder = folder;
                const auto labelArea = ui.LastItemRect();
                area.Minimum.X = std::min(area.Minimum.X, labelArea.Minimum.X);
                area.Minimum.Y = std::min(area.Minimum.Y, labelArea.Minimum.Y);
                area.Maximum.X = std::max(area.Maximum.X, labelArea.Maximum.X);
                area.Maximum.Y = std::max(area.Maximum.Y, labelArea.Maximum.Y);
                area.Minimum.X -= 4.0F;
                area.Minimum.Y -= 4.0F;
                area.Maximum.X =
                    std::max(area.Maximum.X + 4.0F, area.Minimum.X + std::max(ui.ContentAvailable().Width, 1.0F));
                area.Maximum.Y += 4.0F;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderLabelContext");
                ExternalDropTargets.push_back({area, folder});
                AcceptFolderDrop(ui, area, folder, editor);
                ui.SetTooltip((std::filesystem::path("Assets") / folder).generic_string(), {.Delayed = true});
            }
            else
            {
                if (ui.ImageButton("Folder", FolderImage, {32.0F, 32.0F}))
                    CurrentFolder = folder;
                auto area = ui.LastItemRect();
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderImageContext");
                ui.SameLine();
                if (ui.Selectable(folder.filename().string()))
                    CurrentFolder = folder;
                const auto labelArea = ui.LastItemRect();
                area.Minimum.X = std::min(area.Minimum.X, labelArea.Minimum.X);
                area.Minimum.Y = std::min(area.Minimum.Y, labelArea.Minimum.Y);
                area.Maximum.X = std::max(area.Maximum.X, labelArea.Maximum.X);
                area.Maximum.Y = std::max(area.Maximum.Y, labelArea.Maximum.Y);
                area.Minimum.X -= 4.0F;
                area.Minimum.Y -= 4.0F;
                area.Maximum.X =
                    std::max(area.Maximum.X + 4.0F, area.Minimum.X + std::max(ui.ContentAvailable().Width, 1.0F));
                area.Maximum.Y += 4.0F;
                drawDragSource();
                DrawFolderContext(ui, folder, editor, "FolderLabelContext");
                ExternalDropTargets.push_back({area, folder});
                AcceptFolderDrop(ui, area, folder, editor);
                ui.SetTooltip((std::filesystem::path("Assets") / folder).generic_string(), {.Delayed = true});
            }
        }

        void DrawFolderTree(Keire::UiFrame& ui, const std::filesystem::path& relative, IAssetBrowserController& editor)
        {
            const auto children = Folders(relative);
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

        void DrawFolderPane(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (ui.Selectable("Assets", CurrentFolder.empty()))
                CurrentFolder.clear();
            ExternalDropTargets.push_back({ui.LastItemRect(), {}});
            AcceptFolderDrop(ui, {}, editor);
            DrawFolderTree(ui, {}, editor);
        }

        void DrawBreadcrumbs(Keire::UiFrame& ui, IAssetBrowserController& editor)
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
                                  IAssetBrowserController& editor)
        {
            if (!ui.WindowFocused())
                return;
            if (ui.Shortcut({.Key = Keire::UiKey::A, .Primary = true}))
            {
                Selection.clear();
                for (const auto* record : visible)
                    Selection.push_back(record->Id);
                editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
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
                if (const auto record = editor.AssetBrowserDatabase()->Find(Selection.front()))
                    BeginAssetRename(*record);
            if (ui.Shortcut({Keire::UiKey::Enter}) && Selection.size() == 1)
                if (const auto record = editor.AssetBrowserDatabase()->Find(Selection.front()))
                    Open(*record, editor);
            if (ui.Shortcut({Keire::UiKey::Backspace}) && !CurrentFolder.empty())
                CurrentFolder = CurrentFolder.parent_path();
        }

        void DrawBlankContextItems(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (auto create = ui.BeginMenu("Create"); create)
                DrawCreateItems(ui, editor);
            if (ui.MenuItem("Paste", false, ClipboardModeValue != ClipboardMode::Empty))
                Paste(CurrentFolder, editor);
            ui.Separator();
            if (ui.MenuItem("Refresh and Reimport"))
                editor.ImportAssetBrowserAssets();
            if (ui.MenuItem("Reveal in File Explorer"))
                RevealPath(editor, AssetRoot / CurrentFolder);
            if (ui.MenuItem("Open Trash"))
                OpenTrashPopup = true;
        }

        void DrawBlankContext(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (auto context = ui.BeginWindowContextMenu("AssetBlankContext"); context)
                DrawBlankContextItems(ui, editor);
        }

        void DrawContentPane(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            const auto contentDropArea = ui.ContentRect();
            DrawBreadcrumbs(ui, editor);

            const auto folders = Folders(CurrentFolder);
            std::vector<const Keire::AssetSourceRecord*> assets;
            for (const auto& record : editor.AssetBrowserRecords())
                if (record.RelativePath.parent_path() == CurrentFolder &&
                    (Search.empty() || record.RelativePath.filename().string().find(Search) != std::string::npos))
                    assets.push_back(&record);
            std::ranges::sort(assets, {}, [](const auto* record) { return record->RelativePath.filename(); });
            VisibleSelectionOrder.clear();
            VisibleSelectionOrder.reserve(assets.size());
            for (const auto* record : assets)
                VisibleSelectionOrder.push_back(record->Id);

            if (Mode == ViewMode::List)
            {
                for (const auto& folder : folders)
                    if (Search.empty() || folder.filename().string().find(Search) != std::string::npos)
                        DrawFolder(ui, folder, editor, false);
                for (const auto* record : assets)
                    DrawAsset(ui, *record, editor, false);
                const auto remaining = ui.ContentAvailable();
                (void)ui.InvisibleButton("AssetCurrentFolderDrop",
                                         {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
                if (auto context = ui.BeginItemContextMenu("AssetBlankDropContext"); context)
                    DrawBlankContextItems(ui, editor);
                AcceptFolderDrop(ui, contentDropArea, CurrentFolder, editor);
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
            const auto remaining = ui.ContentAvailable();
            (void)ui.InvisibleButton("AssetCurrentFolderDrop",
                                     {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
            if (auto context = ui.BeginItemContextMenu("AssetBlankDropContext"); context)
                DrawBlankContextItems(ui, editor);
            AcceptFolderDrop(ui, contentDropArea, CurrentFolder, editor);
            DrawKeyboardCommands(ui, assets, editor);
            DrawBlankContext(ui, editor);
        }

        void Draw(Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            Focused = false;
            ExternalDropTargets.clear();
            if (auto project = ui.BeginPanel(Registration); project)
            {
                Focused = ui.WindowFocused();
                ui.TextColored(editor.AssetBrowserTheme().Accent, "ASSET BROWSER");
                ui.Separator();
                if (!editor.AssetBrowserDatabase() || !Thumbnails)
                {
                    ui.TextColored(editor.AssetBrowserTheme().Error, editor.AssetBrowserStatus().empty()
                                                                         ? "Asset database is unavailable."
                                                                         : editor.AssetBrowserStatus());
                    return;
                }
                const auto recordRevision = editor.AssetBrowserRecordRevision();
                const bool recordsChanged = recordRevision != ObservedRecordRevision;
                ObservedRecordRevision = recordRevision;
                RefreshFolderCache(recordsChanged);
                if (!FolderImage)
                {
                    const auto pixels = MakeFolderThumbnail(96, 96);
                    FolderImage = ui.CreateImage(96, 96, pixels);
                }
                if (!MaterialGraphFallbackImage)
                {
                    MaterialGraphFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::MaterialGraphAsset::StaticType(), 96, 96));
                    MaterialInstanceFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::MaterialGraphInstanceAsset::StaticType(), 96, 96));
                    VfxFallbackImage =
                        ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), 96, 96));
                }
                for (auto& completed : Thumbnails->DrainCompleted())
                    Images[completed.Asset] = ui.CreateImage(completed.Width, completed.Height, completed.Pixels);
                const auto records = editor.AssetBrowserRecords();
                for (const auto& record : records)
                {
                    if (!recordsChanged && (Images.contains(record.Id) || ImageDigests.contains(record.Id)))
                        continue;
                    std::string digest = record.SourceDigest + record.MetadataDigest;
                    for (const auto dependency : record.Dependencies)
                    {
                        digest += dependency.ToString();
                        const auto dependencyRecord =
                            std::ranges::find(records, dependency, &Keire::AssetSourceRecord::Id);
                        if (dependencyRecord != records.end())
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
                    const auto assets = editor.AssetBrowserAssets();
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
                    else if (assets && record.Type == Keire::AudioClipAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::AudioClipAsset>(record.Id, Keire::AssetPriority::Low);
                        request.PreviewAsset = handle.TryGetLoaded();
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                    }
                    else if (assets && record.Type == Keire::PrefabAsset::StaticType())
                    {
                        const auto prefabSource = Keire::Detail::ReadTextFile(AssetRoot / record.RelativePath,
                                                                              std::size_t{64} * 1024U * 1024U);
                        Keire::Ref<const Keire::PrefabAsset> prefab =
                            Keire::PrefabAsset::Decode(std::as_bytes(std::span(prefabSource)));
                        request.PreviewAsset = prefab;
                        ready = static_cast<bool>(request.PreviewAsset);
                        if (prefab)
                        {
                            bool dependenciesReady = true;
                            const auto composed = Keire::ComposePrefab(
                                record.Id,
                                [&](const Keire::AssetId dependency)
                                {
                                    if (dependency == record.Id)
                                        return prefab;
                                    const auto dependencyRecord = editor.AssetBrowserDatabase()->Find(dependency);
                                    Keire::Ref<const Keire::PrefabAsset> loaded;
                                    if (dependencyRecord)
                                    {
                                        const auto dependencySource =
                                            Keire::Detail::ReadTextFile(AssetRoot / dependencyRecord->RelativePath,
                                                                        std::size_t{64} * 1024U * 1024U);
                                        loaded = Keire::PrefabAsset::Decode(std::as_bytes(std::span(dependencySource)));
                                    }
                                    else
                                    {
                                        const auto dependencyHandle =
                                            assets->Load<Keire::PrefabAsset>(dependency, Keire::AssetPriority::Low);
                                        loaded = dependencyHandle.TryGetLoaded();
                                    }
                                    dependenciesReady = dependenciesReady && static_cast<bool>(loaded);
                                    if (dependencyRecord)
                                        request.Digest +=
                                            dependencyRecord->SourceDigest + dependencyRecord->MetadataDigest;
                                    return loaded;
                                });
                            if (dependenciesReady)
                            {
                                auto previewScene = Keire::CreateRef<Keire::Scene>(record.Id, composed);
                                for (const auto& entity : previewScene->Query<Keire::MeshRendererComponent>())
                                {
                                    const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
                                    const auto transform = entity.GetComponent<Keire::TransformComponent>();
                                    if (!renderer || !renderer->Enabled() || !renderer->Visible() ||
                                        !renderer->Mesh() || !transform)
                                        continue;
                                    Keire::Ref<const Keire::MeshAsset> mesh =
                                        Keire::MeshAsset::ResolveBuiltin(renderer->Mesh());
                                    if (!mesh)
                                    {
                                        const auto meshHandle =
                                            assets->Load<Keire::MeshAsset>(renderer->Mesh(), Keire::AssetPriority::Low);
                                        mesh = meshHandle.TryGetLoaded();
                                    }
                                    dependenciesReady = dependenciesReady && static_cast<bool>(mesh);
                                    if (const auto meshRecord = editor.AssetBrowserDatabase()->Find(renderer->Mesh()))
                                        request.Digest += meshRecord->SourceDigest + meshRecord->MetadataDigest;
                                    if (mesh)
                                        request.PreviewMeshes.push_back({mesh, transform->WorldMatrix()});
                                }
                            }
                            ready = dependenciesReady;
                        }
                    }
                    else if (const auto generated = PrepareGeneratedAssetThumbnail(assets, record, request))
                        ready = *generated;
                    if (ready && Thumbnails->Request(std::move(request)))
                        ImageDigests.emplace(record.Id, std::move(digest));
                }

                if (ui.IconButton("ProjectCreate", Keire::UiIcon::Create, false, {28.0F, 24.0F}))
                    ui.OpenPopup("AssetCreateMenu");
                if (ui.LastItemState().Hovered)
                    ui.SetTooltip("Create asset", {.Delayed = true});
                if (auto create = ui.BeginPopup("AssetCreateMenu"); create)
                    DrawCreateItems(ui, editor);
                ui.SameLine();
                if (ui.IconButton("ProjectRefresh", Keire::UiIcon::Refresh, false, {28.0F, 24.0F}))
                    editor.ImportAssetBrowserAssets();
                if (ui.LastItemState().Hovered)
                    ui.SetTooltip("Refresh and import", {.Delayed = true});
                ui.SameLine();
                if (ui.IconButton("ProjectView", Mode == ViewMode::List ? Keire::UiIcon::Grid : Keire::UiIcon::List,
                                  false, {28.0F, 24.0F}))
                {
                    Mode = Mode == ViewMode::List ? ViewMode::Grid : ViewMode::List;
                    SavePreferences();
                }
                if (ui.LastItemState().Hovered)
                    ui.SetTooltip(Mode == ViewMode::List ? "Grid view" : "List view", {.Delayed = true});
                ui.SameLine();
                if (ui.Button("Trash"))
                    OpenTrashPopup = true;
                ui.SameLine();
                (void)ui.InputTextWithHint("##ProjectSearch", "Search Assets", Search);
                if (Mode == ViewMode::Grid && ui.SliderFloat("Thumbnail Size", ThumbnailSize, 48.0F, 160.0F) &&
                    ui.LastItemState().DeactivatedAfterEdit)
                    SavePreferences();

                ui.Separator();
                const auto browserSize = ui.ContentAvailable();
                constexpr float splitterThickness = 4.0F;
                constexpr float minimumFolderWidth = 150.0F;
                constexpr float minimumContentWidth = 260.0F;
                const float footerHeight = editor.AssetBrowserStatus().empty() ? 30.0F : 54.0F;
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
                if (!editor.AssetBrowserStatus().empty())
                    ui.TextColored(editor.AssetBrowserTheme().MutedText, editor.AssetBrowserStatus());
                ui.TextColored(editor.AssetBrowserTheme().MutedText,
                               std::to_string(Selection.size()) + " selected  |  " +
                                   std::to_string(Thumbnails->PendingCount()) + " thumbnail request(s)");
            }
        }

        IAssetBrowserController& Controller;
        Keire::UiPanelRegistration Registration;
        std::filesystem::path ProjectRoot;
        std::filesystem::path AssetRoot;
        std::filesystem::path CurrentFolder;
        std::filesystem::path PreferencePath;
        AssetBrowserFolderCache FolderCache;
        std::chrono::steady_clock::time_point NextFolderRefresh;
        std::filesystem::path RenamingFolder;
        std::filesystem::path PendingDeleteFolder;
        std::unique_ptr<ThumbnailService> Thumbnails;
        Keire::Ref<Keire::JobSystem> Scheduler;
        std::unordered_map<Keire::AssetId, Keire::Ref<Keire::UiImage>> Images;
        std::unordered_map<Keire::AssetId, std::string> ImageDigests;
        std::uint64_t ObservedRecordRevision = 0;
        Keire::Ref<Keire::UiImage> FolderImage;
        Keire::Ref<Keire::UiImage> MaterialGraphFallbackImage;
        Keire::Ref<Keire::UiImage> MaterialInstanceFallbackImage;
        Keire::Ref<Keire::UiImage> VfxFallbackImage;
        Keire::Ref<Keire::UndoContext> Undo;
        std::vector<Keire::AssetId> Selection;
        std::vector<Keire::AssetId> VisibleSelectionOrder;
        std::vector<Keire::AssetId> PendingDeleteAssets;
        std::vector<Keire::AssetTrashRecord> TrashEntries;
        std::vector<ClipboardEntry> Clipboard;
        std::vector<ExternalDropTarget> ExternalDropTargets;
        Keire::AssetId Renaming;
        Keire::AssetId RevealAsset;
        Keire::AssetId SelectionAnchor;
        Keire::AssetId PendingVariantBase;
        std::string Search;
        std::string RenameBuffer;
        std::string CreateNameBuffer;
        std::string ExternalEditorBuffer;
        std::string TrashError;
        std::filesystem::path ExternalEditor;
        std::filesystem::path PendingCreateFolder;
        NamedCreateKind PendingCreateKind = NamedCreateKind::None;
        Keire::ManagedTypeId PendingManagedType;
        std::optional<Keire::InputActionAssetDefinition> PendingInputActions;
        ViewMode Mode = ViewMode::Grid;
        ClipboardMode ClipboardModeValue = ClipboardMode::Empty;
        float ThumbnailSize = 88.0F;
        float FolderPaneWidth = 210.0F;
        bool OpenRenamePopup = false;
        bool OpenNamedCreatePopup = false;
        bool FocusCreateName = false;
        bool OpenExternalEditorPopup = false;
        bool OpenFolderRenamePopup = false;
        bool OpenDeletePopup = false;
        bool OpenTrashPopup = false;
        bool Focused = false;
    };

    AssetBrowserPanel::AssetBrowserPanel(IAssetBrowserController& controller)
        : m_Impl(std::make_unique<Impl>(controller))
    {
    }
    AssetBrowserPanel::~AssetBrowserPanel() { Close(); }
    void AssetBrowserPanel::SetProjectRoot(const std::filesystem::path& root) { m_Impl->SetProjectRoot(root); }
    void AssetBrowserPanel::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs) { m_Impl->Scheduler = std::move(jobs); }
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
    void AssetBrowserPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Impl->Registration = workspace.RegisterPanel({"editor.project", "Project"});
    }
    Keire::UiPanelRegistration& AssetBrowserPanel::Registration() noexcept { return m_Impl->Registration; }
    void AssetBrowserPanel::RevealAsset(const Keire::AssetId asset) { m_Impl->Reveal(asset, m_Impl->Controller); }
    void AssetBrowserPanel::OpenAsset(const Keire::AssetId asset)
    {
        if (const auto record = m_Impl->Controller.AssetBrowserDatabase()->Find(asset))
            m_Impl->Open(*record, m_Impl->Controller);
    }
    void AssetBrowserPanel::RequestCreateMaterial() { m_Impl->RequestCreateMaterial(); }
    void AssetBrowserPanel::Draw(Keire::UiFrame& ui) { m_Impl->Draw(ui, m_Impl->Controller); }
    void AssetBrowserPanel::Close() noexcept
    {
        if (m_Impl)
            m_Impl->Close();
    }
} // namespace KeireEditor
