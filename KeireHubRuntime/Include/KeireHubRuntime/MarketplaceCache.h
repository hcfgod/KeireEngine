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
        static constexpr std::uint32_t CurrentSchemaVersion = 3;

        std::uint64_t Revision = 0;
        std::string AccountId;
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
        [[nodiscard]] std::filesystem::path VersionedIndexPath() const;
        [[nodiscard]] std::filesystem::path PreviousVersionedIndexPath() const;

        std::filesystem::path m_Root;
    };

    inline constexpr std::uint64_t MarketplaceSessionLeaseDurationSeconds = 15U;
    inline constexpr std::uint64_t MarketplaceSessionLeaseRefreshSeconds = 5U;

    struct MarketplaceSessionLease final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::string AccountId;
        std::uint64_t ExpiresAtUnixSeconds = 0;
        bool SignedIn = false;

        [[nodiscard]] bool operator==(const MarketplaceSessionLease&) const = default;
    };

    class MarketplaceSessionLeaseStore final
    {
      public:
        explicit MarketplaceSessionLeaseStore(std::filesystem::path root);

        [[nodiscard]] HubResult<MarketplaceSessionLease> Load() const;
        [[nodiscard]] HubStatus Save(const MarketplaceSessionLease& lease) const;
        [[nodiscard]] std::filesystem::path Path() const;

      private:
        std::filesystem::path m_Root;
    };

    [[nodiscard]] bool MarketplaceSessionAuthorizes(const MarketplaceCacheSnapshot& snapshot,
                                                    const MarketplaceSessionLease& lease,
                                                    std::uint64_t nowUnixSeconds) noexcept;
} // namespace KeireHub
