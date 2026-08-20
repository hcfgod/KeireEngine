#pragma once

#include "Keire/Assets/AssetPipeline.h"

namespace Keire::Detail
{
    class AssetDatabaseWorkerAccess final
    {
      public:
        [[nodiscard]] static Ref<AssetDatabase> CreateFromSourceIndex(AssetDatabaseSpecification specification,
                                                                      const std::filesystem::path& path);
        static void PublishSourceIndex(const AssetDatabase& database, const std::filesystem::path& path);
        [[nodiscard]] static std::size_t ReloadSourceIndex(AssetDatabase& database, const std::filesystem::path& path);
        [[nodiscard]] static AssetImportResult
        ImportAssetsFromSourceIndex(AssetDatabase& database, std::span<const AssetId> assets, AssetImportPolicy policy,
                                    std::stop_token cancellation = {}, AssetOperationProgressCallback progress = {});
    };
} // namespace Keire::Detail
