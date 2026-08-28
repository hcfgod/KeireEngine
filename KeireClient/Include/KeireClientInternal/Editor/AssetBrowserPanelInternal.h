#pragma once

#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireClient/Editor/AssetBrowserFolderCache.h"
#include "KeireClient/Editor/AssetBrowserPresentation.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"
#include "KeireClient/Editor/MaterialGraphCreationPicker.h"
#include "KeireClient/Editor/NamedAssetCreation.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SelectionRange.h"
#include "KeireClient/Editor/ShaderGraphCreationMenu.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

        using NamedCreateKind = NamedAssetCreationKind;
        using ClipboardMode = AssetBrowserClipboardMode;
        using ClipboardEntry = AssetBrowserClipboardEntry;

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
        void RequestPackageCreate(AssetPackageSelection selection, std::string displayName)
        {
            PendingPackageSelection = std::move(selection);
            PendingPackageDraft = {.PackageId = SuggestedAssetPackageIdentifier(displayName),
                                   .Version = "0.0.1",
                                   .PublisherId = "local",
                                   .DisplayName = std::move(displayName),
                                   .Summary = "Assets exported from the current Kéire project.",
                                   .MinimumEngineVersion = std::string(Keire::GetBuildInfo().Version)};
            OpenPackageCreatePopup = true;
        }
        void ResetPackageCreate() noexcept
        {
            PendingPackageSelection = {};
            PendingPackageDraft = {};
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
            if (Thumbnails)
                Thumbnails->Invalidate(asset);
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
            AssetFallbackImage.Reset();
            ShaderGraphFallbackImage.Reset();
            MaterialGraphFallbackImage.Reset();
            MaterialInstanceFallbackImage.Reset();
            VfxFallbackImage.Reset();
            AudioMixerFallbackImage.Reset();
            AnimationFallbackImage.Reset();
            Selection.clear();
            ExpandedParents.clear();
            FolderSelection.clear();
            VisibleSelectionOrder.clear();
            VisibleFolderOrder.clear();
            SelectionAnchor = {};
            FolderSelectionAnchor.clear();
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
            ResetPackageCreate();
            OpenPackageCreatePopup = false;
            FocusCreateName = false;
            FocusRenameName = false;
            CurrentFolder.clear();
            ProjectRoot.clear();
            AssetRoot.clear();
            FolderCache.Clear();
            VisibleRecords.Clear();
            ObservedRecordRevision = 0;
            NextFolderRefresh = {};
        }

        void LoadPreferences()
        {
            const auto preferences = LoadAssetBrowserPreferences(PreferencePath);
            Mode = preferences.GridView ? ViewMode::Grid : ViewMode::List;
            ThumbnailSize = preferences.ThumbnailSize;
        }
        void SavePreferences() noexcept
        {
            SaveAssetBrowserPreferences(PreferencePath,
                                        {.GridView = Mode == ViewMode::Grid, .ThumbnailSize = ThumbnailSize});
        }

        void RefreshFolderCache(const bool force)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!force && now < NextFolderRefresh)
                return;
            NextFolderRefresh = now + std::chrono::seconds(1);
            (void)FolderCache.Refresh(AssetRoot);
        }

        void Select(const Keire::AssetId asset, const bool additive, IAssetBrowserController& editor)
        {
            SelectAssetBrowserAsset(Selection, FolderSelection, SelectionAnchor, asset, additive, editor);
        }

        void SelectFromClick(const Keire::AssetId asset, Keire::UiFrame& ui, IAssetBrowserController& editor)
        {
            if (!ui.ShiftDown() || !SelectionAnchor)
            {
                Select(asset, ui.ControlDown(), editor);
                return;
            }
            if (!ui.ControlDown())
                FolderSelection.clear();
            Selection = BuildRangeSelection(VisibleSelectionOrder, SelectionAnchor, asset, Selection, ui.ControlDown());
            editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            editor.ClearAssetBrowserSceneSelection();
        }

        void SelectFolder(const std::filesystem::path& folder, const bool additive, IAssetBrowserController& editor)
        {
            if (!additive)
            {
                Selection.clear();
                FolderSelection.clear();
            }
            const auto found = std::ranges::find(FolderSelection, folder);
            if (additive && found != FolderSelection.end())
                FolderSelection.erase(found);
            else if (found == FolderSelection.end())
                FolderSelection.push_back(folder);
            FolderSelectionAnchor = folder;
            editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            editor.ClearAssetBrowserSceneSelection();
        }

        void SelectFolderFromClick(const std::filesystem::path& folder, Keire::UiFrame& ui,
                                   IAssetBrowserController& editor)
        {
            if (!ui.ShiftDown() || FolderSelectionAnchor.empty())
            {
                SelectFolder(folder, ui.ControlDown(), editor);
                return;
            }
            if (!ui.ControlDown())
                Selection.clear();
            FolderSelection = BuildFolderRangeSelection(VisibleFolderOrder, FolderSelectionAnchor, folder,
                                                        FolderSelection, ui.ControlDown());
            editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            editor.ClearAssetBrowserSceneSelection();
        }

        void SelectFolderOnlyIfNeeded(const std::filesystem::path& folder, IAssetBrowserController& editor)
        {
            if (std::ranges::find(FolderSelection, folder) == FolderSelection.end())
                SelectFolder(folder, false, editor);
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
                if (record->ParentSource)
                    ExpandedParents.insert(record->ParentSource);
                Select(asset, false, editor);
                RevealAsset = asset;
            }
        }

        void Open(const Keire::AssetSourceRecord& record, IAssetBrowserController& editor)
        {
            try
            {
                switch (ResolveAssetBrowserOpenAction(record.RelativePath))
                {
                case AssetBrowserOpenAction::InputActions:
                    editor.OpenAssetBrowserInputActions(record.Id);
                    break;
                case AssetBrowserOpenAction::AnimationGraph:
                    editor.OpenAssetBrowserAnimationGraph(record.Id);
                    break;
                case AssetBrowserOpenAction::AudioMixer:
                    editor.OpenAssetBrowserAudioMixer(record.Id);
                    break;
                case AssetBrowserOpenAction::VfxEffect:
                    editor.OpenAssetBrowserVfxEffect(record.Id);
                    break;
                case AssetBrowserOpenAction::Material:
                    editor.OpenAssetBrowserMaterial(record.Id);
                    break;
                case AssetBrowserOpenAction::MaterialGraph:
                    editor.OpenAssetBrowserMaterialGraph(record.Id);
                    break;
                case AssetBrowserOpenAction::MaterialInstance:
                    editor.OpenAssetBrowserMaterialInstance(record.Id);
                    break;
                case AssetBrowserOpenAction::ShaderGraph:
                    editor.OpenAssetBrowserShaderGraph(record.Id);
                    break;
                case AssetBrowserOpenAction::MaterialParameterCollection:
                    editor.OpenAssetBrowserMaterialParameterCollection(record.Id);
                    break;
                case AssetBrowserOpenAction::Scene:
                    editor.OpenAssetBrowserScene(record.Id);
                    break;
                case AssetBrowserOpenAction::Prefab:
                    editor.OpenAssetBrowserPrefab(record.Id);
                    break;
                case AssetBrowserOpenAction::External:
                {
                    const bool reuseManagedSession = editor.PrepareAssetBrowserExternalOpen(record.Id);
                    std::string diagnostic;
                    if (!Keire::Detail::OpenInExternalEditor(AssetRoot / record.RelativePath,
                                                             editor.AssetBrowserExternalEditor(), ProjectRoot,
                                                             diagnostic, reuseManagedSession))
                        throw std::runtime_error(diagnostic);
                    editor.SetAssetBrowserStatus("Opened " + record.RelativePath.filename().string() +
                                                 " in an external editor.");
                    break;
                }
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
                const auto folder = UniqueAssetBrowserFolder(AssetRoot, CurrentFolder / "New Folder");
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

        void DrawManagedDataCreateItems(Keire::UiFrame& ui,
                                        const std::span<const Keire::ManagedAssetTypeDescriptor> types,
                                        const std::string_view prefix = {})
        {
            const auto leafLabel = [](const Keire::ManagedAssetTypeDescriptor* type)
            {
                const auto path = std::string_view(type->MenuPath);
                const auto separator = path.find_last_of('/');
                return separator == std::string_view::npos ? path : path.substr(separator + 1);
            };
            std::vector<const Keire::ManagedAssetTypeDescriptor*> leaves;
            std::set<std::string, std::less<>> childMenus;
            for (const auto& type : types)
            {
                if (type.MenuPath.empty() || !type.MenuPath.starts_with(prefix))
                    continue;
                const auto remainder = std::string_view(type.MenuPath).substr(prefix.size());
                const auto separator = remainder.find('/');
                if (separator == std::string_view::npos)
                    leaves.push_back(&type);
                else
                    childMenus.emplace(remainder.substr(0, separator));
            }
            std::ranges::sort(leaves, {}, leafLabel);
            for (const auto* type : leaves)
            {
                const auto label = std::string_view(type->MenuPath).substr(prefix.size());
                if (ui.MenuItem(std::string(label) + "###ManagedData." + type->StableTypeId.ToString()))
                    RequestManagedDataCreate(*type);
            }
            for (const auto& child : childMenus)
            {
                const auto childPrefix = std::string(prefix) + child + '/';
                if (auto menu = ui.BeginMenu(child + "###ManagedDataMenu." + childPrefix); menu)
                    DrawManagedDataCreateItems(ui, types, childPrefix);
            }
        }

        void DrawManagedDataDiagnostics(Keire::UiFrame& ui, IAssetBrowserController& editor,
                                        const bool hasAuthorableTypes)
        {
            const auto diagnostics = editor.AssetBrowserManagedAssetTypeDiagnostics();
            if (hasAuthorableTypes && diagnostics.empty())
                return;
            const auto label = hasAuthorableTypes ? "ScriptableObject Errors###ManagedDataErrors"
                                                  : "ScriptableObject Asset###ManagedDataUnavailable";
            if (auto menu = ui.BeginMenu(label); menu)
            {
                if (!hasAuthorableTypes)
                    (void)ui.MenuItem("No compiled CreateAssetMenu types", false, false);
                if (diagnostics.empty())
                {
                    (void)ui.MenuItem("Build scripts to populate this menu", false, false);
                    return;
                }
                if (!hasAuthorableTypes)
                    ui.Separator();
                for (const auto& diagnostic : diagnostics)
                {
                    (void)ui.MenuItem("Invalid: " + diagnostic.TypeName, false, false);
                    ui.SetTooltip(FormatManagedAssetTypeDiagnostic(diagnostic), {.Delayed = true});
                }
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
            if (ui.MenuItem("Procedural Motion Profile"))
                RequestNamedCreate(NamedCreateKind::ProceduralMotionProfile, "NewProceduralMotionProfile");
            if (ui.MenuItem("C# Script"))
                RequestNamedCreate(NamedCreateKind::Script, "NewBehaviour");
            if (ui.MenuItem("C# ScriptableObject Class"))
                RequestNamedCreate(NamedCreateKind::ScriptableObjectScript, "NewScriptableObject");
            if (ui.MenuItem("Managed Assembly"))
                RequestNamedCreate(NamedCreateKind::ManagedAssembly, "Gameplay");
            if (ui.MenuItem("Audio Mixer"))
                RequestNamedCreate(NamedCreateKind::AudioMixer, "MainMixer");
            if (ui.MenuItem("Physics Material"))
                RequestNamedCreate(NamedCreateKind::PhysicsMaterial, "PhysicsMaterial");
            if (ui.MenuItem("VFX Effect"))
                RequestNamedCreate(NamedCreateKind::VfxEffect, "VfxEffect");
            if (ui.MenuItem("Material Graph"))
            {
                MaterialGraphCreation.Begin(Selection.empty() ? Keire::AssetId{} : Selection.back(),
                                            editor.AssetBrowserRecords());
                RequestNamedCreate(NamedCreateKind::MaterialGraph, "NewMaterialGraph");
            }
            if (const auto graphTemplate = DrawShaderGraphCreationMenu(ui))
            {
                PendingShaderGraphTemplate = *graphTemplate;
                RequestNamedCreate(NamedCreateKind::ShaderGraph, "NewShaderGraph");
            }
            if (auto reusable = ui.BeginMenu("Reusable Material Graphs"); reusable)
            {
                if (ui.MenuItem("Material Function"))
                    RequestNamedCreate(NamedCreateKind::MaterialFunction, "NewMaterialFunction");
                if (ui.MenuItem("Shader Function"))
                    RequestNamedCreate(NamedCreateKind::ShaderFunction, "NewShaderFunction");
                if (ui.MenuItem("Material Layer"))
                    RequestNamedCreate(NamedCreateKind::MaterialLayer, "NewMaterialLayer");
                if (ui.MenuItem("Material Layer Blend"))
                    RequestNamedCreate(NamedCreateKind::MaterialLayerBlend, "NewMaterialLayerBlend");
            }
            if (ui.MenuItem("Material Parameter Collection"))
                RequestNamedCreate(NamedCreateKind::MaterialParameterCollection, "GlobalMaterialParameters");
            if (ui.MenuItem("Material Instance"))
                RequestNamedCreate(NamedCreateKind::MaterialInstance, "NewMaterialInstance");
            const auto managedTypes = editor.AssetBrowserManagedAssetTypes();
            const bool hasAuthorableManagedTypes =
                std::ranges::any_of(managedTypes, [](const auto& type) { return !type.MenuPath.empty(); });
            DrawManagedDataCreateItems(ui, managedTypes);
            DrawManagedDataDiagnostics(ui, editor, hasAuthorableManagedTypes);
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
                    const auto destination = UniqueAssetBrowserPath(*record, record->RelativePath.parent_path(),
                                                                    *editor.AssetBrowserDatabase());
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

        void DuplicateFolders(IAssetBrowserController& editor)
        {
            DuplicateAssetBrowserFolders(FolderSelection, AssetRoot, editor);
        }

        void MoveAssets(const std::span<const Keire::AssetId> assets, const std::filesystem::path& folder,
                        IAssetBrowserController& editor)
        {
            MoveAssetBrowserAssets(assets, folder, editor);
        }

        void SetClipboard(const ClipboardMode mode, const std::span<const Keire::AssetId> assets)
        {
            ClipboardModeValue = mode;
            SetAssetBrowserClipboard(assets, Clipboard);
        }

        void SetFolderClipboard(const ClipboardMode mode, const std::span<const std::filesystem::path> folders)
        {
            ClipboardModeValue = mode;
            SetAssetBrowserFolderClipboard(folders, Clipboard);
        }

        void Paste(const std::filesystem::path& folder, IAssetBrowserController& editor)
        {
            PasteAssetBrowserClipboard(ClipboardModeValue, Clipboard, AssetRoot, folder, editor);
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
            PendingDeleteFolders.clear();
            OpenDeletePopup = true;
        }

        void RequestDeleteFolders(const std::span<const std::filesystem::path> folders)
        {
            PendingDeleteAssets = Selection;
            PendingDeleteFolders.assign(folders.begin(), folders.end());
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
                ui.Text(std::to_string(PendingDeleteAssets.size()) + " asset(s), " +
                        std::to_string(PendingDeleteFolders.size()) + " folder(s) selected");
                if (ui.Button("Move to Trash"))
                {
                    try
                    {
                        for (const auto& folder : PendingDeleteFolders)
                            editor.MutateAssetBrowser(
                                {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashFolder, .Source = folder}, {},
                                "Delete Folder");
                        for (const auto asset : PendingDeleteAssets)
                        {
                            editor.MutateAssetBrowser(
                                {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = asset}, {},
                                "Delete Asset");
                        }
                        Selection.clear();
                        FolderSelection.clear();
                        editor.SetAssetBrowserSelected({});
                        editor.SetAssetBrowserStatus("Moved selection to recoverable project trash.");
                        PendingDeleteAssets.clear();
                        PendingDeleteFolders.clear();
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
                    PendingDeleteFolders.clear();
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

                    if (auto disabled = ui.BeginDisabled(TrashEntries.empty()); disabled)
                    {
                        if (ui.Button("Restore All"))
                        {
                            for (const auto& record : TrashEntries)
                            {
                                editor.MutateAssetBrowser(
                                    {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash, .Trash = record.Id},
                                    {}, {});
                            }
                            editor.SetAssetBrowserStatus("Restored all entries from trash.");
                            TrashEntries = editor.AssetBrowserDatabase()->TrashRecords();
                        }
                        ui.SameLine();
                        if (ui.Button("Delete All Permanently"))
                        {
                            for (const auto& record : TrashEntries)
                            {
                                editor.MutateAssetBrowser(
                                    {.Kind = Keire::Detail::AssetWorkerMutationKind::PermanentlyDeleteTrash,
                                     .Trash = record.Id},
                                    {}, {});
                            }
                            editor.SetAssetBrowserStatus("Permanently removed all trash entries.");
                            TrashEntries = editor.AssetBrowserDatabase()->TrashRecords();
                        }
                    }

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

        void DrawRenamePopups(Keire::UiFrame& ui, IAssetBrowserController& editor);

        void DrawAssetContext(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                              IAssetBrowserController& editor, const std::string_view id);

        void DrawFolderContext(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor,
                               const std::string_view id);

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

        void ToggleAssetChildren(const Keire::AssetId asset)
        {
            if (ExpandedParents.contains(asset))
                ExpandedParents.erase(asset);
            else
                ExpandedParents.insert(asset);
        }

        void DrawAsset(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, IAssetBrowserController& editor,
                       const bool grid, const std::size_t depth, const std::size_t directChildCount,
                       const bool hasChildren, const bool forceExpanded = false, const float rowHeight = 42.0F)
        {
            auto id = ui.PushId(record.Id.ToString());
            const auto image = Detail::ResolveAssetBrowserImage(
                record, Images, AssetFallbackImage, ShaderGraphFallbackImage, MaterialGraphFallbackImage,
                MaterialInstanceFallbackImage, VfxFallbackImage, AudioMixerFallbackImage, AnimationFallbackImage);
            const bool selected = std::ranges::find(Selection, record.Id) != Selection.end();
            const auto fullName = DisplayName(record.RelativePath);
            const bool expanded = forceExpanded || ExpandedParents.contains(record.Id);
            const bool failed =
                editor.AssetBrowserDatabase()->ImportStatus(record.Id).State == Keire::AssetImportState::Failed;
            bool open = false;
            if (grid)
            {
                const auto size = Keire::UiSize{std::max(ui.ContentAvailable().Width, 1.0F),
                                                Detail::AssetBrowserGridCardHeight(ui, ThumbnailSize)};
                const bool activated = ui.InvisibleButton("AssetCard", size);
                const auto state = ui.LastItemState();
                const auto area = ui.LastItemRect();
                const auto disclosure = Detail::AssetBrowserGridDisclosureArea(area, ThumbnailSize);
                const bool disclosureHovered = hasChildren && disclosure.Contains(ui.PointerState().Position);
                if (activated)
                {
                    if (disclosureHovered)
                        ToggleAssetChildren(record.Id);
                    else
                        SelectFromClick(record.Id, ui, editor);
                }
                open = state.DoubleClicked && !disclosureHovered;
                if (state.Hovered && disclosureHovered)
                    ui.SetTooltip(expanded ? "Collapse generated assets" : "Show generated assets", {.Delayed = true});
                else
                    Detail::DrawAssetBrowserTooltip(ui, record, AssetRoot, *editor.AssetBrowserDatabase());
                if (!disclosureHovered)
                    DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "AssetCardContext");
                Detail::DrawAssetBrowserGridItemVisual(ui, area, image, fullName, AssetTypeName(record), selected,
                                                       state.Hovered, hasChildren, directChildCount, expanded, failed,
                                                       editor.AssetBrowserTheme(), ThumbnailSize);
            }
            else
            {
                auto cursor = ui.CursorPosition();
                cursor.X += static_cast<float>(depth) * 20.0F;
                ui.SetCursorPosition(cursor);
                const auto size = Keire::UiSize{std::max(ui.ContentAvailable().Width, 1.0F), rowHeight};
                const bool activated = ui.InvisibleButton("AssetRow", size);
                const auto state = ui.LastItemState();
                const auto area = ui.LastItemRect();
                const auto disclosure = Detail::AssetBrowserListDisclosureArea(area);
                const bool disclosureHovered = hasChildren && disclosure.Contains(ui.PointerState().Position);
                if (activated)
                {
                    if (disclosureHovered)
                        ToggleAssetChildren(record.Id);
                    else
                        SelectFromClick(record.Id, ui, editor);
                }
                open = state.DoubleClicked && !disclosureHovered;
                if (state.Hovered && disclosureHovered)
                    ui.SetTooltip(expanded ? "Collapse generated assets" : "Show generated assets", {.Delayed = true});
                else
                    Detail::DrawAssetBrowserTooltip(ui, record, AssetRoot, *editor.AssetBrowserDatabase());
                if (!disclosureHovered)
                    DrawAssetDragSource(ui, record);
                DrawAssetContext(ui, record, editor, "AssetRowContext");
                Detail::DrawAssetBrowserListItemVisual(ui, area, image, fullName, AssetTypeName(record), selected,
                                                       state.Hovered, hasChildren, directChildCount, expanded, failed,
                                                       editor.AssetBrowserTheme());
                if (depth > 0)
                {
                    const float railX = area.Minimum.X - 10.0F;
                    const auto rail = Detail::AssetBrowserColorWithAlpha(editor.AssetBrowserTheme().Accent, 0.38F);
                    ui.DrawLine({railX, area.Minimum.Y - 4.0F}, {railX, area.Maximum.Y + 4.0F}, rail);
                    ui.DrawLine({railX, area.Minimum.Y + area.Size().Height * 0.5F},
                                {area.Minimum.X - 3.0F, area.Minimum.Y + area.Size().Height * 0.5F}, rail);
                }
            }
            if (open)
                Open(record, editor);
            if (RevealAsset == record.Id)
                RevealAsset = {};
        }

        void DrawGridAssetGroup(Keire::UiFrame& ui, const std::span<const AssetBrowserHierarchyEntry> entries,
                                IAssetBrowserController& editor, const bool forceExpanded)
        {
            if (entries.empty() || !entries.front().Record)
                return;

            const auto& parent = entries.front();
            DrawAsset(ui, *parent.Record, editor, true, 0, parent.DirectChildCount, parent.HasChildren, forceExpanded);
            if (entries.size() == 1)
                return;

            const auto& theme = editor.AssetBrowserTheme();
            const float width = std::max(ui.ContentAvailable().Width, 1.0F);
            auto trayId = ui.PushId("GeneratedAssetsTray-" + parent.Record->Id.ToString());
            (void)ui.InvisibleButton("GeneratedAssetsHeader", {width, 24.0F});
            const auto header = ui.LastItemRect();
            ui.DrawFilledRectangle(header, theme.RaisedPanel, 3.0F);
            ui.DrawRectangle(header, theme.Border, 1.0F, 3.0F);
            ui.DrawFilledRectangle({{header.Minimum.X, header.Minimum.Y}, {header.Minimum.X + 3.0F, header.Maximum.Y}},
                                   theme.Accent, 3.0F);
            ui.DrawOverlayText({header.Minimum.X + 9.0F, header.Minimum.Y + 4.0F}, theme.MutedText, "Generated assets",
                               11.0F, header);
            const auto headerCount = std::to_string(entries.size() - 1);
            const auto headerCountSize = ui.MeasureText(headerCount, 11.0F);
            ui.DrawOverlayText({header.Maximum.X - headerCountSize.Width - 9.0F, header.Minimum.Y + 4.0F},
                               theme.MutedText, headerCount, 11.0F, header);

            for (std::size_t index = 1; index < entries.size(); ++index)
            {
                const auto& child = entries[index];
                if (!child.Record)
                    continue;
                auto childId = ui.PushId("GeneratedAsset-" + child.Record->Id.ToString());
                auto cursor = ui.CursorPosition();
                cursor.X += 14.0F + static_cast<float>(child.Depth - 1) * 14.0F;
                ui.SetCursorPosition(cursor);
                const auto rowStart = ui.CursorScreenPosition();
                DrawAsset(ui, *child.Record, editor, false, 0, child.DirectChildCount, child.HasChildren, forceExpanded,
                          44.0F);

                const auto rail = Detail::AssetBrowserColorWithAlpha(theme.Accent, 0.38F);
                const float railTop = rowStart.Y - 4.0F;
                const float railBottom = rowStart.Y + 48.0F;
                for (std::size_t level = 0; level < child.Depth; ++level)
                {
                    const float railX = header.Minimum.X + 7.0F + static_cast<float>(level) * 14.0F;
                    ui.DrawLine({railX, railTop}, {railX, railBottom}, rail);
                    if (level + 1 == child.Depth)
                    {
                        ui.DrawLine({railX, rowStart.Y + 22.0F}, {rowStart.X - 3.0F, rowStart.Y + 22.0F}, rail);
                    }
                }
            }
        }

        void AcceptFolderDrop(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor)
        {
            if (auto target = ui.BeginDragTarget(); target)
                Detail::AcceptAssetBrowserFolderPayloads(ui, folder, editor);
        }

        void AcceptFolderDrop(Keire::UiFrame& ui, const Keire::UiItemRect area, const std::filesystem::path& folder,
                              IAssetBrowserController& editor)
        {
            if (auto target = ui.BeginDragTarget(area, "FolderCardDrop"); target)
                Detail::AcceptAssetBrowserFolderPayloads(ui, folder, editor);
        }

        void DrawFolder(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor,
                        const bool grid)
        {
            auto id = ui.PushId(folder.generic_string());
            const bool selected = std::ranges::find(FolderSelection, folder) != FolderSelection.end();
            const auto drawDragSource = [&]
            {
                if (auto source = ui.BeginDragSource(); source)
                {
                    const auto payloadFolders = selected ? FolderSelection : std::vector<std::filesystem::path>{folder};
                    const auto value = EncodeFolderPayload(payloadFolders);
                    ui.SetDragPayload("KEIRE_FOLDERS", std::as_bytes(std::span(value.data(), value.size())));
                    ui.Text(payloadFolders.size() == 1 ? folder.filename().string()
                                                       : std::to_string(payloadFolders.size()) + " folders");
                }
            };
            const auto size = grid ? Keire::UiSize{std::max(ui.ContentAvailable().Width, 1.0F),
                                                   Detail::AssetBrowserGridCardHeight(ui, ThumbnailSize)}
                                   : Keire::UiSize{std::max(ui.ContentAvailable().Width, 1.0F), 42.0F};
            const bool activated = ui.InvisibleButton(grid ? "FolderCard" : "FolderRow", size);
            const auto state = ui.LastItemState();
            const auto area = ui.LastItemRect();
            if (activated)
                SelectFolderFromClick(folder, ui, editor);
            const bool open = state.DoubleClicked;
            if (state.Hovered)
                ui.SetTooltip((std::filesystem::path("Assets") / folder).generic_string(), {.Delayed = true});
            drawDragSource();
            DrawFolderContext(ui, folder, editor, grid ? "FolderCardContext" : "FolderRowContext");
            if (grid)
                Detail::DrawAssetBrowserGridItemVisual(ui, area, FolderImage, folder.filename().string(), "Folder",
                                                       selected, state.Hovered, false, 0, false, false,
                                                       editor.AssetBrowserTheme(), ThumbnailSize);
            else
                Detail::DrawAssetBrowserListItemVisual(ui, area, FolderImage, folder.filename().string(), "Folder",
                                                       selected, state.Hovered, false, 0, false, false,
                                                       editor.AssetBrowserTheme());
            ExternalDropTargets.push_back({area, folder});
            AcceptFolderDrop(ui, area, folder, editor);
            if (open)
                CurrentFolder = folder;
        }

        void DrawFolderTree(Keire::UiFrame& ui, const std::filesystem::path& relative, IAssetBrowserController& editor)
        {
            const auto children = DirectChildAssetFolders(FolderCache.Folders(), relative);
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
                FolderSelection = VisibleFolderOrder;
                for (const auto* record : visible)
                    Selection.push_back(record->Id);
                editor.SetAssetBrowserSelected(Selection.empty() ? Keire::AssetId{} : Selection.back());
            }
            if (ui.Shortcut({.Key = Keire::UiKey::D, .Primary = true}))
            {
                DuplicateAssets(editor);
                DuplicateFolders(editor);
            }
            if (ui.Shortcut({.Key = Keire::UiKey::X, .Primary = true}))
            {
                SetClipboard(ClipboardMode::Cut, Selection);
                for (const auto& folder : FolderSelection)
                    Clipboard.push_back({Keire::AssetId{}, folder});
            }
            if (ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
            {
                SetClipboard(ClipboardMode::Copy, Selection);
                for (const auto& folder : FolderSelection)
                    Clipboard.push_back({Keire::AssetId{}, folder});
            }
            if (ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}))
                Paste(CurrentFolder, editor);
            if (ui.Shortcut({Keire::UiKey::Delete}))
            {
                if (FolderSelection.empty())
                    RequestDeleteAssets(editor);
                else
                    RequestDeleteFolders(FolderSelection);
            }
            if (ui.Shortcut({Keire::UiKey::F2}) && Selection.size() == 1 && FolderSelection.empty())
                if (const auto record = editor.AssetBrowserDatabase()->Find(Selection.front()))
                    BeginAssetRename(*record);
            if (ui.Shortcut({Keire::UiKey::F2}) && FolderSelection.size() == 1 && Selection.empty())
                BeginFolderRename(FolderSelection.front());
            if (ui.Shortcut({Keire::UiKey::Enter}) && Selection.size() + FolderSelection.size() == 1)
            {
                if (!FolderSelection.empty())
                    CurrentFolder = FolderSelection.front();
                else if (const auto record = editor.AssetBrowserDatabase()->Find(Selection.front()))
                    Open(*record, editor);
            }
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
                Detail::RevealAssetBrowserPath(editor, AssetRoot / CurrentFolder);
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

            const auto folders = DirectChildAssetFolders(FolderCache.Folders(), CurrentFolder);
            VisibleFolderOrder.clear();
            for (const auto& folder : folders)
                if (Search.empty() || folder.filename().string().find(Search) != std::string::npos)
                    VisibleFolderOrder.push_back(folder);
            (void)VisibleRecords.Refresh(editor.AssetBrowserRecords(), editor.AssetBrowserRecordRevision(),
                                         CurrentFolder, Search);
            const auto assets = VisibleRecords.Records();
            std::vector<Keire::AssetId> expandedParents(ExpandedParents.begin(), ExpandedParents.end());
            const auto hierarchy = BuildAssetBrowserHierarchy(assets, expandedParents, !Search.empty());
            std::vector<const Keire::AssetSourceRecord*> displayedAssets;
            displayedAssets.reserve(hierarchy.size());
            for (const auto& entry : hierarchy)
                displayedAssets.push_back(entry.Record);
            VisibleSelectionOrder.clear();
            VisibleSelectionOrder.reserve(displayedAssets.size());
            for (const auto* record : displayedAssets)
                VisibleSelectionOrder.push_back(record->Id);

            if (Mode == ViewMode::List)
            {
                for (const auto& folder : folders)
                    if (Search.empty() || folder.filename().string().find(Search) != std::string::npos)
                        DrawFolder(ui, folder, editor, false);
                for (const auto& entry : hierarchy)
                    DrawAsset(ui, *entry.Record, editor, false, entry.Depth, entry.DirectChildCount, entry.HasChildren,
                              !Search.empty());
                const auto remaining = ui.ContentAvailable();
                (void)ui.InvisibleButton("AssetCurrentFolderDrop",
                                         {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
                if (auto context = ui.BeginItemContextMenu("AssetBlankDropContext"); context)
                    DrawBlankContextItems(ui, editor);
                AcceptFolderDrop(ui, contentDropArea, CurrentFolder, editor);
                DrawKeyboardCommands(ui, displayedAssets, editor);
                DrawBlankContext(ui, editor);
                return;
            }

            const float availableWidth = std::max(ui.ContentAvailable().Width, 0.0F);
            if (availableWidth > 1.0F)
            {
                const float cellWidth = std::max(ThumbnailSize + 40.0F, 136.0F);
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
                    for (std::size_t index = 0; index < hierarchy.size();)
                    {
                        const auto begin = index;
                        ++index;
                        while (index < hierarchy.size() && hierarchy[index].Depth > 0)
                            ++index;
                        nextCell();
                        const auto group = std::span(hierarchy).subspan(begin, index - begin);
                        if (group.front().HasChildren)
                            DrawGridAssetGroup(ui, group, editor, !Search.empty());
                        else
                            DrawAsset(ui, *group.front().Record, editor, true, 0, 0, false);
                    }
                }
            }
            const auto remaining = ui.ContentAvailable();
            (void)ui.InvisibleButton("AssetCurrentFolderDrop",
                                     {std::max(remaining.Width, 1.0F), std::max(remaining.Height, 24.0F)});
            if (auto context = ui.BeginItemContextMenu("AssetBlankDropContext"); context)
                DrawBlankContextItems(ui, editor);
            AcceptFolderDrop(ui, contentDropArea, CurrentFolder, editor);
            DrawKeyboardCommands(ui, displayedAssets, editor);
            DrawBlankContext(ui, editor);
        }

        void Draw(Keire::UiFrame& ui, IAssetBrowserController& editor);

        IAssetBrowserController& Controller;
        Keire::UiPanelRegistration Registration;
        std::filesystem::path ProjectRoot;
        std::filesystem::path AssetRoot;
        std::filesystem::path CurrentFolder;
        std::filesystem::path PreferencePath;
        AssetBrowserFolderCache FolderCache;
        AssetBrowserRecordViewCache VisibleRecords;
        std::chrono::steady_clock::time_point NextFolderRefresh;
        std::filesystem::path RenamingFolder;
        std::vector<std::filesystem::path> PendingDeleteFolders;
        std::unique_ptr<ThumbnailService> Thumbnails;
        Keire::Ref<Keire::JobSystem> Scheduler;
        std::unordered_map<Keire::AssetId, Keire::Ref<Keire::UiImage>> Images;
        std::unordered_map<Keire::AssetId, std::string> ImageDigests;
        std::uint64_t ObservedRecordRevision = 0;
        Keire::Ref<Keire::UiImage> FolderImage;
        Keire::Ref<Keire::UiImage> AssetFallbackImage;
        Keire::Ref<Keire::UiImage> ShaderGraphFallbackImage;
        Keire::Ref<Keire::UiImage> MaterialGraphFallbackImage;
        Keire::Ref<Keire::UiImage> MaterialInstanceFallbackImage;
        Keire::Ref<Keire::UiImage> VfxFallbackImage;
        Keire::Ref<Keire::UiImage> AudioMixerFallbackImage;
        Keire::Ref<Keire::UiImage> AnimationFallbackImage;
        Keire::Ref<Keire::UndoContext> Undo;
        std::vector<Keire::AssetId> Selection;
        std::vector<Keire::AssetId> VisibleSelectionOrder;
        std::unordered_set<Keire::AssetId> ExpandedParents;
        std::vector<std::filesystem::path> FolderSelection;
        std::vector<std::filesystem::path> VisibleFolderOrder;
        std::vector<Keire::AssetId> PendingDeleteAssets;
        std::vector<Keire::AssetTrashRecord> TrashEntries;
        std::vector<ClipboardEntry> Clipboard;
        std::vector<AssetBrowserDropTarget> ExternalDropTargets;
        Keire::AssetId Renaming;
        Keire::AssetId RevealAsset;
        Keire::AssetId SelectionAnchor;
        std::filesystem::path FolderSelectionAnchor;
        Keire::AssetId PendingVariantBase;
        MaterialGraphCreationPicker MaterialGraphCreation;
        std::string Search;
        std::string RenameBuffer;
        std::string CreateNameBuffer;
        std::string TrashError;
        std::filesystem::path PendingCreateFolder;
        NamedCreateKind PendingCreateKind = NamedCreateKind::None;
        Keire::ShaderGraphTemplate PendingShaderGraphTemplate = Keire::ShaderGraphTemplate::Lit;
        Keire::ManagedTypeId PendingManagedType;
        std::optional<Keire::InputActionAssetDefinition> PendingInputActions;
        AssetPackageSelection PendingPackageSelection;
        AssetPackageDraft PendingPackageDraft;
        ViewMode Mode = ViewMode::Grid;
        ClipboardMode ClipboardModeValue = ClipboardMode::Empty;
        float ThumbnailSize = 88.0F;
        float FolderPaneWidth = 210.0F;
        bool OpenRenamePopup = false;
        bool OpenNamedCreatePopup = false;
        bool FocusCreateName = false;
        bool FocusRenameName = false;
        bool OpenFolderRenamePopup = false;
        bool OpenPackageCreatePopup = false;
        bool OpenDeletePopup = false;
        bool OpenTrashPopup = false;
        bool Focused = false;
    };
} // namespace KeireEditor
