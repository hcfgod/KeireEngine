#include "TestSupport.h"

#include "KeireHubRuntime/HubUpdateManager.h"

#include <doctest/doctest.h>

#include <algorithm>

namespace
{
    constexpr std::string_view InstallerDigest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    KeireHub::HubUpdateRequest MakeRequest(const KeireHubTests::TemporaryDirectory& temporary)
    {
        const auto cacheRoot = temporary.Path() / "Cache";
        const auto installer = cacheRoot / "sha256" / "ba" / "hub-installer.exe";
        const auto installRoot = temporary.Path() / "InstalledHub";
        KeireHubTests::WriteText(installer, "abc");
        KeireHubTests::WriteText(installRoot / "hub-package.json", "{}\n");
        return {.InstallerPath = installer,
                .VerifiedCacheRoot = cacheRoot,
                .HubInstallRoot = installRoot,
                .PackageId = "keire.hub.windows",
                .Sha256 = std::string(InstallerDigest),
                .CurrentVersion = "0.1.0",
                .TargetVersion = "0.2.0",
                .Platform = std::string(KeireHub::HubUpdateManager::HostPlatformIdentity()),
                .Architecture = std::string(KeireHub::HubUpdateManager::HostArchitectureIdentity()),
                .SignatureKeyId = "ed25519-00000000000000000000000000000000",
                .CatalogSequence = 42,
                .CurrentProcessId = 1234,
                .StartedUnixSeconds = 100,
                .RequirePlatformSignature = true};
    }
} // namespace

TEST_CASE("Hub update handoff verifies the installer and records resumable state")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    const auto request = MakeRequest(temporary);
    bool signatureVerified = false;
    bool launched = false;

    REQUIRE(manager.BeginInstallerHandoff(
        request,
        [&](const std::filesystem::path& path)
        {
            signatureVerified = path == request.InstallerPath;
            return KeireHub::HubStatus::Success();
        },
        [&](const KeireHub::HubUpdateLaunch& launch)
        {
            launched = launch.Executable == request.InstallerPath;
            CHECK(launch.Arguments.front() == "--keire-hub-update");
            CHECK(std::ranges::find(launch.Arguments, "--wait-process") != launch.Arguments.end());
            return KeireHub::HubStatus::Success();
        }));
    CHECK(signatureVerified);
    CHECK(launched);
    CHECK(std::filesystem::is_regular_file(manager.ResumeTokenPath()));

    auto recovery = manager.Reconcile("0.1.0");
    REQUIRE(recovery);
    CHECK(recovery.Value().State == KeireHub::HubUpdateResumeState::RecoveryRequired);
    CHECK(std::filesystem::exists(manager.ResumeTokenPath()));

    recovery = manager.Reconcile("0.2.0");
    REQUIRE(recovery);
    CHECK(recovery.Value().State == KeireHub::HubUpdateResumeState::Updated);
    CHECK_FALSE(std::filesystem::exists(manager.ResumeTokenPath()));
}

TEST_CASE("Hub update reconciliation accepts a newer installed semantic version")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    const auto request = MakeRequest(temporary);

    REQUIRE(manager.BeginInstallerHandoff(
        request, [](const std::filesystem::path&) { return KeireHub::HubStatus::Success(); },
        [](const KeireHub::HubUpdateLaunch&) { return KeireHub::HubStatus::Success(); }));

    const auto recovery = manager.Reconcile("0.3.0+installer-build");
    REQUIRE(recovery);
    CHECK(recovery.Value().State == KeireHub::HubUpdateResumeState::Updated);
    CHECK_FALSE(std::filesystem::exists(manager.ResumeTokenPath()));
}

TEST_CASE("Hub update reconciliation treats an absent resume token as no pending update")
{
    KeireHubTests::TemporaryDirectory temporary;
    for (const auto& token : {temporary.Path() / "MissingPreferences" / "hub-update.json",
                              temporary.Path() / "Preferences" / "hub-update.json"})
    {
        if (token.parent_path().filename() == "Preferences")
            std::filesystem::create_directories(token.parent_path());
        KeireHub::HubUpdateManager manager(token);
        const auto recovery = manager.Reconcile("0.1.0");
        REQUIRE(recovery);
        CHECK(recovery.Value().State == KeireHub::HubUpdateResumeState::None);
        CHECK_FALSE(std::filesystem::exists(token));
    }
}

TEST_CASE("Hub update handoff rejects unverified payloads and removes records after launch failure")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    auto request = MakeRequest(temporary);
    request.Sha256 = KeireHubTests::Digest('f');
    CHECK_FALSE(manager.BeginInstallerHandoff(
        request, [](const std::filesystem::path&) { return KeireHub::HubStatus::Success(); },
        [](const KeireHub::HubUpdateLaunch&) { return KeireHub::HubStatus::Success(); }));
    CHECK_FALSE(std::filesystem::exists(manager.ResumeTokenPath()));

    request.Sha256 = std::string(InstallerDigest);
    CHECK_FALSE(manager.BeginInstallerHandoff(
        request, [](const std::filesystem::path&) { return KeireHub::HubStatus::Success(); },
        [](const KeireHub::HubUpdateLaunch&)
        {
            return KeireHub::HubStatus::Failure(
                {.Code = KeireHub::HubErrorCode::WorkerInterrupted, .Message = "The installer could not be launched."});
        }));
    CHECK_FALSE(std::filesystem::exists(manager.ResumeTokenPath()));
}

TEST_CASE("Hub update handoff confines installers to the verified cache")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    auto request = MakeRequest(temporary);
    request.InstallerPath = temporary.Path() / "outside.exe";
    KeireHubTests::WriteText(request.InstallerPath, "abc");

    const auto result = manager.BeginInstallerHandoff(
        request, [](const std::filesystem::path&) { return KeireHub::HubStatus::Success(); },
        [](const KeireHub::HubUpdateLaunch&) { return KeireHub::HubStatus::Success(); });
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == KeireHub::HubErrorCode::UnsafeInstallRoot);
}

TEST_CASE("Hub update handoff rejects downgrade and missing-key requests before launch")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    auto request = MakeRequest(temporary);
    bool launched = false;
    const auto launcher = [&](const KeireHub::HubUpdateLaunch&)
    {
        launched = true;
        return KeireHub::HubStatus::Success();
    };
    const auto signature = [](const std::filesystem::path&) { return KeireHub::HubStatus::Success(); };

    request.TargetVersion = "0.0.9";
    auto result = manager.BeginInstallerHandoff(request, signature, launcher);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == KeireHub::HubErrorCode::InvalidArgument);
    CHECK_FALSE(launched);

    request.TargetVersion = "0.1.0+different-build";
    result = manager.BeginInstallerHandoff(request, signature, launcher);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == KeireHub::HubErrorCode::InvalidArgument);
    CHECK_FALSE(launched);

    request.TargetVersion = "0.2.0";
    request.SignatureKeyId.clear();
    result = manager.BeginInstallerHandoff(request, signature, launcher);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == KeireHub::HubErrorCode::InvalidArgument);
    CHECK_FALSE(launched);
}

TEST_CASE("Hub update reconciliation rejects incomplete or forged resume records")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto token = temporary.Path() / "Preferences" / "hub-update.json";
    KeireHub::HubUpdateManager manager(token);
    KeireHubTests::WriteText(token, R"({"schemaVersion":1,"previousVersion":"0.1.0","targetVersion":"0.2.0"})");

    const auto result = manager.Reconcile("0.2.0");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == KeireHub::HubErrorCode::InvalidData);
    CHECK(std::filesystem::exists(token));
}
