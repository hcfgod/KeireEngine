#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <string_view>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumShaderNumericProperties = 64;
    inline constexpr std::size_t MaximumShaderTextureProperties = 16;
    inline constexpr std::size_t MaximumShaderProperties =
        MaximumShaderNumericProperties + MaximumShaderTextureProperties;
    inline constexpr std::size_t MaximumShaderDependencies = 256;

    [[nodiscard]] bool ValidShaderIdentifier(std::string_view value);
    void ValidateShaderDefinition(const ShaderAssetDefinition& definition, bool requireVariants,
                                  bool allowMissingVariants = false);
    void ValidateMaterialDefinition(const MaterialAssetDefinition& definition);
} // namespace Keire::Detail
