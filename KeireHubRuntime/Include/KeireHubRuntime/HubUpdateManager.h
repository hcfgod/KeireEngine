#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct HubUpdateRequest final
    {
        std::filesystem::path InstallerPath;
        std::filesystem::path VerifiedCacheRoot;
        std::filesystem::path HubInstallRoot;
        std::string PackageId;
        std::string Sha256;
        std::string CurrentVersion;
        std::string TargetVersion;
        std::string Platform;
        std::string Architecture;
        std::string SignatureKeyId;
        std::uint64_t CatalogSequence = 0;
        std::uint64_t CurrentProcessId = 0;
        std::uint64_t StartedUnixSeconds = 0;
        bool RequirePlatformSignature = false;
    };

    struct HubUpdateLaunch final
    {
        std::filesystem::path Executable;
        std::vector<std::string> Arguments;
    };

    enum class HubUpdateResumeState
    {
        None,
        Updated,
        RecoveryRequired
    };

    struct HubUpdateResumeResult final
    {
        HubUpdateResumeState State = HubUpdateResumeState::None;
        std::string PreviousVersion;
        std::string TargetVersion;
        std::string Message;
    };

    class HubUpdateManager final
    {
      public:
        static constexpr std::uint32_t CurrentResumeSchemaVersion = 1;

        using PlatformSignatureVerifier = std::function<HubStatus(const std::filesystem::path&)>;
        using InstallerLauncher = std::function<HubStatus(const HubUpdateLaunch&)>;

        explicit HubUpdateManager(std::filesystem::path resumeTokenPath);

        [[nodiscard]] HubStatus BeginInstallerHandoff(const HubUpdateRequest& request,
                                                      const PlatformSignatureVerifier& signatureVerifier,
                                                      const InstallerLauncher& launcher);
        [[nodiscard]] HubResult<HubUpdateResumeResult> Reconcile(std::string_view installedVersion);
        [[nodiscard]] HubStatus DiscardRecovery();

        [[nodiscard]] const std::filesystem::path& ResumeTokenPath() const noexcept;

        [[nodiscard]] static std::string_view HostPlatformIdentity() noexcept;
        [[nodiscard]] static std::string_view HostArchitectureIdentity() noexcept;

      private:
        std::filesystem::path m_ResumeTokenPath;
    };
} // namespace KeireHub
