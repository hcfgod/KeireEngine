#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <span>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] ShaderAssetDefinition DecodeCanonicalShaderAsset(std::span<const std::byte> bytes);
    [[nodiscard]] std::vector<std::byte> EncodeCanonicalShaderAsset(const ShaderAssetDefinition& definition);
} // namespace Keire::Detail
