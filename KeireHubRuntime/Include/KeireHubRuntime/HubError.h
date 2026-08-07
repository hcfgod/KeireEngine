#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace KeireHub
{
    enum class HubErrorCode
    {
        InvalidArgument,
        InvalidData,
        UnsupportedSchema,
        IoRead,
        IoWrite,
        MigrationFailed,
        DuplicateIdentifier,
        NotFound,
        InvalidTransition,
        UnsafeInstallRoot,
        EditorManifestInvalid,
        EditorInventoryInvalid,
        EditorRunning,
        InstallationBusy,
        PackageManifestInvalid,
        PackageMissingDependency,
        PackageVersionUnsatisfied,
        PackageConflict,
        PackageDependencyCycle,
        PackageHostIncompatible,
        InsufficientDiskSpace,
        DownloadUnavailable,
        DownloadProtocolInvalid,
        DownloadChecksumMismatch,
        DownloadSizeMismatch,
        WorkerProtocolInvalid,
        WorkerInterrupted,
        TemplateNotFound,
        TemplateIncompatible,
        TemplatePayloadInvalid,
        DestinationConflict,
        ProjectValidationFailed,
        ProcessLaunchFailed,
        AccountConfigurationInvalid,
        AccountTransportFailed,
        AccountAuthenticationFailed,
        AccountSessionInvalid,
        AccountProfileInvalid,
        DistributionConfigurationInvalid,
        CatalogTransportFailed,
        CatalogSignatureInvalid,
        CatalogUntrustedKey,
        CatalogReplay,
        CatalogExpired,
        CatalogIdentityMismatch,
        CatalogCacheInvalid
    };

    [[nodiscard]] std::string_view ToString(HubErrorCode code) noexcept;
    [[nodiscard]] std::optional<HubErrorCode> ParseHubErrorCode(std::string_view code) noexcept;

    struct HubError final
    {
        HubErrorCode Code = HubErrorCode::InvalidData;
        std::string Message;
        bool Retryable = false;
        std::string AffectedItem;
        std::string TechnicalDetails;
        std::string LogReference;
    };

    class HubStatus final
    {
      public:
        [[nodiscard]] static HubStatus Success() noexcept;
        [[nodiscard]] static HubStatus Failure(HubError error);

        [[nodiscard]] bool HasValue() const noexcept;
        explicit operator bool() const noexcept;
        [[nodiscard]] const HubError& Error() const;

      private:
        explicit HubStatus(std::optional<HubError> error) noexcept;
        std::optional<HubError> m_Error;
    };

    template <typename T> class HubResult final
    {
      public:
        [[nodiscard]] static HubResult Success(T value) { return HubResult(std::move(value)); }

        [[nodiscard]] static HubResult Failure(HubError error) { return HubResult(std::move(error)); }

        [[nodiscard]] bool HasValue() const noexcept { return std::holds_alternative<T>(m_Value); }

        explicit operator bool() const noexcept { return HasValue(); }

        [[nodiscard]] T& Value() & { return std::get<T>(m_Value); }
        [[nodiscard]] const T& Value() const& { return std::get<T>(m_Value); }
        [[nodiscard]] T&& Value() && { return std::get<T>(std::move(m_Value)); }
        [[nodiscard]] const HubError& Error() const { return std::get<HubError>(m_Value); }

      private:
        explicit HubResult(T value) : m_Value(std::move(value)) {}
        explicit HubResult(HubError error) : m_Value(std::move(error)) {}

        std::variant<T, HubError> m_Value;
    };
} // namespace KeireHub
