#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <filesystem>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumTextureDimension = std::size_t{16} * 1024U;

    [[nodiscard]] TextureImportSettings NormalizeTextureSettings(TextureImportSettings settings);
    [[nodiscard]] TextureImportSettings ApplyTextureImportSettings(TextureImportSettings settings,
                                                                   const AssetImportSettings& values);
    [[nodiscard]] TextureImportSettings ReadTextureSettings(const std::filesystem::path& metadataPath,
                                                            TextureImportSettings settings);
} // namespace Keire::Detail
