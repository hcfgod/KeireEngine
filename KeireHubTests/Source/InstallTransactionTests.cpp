#include <doctest/doctest.h>

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/InstallTransaction.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    struct TestRegistrationStore final
    {
        std::optional<InstallRegistration> Value;
        bool AllowLegacy = false;
        bool FailNextWrite = false;
        std::optional<InstallLegacyCandidate> LegacyCandidate;

        [[nodiscard]] InstallRegistrationStore Interface()
        {
            return {.Read = [this](const InstallProduct)
                    { return HubResult<std::optional<InstallRegistration>>::Success(Value); },
                    .Write =
                        [this](const InstallRegistration& registration)
                    {
                        if (std::exchange(FailNextWrite, false))
                        {
                            Value = registration;
                            return HubStatus::Failure(
                                {.Code = HubErrorCode::IoWrite, .Message = "The injected registration write failed."});
                        }
                        Value = registration;
                        return HubStatus::Success();
                    },
                    .Remove =
                        [this](const InstallRegistration& expected)
                    {
                        if (Value && (Value->InstallationId != expected.InstallationId ||
                                      Value->ReceiptSha256 != expected.ReceiptSha256))
                        {
                            return HubStatus::Failure(
                                {.Code = HubErrorCode::InvalidTransition, .Message = "The test registration changed."});
                        }
                        Value.reset();
                        return HubStatus::Success();
                    },
                    .ValidateLegacy =
                        [this](const InstallLegacyCandidate& candidate)
                    {
                        if (!AllowLegacy)
                        {
                            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                                       .Message = "The test legacy registration is absent."});
                        }
                        LegacyCandidate = candidate;
                        return HubStatus::Success();
                    }};
        }
    };

    [[nodiscard]] std::string FileSha(const std::filesystem::path& path)
    {
        auto digest = KeireHub::Detail::HashInstallFile(path);
        if (!digest)
            throw std::runtime_error(digest.Error().Message);
        return std::move(digest).Value();
    }

    [[nodiscard]] std::filesystem::path CreatePackage(const std::filesystem::path& root, const InstallProduct product,
                                                      const std::string_view version, const std::string_view payload)
    {
        const auto source = root / (std::string(ToString(product)) + '-' + std::string(version));
        KeireHubTests::WriteText(source / "bin" / "app.exe", payload);
        KeireHubTests::WriteText(source / "bin" / "KeireInstallWorker.exe",
                                 std::string("trusted-worker-") + std::string(payload));
        KeireHubTests::WriteText(source / "Config" / "default.txt", std::string(payload) + "-config");

        KeireHub::Detail::Json files = KeireHub::Detail::Json::array();
        std::uint64_t payloadBytes = 0;
        for (const auto& relative :
             {std::filesystem::path("bin/app.exe"), std::filesystem::path("bin/KeireInstallWorker.exe"),
              std::filesystem::path("Config/default.txt")})
        {
            const auto size = std::filesystem::file_size(source / relative);
            payloadBytes += size;
            files.push_back({{"path", KeireHub::Detail::PathToUtf8(relative.generic_string())},
                             {"sizeBytes", size},
                             {"sha256", FileSha(source / relative)},
                             {"mode", 420}});
        }
        const auto manifestName = std::string(ToString(product)) + "-package.json";
        KeireHub::Detail::Json document{{"schemaVersion", 2},
                                        {"artifact", ToString(product)},
                                        {"packageId", std::string("keire.") + std::string(ToString(product))},
                                        {"version", version},
                                        {"channel", "Stable"},
                                        {"commit", std::string(40, 'a')},
                                        {"platform", "Windows"},
                                        {"architecture", "x86_64"},
                                        {"configuration", "Dist"},
                                        {"inventoryExcludes", {manifestName}},
                                        {"files", std::move(files)},
                                        {"installedSizeBytes", 0},
                                        {"manifestFingerprint", ""}};
        auto fingerprintPayload = document;
        fingerprintPayload.erase("manifestFingerprint");
        fingerprintPayload.erase("installedSizeBytes");
        const auto canonical = fingerprintPayload.dump();
        document["manifestFingerprint"] =
            KeireHub::Detail::Sha256Hex(std::as_bytes(std::span(canonical.data(), canonical.size())));
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto encoded = document.dump(2) + '\n';
            const auto installedBytes = payloadBytes + encoded.size();
            if (document["installedSizeBytes"].get<std::uint64_t>() == installedBytes)
            {
                KeireHubTests::WriteText(source / manifestName, encoded);
                return source;
            }
            document["installedSizeBytes"] = installedBytes;
        }
        throw std::runtime_error("The fixture manifest size did not converge.");
    }

    [[nodiscard]] InstallTransactionRequest Request(const InstallProduct product, const std::filesystem::path& source,
                                                    const std::filesystem::path& destination,
                                                    TestRegistrationStore& registration)
    {
        return {.Product = product,
                .SourceRoot = source,
                .DestinationRoot = destination,
                .Registration = registration.Interface()};
    }

    [[nodiscard]] std::filesystem::path TransactionLocatorPath(const std::filesystem::path& destination)
    {
        auto locator = destination;
        locator += ".__keire-install-transaction.locator.json";
        return locator;
    }

    [[nodiscard]] std::filesystem::path TransactionRoot(const std::filesystem::path& destination)
    {
        const auto document = KeireHub::Detail::ReadJsonFile(TransactionLocatorPath(destination), 16U * 1024U);
        if (!document)
            throw std::runtime_error(document.Error().Message);
        return destination.parent_path() /
               KeireHub::Detail::PathFromUtf8(document.Value().at("transactionRootName").get<std::string>());
    }

#if defined(_WIN32) && defined(KEIRE_INSTALL_TRANSACTION_TESTING)
    struct RootSwapAttack final
    {
        std::filesystem::path Destination;
        std::filesystem::path Original;
        std::error_code Error;
        bool Fired = false;
    };

    RootSwapAttack* s_RootSwapAttack = nullptr;

    void SwapVisibleInstallRoot(const std::string_view operation, const std::filesystem::path&)
    {
        if (!s_RootSwapAttack || s_RootSwapAttack->Fired || operation != "remove-root" ||
            std::filesystem::exists(TransactionLocatorPath(s_RootSwapAttack->Destination)))
        {
            return;
        }
        std::filesystem::rename(s_RootSwapAttack->Destination, s_RootSwapAttack->Original, s_RootSwapAttack->Error);
        if (s_RootSwapAttack->Error)
            return;
        KeireHubTests::WriteText(s_RootSwapAttack->Destination / "Config" / "unowned.txt", "preserve-me");
        s_RootSwapAttack->Fired = true;
    }

    class ScopedInstallMutationHook final
    {
      public:
        explicit ScopedInstallMutationHook(RootSwapAttack& attack)
        {
            s_RootSwapAttack = &attack;
            KeireHub::Detail::SetInstallMutationHookForTesting(&SwapVisibleInstallRoot);
        }

        ~ScopedInstallMutationHook()
        {
            KeireHub::Detail::SetInstallMutationHookForTesting(nullptr);
            s_RootSwapAttack = nullptr;
        }

        ScopedInstallMutationHook(const ScopedInstallMutationHook&) = delete;
        ScopedInstallMutationHook& operator=(const ScopedInstallMutationHook&) = delete;
    };

    class ScopedTransientRenameFailures final
    {
      public:
        explicit ScopedTransientRenameFailures(const std::size_t failureCount)
        {
            KeireHub::Detail::SetInstallMutationTransientRenameFailuresForTesting(failureCount);
        }

        ~ScopedTransientRenameFailures()
        {
            KeireHub::Detail::SetInstallMutationTransientRenameFailuresForTesting(0);
            KeireHub::Detail::SetInstallMutationTransientDeleteFailuresForTesting(0);
            KeireHub::Detail::SetInstallMutationTransientDirectoryNotEmptyFailuresForTesting(0);
        }

        ScopedTransientRenameFailures(const ScopedTransientRenameFailures&) = delete;
        ScopedTransientRenameFailures& operator=(const ScopedTransientRenameFailures&) = delete;
    };
#endif

    void AddInstallerGeneratedFiles(const std::filesystem::path& root, const InstallProduct product,
                                    const std::string_view marker)
    {
        KeireHubTests::WriteText(root / "Uninstall.exe", "fixture-uninstaller");
        KeireHubTests::WriteText(root / (std::string(".keire-") + std::string(ToString(product)) + "-install"), marker);
    }

    void CopyLegacyPackage(const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        std::error_code error;
        std::filesystem::copy(
            source, destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
        if (error)
            throw std::runtime_error("Could not create a legacy package fixture: " + error.message());
    }
} // namespace

#if defined(_WIN32) && defined(KEIRE_INSTALL_TRANSACTION_TESTING)
TEST_CASE("anchored install mutations retry transient Windows file-filter interference")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "transaction";
    KeireHub::Detail::InstallMutationFileSystem files(root, true, true);
    ScopedTransientRenameFailures failures(3);

    const auto written = files.WriteTextAtomically("journal.json", "first\n", false);
    if (!written)
        INFO(written.Error().TechnicalDetails);
    REQUIRE(written);
    KeireHub::Detail::SetInstallMutationTransientRenameFailuresForTesting(2);
    const auto replaced = files.WriteTextAtomically("journal.json", "second\n", true);
    if (!replaced)
        INFO(replaced.Error().TechnicalDetails);
    REQUIRE(replaced);
    const auto contents = files.ReadText("journal.json", 1024);
    REQUIRE(contents);
    CHECK(contents.Value() == "second\n");

    const auto owned = files.Describe("journal.json");
    REQUIRE(owned);
    KeireHub::Detail::SetInstallMutationTransientDeleteFailuresForTesting(3);
    const auto removed = files.RemoveVerified(owned.Value());
    KeireHub::Detail::SetInstallMutationTransientDeleteFailuresForTesting(0);
    if (!removed)
        INFO(removed.Error().TechnicalDetails);
    REQUIRE(removed);
    CHECK_FALSE(std::filesystem::exists(root / "journal.json"));
}

TEST_CASE("anchored install directory removal retries pending Windows child deletion")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "transaction";
    {
        KeireHub::Detail::InstallMutationFileSystem files(root, true, true);
        KeireHub::Detail::SetInstallMutationTransientDirectoryNotEmptyFailuresForTesting(3);
        const auto removed = files.RemoveRootIfEmpty();
        KeireHub::Detail::SetInstallMutationTransientDirectoryNotEmptyFailuresForTesting(0);
        if (!removed)
            INFO(removed.Error().TechnicalDetails);
        REQUIRE(removed);
    }
    CHECK_FALSE(std::filesystem::exists(root));
}
#endif

TEST_CASE("install worker accepts only an absent or empty fresh destination")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "1.2.3", "fresh");
    const auto destination = temporary.Path() / "installed-hub";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Hub, source, destination, registration);

    const auto freshInstall = InstallPackageTransaction(request);
    if (!freshInstall)
    {
        FAIL(freshInstall.Error().Message << " " << freshInstall.Error().AffectedItem << " "
                                          << freshInstall.Error().TechnicalDetails);
    }
    REQUIRE(freshInstall);
    REQUIRE(registration.Value);
    CHECK(std::filesystem::is_regular_file(destination / InstallReceiptFileName));
    CHECK(std::filesystem::is_regular_file(destination / InstallMarkerFileName));
    CHECK(VerifyInstalledPackage(request));
    CHECK(KeireHub::Detail::ReadInstallerPackageManifest(destination, InstallProduct::Hub));

    for (const auto& unownedDirectory : {"Config", "Docs", "Samples"})
    {
        const auto rejectedRoot = temporary.Path() / (std::string("unowned-") + unownedDirectory);
        KeireHubTests::WriteText(rejectedRoot / unownedDirectory / "user.txt", "preserve-me");
        TestRegistrationStore rejectedRegistration;
        auto rejected = Request(InstallProduct::Hub, source, rejectedRoot, rejectedRegistration);
        CHECK_FALSE(InstallPackageTransaction(rejected));
        CHECK(KeireHubTests::ReadText(rejectedRoot / unownedDirectory / "user.txt") == "preserve-me");
        CHECK_FALSE(rejectedRegistration.Value);
    }
}

TEST_CASE("install worker updates exact owned inventory and refuses drift or unknown collisions")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Editor, "1.0.0", "old");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Editor, "2.0.0", "new");
    const auto destination = temporary.Path() / "editor";
    TestRegistrationStore registration;
    REQUIRE(InstallPackageTransaction(Request(InstallProduct::Editor, first, destination, registration)));
    const auto installationId = registration.Value->InstallationId;
    KeireHubTests::WriteText(destination / "Docs" / "private-note.txt", "unknown");

    REQUIRE(InstallPackageTransaction(Request(InstallProduct::Editor, second, destination, registration)));
    CHECK(registration.Value->InstallationId == installationId);
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "new");
    CHECK(KeireHubTests::ReadText(destination / "Docs" / "private-note.txt") == "unknown");

    KeireHubTests::WriteText(destination / "Config" / "default.txt", "locally-modified");
    CHECK_FALSE(InstallPackageTransaction(Request(InstallProduct::Editor, first, destination, registration)));
    CHECK(KeireHubTests::ReadText(destination / "Config" / "default.txt") == "locally-modified");
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "new");
}

TEST_CASE("install worker restores the previous installation when registration publication fails")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Editor, "1.0.0", "registered-old");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Editor, "2.0.0", "unregistered-new");
    const auto destination = temporary.Path() / "editor-registration-failure";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Editor, first, destination, registration);
    REQUIRE(InstallPackageTransaction(request));
    REQUIRE(registration.Value);
    const auto oldRegistration = *registration.Value;
    const auto oldReceipt = KeireHubTests::ReadText(destination / InstallReceiptFileName);
    KeireHubTests::WriteText(destination / "Docs" / "private.txt", "preserve-me");

    registration.FailNextWrite = true;
    request.SourceRoot = second;
    const auto updated = InstallPackageTransaction(request);
    REQUIRE_FALSE(updated);
    CHECK(updated.Error().Code == HubErrorCode::IoWrite);
    REQUIRE(registration.Value);
    CHECK(registration.Value->InstallationId == oldRegistration.InstallationId);
    CHECK(registration.Value->ReceiptSha256 == oldRegistration.ReceiptSha256);
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "registered-old");
    CHECK(KeireHubTests::ReadText(destination / InstallReceiptFileName) == oldReceipt);
    CHECK(KeireHubTests::ReadText(destination / "Docs" / "private.txt") == "preserve-me");
    CHECK(VerifyInstalledPackage(request));
    CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
}

TEST_CASE("install worker migrates only exact manifest-backed legacy installations")
{
    KeireHubTests::TemporaryDirectory temporary;
    for (const auto product : {InstallProduct::Editor, InstallProduct::Hub})
    {
        const auto legacySource = CreatePackage(temporary.Path(), product, "1.0.0", "legacy");
        const auto replacement = CreatePackage(temporary.Path(), product, "2.0.0", "replacement");
        AddInstallerGeneratedFiles(legacySource, product, "legacy-marker\r\n");
        AddInstallerGeneratedFiles(replacement, product, "replacement-marker\r\n");
        const auto destination = temporary.Path() / (std::string(ToString(product)) + "-legacy");
        CopyLegacyPackage(legacySource, destination);
        KeireHubTests::WriteText(destination / "Config" / "private" / "user.json", "private-config");
        KeireHubTests::WriteText(destination / "Docs" / "private.txt", "private-doc");
        KeireHubTests::WriteText(destination / "Samples" / "private.txt", "private-sample");

        TestRegistrationStore registration;
        registration.AllowLegacy = true;
        auto request = Request(product, replacement, destination, registration);
        REQUIRE(InstallPackageTransaction(request));
        REQUIRE(registration.LegacyCandidate);
        CHECK(registration.LegacyCandidate->Root == destination);
        CHECK(registration.LegacyCandidate->Version == "1.0.0");
        CHECK(std::filesystem::is_regular_file(destination / InstallReceiptFileName));
        CHECK(std::filesystem::is_regular_file(destination / InstallMarkerFileName));
        CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "replacement");
        CHECK(KeireHubTests::ReadText(destination / "Config" / "private" / "user.json") == "private-config");
        CHECK(KeireHubTests::ReadText(destination / "Docs" / "private.txt") == "private-doc");
        CHECK(KeireHubTests::ReadText(destination / "Samples" / "private.txt") == "private-sample");

        REQUIRE(UninstallPackageTransaction(request));
        CHECK(KeireHubTests::ReadText(destination / "Config" / "private" / "user.json") == "private-config");
        CHECK(KeireHubTests::ReadText(destination / "Docs" / "private.txt") == "private-doc");
        CHECK(KeireHubTests::ReadText(destination / "Samples" / "private.txt") == "private-sample");
    }
}

TEST_CASE("install worker recovery restores the previous package byte for byte")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Hub, "1.0.0", "stable-old");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Hub, "2.0.0", "replacement");
    const std::array phases{InstallTransactionPhase::Staged, InstallTransactionPhase::BackupMoved,
                            InstallTransactionPhase::PayloadActivated, InstallTransactionPhase::RegistrationWritten,
                            InstallTransactionPhase::Verified};

    for (const auto phase : phases)
    {
        const auto destination = temporary.Path() / ("recovery-" + std::to_string(static_cast<int>(phase)));
        TestRegistrationStore registration;
        REQUIRE(InstallPackageTransaction(Request(InstallProduct::Hub, first, destination, registration)));
        const auto oldReceipt = KeireHubTests::ReadText(destination / InstallReceiptFileName);
        const auto oldRegistration = *registration.Value;
        auto interrupted = Request(InstallProduct::Hub, second, destination, registration);
        interrupted.ContinueAfterPhase = [phase](const InstallTransactionPhase current) { return current != phase; };
        const auto status = InstallPackageTransaction(interrupted);
        REQUIRE_FALSE(status);
        CHECK(status.Error().Code == HubErrorCode::WorkerInterrupted);

        REQUIRE(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "stable-old");
        CHECK(KeireHubTests::ReadText(destination / InstallReceiptFileName) == oldReceipt);
        REQUIRE(registration.Value);
        CHECK(registration.Value->ReceiptSha256 == oldRegistration.ReceiptSha256);
        CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
    }
}

TEST_CASE("install worker recovery finalizes a committed replacement idempotently")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Editor, "1.0.0", "stable-old");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Editor, "2.0.0", "committed-new");
    const auto destination = temporary.Path() / "committed-recovery";
    TestRegistrationStore registration;
    REQUIRE(InstallPackageTransaction(Request(InstallProduct::Editor, first, destination, registration)));

    auto interrupted = Request(InstallProduct::Editor, second, destination, registration);
    interrupted.ContinueAfterPhase = [](const InstallTransactionPhase phase)
    { return phase != InstallTransactionPhase::Committed; };
    const auto status = InstallPackageTransaction(interrupted);
    REQUIRE_FALSE(status);
    CHECK(status.Error().Code == HubErrorCode::WorkerInterrupted);
    REQUIRE(registration.Value);
    CHECK(registration.Value->Version == "2.0.0");
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "committed-new");
    CHECK(std::filesystem::exists(TransactionLocatorPath(destination)));

    REQUIRE(RecoverInstallTransaction(Request(InstallProduct::Editor, {}, destination, registration)));
    CHECK(RecoverInstallTransaction(Request(InstallProduct::Editor, {}, destination, registration)));
    REQUIRE(registration.Value);
    CHECK(registration.Value->Version == "2.0.0");
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "committed-new");
    CHECK(VerifyInstalledPackage(Request(InstallProduct::Editor, {}, destination, registration)));
    CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
}

TEST_CASE("install worker defers commit until its shell work succeeds")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Editor, "2.1.0", "old");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Editor, "2.2.0", "new");
    const auto destination = temporary.Path() / "editor";
    TestRegistrationStore registration;
    REQUIRE(InstallPackageTransaction(Request(InstallProduct::Editor, first, destination, registration)));

    auto deferred = Request(InstallProduct::Editor, second, destination, registration);
    deferred.DeferCommit = true;
    REQUIRE(InstallPackageTransaction(deferred));
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "new");
    CHECK(std::filesystem::exists(TransactionLocatorPath(destination)));

    REQUIRE(RecoverInstallTransaction(Request(InstallProduct::Editor, {}, destination, registration)));
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "old");

    REQUIRE(InstallPackageTransaction(deferred));
    REQUIRE(CommitInstallTransaction(Request(InstallProduct::Editor, {}, destination, registration)));
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "new");
    CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
    CHECK(CommitInstallTransaction(Request(InstallProduct::Editor, {}, destination, registration)));
}

TEST_CASE("install worker uninstall preserves modified files and nested unknown neighbors")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Editor, "3.0.0", "owned");
    const auto destination = temporary.Path() / "editor";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Editor, source, destination, registration);
    REQUIRE(InstallPackageTransaction(request));
    KeireHubTests::WriteText(destination / "Config" / "default.txt", "user-modified");
    KeireHubTests::WriteText(destination / "Config" / "nested" / "user.json", "private");
    KeireHubTests::WriteText(destination / "bin" / "KeireInstallWorker.exe", "untrusted-replacement");

    const auto removed = UninstallPackageTransaction(request);
    if (!removed)
    {
        FAIL(removed.Error().Message << " " << removed.Error().AffectedItem << " " << removed.Error().TechnicalDetails);
    }
    REQUIRE(removed);
    CHECK(removed.Value().PreservedModifiedFileCount == 2);
    CHECK_FALSE(registration.Value);
    CHECK(KeireHubTests::ReadText(destination / "Config" / "default.txt") == "user-modified");
    CHECK(KeireHubTests::ReadText(destination / "Config" / "nested" / "user.json") == "private");
    CHECK(KeireHubTests::ReadText(destination / "bin" / "KeireInstallWorker.exe") == "untrusted-replacement");
    CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
    CHECK_FALSE(std::filesystem::exists(destination / InstallReceiptFileName));
}

TEST_CASE("install worker recovers every durable uninstall phase repeatedly")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "3.1.0", "durable-uninstall");
    const std::array phases{InstallTransactionPhase::Staged, InstallTransactionPhase::BackupMoved,
                            InstallTransactionPhase::RegistrationWritten, InstallTransactionPhase::Committed};

    for (const auto phase : phases)
    {
        const auto destination = temporary.Path() / ("uninstall-recovery-" + std::to_string(static_cast<int>(phase)));
        TestRegistrationStore registration;
        auto request = Request(InstallProduct::Hub, source, destination, registration);
        REQUIRE(InstallPackageTransaction(request));
        REQUIRE(registration.Value);
        const auto oldRegistration = *registration.Value;
        const auto oldReceipt = KeireHubTests::ReadText(destination / InstallReceiptFileName);
        KeireHubTests::WriteText(destination / "Docs" / "private.txt", "preserve-me");

        request.ContinueAfterPhase = [phase](const InstallTransactionPhase current) { return current != phase; };
        const auto interrupted = UninstallPackageTransaction(request);
        REQUIRE_FALSE(interrupted);
        CHECK(interrupted.Error().Code == HubErrorCode::WorkerInterrupted);
        CHECK(std::filesystem::exists(TransactionLocatorPath(destination)));

        REQUIRE(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
        CHECK(KeireHubTests::ReadText(destination / "Docs" / "private.txt") == "preserve-me");
        if (phase == InstallTransactionPhase::Committed)
        {
            CHECK_FALSE(registration.Value);
            CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
            CHECK_FALSE(std::filesystem::exists(destination / InstallReceiptFileName));
        }
        else
        {
            REQUIRE(registration.Value);
            CHECK(registration.Value->ReceiptSha256 == oldRegistration.ReceiptSha256);
            CHECK(KeireHubTests::ReadText(destination / InstallReceiptFileName) == oldReceipt);
            CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "durable-uninstall");
            CHECK(VerifyInstalledPackage(Request(InstallProduct::Hub, {}, destination, registration)));
        }
    }
}

#if defined(_WIN32) && defined(KEIRE_INSTALL_TRANSACTION_TESTING)
TEST_CASE("install worker removes only its retained root after a post-cleanup path swap")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "3.1.0", "owned");
    const auto destination = temporary.Path() / "hub";
    const auto original = temporary.Path() / "anchored-original";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Hub, source, destination, registration);
    REQUIRE(InstallPackageTransaction(request));

    RootSwapAttack attack{.Destination = destination, .Original = original};
    {
        ScopedInstallMutationHook hook(attack);
        REQUIRE(UninstallPackageTransaction(request));
    }

    REQUIRE(attack.Fired);
    REQUIRE_FALSE(attack.Error);
    CHECK(KeireHubTests::ReadText(destination / "Config" / "unowned.txt") == "preserve-me");
    CHECK_FALSE(std::filesystem::exists(original));
    CHECK_FALSE(registration.Value);
}
#endif

TEST_CASE("install worker rejects receipt registration mismatches and links")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "4.0.0", "owned");
    const auto destination = temporary.Path() / "hub";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Hub, source, destination, registration);
    REQUIRE(InstallPackageTransaction(request));
    registration.Value->InstallationId = "mismatched-installation";
    CHECK_FALSE(VerifyInstalledPackage(request));

    const auto unsafeSource = CreatePackage(temporary.Path(), InstallProduct::Editor, "4.0.1", "unsafe");
    const auto link = unsafeSource / "unsafe-link";
    std::error_code error;
    std::filesystem::create_directory_symlink(temporary.Path(), link, error);
    if (!error)
    {
        TestRegistrationStore unsafeRegistration;
        CHECK_FALSE(InstallPackageTransaction(Request(InstallProduct::Editor, unsafeSource,
                                                      temporary.Path() / "unsafe-destination", unsafeRegistration)));
    }
}

TEST_CASE("install worker revalidates owned bytes at the mutation boundary")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = CreatePackage(temporary.Path(), InstallProduct::Hub, "5.0.0", "old-bytes");
    const auto second = CreatePackage(temporary.Path(), InstallProduct::Hub, "5.1.0", "new-bytes");
    const auto destination = temporary.Path() / "hub";
    TestRegistrationStore registration;
    REQUIRE(InstallPackageTransaction(Request(InstallProduct::Hub, first, destination, registration)));

    auto attacked = Request(InstallProduct::Hub, second, destination, registration);
    attacked.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
    {
        if (phase == InstallTransactionPhase::Staged)
            KeireHubTests::WriteText(destination / "bin" / "app.exe", "changed-after-classification");
        return true;
    };
    CHECK_FALSE(InstallPackageTransaction(attacked));
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "changed-after-classification");
    CHECK_FALSE(std::filesystem::exists(TransactionLocatorPath(destination)));
}

TEST_CASE("install worker rechecks parent components after staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Editor, "5.0.0", "payload");
    const auto destination = temporary.Path() / "editor";
    TestRegistrationStore registration;
    auto attacked = Request(InstallProduct::Editor, source, destination, registration);
    attacked.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
    {
        if (phase == InstallTransactionPhase::Staged)
        {
            std::filesystem::create_directories(destination);
            KeireHubTests::WriteText(destination / "Config", "parent-substitution");
        }
        return true;
    };
    CHECK_FALSE(InstallPackageTransaction(attacked));
    CHECK(KeireHubTests::ReadText(destination / "Config") == "parent-substitution");
    CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
}

TEST_CASE("install worker rejects a reparse substitution after staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "5.2.0", "payload");
    const auto destination = temporary.Path() / "hub";
    const auto outside = temporary.Path() / "outside";
    KeireHubTests::WriteText(outside / "private.txt", "do-not-touch");

    const auto probe = temporary.Path() / "reparse-probe";
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, probe, linkError);
    if (linkError)
    {
        WARN("Directory-link creation is unavailable; the deterministic non-directory substitution case still runs.");
        return;
    }
    REQUIRE(std::filesystem::remove(probe));

    TestRegistrationStore registration;
    auto attacked = Request(InstallProduct::Hub, source, destination, registration);
    attacked.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
    {
        if (phase == InstallTransactionPhase::Staged)
        {
            std::filesystem::create_directories(destination);
            std::filesystem::create_directory_symlink(outside, destination / "Config", linkError);
        }
        return true;
    };
    CHECK_FALSE(InstallPackageTransaction(attacked));
    REQUIRE_FALSE(linkError);
    CHECK(KeireHubTests::ReadText(outside / "private.txt") == "do-not-touch");
    CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
    CHECK_FALSE(registration.Value);
}

TEST_CASE("install worker rejects an uninstall-time reparse substitution and restores prior moves")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Editor, "5.3.0", "payload");
    const auto destination = temporary.Path() / "editor";
    TestRegistrationStore registration;
    auto request = Request(InstallProduct::Editor, source, destination, registration);
    REQUIRE(InstallPackageTransaction(request));

    const auto probe = temporary.Path() / "reparse-probe";
    const auto originalConfig = temporary.Path() / "original-config";
    std::error_code linkError;
    std::filesystem::create_directory_symlink(destination / "Config", probe, linkError);
    if (linkError)
    {
        WARN("Directory-link creation is unavailable; uninstall hash-drift preservation remains covered.");
        return;
    }
    REQUIRE(std::filesystem::remove(probe));

    request.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
    {
        if (phase == InstallTransactionPhase::Staged)
        {
            std::filesystem::rename(destination / "Config", originalConfig, linkError);
            if (!linkError)
                std::filesystem::create_directory_symlink(originalConfig, destination / "Config", linkError);
        }
        return true;
    };
    const auto removed = UninstallPackageTransaction(request);
    CHECK_FALSE(removed);
    REQUIRE_FALSE(linkError);
    REQUIRE(registration.Value);
    CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "payload");
    CHECK(KeireHubTests::ReadText(originalConfig / "default.txt") == "payload-config");
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(destination / "Config")));
}

TEST_CASE("install worker cleanup preserves unknown nested transaction content")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = CreatePackage(temporary.Path(), InstallProduct::Hub, "6.0.0", "payload");

    SUBCASE("committed install cleanup")
    {
        const auto destination = temporary.Path() / "install";
        std::filesystem::path unknown;
        TestRegistrationStore registration;
        auto request = Request(InstallProduct::Hub, source, destination, registration);
        request.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
        {
            if (phase == InstallTransactionPhase::Verified)
            {
                unknown = TransactionRoot(destination) / "backup" / "Config" / "private" / "user.txt";
                KeireHubTests::WriteText(unknown, "preserve-me");
            }
            return true;
        };
        CHECK_FALSE(InstallPackageTransaction(request));
        CHECK(KeireHubTests::ReadText(destination / "bin" / "app.exe") == "payload");
        CHECK(KeireHubTests::ReadText(unknown) == "preserve-me");
        CHECK_FALSE(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK(KeireHubTests::ReadText(unknown) == "preserve-me");
    }

    SUBCASE("rollback cleanup")
    {
        const auto destination = temporary.Path() / "rollback";
        std::filesystem::path unknown;
        TestRegistrationStore registration;
        auto request = Request(InstallProduct::Hub, source, destination, registration);
        request.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
        {
            if (phase == InstallTransactionPhase::Staged)
            {
                unknown = TransactionRoot(destination) / "stage" / "Config" / "private" / "user.txt";
                KeireHubTests::WriteText(unknown, "preserve-me");
            }
            return phase != InstallTransactionPhase::Staged;
        };
        CHECK_FALSE(InstallPackageTransaction(request));
        CHECK_FALSE(RecoverInstallTransaction(Request(InstallProduct::Hub, {}, destination, registration)));
        CHECK(KeireHubTests::ReadText(unknown) == "preserve-me");
        CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
    }

    SUBCASE("committed uninstall cleanup")
    {
        const auto destination = temporary.Path() / "uninstall";
        std::filesystem::path unknown;
        TestRegistrationStore registration;
        auto request = Request(InstallProduct::Hub, source, destination, registration);
        REQUIRE(InstallPackageTransaction(request));
        request.ContinueAfterPhase = [&](const InstallTransactionPhase phase)
        {
            if (phase == InstallTransactionPhase::Staged)
            {
                unknown = TransactionRoot(destination) / "backup" / "Config" / "private" / "user.txt";
                KeireHubTests::WriteText(unknown, "preserve-me");
            }
            return true;
        };
        CHECK_FALSE(UninstallPackageTransaction(request));
        CHECK(KeireHubTests::ReadText(unknown) == "preserve-me");
        CHECK_FALSE(std::filesystem::exists(destination / "bin" / "app.exe"));
        CHECK_FALSE(registration.Value);
    }
}
