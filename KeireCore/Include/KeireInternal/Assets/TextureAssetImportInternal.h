#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/Assets/TextureImportBackend.h"

#include <optional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] TextureMipLevel DownsampleImportedTexture(const TextureMipLevel& source, bool normalMap);
    [[nodiscard]] std::vector<TextureMipLevel>
    ImportTexturePayload(std::span<const std::byte> bytes, const TextureImportSettings& settings,
                         const TextureDecodeBackend& backend,
                         std::optional<DecodedFloatTexture> decoded = std::nullopt);
} // namespace Keire::Detail
