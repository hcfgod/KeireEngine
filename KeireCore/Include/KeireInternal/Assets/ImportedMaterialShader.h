#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <optional>
#include <string>
#include <unordered_set>

namespace Keire
{
    namespace Detail
    {
        struct ImportedMaterialShader final
        {
            AssetId Id;
            std::unordered_set<std::string> Properties;
        };

        [[nodiscard]] std::optional<ImportedMaterialShader>
        FindImportedMaterialShader(const AssetImportContext& context);
    } // namespace Detail
} // namespace Keire
