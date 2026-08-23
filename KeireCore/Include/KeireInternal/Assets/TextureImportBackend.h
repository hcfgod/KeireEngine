#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    struct DecodedFloatTexture final
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::vector<float> Pixels;
    };

    using TextureDecodeBackend = std::function<DecodedFloatTexture(std::span<const std::byte>)>;

    [[nodiscard]] AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings,
                                                                         TextureDecodeBackend backend);
} // namespace Keire::Detail
