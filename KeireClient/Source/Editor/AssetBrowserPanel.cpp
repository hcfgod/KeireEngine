#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireClient/Editor/AssetBrowserPresentation.h"

#include "KeireClient/Editor/AssetBrowserFolderCache.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
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
#include <optional>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
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
                    ui.SetTooltip(diagnostic.Message, {.Delayed = true});
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
                                created = Detail::CreateNamedAsset(editor, PendingCreateKind, CreateNameBuffer,
                                                                   PendingManagedType, PendingInputActions,
                                                                   PendingVariantBase, MaterialGraphCreation.Shader(),
                                                                   PendingShaderGraphTemplate);
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
                            : "Package " + std::to_string(PendingPackageSelection.Assets.size()) +
                                  " selected asset(s)");
                (void)ui.InputText("Display name", PendingPackageDraft.DisplayName);
                (void)ui.InputText("Package ID", PendingPackageDraft.PackageId);
                (void)ui.InputText("Version", PendingPackageDraft.Version);
                (void)ui.InputText("Publisher ID", PendingPackageDraft.PublisherId);
                (void)ui.InputText("Minimum Kéire version", PendingPackageDraft.MinimumEngineVersion);
                (void)ui.InputText("Summary", PendingPackageDraft.Summary);
                const bool complete = !PendingPackageDraft.DisplayName.empty() &&
                                      !PendingPackageDraft.PackageId.empty() && !PendingPackageDraft.Version.empty() &&
                                      !PendingPackageDraft.PublisherId.empty() &&
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
                    const auto displayName =
                        Selection.size() == 1 ? DisplayName(record.RelativePath) : "Selected Assets";
                    RequestPackageCreate({.Assets = Selection}, displayName);
                }
                if (ui.MenuItem("Delete"))
                    RequestDeleteAssets(editor);
                ui.Separator();
                if (ui.MenuItem("Reimport"))
                    editor.ImportAssetBrowserAssets();
                if (record.Type == Keire::MeshAsset::StaticType() && !record.SubAssets.empty() &&
                    ui.MenuItem("Extract Materials"))
                    editor.ExtractAssetBrowserMaterials(record.Id);
                if (ui.MenuItem("Reveal in File Explorer"))
                    Detail::RevealAssetBrowserPath(editor, AssetRoot / record.RelativePath);
                if (ui.MenuItem("Copy Relative Path"))
                    Detail::CopyAssetBrowserText(
                        editor, (std::filesystem::path("Assets") / record.RelativePath).generic_string());
                if (ui.MenuItem("Copy Asset ID"))
                    Detail::CopyAssetBrowserText(editor, record.Id.ToString());
            }
        }

        void DrawFolderContext(Keire::UiFrame& ui, const std::filesystem::path& folder, IAssetBrowserController& editor,
                               const std::string_view id)
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
                    editor.ImportAssetBrowserAssets();
                if (ui.MenuItem("Reveal in File Explorer"))
                    Detail::RevealAssetBrowserPath(editor, AssetRoot / folder);
                if (ui.MenuItem("Copy Relative Path"))
                    Detail::CopyAssetBrowserText(editor, (std::filesystem::path("Assets") / folder).generic_string());
            }
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
                if (!ShaderGraphFallbackImage)
                {
                    AssetFallbackImage = ui.CreateImage(96, 96, MakeAssetFallbackThumbnail({}, 96, 96));
                    ShaderGraphFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::ShaderGraphAsset::StaticType(), 96, 96));
                    MaterialGraphFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::MaterialGraphAsset::StaticType(), 96, 96));
                    MaterialInstanceFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::MaterialInstanceAsset::StaticType(), 96, 96));
                    VfxFallbackImage =
                        ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), 96, 96));
                    AudioMixerFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::AudioMixerAsset::StaticType(), 96, 96));
                    AnimationFallbackImage = ui.CreateImage(
                        96, 96, MakeAssetFallbackThumbnail(Keire::AnimationSourceAsset::StaticType(), 96, 96));
                }
                for (auto& completed : Thumbnails->DrainCompleted())
                    Images[completed.Asset] = ui.CreateImage(completed.Width, completed.Height, completed.Pixels);
                const auto records = editor.AssetBrowserRecords();
                (void)VisibleRecords.Refresh(records, recordRevision, CurrentFolder, Search);
                for (const auto* visibleRecord : VisibleRecords.Records())
                {
                    const auto& record = *visibleRecord;
                    if (!recordsChanged && (Images.contains(record.Id) || ImageDigests.contains(record.Id)))
                        continue;
                    std::string digest = record.SourceDigest + record.MetadataDigest;
                    for (const auto dependency : record.Dependencies)
                    {
                        digest += dependency.ToString();
                        if (const auto dependencyRecord = editor.AssetBrowserDatabase()->Find(dependency))
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
                        if (handle.State() != Keire::AssetState::Reloading)
                            request.PreviewAsset = handle.TryGetLoaded();
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                    }
                    else if (assets && record.Type == Keire::MeshAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::MeshAsset>(record.Id, Keire::AssetPriority::Low);
                        if (handle.State() != Keire::AssetState::Reloading)
                            request.PreviewAsset = handle.TryGetLoaded();
                        request.Missing = handle.State() == Keire::AssetState::Failed;
                        if (!request.PreviewAsset && request.Missing)
                            request.PreviewAsset = handle.Get();
                        ready = static_cast<bool>(request.PreviewAsset);
                    }
                    else if (assets && record.Type == Keire::AudioClipAsset::StaticType())
                    {
                        const auto handle = assets->Load<Keire::AudioClipAsset>(record.Id, Keire::AssetPriority::Low);
                        if (handle.State() != Keire::AssetState::Reloading)
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
                               std::to_string(Selection.size() + FolderSelection.size()) + " selected  |  " +
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
        return ResolveAssetBrowserDropFolder(m_Impl->ExternalDropTargets, position, m_Impl->CurrentFolder);
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
