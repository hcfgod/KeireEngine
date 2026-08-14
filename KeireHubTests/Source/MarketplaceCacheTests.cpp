#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/MarketplaceCache.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

using namespace KeireHub;

namespace
{
    constexpr std::string_view AccountId = "40112233-4455-6677-8899-aabbccddeeff";

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
    MarketplaceCacheSnapshot snapshot{.Revision = 7U,
                                      .AccountId = std::string(AccountId),
                                      .RequestedProductId = "00112233-4455-6677-8899-aabbccddeeff",
                                      .Items = {ReadyItem()}};

    const auto saved = store.Save(snapshot);
    INFO("Cache save failure: ", saved ? std::string{} : saved.Error().TechnicalDetails);
    REQUIRE(saved);

    const auto legacyDocument = nlohmann::json::parse(KeireHubTests::ReadText(store.IndexPath()));
    const auto previousVersionedDocument =
        nlohmann::json::parse(KeireHubTests::ReadText(store.Root() / "marketplace-cache-v2.json"));
    const auto versionedPath = store.Root() / "marketplace-cache-v3.json";
    const auto versionedDocument = nlohmann::json::parse(KeireHubTests::ReadText(versionedPath));
    REQUIRE(legacyDocument.at("schemaVersion") == 1U);
    REQUIRE(previousVersionedDocument.at("schemaVersion") == 2U);
    REQUIRE(versionedDocument.at("schemaVersion") == MarketplaceCacheSnapshot::CurrentSchemaVersion);
    CHECK(legacyDocument.at("items").empty());
    CHECK(previousVersionedDocument.at("items").empty());
    REQUIRE(versionedDocument.at("items").size() == 1U);
    CHECK(versionedDocument.at("accountId").get<std::string>() == AccountId);
    CHECK(versionedDocument.at("items").front().at("state") == "ready");
    CHECK(versionedDocument.at("items").front().at("signedPublication") == PublicationEnvelope());

    const auto loaded = store.Load();
    REQUIRE(loaded);
    CHECK(loaded.Value() == snapshot);
    CHECK(store.ArchivePath(loaded.Value().Items.front()).is_absolute());
    CHECK(store.ArchivePath(loaded.Value().Items.front()).extension() == ".keireassetpackage");
    CHECK(store.ArchivePath(loaded.Value().Items.front()).string().find(snapshot.Items.front().ArchiveSha256) !=
          std::string::npos);

    REQUIRE(std::filesystem::remove(versionedPath));
    const auto legacyLoaded = store.Load();
    REQUIRE(legacyLoaded);
    CHECK(legacyLoaded.Value().AccountId.empty());
    CHECK(legacyLoaded.Value().Items.empty());
}

TEST_CASE("Marketplace cache loads an unversioned schema two snapshot during migration")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceCacheStore store(temporary.Path() / "MarketplacePackages");
    const MarketplaceCacheSnapshot snapshot{.Revision = 8U,
                                            .AccountId = std::string(AccountId),
                                            .RequestedProductId = "00112233-4455-6677-8899-aabbccddeeff",
                                            .Items = {ReadyItem()}};
    REQUIRE(store.Save(snapshot));

    const auto currentPath = store.Root() / "marketplace-cache-v3.json";
    const auto previousVersionedPath = store.Root() / "marketplace-cache-v2.json";
    auto previousVersion = nlohmann::json::parse(KeireHubTests::ReadText(currentPath));
    previousVersion["schemaVersion"] = 2U;
    previousVersion.erase("accountId");
    KeireHubTests::WriteText(previousVersionedPath, previousVersion.dump());
    REQUIRE(std::filesystem::remove(currentPath));

    const auto loaded = store.Load();
    REQUIRE(loaded);
    auto expected = snapshot;
    expected.AccountId.clear();
    CHECK(loaded.Value() == expected);
}

TEST_CASE("Marketplace cache does not fall back when the signed versioned snapshot is invalid")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceCacheStore store(temporary.Path() / "MarketplacePackages");
    REQUIRE(store.Save({.Revision = 9U, .AccountId = std::string(AccountId), .Items = {ReadyItem()}}));
    KeireHubTests::WriteText(store.Root() / "marketplace-cache-v3.json", "{");

    const auto loaded = store.Load();
    REQUIRE_FALSE(loaded);
}

TEST_CASE("Marketplace cache rejects duplicate identities and incomplete ready entries")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceCacheStore store(temporary.Path() / "MarketplacePackages");
    const auto item = ReadyItem();
    CHECK_FALSE(store.Save({.Revision = 1U, .AccountId = std::string(AccountId), .Items = {item, item}}));

    auto incomplete = item;
    incomplete.ArchiveSha256.clear();
    CHECK_FALSE(store.Save({.Revision = 1U, .AccountId = std::string(AccountId), .Items = {incomplete}}));
}

TEST_CASE("Marketplace session leases authorize only the live matching account")
{
    KeireHubTests::TemporaryDirectory temporary;
    MarketplaceSessionLeaseStore store(temporary.Path() / "MarketplacePackages");
    const MarketplaceSessionLease lease{
        .AccountId = std::string(AccountId), .ExpiresAtUnixSeconds = 115U, .SignedIn = true};
    REQUIRE(store.Save(lease));

    const auto loaded = store.Load();
    REQUIRE(loaded);
    CHECK(loaded.Value() == lease);

    const MarketplaceCacheSnapshot matching{.AccountId = std::string(AccountId)};
    CHECK(MarketplaceSessionAuthorizes(matching, loaded.Value(), 100U));
    CHECK(MarketplaceSessionAuthorizes(matching, loaded.Value(), 115U));
    CHECK_FALSE(MarketplaceSessionAuthorizes(matching, loaded.Value(), 116U));
    CHECK_FALSE(
        MarketplaceSessionAuthorizes({.AccountId = "50112233-4455-6677-8899-aabbccddeeff"}, loaded.Value(), 100U));

    auto signedOut = lease;
    signedOut.SignedIn = false;
    signedOut.AccountId.clear();
    signedOut.ExpiresAtUnixSeconds = 0U;
    REQUIRE(store.Save(signedOut));
    CHECK_FALSE(MarketplaceSessionAuthorizes(matching, signedOut, 100U));
}
