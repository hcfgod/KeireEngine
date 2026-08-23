#pragma once

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/NamedAssetCreation.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace KeireEditor::Detail
{
    [[nodiscard]] bool CreateNamedAsset(IAssetBrowserController& controller, NamedAssetCreationKind kind,
                                        std::string_view name, Keire::ManagedTypeId managedType,
                                        const std::optional<Keire::InputActionAssetDefinition>& inputActions,
                                        Keire::AssetId variantBase, Keire::AssetId materialShader,
                                        Keire::ShaderGraphTemplate shaderTemplate);
    void CopyAssetBrowserText(IAssetBrowserController& controller, std::string_view value);
    void RevealAssetBrowserPath(IAssetBrowserController& controller, const std::filesystem::path& path);
    [[nodiscard]] Keire::Ref<Keire::UiImage> ResolveAssetBrowserImage(
        const Keire::AssetSourceRecord& record,
        const std::unordered_map<Keire::AssetId, Keire::Ref<Keire::UiImage>>& images,
        const Keire::Ref<Keire::UiImage>& assetFallback, const Keire::Ref<Keire::UiImage>& shaderGraphFallback,
        const Keire::Ref<Keire::UiImage>& materialGraphFallback,
        const Keire::Ref<Keire::UiImage>& materialInstanceFallback, const Keire::Ref<Keire::UiImage>& vfxFallback,
        const Keire::Ref<Keire::UiImage>& audioMixerFallback, const Keire::Ref<Keire::UiImage>& animationFallback);
    void DrawAssetBrowserTooltip(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                                 const std::filesystem::path& assetRoot, const Keire::AssetDatabase& database);
    void AcceptAssetBrowserFolderPayloads(Keire::UiFrame& ui, const std::filesystem::path& folder,
                                          IAssetBrowserController& controller);
    [[nodiscard]] Keire::UiColor AssetBrowserColorWithAlpha(Keire::UiColor color, float alpha) noexcept;
    [[nodiscard]] float AssetBrowserGridCardHeight(Keire::UiFrame& ui, float thumbnailSize);
    [[nodiscard]] Keire::UiItemRect AssetBrowserGridDisclosureArea(Keire::UiItemRect card,
                                                                   float thumbnailSize) noexcept;
    [[nodiscard]] Keire::UiItemRect AssetBrowserListDisclosureArea(Keire::UiItemRect row) noexcept;
    void DrawAssetBrowserGridItemVisual(Keire::UiFrame& ui, Keire::UiItemRect area,
                                        const Keire::Ref<Keire::UiImage>& image, std::string_view name,
                                        std::string_view type, bool selected, bool hovered, bool hasChildren,
                                        std::size_t childCount, bool expanded, bool failed,
                                        const Keire::UiThemeDefinition& theme, float thumbnailSize);
    void DrawAssetBrowserListItemVisual(Keire::UiFrame& ui, Keire::UiItemRect area,
                                        const Keire::Ref<Keire::UiImage>& image, std::string_view name,
                                        std::string_view type, bool selected, bool hovered, bool hasChildren,
                                        std::size_t childCount, bool expanded, bool failed,
                                        const Keire::UiThemeDefinition& theme);
} // namespace KeireEditor::Detail
