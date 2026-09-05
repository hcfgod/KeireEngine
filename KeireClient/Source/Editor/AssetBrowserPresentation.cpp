#include "KeireClient/Editor/AssetBrowserPresentation.h"

#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace KeireEditor::Detail
{
    bool CreateNamedAsset(IAssetBrowserController& controller, const NamedAssetCreationKind kind,
                          const std::string_view name, const Keire::ManagedTypeId managedType,
                          const std::optional<Keire::InputActionAssetDefinition>& inputActions,
                          const Keire::AssetId variantBase, const Keire::AssetId materialShader,
                          const Keire::ShaderGraphTemplate shaderTemplate)
    {
        switch (kind)
        {
        case NamedAssetCreationKind::Scene:
            return controller.CreateAssetBrowserScene(name);
        case NamedAssetCreationKind::Material:
            return controller.CreateAssetBrowserMaterial(name);
        case NamedAssetCreationKind::AnimationGraph:
            return controller.CreateAssetBrowserAnimationGraph(name);
        case NamedAssetCreationKind::ProceduralMotionProfile:
            return controller.CreateAssetBrowserProceduralMotionProfile(name);
        case NamedAssetCreationKind::Script:
            return controller.CreateAssetBrowserScript(name);
        case NamedAssetCreationKind::ScriptableObjectScript:
            return controller.CreateAssetBrowserScriptableObjectScript(name);
        case NamedAssetCreationKind::ManagedAssembly:
            return controller.CreateAssetBrowserManagedAssembly(name);
        case NamedAssetCreationKind::ManagedData:
            return controller.CreateAssetBrowserManagedData(managedType, name);
        case NamedAssetCreationKind::AudioMixer:
            return controller.CreateAssetBrowserAudioMixer(name);
        case NamedAssetCreationKind::PhysicsMaterial:
            return controller.CreateAssetBrowserPhysicsMaterial(name);
        case NamedAssetCreationKind::VfxEffect:
            return controller.CreateAssetBrowserVfxEffect(name);
        case NamedAssetCreationKind::UiDocument:
            return controller.CreateAssetBrowserUiDocument(name);
        case NamedAssetCreationKind::UiStyleSheet:
            return controller.CreateAssetBrowserUiStyleSheet(name);
        case NamedAssetCreationKind::UiFontFamily:
            return controller.CreateAssetBrowserUiFontFamily(name, variantBase);
        case NamedAssetCreationKind::MaterialGraph:
            return controller.CreateAssetBrowserMaterialGraph(name, materialShader);
        case NamedAssetCreationKind::ShaderGraph:
            return controller.CreateAssetBrowserShaderGraph(name, shaderTemplate);
        case NamedAssetCreationKind::MaterialFunction:
            return controller.CreateAssetBrowserReusableGraph(name, Keire::ShaderGraphPurpose::MaterialFunction);
        case NamedAssetCreationKind::ShaderFunction:
            return controller.CreateAssetBrowserReusableGraph(name, Keire::ShaderGraphPurpose::ShaderFunction);
        case NamedAssetCreationKind::MaterialLayer:
            return controller.CreateAssetBrowserReusableGraph(name, Keire::ShaderGraphPurpose::MaterialLayer);
        case NamedAssetCreationKind::MaterialLayerBlend:
            return controller.CreateAssetBrowserReusableGraph(name, Keire::ShaderGraphPurpose::MaterialLayerBlend);
        case NamedAssetCreationKind::MaterialParameterCollection:
            return controller.CreateAssetBrowserMaterialParameterCollection(name);
        case NamedAssetCreationKind::MaterialInstance:
            return controller.CreateAssetBrowserMaterialInstance(name, variantBase);
        case NamedAssetCreationKind::Prefab:
            return controller.CreateAssetBrowserPrefab(name);
        case NamedAssetCreationKind::PrefabVariant:
            return controller.CreateAssetBrowserPrefabVariant(variantBase, name);
        case NamedAssetCreationKind::Shader:
            return controller.CreateAssetBrowserShader(name);
        case NamedAssetCreationKind::InputActions:
            return inputActions && controller.CreateAssetBrowserInputActions(*inputActions, name);
        case NamedAssetCreationKind::None:
            return false;
        }
        return false;
    }

    void CopyAssetBrowserText(IAssetBrowserController& controller, const std::string_view value)
    {
        try
        {
            controller.CopyAssetBrowserText(value);
            controller.SetAssetBrowserStatus("Copied to clipboard.");
        }
        catch (const std::exception& error)
        {
            controller.ReportAssetBrowserError(std::string("Clipboard operation failed: ") + error.what());
        }
    }

    void RevealAssetBrowserPath(IAssetBrowserController& controller, const std::filesystem::path& path)
    {
        std::string diagnostic;
        if (!Keire::Detail::RevealInFileManager(path, diagnostic))
            controller.ReportAssetBrowserError("Reveal failed: " + diagnostic);
    }

    Keire::Ref<Keire::UiImage> ResolveAssetBrowserImage(
        const Keire::AssetSourceRecord& record,
        const std::unordered_map<Keire::AssetId, Keire::Ref<Keire::UiImage>>& images,
        const Keire::Ref<Keire::UiImage>& assetFallback, const Keire::Ref<Keire::UiImage>& shaderGraphFallback,
        const Keire::Ref<Keire::UiImage>& materialGraphFallback,
        const Keire::Ref<Keire::UiImage>& materialInstanceFallback, const Keire::Ref<Keire::UiImage>& vfxFallback,
        const Keire::Ref<Keire::UiImage>& audioMixerFallback, const Keire::Ref<Keire::UiImage>& animationFallback)
    {
        if (const auto found = images.find(record.Id); found != images.end())
            return found->second;
        if (record.Type == Keire::ShaderGraphAsset::StaticType())
            return shaderGraphFallback;
        if (record.Type == Keire::MaterialGraphAsset::StaticType())
            return materialGraphFallback;
        if (record.Type == Keire::MaterialInstanceAsset::StaticType() ||
            record.Type == Keire::ShaderGraphInstanceAsset::StaticType())
            return materialInstanceFallback;
        if (record.Type == Keire::VfxEffectAsset::StaticType())
            return vfxFallback;
        if (record.Type == Keire::AudioMixerAsset::StaticType())
            return audioMixerFallback;
        if (record.Type == Keire::AnimationSourceAsset::StaticType() ||
            record.Type == Keire::AnimationClipAsset::StaticType())
            return animationFallback;
        return assetFallback;
    }

    void DrawAssetBrowserTooltip(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                                 const std::filesystem::path& assetRoot, const Keire::AssetDatabase& database)
    {
        if (!ui.LastItemState().Hovered)
            return;
        std::error_code error;
        const auto bytes = std::filesystem::file_size(assetRoot / record.RelativePath, error);
        const auto status = database.ImportStatus(record.Id);
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

    void AcceptAssetBrowserFolderPayloads(Keire::UiFrame& ui, const std::filesystem::path& folder,
                                          IAssetBrowserController& controller)
    {
        std::vector<std::byte> payload;
        if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
        {
            try
            {
                MoveAssetBrowserAssets(DecodeAssetPayload(payload), folder, controller);
            }
            catch (const std::exception& error)
            {
                controller.ReportAssetBrowserError(std::string("Asset move failed: ") + error.what());
            }
        }
        payload.clear();
        if (ui.AcceptDragPayload("KEIRE_FOLDERS", payload))
        {
            try
            {
                const auto sources = DecodeFolderPayload(payload);
                for (const auto& source : sources)
                {
                    const auto destination = folder / source.filename();
                    controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                   .Source = source,
                                                   .Destination = destination},
                                                  {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                   .Source = destination,
                                                   .Destination = source},
                                                  "Move Folder");
                }
                controller.SetAssetBrowserStatus("Queued " + std::to_string(sources.size()) + " folder move(s).");
            }
            catch (const std::exception& error)
            {
                controller.ReportAssetBrowserError(std::string("Folder move failed: ") + error.what());
            }
        }
        payload.clear();
        if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
        {
            try
            {
                controller.CreateAssetBrowserPrefabFromObject(DecodeSingleAssetPayload(payload), folder);
            }
            catch (const std::exception& error)
            {
                controller.ReportAssetBrowserError(std::string("Prefab creation failed: ") + error.what());
            }
        }
    }

    Keire::UiColor AssetBrowserColorWithAlpha(Keire::UiColor color, const float alpha) noexcept
    {
        color.Alpha = alpha;
        return color;
    }

    float AssetBrowserGridCardHeight(Keire::UiFrame& ui, const float thumbnailSize)
    {
        const float lineHeight = std::max(ui.MeasureText("Ag").Height, 12.0F);
        return thumbnailSize + lineHeight * 2.0F + 26.0F;
    }

    namespace
    {
        [[nodiscard]] Keire::UiItemRect GridThumbnailArea(const Keire::UiItemRect card,
                                                          const float thumbnailSize) noexcept
        {
            const float left = card.Minimum.X + (card.Size().Width - thumbnailSize) * 0.5F;
            return {{left, card.Minimum.Y + 8.0F}, {left + thumbnailSize, card.Minimum.Y + 8.0F + thumbnailSize}};
        }

        void DrawItemChrome(Keire::UiFrame& ui, const Keire::UiItemRect area, const bool selected, const bool hovered,
                            const Keire::UiThemeDefinition& theme, const float rounding)
        {
            ui.DrawFilledRectangle(area, theme.Panel, rounding);
            if (selected)
                ui.DrawFilledRectangle(area, theme.Selection, rounding);
            ui.DrawRectangle(area, selected ? theme.Accent : (hovered ? theme.AccentHovered : theme.Border),
                             selected ? 2.0F : 1.0F, rounding);
        }

        [[nodiscard]] float CountBadgeWidth(Keire::UiFrame& ui, const std::size_t count)
        {
            return std::max(ui.MeasureText(std::to_string(count), 11.0F).Width + 10.0F, 20.0F);
        }

        void DrawCountBadge(Keire::UiFrame& ui, const Keire::UiItemRect area, const std::size_t count,
                            const Keire::UiThemeDefinition& theme)
        {
            const auto label = std::to_string(count);
            const auto text = ui.MeasureText(label, 11.0F);
            const float width = CountBadgeWidth(ui, count);
            const Keire::UiItemRect badge{{area.Maximum.X - width, area.Minimum.Y}, area.Maximum};
            ui.DrawFilledRectangle(badge, theme.RaisedPanel, 8.0F);
            ui.DrawRectangle(badge, theme.Border, 1.0F, 8.0F);
            ui.DrawOverlayText({badge.Minimum.X + (badge.Size().Width - text.Width) * 0.5F, badge.Minimum.Y + 2.0F},
                               theme.Text, label, 11.0F, badge);
        }

        void DrawDisclosureTriangle(Keire::UiFrame& ui, const Keire::UiItemRect area, const bool expanded,
                                    const Keire::UiColor color)
        {
            const auto triangle = AssetBrowserDisclosureTriangle(area, expanded);
            ui.DrawFilledTriangle(triangle[0], triangle[1], triangle[2], color);
        }
    } // namespace

    Keire::UiItemRect AssetBrowserGridDisclosureArea(const Keire::UiItemRect card, const float thumbnailSize) noexcept
    {
        const auto thumbnail = GridThumbnailArea(card, thumbnailSize);
        return {{card.Minimum.X + 8.0F, thumbnail.Maximum.Y + 3.0F},
                {card.Minimum.X + 26.0F, thumbnail.Maximum.Y + 21.0F}};
    }

    Keire::UiItemRect AssetBrowserListDisclosureArea(const Keire::UiItemRect row) noexcept
    {
        return {{row.Minimum.X + 4.0F, row.Minimum.Y + 10.0F}, {row.Minimum.X + 24.0F, row.Minimum.Y + 30.0F}};
    }

    void DrawAssetBrowserGridItemVisual(Keire::UiFrame& ui, const Keire::UiItemRect area,
                                        const Keire::Ref<Keire::UiImage>& image, const std::string_view name,
                                        const std::string_view type, const bool selected, const bool hovered,
                                        const bool hasChildren, const std::size_t childCount, const bool expanded,
                                        const bool failed, const Keire::UiThemeDefinition& theme,
                                        const float thumbnailSize)
    {
        DrawItemChrome(ui, area, selected, hovered, theme, 4.0F);
        const auto thumbnail = GridThumbnailArea(area, thumbnailSize);
        ui.DrawImage(image, thumbnail);
        ui.DrawRectangle(thumbnail, selected ? theme.Accent : theme.Border, selected ? 2.0F : 1.0F, 3.0F);
        if (failed)
            ui.DrawFilledCircle({thumbnail.Minimum.X + 7.0F, thumbnail.Minimum.Y + 7.0F}, 4.0F, theme.Error);

        const float badgeWidth = hasChildren ? CountBadgeWidth(ui, childCount) : 0.0F;
        const auto nameArea = AssetBrowserGridNameArea(area, thumbnailSize, badgeWidth, hasChildren);
        const float nameWidth = std::max(nameArea.Size().Width, 1.0F);
        const float typeWidth = std::max(area.Size().Width - 16.0F, 1.0F);
        const auto visibleName = ElideAssetDisplayName(name, nameWidth, [&ui](const std::string_view value)
                                                       { return ui.MeasureText(value, 13.0F).Width; });
        const auto visibleType = ElideAssetDisplayName(type, typeWidth, [&ui](const std::string_view value)
                                                       { return ui.MeasureText(value, 11.0F).Width; });
        const auto nameSize = ui.MeasureText(visibleName, 13.0F);
        const auto typeSize = ui.MeasureText(visibleType, 11.0F);
        const float nameY = thumbnail.Maximum.Y + 6.0F;
        ui.DrawOverlayText({nameArea.Minimum.X + (nameArea.Size().Width - nameSize.Width) * 0.5F, nameY}, theme.Text,
                           visibleName, 13.0F, nameArea);
        ui.DrawOverlayText({area.Minimum.X + (area.Size().Width - typeSize.Width) * 0.5F, nameY + 16.0F},
                           theme.MutedText, visibleType, 11.0F, area);

        if (!hasChildren)
            return;
        const auto disclosure = AssetBrowserGridDisclosureArea(area, thumbnailSize);
        DrawDisclosureTriangle(ui, disclosure, expanded, theme.Text);
        DrawCountBadge(ui,
                       {{area.Maximum.X - 8.0F - badgeWidth, thumbnail.Maximum.Y + 3.0F},
                        {area.Maximum.X - 8.0F, thumbnail.Maximum.Y + 21.0F}},
                       childCount, theme);
    }

    void DrawAssetBrowserListItemVisual(Keire::UiFrame& ui, const Keire::UiItemRect area,
                                        const Keire::Ref<Keire::UiImage>& image, const std::string_view name,
                                        const std::string_view type, const bool selected, const bool hovered,
                                        const bool hasChildren, const std::size_t childCount, const bool expanded,
                                        const bool failed, const Keire::UiThemeDefinition& theme)
    {
        DrawItemChrome(ui, area, selected, hovered, theme, 3.0F);
        if (hasChildren)
            DrawDisclosureTriangle(ui, AssetBrowserListDisclosureArea(area), expanded, theme.Text);

        const Keire::UiItemRect thumbnail{{area.Minimum.X + 28.0F, area.Minimum.Y + 6.0F},
                                          {area.Minimum.X + 58.0F, area.Minimum.Y + 36.0F}};
        ui.DrawImage(image, thumbnail);
        ui.DrawRectangle(thumbnail, selected ? theme.Accent : theme.Border, selected ? 2.0F : 1.0F, 3.0F);
        if (failed)
            ui.DrawFilledCircle({thumbnail.Minimum.X + 5.0F, thumbnail.Minimum.Y + 5.0F}, 3.5F, theme.Error);

        float trailingWidth = 8.0F;
        if (hasChildren)
        {
            const auto count = std::to_string(childCount);
            trailingWidth = std::max(ui.MeasureText(count, 11.0F).Width + 24.0F, 38.0F);
            DrawCountBadge(ui,
                           {{area.Maximum.X - trailingWidth, area.Minimum.Y + 12.0F},
                            {area.Maximum.X - 8.0F, area.Minimum.Y + 30.0F}},
                           childCount, theme);
        }
        const float textWidth = std::max(area.Maximum.X - trailingWidth - (thumbnail.Maximum.X + 8.0F), 1.0F);
        const auto visibleName = ElideAssetDisplayName(name, textWidth, [&ui](const std::string_view value)
                                                       { return ui.MeasureText(value, 13.0F).Width; });
        const auto visibleType = ElideAssetDisplayName(type, textWidth, [&ui](const std::string_view value)
                                                       { return ui.MeasureText(value, 11.0F).Width; });
        ui.DrawOverlayText({thumbnail.Maximum.X + 8.0F, area.Minimum.Y + 5.0F}, theme.Text, visibleName, 13.0F, area);
        ui.DrawOverlayText({thumbnail.Maximum.X + 8.0F, area.Minimum.Y + 22.0F}, theme.MutedText, visibleType, 11.0F,
                           area);
    }
} // namespace KeireEditor::Detail
