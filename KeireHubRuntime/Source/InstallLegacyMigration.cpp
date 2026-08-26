#include <KeireHubRuntimeInternal/InstallLegacyMigrationInternal.h>

#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/InstallTransactionLocatorInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <ranges>

namespace KeireHub::Detail
{
    namespace
    {
        [[nodiscard]] HubError MigrationError(const HubErrorCode code, std::string message,
                                              const std::filesystem::path& path = {}, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = PathToUtf8(path),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            auto leftKey = NormalizedInstallPathKey(std::filesystem::absolute(left).lexically_normal());
            auto rightKey = NormalizedInstallPathKey(std::filesystem::absolute(right).lexically_normal());
            return leftKey == rightKey;
        }

        [[nodiscard]] bool Matches(const InstallRegistration& registration, const InstallReceipt& receipt,
                                   const std::filesystem::path& root)
        {
            return registration.ProductId == receipt.ProductId &&
                   registration.InstallationId == receipt.InstallationId && SamePath(registration.Root, root) &&
                   registration.Version == receipt.Version &&
                   registration.ManifestFingerprint == receipt.ManifestFingerprint &&
                   registration.ReceiptSha256 == receipt.DocumentSha256;
        }

        [[nodiscard]] HubResult<InstallReceipt>
        BuildReceipt(const InstallerPackageManifest& manifest, const InstallProduct product,
                     const std::filesystem::path& root, const std::string_view installationId,
                     InstallMutationFileSystem& files)
        {
            InstallReceipt receipt{.ProductId = manifest.ProductId,
                                   .InstallationId = std::string(installationId),
                                   .Product = product,
                                   .Version = manifest.Version,
                                   .BuildIdentity = manifest.BuildIdentity,
                                   .ManifestFingerprint = manifest.Fingerprint,
                                   .Files = manifest.Files};
            const auto legacyMarker =
                std::filesystem::path(std::string(".keire-") + std::string(ToString(product)) + "-install");
            for (const auto& path : {manifest.ManifestPath, legacyMarker, std::filesystem::path("Uninstall.exe")})
            {
                auto described = files.Describe(path);
                if (!described)
                {
                    return HubResult<InstallReceipt>::Failure(MigrationError(
                        HubErrorCode::UnsafeInstallRoot, "A required legacy installation file is missing or unsafe.",
                        root / path, described.Error().TechnicalDetails));
                }
                receipt.Files.push_back(std::move(described).Value());
            }
            auto marker = EncodeInstallMarker({.ProductId = receipt.ProductId,
                                               .InstallationId = receipt.InstallationId,
                                               .Product = receipt.Product,
                                               .ManifestFingerprint = receipt.ManifestFingerprint});
            if (!marker)
                return HubResult<InstallReceipt>::Failure(marker.Error());
            receipt.Files.push_back({.Path = InstallMarkerFileName,
                                     .SizeBytes = marker.Value().size(),
                                     .Sha256 = HashInstallDocument(marker.Value())});
            auto encoded = EncodeInstallReceipt(receipt);
            if (!encoded)
                return HubResult<InstallReceipt>::Failure(encoded.Error());
            receipt.DocumentSha256 = HashInstallDocument(encoded.Value());
            return HubResult<InstallReceipt>::Success(std::move(receipt));
        }

        [[nodiscard]] HubStatus VerifyInventory(InstallMutationFileSystem& files, const InstallReceipt& receipt,
                                                const bool markerMayBeMissing)
        {
            for (const auto& expected : receipt.Files)
            {
                auto actual = files.Describe(expected.Path, markerMayBeMissing && expected.Path == InstallMarkerFileName);
                if (!actual)
                    return HubStatus::Failure(actual.Error());
                if (actual.Value().Sha256.empty() && expected.Path == InstallMarkerFileName)
                    continue;
                if (actual.Value().SizeBytes != expected.SizeBytes || actual.Value().Sha256 != expected.Sha256)
                {
                    return HubStatus::Failure(MigrationError(
                        HubErrorCode::InvalidData, "A legacy installation file changed during migration.",
                        expected.Path));
                }
            }
            return HubStatus::Success();
        }
    } // namespace

    HubResult<InstallReceipt> MigrateLegacyInstallation(InstallMutationAuthority& mutation,
                                                        const InstallTransactionRequest& request)
    {
        if (!request.Registration.ValidateLegacy)
        {
            return HubResult<InstallReceipt>::Failure(MigrationError(
                HubErrorCode::UnsafeInstallRoot, "The receipt-less installation has no legacy ownership validator.",
                request.DestinationRoot));
        }
        auto manifest = ReadInstallerPackageManifest(request.DestinationRoot, request.Product, true);
        if (!manifest)
            return HubResult<InstallReceipt>::Failure(manifest.Error());
        const InstallLegacyCandidate candidate{.Product = request.Product,
                                               .ProductId = manifest.Value().ProductId,
                                               .Root = request.DestinationRoot,
                                               .Version = manifest.Value().Version,
                                               .ManifestFingerprint = manifest.Value().Fingerprint};
        if (const auto validated = request.Registration.ValidateLegacy(candidate); !validated)
            return HubResult<InstallReceipt>::Failure(validated.Error());
        auto pinned = mutation.Pin(request.DestinationRoot);
        if (!pinned)
            return HubResult<InstallReceipt>::Failure(pinned.Error());
        auto files = pinned.Value();

        std::string installationId;
        auto existingReceipt = files->Describe(InstallReceiptFileName, true);
        if (!existingReceipt)
            return HubResult<InstallReceipt>::Failure(existingReceipt.Error());
        if (!existingReceipt.Value().Sha256.empty())
        {
            auto receipt = ReadInstallReceipt(request.DestinationRoot);
            if (!receipt)
                return HubResult<InstallReceipt>::Failure(receipt.Error());
            installationId = receipt.Value().InstallationId;
        }
        else
        {
            auto existingMarker = files->Describe(InstallMarkerFileName, true);
            if (!existingMarker)
                return HubResult<InstallReceipt>::Failure(existingMarker.Error());
            if (!existingMarker.Value().Sha256.empty())
            {
                auto marker = ReadInstallMarker(request.DestinationRoot);
                if (!marker || marker.Value().Product != request.Product ||
                    marker.Value().ProductId != manifest.Value().ProductId ||
                    marker.Value().ManifestFingerprint != manifest.Value().Fingerprint)
                {
                    return HubResult<InstallReceipt>::Failure(
                        marker ? MigrationError(HubErrorCode::UnsafeInstallRoot,
                                                "The partial legacy migration marker is not manifest-bound.",
                                                request.DestinationRoot / InstallMarkerFileName)
                               : marker.Error());
                }
                installationId = marker.Value().InstallationId;
            }
            else
            {
                installationId = SecureInstallRandomId();
            }
        }

        auto expected = BuildReceipt(manifest.Value(), request.Product, request.DestinationRoot, installationId, *files);
        if (!expected)
            return expected;
        auto marker = EncodeInstallMarker({.ProductId = expected.Value().ProductId,
                                           .InstallationId = expected.Value().InstallationId,
                                           .Product = expected.Value().Product,
                                           .ManifestFingerprint = expected.Value().ManifestFingerprint});
        auto receiptDocument = EncodeInstallReceipt(expected.Value());
        if (!marker || !receiptDocument)
            return HubResult<InstallReceipt>::Failure(!marker ? marker.Error() : receiptDocument.Error());

        bool wroteMarker = false;
        bool wroteReceipt = false;
        auto markerFile = files->Describe(InstallMarkerFileName, true);
        if (!markerFile)
            return HubResult<InstallReceipt>::Failure(markerFile.Error());
        if (markerFile.Value().Sha256.empty())
        {
            if (const auto written = files->WriteTextAtomically(InstallMarkerFileName, marker.Value(), false); !written)
                return HubResult<InstallReceipt>::Failure(written.Error());
            wroteMarker = true;
        }
        auto receiptFile = files->Describe(InstallReceiptFileName, true);
        if (!receiptFile)
            return HubResult<InstallReceipt>::Failure(receiptFile.Error());
        if (receiptFile.Value().Sha256.empty())
        {
            if (const auto written =
                    files->WriteTextAtomically(InstallReceiptFileName, receiptDocument.Value(), false);
                !written)
                return HubResult<InstallReceipt>::Failure(written.Error());
            wroteReceipt = true;
        }
        else
        {
            auto actual = files->ReadText(InstallReceiptFileName, std::size_t{16U} * 1024U * 1024U);
            if (!actual || actual.Value() != receiptDocument.Value())
            {
                return HubResult<InstallReceipt>::Failure(
                    actual ? MigrationError(HubErrorCode::UnsafeInstallRoot,
                                            "The partial legacy migration receipt is not exact.",
                                            request.DestinationRoot / InstallReceiptFileName)
                           : actual.Error());
            }
        }
        if (const auto verified = VerifyInventory(*files, expected.Value(), false); !verified)
            return HubResult<InstallReceipt>::Failure(verified.Error());

        auto registration = request.Registration.Read(request.Product);
        if (!registration)
            return HubResult<InstallReceipt>::Failure(registration.Error());
        const InstallRegistration expectedRegistration{.ProductId = expected.Value().ProductId,
                                                       .InstallationId = expected.Value().InstallationId,
                                                       .Root = request.DestinationRoot,
                                                       .Version = expected.Value().Version,
                                                       .ManifestFingerprint = expected.Value().ManifestFingerprint,
                                                       .ReceiptSha256 = expected.Value().DocumentSha256};
        if (registration.Value() && !Matches(*registration.Value(), expected.Value(), request.DestinationRoot))
        {
            return HubResult<InstallReceipt>::Failure(MigrationError(
                HubErrorCode::UnsafeInstallRoot, "A legacy migration found a conflicting modern registration.",
                request.DestinationRoot));
        }
        if (!registration.Value())
        {
            if (const auto written = request.Registration.Write(expectedRegistration); !written)
            {
                if (wroteReceipt)
                {
                    if (auto owned = files->Describe(InstallReceiptFileName); owned)
                        (void)files->RemoveVerified(owned.Value());
                }
                if (wroteMarker)
                {
                    if (auto owned = files->Describe(InstallMarkerFileName); owned)
                        (void)files->RemoveVerified(owned.Value());
                }
                return HubResult<InstallReceipt>::Failure(written.Error());
            }
        }
        return expected;
    }
} // namespace KeireHub::Detail
