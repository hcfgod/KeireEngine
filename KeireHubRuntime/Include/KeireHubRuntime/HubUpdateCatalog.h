#pragma once

#include "KeireHubRuntime/DistributionCatalog.h"

#include <optional>
#include <string_view>

namespace KeireHub
{
    struct HubUpdateCandidate final
    {
        PackageManifest Package;
        DistributionPackageCatalogIdentity CatalogIdentity;
        DistributionCatalogSourceState Source = DistributionCatalogSourceState::Unavailable;
        bool Required = false;
    };

    // Consumes only immutable catalogs that have already crossed the signature, identity, expiry, and replay gates.
    // Equal versions prefer the least experimental channel so opting into previews never obscures a stable installer.
    [[nodiscard]] HubResult<std::optional<HubUpdateCandidate>>
    SelectHubUpdate(const DistributionCatalogSnapshot& catalogs, std::string_view installedHubVersion,
                    std::string_view nativePackageFormat = {});
} // namespace KeireHub
