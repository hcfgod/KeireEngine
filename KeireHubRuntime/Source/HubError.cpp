#include "KeireHubRuntime/HubError.h"

namespace KeireHub
{
    std::string_view ToString(const HubErrorCode code) noexcept
    {
        switch (code)
        {
        case HubErrorCode::InvalidArgument:
            return "hub.invalid_argument";
        case HubErrorCode::InvalidData:
            return "hub.invalid_data";
        case HubErrorCode::UnsupportedSchema:
            return "hub.unsupported_schema";
        case HubErrorCode::IoRead:
            return "hub.io_read";
        case HubErrorCode::IoWrite:
            return "hub.io_write";
        case HubErrorCode::MigrationFailed:
            return "hub.migration_failed";
        case HubErrorCode::DuplicateIdentifier:
            return "hub.duplicate_identifier";
        case HubErrorCode::NotFound:
            return "hub.not_found";
        case HubErrorCode::InvalidTransition:
            return "hub.invalid_transition";
        case HubErrorCode::UnsafeInstallRoot:
            return "hub.unsafe_install_root";
        case HubErrorCode::EditorManifestInvalid:
            return "hub.editor_manifest_invalid";
        case HubErrorCode::EditorInventoryInvalid:
            return "hub.editor_inventory_invalid";
        case HubErrorCode::EditorRunning:
            return "hub.editor_running";
        case HubErrorCode::InstallationBusy:
            return "hub.installation_busy";
        case HubErrorCode::PackageManifestInvalid:
            return "hub.package_manifest_invalid";
        case HubErrorCode::PackageMissingDependency:
            return "hub.package_missing_dependency";
        case HubErrorCode::PackageVersionUnsatisfied:
            return "hub.package_version_unsatisfied";
        case HubErrorCode::PackageConflict:
            return "hub.package_conflict";
        case HubErrorCode::PackageDependencyCycle:
            return "hub.package_dependency_cycle";
        case HubErrorCode::PackageHostIncompatible:
            return "hub.package_host_incompatible";
        case HubErrorCode::InsufficientDiskSpace:
            return "hub.insufficient_disk_space";
        case HubErrorCode::DownloadUnavailable:
            return "hub.download_unavailable";
        case HubErrorCode::DownloadProtocolInvalid:
            return "hub.download_protocol_invalid";
        case HubErrorCode::DownloadChecksumMismatch:
            return "hub.download_checksum_mismatch";
        case HubErrorCode::DownloadSizeMismatch:
            return "hub.download_size_mismatch";
        case HubErrorCode::WorkerProtocolInvalid:
            return "hub.worker_protocol_invalid";
        case HubErrorCode::WorkerInterrupted:
            return "hub.worker_interrupted";
        case HubErrorCode::TemplateNotFound:
            return "hub.template_not_found";
        case HubErrorCode::TemplateIncompatible:
            return "hub.template_incompatible";
        case HubErrorCode::TemplatePayloadInvalid:
            return "hub.template_payload_invalid";
        case HubErrorCode::DestinationConflict:
            return "hub.destination_conflict";
        case HubErrorCode::ProjectValidationFailed:
            return "hub.project_validation_failed";
        case HubErrorCode::ProcessLaunchFailed:
            return "hub.process_launch_failed";
        case HubErrorCode::DistributionConfigurationInvalid:
            return "hub.distribution_configuration_invalid";
        case HubErrorCode::CatalogTransportFailed:
            return "hub.catalog_transport_failed";
        case HubErrorCode::CatalogSignatureInvalid:
            return "hub.catalog_signature_invalid";
        case HubErrorCode::CatalogUntrustedKey:
            return "hub.catalog_untrusted_key";
        case HubErrorCode::CatalogReplay:
            return "hub.catalog_replay";
        case HubErrorCode::CatalogExpired:
            return "hub.catalog_expired";
        case HubErrorCode::CatalogIdentityMismatch:
            return "hub.catalog_identity_mismatch";
        case HubErrorCode::CatalogCacheInvalid:
            return "hub.catalog_cache_invalid";
        }
        return "hub.unknown";
    }

    std::optional<HubErrorCode> ParseHubErrorCode(const std::string_view code) noexcept
    {
        constexpr HubErrorCode values[]{HubErrorCode::InvalidArgument,
                                        HubErrorCode::InvalidData,
                                        HubErrorCode::UnsupportedSchema,
                                        HubErrorCode::IoRead,
                                        HubErrorCode::IoWrite,
                                        HubErrorCode::MigrationFailed,
                                        HubErrorCode::DuplicateIdentifier,
                                        HubErrorCode::NotFound,
                                        HubErrorCode::InvalidTransition,
                                        HubErrorCode::UnsafeInstallRoot,
                                        HubErrorCode::EditorManifestInvalid,
                                        HubErrorCode::EditorInventoryInvalid,
                                        HubErrorCode::EditorRunning,
                                        HubErrorCode::InstallationBusy,
                                        HubErrorCode::PackageManifestInvalid,
                                        HubErrorCode::PackageMissingDependency,
                                        HubErrorCode::PackageVersionUnsatisfied,
                                        HubErrorCode::PackageConflict,
                                        HubErrorCode::PackageDependencyCycle,
                                        HubErrorCode::PackageHostIncompatible,
                                        HubErrorCode::InsufficientDiskSpace,
                                        HubErrorCode::DownloadUnavailable,
                                        HubErrorCode::DownloadProtocolInvalid,
                                        HubErrorCode::DownloadChecksumMismatch,
                                        HubErrorCode::DownloadSizeMismatch,
                                        HubErrorCode::WorkerProtocolInvalid,
                                        HubErrorCode::WorkerInterrupted,
                                        HubErrorCode::TemplateNotFound,
                                        HubErrorCode::TemplateIncompatible,
                                        HubErrorCode::TemplatePayloadInvalid,
                                        HubErrorCode::DestinationConflict,
                                        HubErrorCode::ProjectValidationFailed,
                                        HubErrorCode::ProcessLaunchFailed,
                                        HubErrorCode::DistributionConfigurationInvalid,
                                        HubErrorCode::CatalogTransportFailed,
                                        HubErrorCode::CatalogSignatureInvalid,
                                        HubErrorCode::CatalogUntrustedKey,
                                        HubErrorCode::CatalogReplay,
                                        HubErrorCode::CatalogExpired,
                                        HubErrorCode::CatalogIdentityMismatch,
                                        HubErrorCode::CatalogCacheInvalid};
        for (const auto value : values)
        {
            if (ToString(value) == code)
                return value;
        }
        return std::nullopt;
    }

    HubStatus HubStatus::Success() noexcept { return HubStatus(std::nullopt); }

    HubStatus HubStatus::Failure(HubError error) { return HubStatus(std::move(error)); }

    bool HubStatus::HasValue() const noexcept { return !m_Error.has_value(); }

    HubStatus::operator bool() const noexcept { return HasValue(); }

    const HubError& HubStatus::Error() const
    {
        if (!m_Error)
            throw std::logic_error("A successful Hub status has no error.");
        return *m_Error;
    }

    HubStatus::HubStatus(std::optional<HubError> error) noexcept : m_Error(std::move(error)) {}
} // namespace KeireHub
