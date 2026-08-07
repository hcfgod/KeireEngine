#pragma once

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/CatalogModels.h"
#include "KeireHubRuntime/DistributionConfiguration.h"
#include "KeireHubRuntime/HubSettingsStore.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireHub
{
    struct DistributionPackageCatalogIdentity final
    {
        std::string KeyId;
        std::uint64_t Sequence = 0;
        std::string ExpiresAt;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
    };

    struct DistributionPackageCatalog final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        DistributionPackageCatalogIdentity Identity;
        std::optional<SemanticVersion> MinimumSupportedHubVersion;
        std::vector<PackageManifest> Packages;
    };

    // Signature verification remains the CatalogClient boundary; this parser validates only already-verified bytes.
    [[nodiscard]] HubResult<DistributionPackageCatalog>
    ParseDistributionPackageCatalog(std::span<const std::byte> exactBytes,
                                    const DistributionPackageCatalogIdentity& expectedIdentity);

    enum class DistributionCatalogSourceState
    {
        OnlineDisabled,
        NotLoaded,
        Online,
        LastKnownGood,
        OfflineLastKnownGood,
        Unavailable
    };

    struct DistributionCatalogSourceStatus final
    {
        DistributionCatalogSourceState State = DistributionCatalogSourceState::NotLoaded;
        std::uint64_t Sequence = 0;
        std::string KeyId;
        std::string ExpiresAt;
        std::optional<HubError> Error;
    };

    struct DistributionPackageCatalogSnapshot final
    {
        std::string Channel;
        std::shared_ptr<const DistributionPackageCatalog> Catalog;
        DistributionCatalogSourceStatus Status;
    };

    struct DistributionContentCatalogSnapshot final
    {
        std::string Locale = "en-US";
        std::shared_ptr<const HubContentCatalog> Catalog;
        DistributionCatalogSourceStatus Status;
    };

    struct DistributionCatalogSnapshot final
    {
        bool OnlineDiscoveryEnabled = false;
        bool OfflineMode = false;
        std::vector<DistributionPackageCatalogSnapshot> PackageCatalogs;
        DistributionContentCatalogSnapshot Content;
    };

    struct DistributionCatalogEnvironment final
    {
        std::filesystem::path HubExecutable;
        std::filesystem::path CatalogCacheRoot;
        std::filesystem::path SignatureVerifierLibrary;
        std::string HostPlatform;
        std::string HostArchitecture;
        std::string Locale = "en-US";
        std::chrono::seconds MinimumRemainingValidity{0};
        CatalogClock Clock;
        CatalogTransport Transport;
    };

    class DistributionCatalogSession final
    {
      public:
        [[nodiscard]] static HubResult<DistributionCatalogSession> Create(const DistributionConfiguration& distribution,
                                                                          const HubSettings& settings,
                                                                          DistributionCatalogEnvironment environment);

        [[nodiscard]] HubStatus Refresh();
        [[nodiscard]] std::shared_ptr<const DistributionCatalogSnapshot> Snapshot() const noexcept;

      private:
        DistributionCatalogSession(std::optional<CatalogClient> client, bool offline,
                                   std::shared_ptr<const DistributionCatalogSnapshot> snapshot);

        std::optional<CatalogClient> m_Client;
        bool m_Offline = false;
        std::shared_ptr<const DistributionCatalogSnapshot> m_Snapshot;
    };
} // namespace KeireHub
