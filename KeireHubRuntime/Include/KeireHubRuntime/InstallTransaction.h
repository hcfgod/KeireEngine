#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    inline constexpr const char* InstallReceiptFileName = ".keire-install-receipt.json";
    inline constexpr const char* InstallMarkerFileName = ".keire-install-marker.json";

    enum class InstallProduct
    {
        Editor,
        Hub
    };

    enum class InstallTransactionPhase
    {
        Staged,
        BackupMoved,
        PayloadActivated,
        RegistrationWritten,
        Verified,
        Committed
    };

    struct InstallOwnedFile final
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
    };

    struct InstallReceipt final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::string ProductId;
        std::string InstallationId;
        InstallProduct Product = InstallProduct::Editor;
        std::string Version;
        std::string BuildIdentity;
        std::string ManifestFingerprint;
        std::vector<InstallOwnedFile> Files;
        // Computed from the exact persisted receipt bytes and never encoded into the receipt itself.
        std::string DocumentSha256;
    };

    struct InstallRegistration final
    {
        std::string ProductId;
        std::string InstallationId;
        std::filesystem::path Root;
        std::string Version;
        std::string ManifestFingerprint;
        std::string ReceiptSha256;
    };

    struct InstallLegacyCandidate final
    {
        InstallProduct Product = InstallProduct::Editor;
        std::string ProductId;
        std::filesystem::path Root;
        std::string Version;
        std::string ManifestFingerprint;
    };

    struct InstallRegistrationStore final
    {
        std::function<HubResult<std::optional<InstallRegistration>>(InstallProduct)> Read;
        std::function<HubStatus(const InstallRegistration&)> Write;
        std::function<HubStatus(const InstallRegistration&)> Remove;
        // Optional and read-only. A non-empty receipt-less destination is migratable only when this validates the
        // exact legacy marker and matching product/uninstall registration for the manifest-backed candidate.
        std::function<HubStatus(const InstallLegacyCandidate&)> ValidateLegacy;
    };

    struct InstallTransactionRequest final
    {
        InstallProduct Product = InstallProduct::Editor;
        std::filesystem::path SourceRoot;
        std::filesystem::path DestinationRoot;
        InstallRegistrationStore Registration;
        // Returning false after a durable phase boundary leaves the transaction for the next recovery call.
        std::function<bool(InstallTransactionPhase)> ContinueAfterPhase;
        // Keeps a verified transaction recoverable until its UI shell has completed shortcuts and other shell work.
        bool DeferCommit = false;
    };

    struct InstallUninstallResult final
    {
        std::uint64_t RemovedFileCount = 0;
        std::uint64_t PreservedModifiedFileCount = 0;
    };

    [[nodiscard]] std::string_view ToString(InstallProduct product) noexcept;
    [[nodiscard]] std::optional<InstallProduct> ParseInstallProduct(std::string_view value) noexcept;
    [[nodiscard]] HubStatus ValidateInstallReceipt(const InstallReceipt& receipt);
    [[nodiscard]] HubResult<std::string> EncodeInstallReceipt(const InstallReceipt& receipt);
    [[nodiscard]] HubResult<InstallReceipt> ReadInstallReceipt(const std::filesystem::path& root);

    // InstallPackageTransaction stages and validates the complete source, backs up only previously receipt-owned
    // files, activates the new inventory, writes registration, verifies the result, and commits. A fault callback may
    // intentionally leave a durable transaction, which RecoverInstallTransaction rolls back idempotently.
    [[nodiscard]] HubStatus InstallPackageTransaction(const InstallTransactionRequest& request);
    [[nodiscard]] HubStatus CommitInstallTransaction(const InstallTransactionRequest& request);
    [[nodiscard]] HubStatus RecoverInstallTransaction(const InstallTransactionRequest& request);
    [[nodiscard]] HubStatus VerifyInstalledPackage(const InstallTransactionRequest& request);
    [[nodiscard]] HubResult<InstallUninstallResult>
    UninstallPackageTransaction(const InstallTransactionRequest& request);
} // namespace KeireHub
