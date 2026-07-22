#pragma once

#include "Keire/Assets/AssetPipeline.h"

namespace Keire::Detail
{
    class AssetDatabaseWorkerAccess final
    {
      public:
        static void PublishSourceIndex(const AssetDatabase& database, const std::filesystem::path& path);
        [[nodiscard]] static std::size_t ReloadSourceIndex(AssetDatabase& database, const std::filesystem::path& path);
    };
} // namespace Keire::Detail
