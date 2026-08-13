#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/MarketplaceCache.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace KeireHub;

namespace
{
    [[nodiscard]] MarketplaceCacheItem ReadyItem()
    {
        return {.ProductId = "00112233-4455-6677-8899-aabbccddeeff",
                .EntitlementId = "10112233-4455-6677-8899-aabbccddeeff",
                .VersionId = "20112233-4455-6677-8899-aabbccddeeff",
                .Slug = "sandbox-content",
                .DisplayName = "Kéire Sandbox Content Pack",
                .ShortDescription = "Production-ready sample assets.",
                .PublisherName = "Kéire",
                .CategoryName = "Samples",
                .LicenseSpdx = "MIT",
                .PackageId = "com.keire.sandbox-content",
                .Version = "1.0.0",
                .InstallKind = "asset_import",
                .ArchiveSha256 = std::string(64U, 'a'),
                .ArchiveSizeBytes = 4096U,
                .State = MarketplaceCacheState::Ready,
                .Entitled = true};
    }
} // namespace

TEST_CASE("Marketplace cache atomically round-trips token-free entitled package state")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceCacheStore store(temporary.Path() / "MarketplacePackages");
    MarketplaceCacheSnapshot snapshot{
        .Revision = 7U, .RequestedProductId = "00112233-4455-6677-8899-aabbccddeeff", .Items = {ReadyItem()}};

    REQUIRE(store.Save(snapshot));
    const auto loaded = store.Load();
    REQUIRE(loaded);
    CHECK(loaded.Value() == snapshot);
    CHECK(store.ArchivePath(loaded.Value().Items.front()).is_absolute());
    CHECK(store.ArchivePath(loaded.Value().Items.front()).string().find(snapshot.Items.front().ArchiveSha256) !=
          std::string::npos);
}

TEST_CASE("Marketplace cache rejects duplicate identities and incomplete ready entries")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceCacheStore store(temporary.Path() / "MarketplacePackages");
    const auto item = ReadyItem();
    CHECK_FALSE(store.Save({.Revision = 1U, .Items = {item, item}}));

    auto incomplete = item;
    incomplete.ArchiveSha256.clear();
    CHECK_FALSE(store.Save({.Revision = 1U, .Items = {incomplete}}));
}
