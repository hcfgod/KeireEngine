#include "KeireClientInternal/Editor/AssetBrowserPanelInternal.h"

namespace KeireEditor
{
    void AssetBrowserPanel::Impl::Draw(Keire::UiFrame& ui, IAssetBrowserController& editor)
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
                ShaderGraphFallbackImage =
                    ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::ShaderGraphAsset::StaticType(), 96, 96));
                MaterialGraphFallbackImage =
                    ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::MaterialGraphAsset::StaticType(), 96, 96));
                MaterialInstanceFallbackImage = ui.CreateImage(
                    96, 96, MakeAssetFallbackThumbnail(Keire::MaterialInstanceAsset::StaticType(), 96, 96));
                VfxFallbackImage =
                    ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), 96, 96));
                AudioMixerFallbackImage =
                    ui.CreateImage(96, 96, MakeAssetFallbackThumbnail(Keire::AudioMixerAsset::StaticType(), 96, 96));
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
                    const auto prefabSource =
                        Keire::Detail::ReadTextFile(AssetRoot / record.RelativePath, std::size_t{64} * 1024U * 1024U);
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
                                    const auto dependencySource = Keire::Detail::ReadTextFile(
                                        AssetRoot / dependencyRecord->RelativePath, std::size_t{64} * 1024U * 1024U);
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
                                    request.Digest += dependencyRecord->SourceDigest + dependencyRecord->MetadataDigest;
                                return loaded;
                            });
                        if (dependenciesReady)
                        {
                            auto previewScene = Keire::CreateRef<Keire::Scene>(record.Id, composed);
                            for (const auto& entity : previewScene->Query<Keire::MeshRendererComponent>())
                            {
                                const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
                                const auto transform = entity.GetComponent<Keire::TransformComponent>();
                                if (!renderer || !renderer->Enabled() || !renderer->Visible() || !renderer->Mesh() ||
                                    !transform)
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
            if (ui.IconButton("ProjectView", Mode == ViewMode::List ? Keire::UiIcon::Grid : Keire::UiIcon::List, false,
                              {28.0F, 24.0F}))
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
                (void)ui.Splitter(Keire::UiAxis::Horizontal, "AssetBrowserSplitter", FolderPaneWidth, contentPaneWidth,
                                  minimumFolderWidth, minimumContentWidth, splitterThickness);
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
} // namespace KeireEditor
