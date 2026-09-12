#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <vector>

namespace Keire::Detail
{
    struct RuntimeUiMaterialValues
    {
        std::vector<Vector4> Numeric;
        std::vector<AssetId> Textures;
    };

    void ValidateRuntimeUiShader(const ShaderAssetDefinition& shader);
    [[nodiscard]] RuntimeUiMaterialValues BuildRuntimeUiMaterialValues(const ShaderAssetDefinition& shader,
                                                                       const MaterialAssetDefinition& material);
} // namespace Keire::Detail
