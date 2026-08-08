#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/HubUpdateCatalog.h"

#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view KeyId = "ed25519-00000000000000000000000000000000";
    constexpr std::string_view Expiry = "2035-01-01T00:00:00Z";

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::invalid_argument(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] PackageManifest Installer(const std::string_view id, const std::string_view version,
                                            const std::string_view channel)
    {
        return {.SchemaVersion = PackageManifest::CurrentSchemaVersion,
                .Id = std::string(id),
                .Version = Version(version),
                .Kind = PackageKind::HubInstaller,
                .DisplayName = "Kéire Hub " + std::string(version),
                .Channel = std::string(channel),
                .Platform = "windows",
                .Architecture = "x86_64",
                .EngineCompatibility = std::nullopt,
                .Dependencies = {},
                .Conflicts = {},
                .ArtifactSizeBytes = 3,
                .ArtifactSha256 = KeireHubTests::Digest('a'),
                .InstalledSizeBytes = 3,
                .Files = {{.Path = "KeireHubInstaller.exe",
                           .SizeBytes = 3,
                           .Sha256 = KeireHubTests::Digest('b'),
                           .Mode = 0755U}},
                .LicenseReferences = {},
                .SignatureKeyId = std::string(KeyId)};
    }

    [[nodiscard]] DistributionPackageCatalogSnapshot
    Source(std::string channel, const std::uint64_t sequence, std::vector<PackageManifest> packages,
           const DistributionCatalogSourceState state = DistributionCatalogSourceState::Online,
           const std::string_view minimumHubVersion = {})
    {
        DistributionPackageCatalog catalog;
        catalog.Identity = {.KeyId = std::string(KeyId),
                            .Sequence = sequence,
                            .ExpiresAt = std::string(Expiry),
                            .Channel = channel,
                            .Platform = "windows",
                            .Architecture = "x86_64"};
        if (!minimumHubVersion.empty())
            catalog.MinimumSupportedHubVersion = Version(minimumHubVersion);
        catalog.Packages = std::move(packages);
        return {.Channel = std::move(channel),
                .Catalog = std::make_shared<const DistributionPackageCatalog>(std::move(catalog)),
                .Status = {.State = state,
                           .Sequence = sequence,
                           .KeyId = std::string(KeyId),
                           .ExpiresAt = std::string(Expiry),
                           .Error = std::nullopt}};
    }
} // namespace

TEST_CASE("Hub update selection chooses the newest trusted installer and prefers stable ties")
{
    DistributionCatalogSnapshot catalogs;
    catalogs.PackageCatalogs.push_back(Source("nightly", 9, {Installer("hub.nightly", "2.0.0", "nightly")}));
    catalogs.PackageCatalogs.push_back(Source("preview", 8, {Installer("hub.preview", "2.0.0", "preview")}));
    catalogs.PackageCatalogs.push_back(Source("stable", 7, {Installer("hub.stable", "2.0.0", "stable")}));
    catalogs.PackageCatalogs.push_back(Source("stable", 10, {Installer("hub.old", "1.0.0", "stable")}));

    const auto selected = SelectHubUpdate(catalogs, "1.5.0");
    REQUIRE(selected);
    REQUIRE(selected.Value());
    CHECK(selected.Value()->Package.Id == "hub.stable");
    CHECK(selected.Value()->CatalogIdentity.Sequence == 7U);
    CHECK_FALSE(selected.Value()->Required);
}

TEST_CASE("Hub update selection accepts last-known-good catalogs and ignores unverified status")
{
    DistributionCatalogSnapshot catalogs;
    catalogs.OfflineMode = true;
    catalogs.PackageCatalogs.push_back(Source("stable", 7, {Installer("hub.cached", "1.1.0", "stable")},
                                              DistributionCatalogSourceState::OfflineLastKnownGood));
    auto mismatched = Source("preview", 8, {Installer("hub.unverified", "9.0.0", "preview")});
    mismatched.Status.Sequence = 99;
    catalogs.PackageCatalogs.push_back(std::move(mismatched));
    catalogs.PackageCatalogs.push_back(Source("nightly", 9, {Installer("hub.unavailable", "10.0.0", "nightly")},
                                              DistributionCatalogSourceState::Unavailable));

    const auto selected = SelectHubUpdate(catalogs, "1.0.0");
    REQUIRE(selected);
    REQUIRE(selected.Value());
    CHECK(selected.Value()->Package.Id == "hub.cached");
    CHECK(selected.Value()->Source == DistributionCatalogSourceState::OfflineLastKnownGood);
}

TEST_CASE("Hub update selection carries signed minimum-version policy")
{
    DistributionCatalogSnapshot catalogs;
    catalogs.PackageCatalogs.push_back(Source("stable", 7, {Installer("hub.required", "2.0.0", "stable")},
                                              DistributionCatalogSourceState::Online, "1.5.0"));

    const auto selected = SelectHubUpdate(catalogs, "1.0.0");
    REQUIRE(selected);
    REQUIRE(selected.Value());
    CHECK(selected.Value()->Required);

    catalogs.PackageCatalogs.front().Catalog = std::make_shared<const DistributionPackageCatalog>(
        DistributionPackageCatalog{.SchemaVersion = DistributionPackageCatalog::CurrentSchemaVersion,
                                   .Identity = catalogs.PackageCatalogs.front().Catalog->Identity,
                                   .MinimumSupportedHubVersion = Version("1.5.0"),
                                   .Packages = {}});
    const auto missingInstaller = SelectHubUpdate(catalogs, "1.0.0");
    REQUIRE_FALSE(missingInstaller);
    CHECK(missingInstaller.Error().Code == HubErrorCode::CatalogCacheInvalid);
    CHECK_FALSE(missingInstaller.Error().Retryable);
}

TEST_CASE("Hub update selection rejects an invalid installed version")
{
    const auto selected = SelectHubUpdate({}, "not a version");
    REQUIRE_FALSE(selected);
    CHECK(selected.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Hub update selection rejects installer identity wildcards and catalog mismatches")
{
    const auto checkMismatch = [](auto mutate)
    {
        auto installer = Installer("hub.mismatched", "2.0.0", "stable");
        mutate(installer);
        DistributionCatalogSnapshot catalogs;
        catalogs.PackageCatalogs.push_back(Source("stable", 7, {std::move(installer)}));

        const auto selected = SelectHubUpdate(catalogs, "1.0.0");
        REQUIRE_FALSE(selected);
        CHECK(selected.Error().Code == HubErrorCode::CatalogIdentityMismatch);
    };

    checkMismatch([](PackageManifest& installer) { installer.Platform = "any"; });
    checkMismatch([](PackageManifest& installer) { installer.Architecture = "any"; });
    checkMismatch([](PackageManifest& installer) { installer.Channel = "preview"; });
    checkMismatch([](PackageManifest& installer) { installer.SignatureKeyId = "test-key"; });
}
