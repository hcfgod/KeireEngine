#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KeireHub
{
    enum class MarketplaceCacheState : std::uint8_t
    {
        Entitled,
        Downloading,
        Ready,
        Failed,
        Unavailable
    };

    struct MarketplaceCacheItem final
    {
        std::string ProductId;
        std::string EntitlementId;
        std::string VersionId;
        std::string Slug;
        std::string DisplayName;
        std::string ShortDescription;
        std::string PublisherName;
        std::string CategoryName;
        std::string LicenseSpdx;
        std::string PackageId;
        std::string Version;
        std::string InstallKind;
        std::string ArchiveSha256;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string SignedPublication;
        MarketplaceCacheState State = MarketplaceCacheState::Entitled;
        std::string FailureMessage;
        bool Entitled = false;

        [[nodiscard]] bool operator==(const MarketplaceCacheItem&) const = default;
    };

    struct MarketplaceCacheSnapshot final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 2;

        std::uint64_t Revision = 0;
        std::string RequestedProductId;
        std::vector<MarketplaceCacheItem> Items;

        [[nodiscard]] bool operator==(const MarketplaceCacheSnapshot&) const = default;
    };

    class MarketplaceCacheStore final
    {
      public:
        explicit MarketplaceCacheStore(std::filesystem::path root);

        [[nodiscard]] HubResult<MarketplaceCacheSnapshot> Load() const;
        [[nodiscard]] HubStatus Save(const MarketplaceCacheSnapshot& snapshot) const;
        [[nodiscard]] std::filesystem::path ArchivePath(const MarketplaceCacheItem& item) const;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_Root; }
        [[nodiscard]] std::filesystem::path IndexPath() const;

      private:
        std::filesystem::path m_Root;
    };
} // namespace KeireHub
