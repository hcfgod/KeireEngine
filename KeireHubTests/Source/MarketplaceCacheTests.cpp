#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/MarketplaceCache.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>

using namespace KeireHub;

namespace
{
    [[nodiscard]] std::string JsonEscape(const std::string_view value)
    {
        std::string result;
        for (const auto character : value)
        {
            if (character == '\\' || character == '"')
                result.push_back('\\');
            result.push_back(character);
        }
        return result;
    }

    [[nodiscard]] std::string PublicationEnvelope()
    {
        const std::string productId = "00112233-4455-6677-8899-aabbccddeeff";
        const std::string versionId = "20112233-4455-6677-8899-aabbccddeeff";
        const std::string digest(64U, 'a');
        const std::string document = "{\"schemaVersion\":1,\"keyId\":\"ed25519-test-key\",\"sequence\":1,\"expiresAt\":"
                                     "\"2099-08-12T07:00:00Z\",\"productId\":\"" +
                                     productId + "\",\"versionId\":\"" + versionId + "\",\"artifactSha256\":\"" +
                                     digest + "\",\"artifactSizeBytes\":4096,\"manifestSha256\":\"" +
                                     std::string(64U, 'b') + "\",\"releaseStoragePath\":\"" + productId + '/' +
                                     versionId + '/' + digest + ".keireassetpackage\"}";
        return "{\"schemaVersion\":1,\"document\":\"" + JsonEscape(document) +
               "\",\"signature\":{\"algorithm\":\"ed25519\",\"keyId\":\"ed25519-test-key\",\"value\":\"" +
               std::string(86U, 'A') + "==\",\"sequence\":1,\"expiresAt\":\"2099-08-12T07:00:00Z\"}}";
    }

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
                .SignedPublication = PublicationEnvelope(),
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

    const auto saved = store.Save(snapshot);
    INFO("Cache save failure: ", saved ? std::string{} : saved.Error().TechnicalDetails);
    REQUIRE(saved);
    const auto loaded = store.Load();
    REQUIRE(loaded);
    CHECK(loaded.Value() == snapshot);
    CHECK(store.ArchivePath(loaded.Value().Items.front()).is_absolute());
    CHECK(store.ArchivePath(loaded.Value().Items.front()).extension() == ".keireassetpackage");
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
