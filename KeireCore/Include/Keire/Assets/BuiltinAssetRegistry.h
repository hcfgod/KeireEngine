#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <vector>

namespace Keire
{
    [[nodiscard]] KEIRE_API std::vector<AssetImporterRegistration> CreateBuiltinAssetImporters();
    [[nodiscard]] KEIRE_API std::vector<AssetDecoderRegistration> CreateBuiltinAssetDecoders();

    KEIRE_API void AppendMissingBuiltinAssetDecoders(std::vector<AssetDecoderRegistration>& registrations);
} // namespace Keire
