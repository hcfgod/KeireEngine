#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubUpdateHandoffWorkflow.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace
{
    constexpr std::string_view InstallerDigest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    [[nodiscard]] KeireHub::HubResult<KeireHub::HubUpdatePlatformSignatureState>
    ValidPlatformSignature(const std::filesystem::path&)
    {
        return KeireHub::HubResult<KeireHub::HubUpdatePlatformSignatureState>::Success(
            KeireHub::HubUpdatePlatformSignatureState::Valid);
    }

    [[nodiscard]] KeireHub::HubUpdateRequest Request(const KeireHubTests::TemporaryDirectory& temporary)
    {
        const auto cacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");
        const auto installer = cacheRoot / "sha256" / "ba" / "installer.package";
        const auto installRoot = std::filesystem::absolute(temporary.Path() / "InstalledHub");
        KeireHubTests::WriteText(installer, "abc");
        KeireHubTests::WriteText(installRoot / "hub-package.json", "{}\n");
        return {.InstallerPath = installer,
                .VerifiedCacheRoot = cacheRoot,
                .HubInstallRoot = installRoot,
                .PackageId = "keire.hub.stable",
                .Sha256 = std::string(InstallerDigest),
                .CurrentVersion = "1.0.0",
                .TargetVersion = "2.0.0",
                .Platform = std::string(KeireHub::HubUpdateManager::HostPlatformIdentity()),
                .Architecture = std::string(KeireHub::HubUpdateManager::HostArchitectureIdentity()),
                .SignatureKeyId = "ed25519-00000000000000000000000000000000",
                .CatalogSequence = 7,
                .CurrentProcessId = 1234,
                .StartedUnixSeconds = 100,
                .PlatformSignaturePolicy = KeireHub::HubUpdatePlatformSignaturePolicy::Required};
    }

    [[nodiscard]] bool WaitForState(KeireHub::HubUpdateHandoffWorkflow& workflow,
                                    const KeireHub::HubUpdateHandoffState state)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do
        {
            if (workflow.Snapshot()->State == state)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return workflow.Snapshot()->State == state;
    }
} // namespace

TEST_CASE("Hub update handoff workflow keeps digest and signature work off the owner thread")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    KeireHub::HubUpdateHandoffWorkflow workflow;
    std::atomic<bool> signatureEntered = false;
    std::atomic<bool> releaseSignature = false;

    REQUIRE(workflow.Start(
        manager, Request(temporary),
        [&](const std::filesystem::path&)
        {
            signatureEntered = true;
            while (!releaseSignature)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return KeireHub::HubResult<KeireHub::HubUpdatePlatformSignatureState>::Success(
                KeireHub::HubUpdatePlatformSignatureState::Valid);
        },
        [](const KeireHub::HubUpdateLaunch&) { return KeireHub::HubStatus::Success(); }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!signatureEntered && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(signatureEntered);
    CHECK(workflow.Snapshot()->State == KeireHub::HubUpdateHandoffState::Verifying);

    const auto duplicate =
        workflow.Start(manager, Request(temporary), ValidPlatformSignature,
                       [](const KeireHub::HubUpdateLaunch&) { return KeireHub::HubStatus::Success(); });
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().Code == KeireHub::HubErrorCode::InvalidTransition);

    releaseSignature = true;
    REQUIRE(WaitForState(workflow, KeireHub::HubUpdateHandoffState::Launched));
    CHECK(std::filesystem::is_regular_file(manager.ResumeTokenPath()));
}

TEST_CASE("Hub update handoff workflow publishes sanitized launch failures")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubUpdateManager manager(temporary.Path() / "Preferences" / "hub-update.json");
    KeireHub::HubUpdateHandoffWorkflow workflow;
    REQUIRE(workflow.Start(manager, Request(temporary), ValidPlatformSignature,
                           [](const KeireHub::HubUpdateLaunch&)
                           {
                               return KeireHub::HubStatus::Failure({.Code = KeireHub::HubErrorCode::WorkerInterrupted,
                                                                    .Message = "The test installer did not launch.",
                                                                    .Retryable = true,
                                                                    .AffectedItem = {},
                                                                    .TechnicalDetails = "test",
                                                                    .LogReference = {}});
                           }));
    REQUIRE(WaitForState(workflow, KeireHub::HubUpdateHandoffState::Failed));
    REQUIRE(workflow.Snapshot()->Failure);
    CHECK(workflow.Snapshot()->Failure->Message == "The test installer did not launch.");
    CHECK_FALSE(std::filesystem::exists(manager.ResumeTokenPath()));
}
