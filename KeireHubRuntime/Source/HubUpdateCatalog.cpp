#include "KeireHubRuntime/HubUpdateCatalog.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool IsTrustedSource(const DistributionCatalogSourceState state) noexcept
        {
            return state == DistributionCatalogSourceState::Online ||
                   state == DistributionCatalogSourceState::LastKnownGood ||
                   state == DistributionCatalogSourceState::OfflineLastKnownGood;
        }

        [[nodiscard]] int ChannelPreference(const std::string_view channel) noexcept
        {
            constexpr std::array preferences{std::string_view{"stable"}, std::string_view{"preview"},
                                             std::string_view{"nightly"}};
            const auto found = std::ranges::find(preferences, channel);
            return found == preferences.end() ? static_cast<int>(preferences.size())
                                              : static_cast<int>(found - preferences.begin());
        }

        [[nodiscard]] bool MatchesVerifiedSource(const DistributionPackageCatalogSnapshot& source) noexcept
        {
            if (!source.Catalog || !IsTrustedSource(source.Status.State))
                return false;
            const auto& identity = source.Catalog->Identity;
            return source.Channel == identity.Channel && source.Status.Sequence == identity.Sequence &&
                   source.Status.KeyId == identity.KeyId && source.Status.ExpiresAt == identity.ExpiresAt;
        }

        [[nodiscard]] bool MatchesCatalogIdentity(const PackageManifest& package,
                                                  const DistributionPackageCatalogIdentity& identity) noexcept
        {
            return package.Channel == identity.Channel && package.Platform == identity.Platform &&
                   package.Architecture == identity.Architecture && package.SignatureKeyId == identity.KeyId;
        }

        [[nodiscard]] bool BetterCandidate(const HubUpdateCandidate& candidate,
                                           const HubUpdateCandidate& current) noexcept
        {
            if (candidate.Package.Version != current.Package.Version)
                return candidate.Package.Version > current.Package.Version;
            const auto candidateChannel = ChannelPreference(candidate.CatalogIdentity.Channel);
            const auto currentChannel = ChannelPreference(current.CatalogIdentity.Channel);
            if (candidateChannel != currentChannel)
                return candidateChannel < currentChannel;
            if (candidate.CatalogIdentity.Sequence != current.CatalogIdentity.Sequence)
                return candidate.CatalogIdentity.Sequence > current.CatalogIdentity.Sequence;
            return candidate.Package.Id < current.Package.Id;
        }
    } // namespace

    HubResult<std::optional<HubUpdateCandidate>> SelectHubUpdate(const DistributionCatalogSnapshot& catalogs,
                                                                 const std::string_view installedHubVersion)
    {
        auto installed = SemanticVersion::Parse(installedHubVersion);
        if (!installed)
        {
            auto error = installed.Error();
            error.Code = HubErrorCode::InvalidArgument;
            error.Message = "The installed Hub version is invalid.";
            error.AffectedItem = std::string(installedHubVersion);
            return HubResult<std::optional<HubUpdateCandidate>>::Failure(std::move(error));
        }

        std::optional<HubUpdateCandidate> selected;
        std::optional<SemanticVersion> minimumRequiredVersion;
        for (const auto& source : catalogs.PackageCatalogs)
        {
            if (!MatchesVerifiedSource(source))
                continue;
            const auto& catalog = *source.Catalog;
            if (catalog.MinimumSupportedHubVersion &&
                (!minimumRequiredVersion || *catalog.MinimumSupportedHubVersion > *minimumRequiredVersion))
            {
                minimumRequiredVersion = catalog.MinimumSupportedHubVersion;
            }
            for (const auto& package : catalog.Packages)
            {
                if (package.Kind != PackageKind::HubInstaller || package.Version <= installed.Value())
                    continue;
                if (const auto status = ValidatePackageManifest(package); !status)
                {
                    return HubResult<std::optional<HubUpdateCandidate>>::Failure(status.Error());
                }
                if (!MatchesCatalogIdentity(package, catalog.Identity))
                {
                    return HubResult<std::optional<HubUpdateCandidate>>::Failure(
                        {.Code = HubErrorCode::CatalogIdentityMismatch,
                         .Message = "A signed Hub installer does not match its catalog endpoint.",
                         .AffectedItem = package.Id,
                         .TechnicalDetails = {},
                         .LogReference = {}});
                }
                HubUpdateCandidate candidate{
                    .Package = package, .CatalogIdentity = catalog.Identity, .Source = source.Status.State};
                if (!selected || BetterCandidate(candidate, *selected))
                    selected = std::move(candidate);
            }
        }

        const bool updateRequired = minimumRequiredVersion && installed.Value() < *minimumRequiredVersion;
        if (updateRequired && (!selected || selected->Package.Version < *minimumRequiredVersion))
        {
            return HubResult<std::optional<HubUpdateCandidate>>::Failure(
                {.Code = HubErrorCode::CatalogCacheInvalid,
                 .Message = "The signed catalog requires a newer Hub but provides no compatible installer.",
                 .Retryable = false,
                 .AffectedItem = std::string(installedHubVersion),
                 .TechnicalDetails = {},
                 .LogReference = {}});
        }
        if (selected)
            selected->Required = updateRequired;
        return HubResult<std::optional<HubUpdateCandidate>>::Success(std::move(selected));
    }
} // namespace KeireHub
