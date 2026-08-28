#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumTextureDimension = std::size_t{16} * 1024U;

    void ApplyAtlasSampling(TextureImportSettings& settings) noexcept;
    [[nodiscard]] bool ApplyAutomaticAtlasSampling(TextureImportSettings& settings, std::vector<TextureMipLevel>& mips);
    [[nodiscard]] TextureImportSettings NormalizeTextureSettings(TextureImportSettings settings);
    [[nodiscard]] TextureImportSettings ApplyTextureImportSettings(TextureImportSettings settings,
                                                                   const AssetImportSettings& values);
    [[nodiscard]] TextureImportSettings ReadTextureSettings(const std::filesystem::path& metadataPath,
                                                            TextureImportSettings settings);
    [[nodiscard]] std::vector<AssetImportOptionDescriptor> TextureImportOptionDescriptors();
    [[nodiscard]] AssetImportSettings NormalizeTextureImportOptionValues(TextureImportSettings settings,
                                                                         const AssetImportSettings& values);
    [[nodiscard]] AssetImportSettings SuggestTextureImportOptionValues(const std::filesystem::path& path,
                                                                       const AssetImportSettings& defaults);
} // namespace Keire::Detail
