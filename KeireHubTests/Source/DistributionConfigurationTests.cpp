#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/DistributionConfiguration.h"

#include <doctest/doctest.h>

namespace
{
    constexpr std::string_view ZeroKey = R"({
        "schemaVersion": 1,
        "algorithm": "Ed25519",
        "keyId": "ed25519-66687aadf862bd776c8fc18b8e9f8e20",
        "publicKey": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "fingerprint": "sha256:66687aadf862bd776c8fc18b8e9f8e20089714856ee233b3902a591d0d5f2925"
    })";
    constexpr std::string_view OneKey = R"({
        "schemaVersion": 1,
        "algorithm": "Ed25519",
        "keyId": "ed25519-72cd6e8422c407fb6d098690f1130b7d",
        "publicKey": "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=",
        "fingerprint": "sha256:72cd6e8422c407fb6d098690f1130b7ded7ec2f7f5e1d30bd9d521f015363793"
    })";
} // namespace

TEST_CASE("Missing and explicitly disabled distribution configurations keep online discovery off")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Distribution.json";
    auto loaded = KeireHub::LoadDistributionConfiguration(path);
    REQUIRE(loaded);
    CHECK_FALSE(loaded.Value().OnlineDiscoveryEnabled);
    CHECK(loaded.Value().TrustedPublicKeyDocuments.empty());

    KeireHubTests::WriteText(path, R"({"schemaVersion":1,"onlineDiscoveryEnabled":false})");
    loaded = KeireHub::LoadDistributionConfiguration(path);
    REQUIRE(loaded);
    CHECK_FALSE(loaded.Value().OnlineDiscoveryEnabled);
}

TEST_CASE("Enabled distribution configuration preserves canonical endpoint and trusted keys")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Distribution.json";
    KeireHubTests::WriteText(path, std::string(R"({"schemaVersion":1,"onlineDiscoveryEnabled":true,
                                "serviceBaseUrl":"https://updates.keire.dev","minimumSequence":42,
                                "trustedKeys":[)") +
                                       std::string(ZeroKey) + ',' + std::string(OneKey) + "]}");

    const auto loaded = KeireHub::LoadDistributionConfiguration(path);
    REQUIRE(loaded);
    CHECK(loaded.Value().OnlineDiscoveryEnabled);
    CHECK(loaded.Value().ServiceBaseUrl == "https://updates.keire.dev");
    CHECK(loaded.Value().MinimumSequence == 42);
    REQUIRE(loaded.Value().TrustedPublicKeyDocuments.size() == 2);
    CHECK(loaded.Value().TrustedPublicKeyDocuments[0].find("ed25519-66687aad") != std::string::npos);
    CHECK(loaded.Value().TrustedPublicKeyDocuments[1].find("ed25519-72cd6e84") != std::string::npos);
}

TEST_CASE("Distribution configuration rejects non-HTTPS endpoints and forged key identities")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Distribution.json";
    KeireHubTests::WriteText(path, std::string(R"({"schemaVersion":1,"onlineDiscoveryEnabled":true,
                                "serviceBaseUrl":"http://updates.keire.dev","minimumSequence":1,
                                "trustedKeys":[)") +
                                       std::string(ZeroKey) + "]}");
    CHECK_FALSE(KeireHub::LoadDistributionConfiguration(path));

    auto forged = std::string(ZeroKey);
    forged.replace(forged.find("66687aad"), 8, "12345678");
    KeireHubTests::WriteText(path, std::string(R"({"schemaVersion":1,"onlineDiscoveryEnabled":true,
                                "serviceBaseUrl":"https://updates.keire.dev","minimumSequence":1,
                                "trustedKeys":[)") +
                                       forged + "]}");
    const auto loaded = KeireHub::LoadDistributionConfiguration(path);
    REQUIRE_FALSE(loaded);
    CHECK(loaded.Error().Code == KeireHub::HubErrorCode::DistributionConfigurationInvalid);
}
