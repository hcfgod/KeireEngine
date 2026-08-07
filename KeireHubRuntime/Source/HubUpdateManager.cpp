#include "KeireHubRuntime/HubUpdateManager.h"

#include "KeireHubRuntime/PackageResolver.h"

#include "DistributionEncoding.h"
#include "Persistence.h"
#include "Sha256.h"

#include <stdexcept>
#include <system_error>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::uint64_t MaximumInstallerBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaximumResumeTokenBytes = 64 * 1024;

        [[nodiscard]] bool IsUpgrade(const std::string_view current, const std::string_view target) noexcept
        {
            const auto currentVersion = SemanticVersion::Parse(current);
            const auto targetVersion = SemanticVersion::Parse(target);
            return currentVersion && targetVersion && targetVersion.Value() > currentVersion.Value();
        }

        [[nodiscard]] bool IsDescendant(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            std::error_code error;
            const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
            if (error)
                return false;
            const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error || canonicalPath == canonicalRoot)
                return false;
            auto pathIterator = canonicalPath.begin();
            for (auto rootIterator = canonicalRoot.begin(); rootIterator != canonicalRoot.end(); ++rootIterator)
            {
                if (pathIterator == canonicalPath.end() || *pathIterator != *rootIterator)
                    return false;
                ++pathIterator;
            }
            return true;
        }

        [[nodiscard]] HubStatus ValidateRequest(const HubUpdateRequest& request)
        {
            if (!request.InstallerPath.is_absolute() || !request.VerifiedCacheRoot.is_absolute() ||
                !request.HubInstallRoot.is_absolute() || !Detail::IsBoundedIdentifier(request.PackageId) ||
                !Detail::IsSha256(request.Sha256) || !IsUpgrade(request.CurrentVersion, request.TargetVersion) ||
                request.Platform != HubUpdateManager::HostPlatformIdentity() ||
                request.Architecture != HubUpdateManager::HostArchitectureIdentity() ||
                !Detail::IsDistributionKeyId(request.SignatureKeyId) || request.CatalogSequence == 0 ||
                request.CurrentProcessId == 0 || request.StartedUnixSeconds == 0)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidArgument,
                     .Message = "The Hub update request is incomplete or incompatible with this computer.",
                     .AffectedItem = request.PackageId});
            }

            std::error_code error;
            const auto installerStatus = std::filesystem::symlink_status(request.InstallerPath, error);
            if (error || !std::filesystem::is_regular_file(installerStatus) ||
                std::filesystem::is_symlink(installerStatus) ||
                !IsDescendant(request.InstallerPath, request.VerifiedCacheRoot))
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::UnsafeInstallRoot,
                     .Message = "The verified Hub installer is not in the protected package cache.",
                     .AffectedItem = Detail::PathToUtf8(request.InstallerPath)});
            }

            const auto installRootStatus = std::filesystem::symlink_status(request.HubInstallRoot, error);
            if (error || !std::filesystem::is_directory(installRootStatus) ||
                std::filesystem::is_symlink(installRootStatus))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The installed Hub root could not be verified.",
                                           .AffectedItem = Detail::PathToUtf8(request.HubInstallRoot)});
            }

            const auto packageManifest = request.HubInstallRoot / "hub-package.json";
            const auto manifestStatus = std::filesystem::symlink_status(packageManifest, error);
            if (error || !std::filesystem::is_regular_file(manifestStatus) ||
                std::filesystem::is_symlink(manifestStatus))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The installed Hub package marker is missing.",
                                           .AffectedItem = Detail::PathToUtf8(request.HubInstallRoot)});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveToken(const std::filesystem::path& path)
        {
            std::error_code error;
            const bool removed = std::filesystem::remove(path, error);
            if (error)
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                           .Message = "The Hub update recovery record could not be removed.",
                                           .AffectedItem = Detail::PathToUtf8(path),
                                           .TechnicalDetails = error.message()});
            }
            (void)removed;
            return HubStatus::Success();
        }
    } // namespace

    HubUpdateManager::HubUpdateManager(std::filesystem::path resumeTokenPath)
        : m_ResumeTokenPath(std::move(resumeTokenPath))
    {
    }

    HubStatus HubUpdateManager::BeginInstallerHandoff(const HubUpdateRequest& request,
                                                      const PlatformSignatureVerifier& signatureVerifier,
                                                      const InstallerLauncher& launcher)
    {
        std::error_code tokenError;
        const auto tokenStatus = std::filesystem::symlink_status(m_ResumeTokenPath, tokenError);
        if (!tokenError && tokenStatus.type() != std::filesystem::file_type::not_found)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Resolve the previous Hub update before starting another one.",
                                       .AffectedItem = Detail::PathToUtf8(m_ResumeTokenPath)});
        }
        if (const auto validation = ValidateRequest(request); !validation)
            return validation;
        if (!launcher || (request.RequirePlatformSignature && !signatureVerifier))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The Hub update cannot be handed to a trusted native installer.",
                                       .AffectedItem = request.PackageId});
        }

        auto digest = Detail::Sha256File(request.InstallerPath, MaximumInstallerBytes);
        if (!digest)
            return HubStatus::Failure(digest.Error());
        if (digest.Value() != request.Sha256)
        {
            return HubStatus::Failure({.Code = HubErrorCode::DownloadChecksumMismatch,
                                       .Message = "The downloaded Hub installer failed integrity verification.",
                                       .Retryable = true,
                                       .AffectedItem = request.PackageId});
        }
        if (request.RequirePlatformSignature)
        {
            if (const auto status = signatureVerifier(request.InstallerPath); !status)
                return status;
        }

        const Detail::Json resumeToken{{"schemaVersion", CurrentResumeSchemaVersion},
                                       {"packageId", request.PackageId},
                                       {"sha256", request.Sha256},
                                       {"previousVersion", request.CurrentVersion},
                                       {"targetVersion", request.TargetVersion},
                                       {"platform", request.Platform},
                                       {"architecture", request.Architecture},
                                       {"signatureKeyId", request.SignatureKeyId},
                                       {"catalogSequence", request.CatalogSequence},
                                       {"installerPath", Detail::PathToUtf8(request.InstallerPath)},
                                       {"installRoot", Detail::PathToUtf8(request.HubInstallRoot)},
                                       {"startedAt", request.StartedUnixSeconds}};
        if (const auto status = Detail::WriteJsonFileAtomically(m_ResumeTokenPath, resumeToken); !status)
            return status;

        HubUpdateLaunch launch{.Executable = request.InstallerPath,
                               .Arguments = {"--keire-hub-update", "--install-root",
                                             Detail::PathToUtf8(request.HubInstallRoot), "--resume-token",
                                             Detail::PathToUtf8(m_ResumeTokenPath), "--wait-process",
                                             std::to_string(request.CurrentProcessId), "--from-version",
                                             request.CurrentVersion, "--to-version", request.TargetVersion}};
        if (const auto status = launcher(launch); !status)
        {
            (void)RemoveToken(m_ResumeTokenPath);
            return status;
        }
        return HubStatus::Success();
    }

    HubResult<HubUpdateResumeResult> HubUpdateManager::Reconcile(const std::string_view installedVersion)
    {
        std::error_code error;
        const auto tokenStatus = std::filesystem::symlink_status(m_ResumeTokenPath, error);
        if (tokenStatus.type() == std::filesystem::file_type::not_found ||
            error == std::make_error_code(std::errc::no_such_file_or_directory))
            return HubResult<HubUpdateResumeResult>::Success({});
        if (error || !std::filesystem::is_regular_file(tokenStatus) || std::filesystem::is_symlink(tokenStatus))
        {
            return HubResult<HubUpdateResumeResult>::Failure({.Code = HubErrorCode::InvalidData,
                                                              .Message = "The Hub update recovery record is unsafe.",
                                                              .AffectedItem = Detail::PathToUtf8(m_ResumeTokenPath),
                                                              .TechnicalDetails = error.message()});
        }
        const auto installedSemanticVersion = SemanticVersion::Parse(installedVersion);
        if (!installedSemanticVersion)
        {
            return HubResult<HubUpdateResumeResult>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The installed Hub version is invalid."});
        }

        auto document = Detail::ReadJsonFile(m_ResumeTokenPath, MaximumResumeTokenBytes);
        if (!document)
            return HubResult<HubUpdateResumeResult>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            if (!value.is_object() || !value.contains("schemaVersion") ||
                !value.at("schemaVersion").is_number_unsigned())
            {
                throw std::runtime_error("invalid resume record header");
            }
            if (value.at("schemaVersion").get<std::uint32_t>() != CurrentResumeSchemaVersion)
            {
                return HubResult<HubUpdateResumeResult>::Failure(
                    {.Code = HubErrorCode::UnsupportedSchema,
                     .Message = "The Hub update recovery record uses an unsupported schema.",
                     .AffectedItem = Detail::PathToUtf8(m_ResumeTokenPath)});
            }
            if (value.size() != 12U)
                throw std::runtime_error("invalid resume record fields");
            const auto packageId = value.at("packageId").get<std::string>();
            const auto sha256 = value.at("sha256").get<std::string>();
            const auto previousVersion = value.at("previousVersion").get<std::string>();
            const auto targetVersion = value.at("targetVersion").get<std::string>();
            const auto platform = value.at("platform").get<std::string>();
            const auto architecture = value.at("architecture").get<std::string>();
            const auto keyId = value.at("signatureKeyId").get<std::string>();
            const auto sequence = value.at("catalogSequence").get<std::uint64_t>();
            const auto installerPath = Detail::PathFromUtf8(value.at("installerPath").get<std::string>());
            const auto installRoot = Detail::PathFromUtf8(value.at("installRoot").get<std::string>());
            const auto startedAt = value.at("startedAt").get<std::uint64_t>();
            if (!Detail::IsBoundedIdentifier(packageId) || !Detail::IsSha256(sha256) ||
                !IsUpgrade(previousVersion, targetVersion) || platform != HostPlatformIdentity() ||
                architecture != HostArchitectureIdentity() || !Detail::IsDistributionKeyId(keyId) || sequence == 0 ||
                !installerPath.is_absolute() || !installRoot.is_absolute() || startedAt == 0)
            {
                throw std::runtime_error("invalid resume record values");
            }

            const auto targetSemanticVersion = SemanticVersion::Parse(targetVersion);
            if (targetSemanticVersion && installedSemanticVersion.Value() >= targetSemanticVersion.Value())
            {
                if (const auto status = RemoveToken(m_ResumeTokenPath); !status)
                    return HubResult<HubUpdateResumeResult>::Failure(status.Error());
                return HubResult<HubUpdateResumeResult>::Success({.State = HubUpdateResumeState::Updated,
                                                                  .PreviousVersion = previousVersion,
                                                                  .TargetVersion = targetVersion,
                                                                  .Message = "Kéire Hub was updated successfully."});
            }
            return HubResult<HubUpdateResumeResult>::Success(
                {.State = HubUpdateResumeState::RecoveryRequired,
                 .PreviousVersion = previousVersion,
                 .TargetVersion = targetVersion,
                 .Message = "The Hub update did not complete. Run the verified installer again or keep this version."});
        }
        catch (const std::exception& exception)
        {
            return HubResult<HubUpdateResumeResult>::Failure({.Code = HubErrorCode::InvalidData,
                                                              .Message = "The Hub update recovery record is invalid.",
                                                              .AffectedItem = Detail::PathToUtf8(m_ResumeTokenPath),
                                                              .TechnicalDetails = exception.what()});
        }
    }

    HubStatus HubUpdateManager::DiscardRecovery() { return RemoveToken(m_ResumeTokenPath); }

    const std::filesystem::path& HubUpdateManager::ResumeTokenPath() const noexcept { return m_ResumeTokenPath; }

    std::string_view HubUpdateManager::HostPlatformIdentity() noexcept
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown";
#endif
    }

    std::string_view HubUpdateManager::HostArchitectureIdentity() noexcept
    {
#if defined(_M_X64) || defined(__x86_64__)
        return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
        return "arm64";
#else
        return "unknown";
#endif
    }
} // namespace KeireHub
