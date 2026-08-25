#include "KeireHub/HubUpdatePlatform.h"

#include "KeireInternal/Process.h"

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <Softpub.h>
#include <wintrust.h>
#else
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
#if defined(__linux__)
        [[nodiscard]] std::string_view FirstLinuxExecutable(const std::span<const std::string_view> candidates) noexcept
        {
            for (const auto candidate : candidates)
            {
                if (access(candidate.data(), X_OK) == 0)
                    return candidate;
            }
            return {};
        }

        [[nodiscard]] std::string_view LinuxPrivilegeTool() noexcept
        {
            constexpr std::array candidates{std::string_view{"/usr/bin/pkexec"}, std::string_view{"/usr/sbin/pkexec"}};
            return FirstLinuxExecutable(candidates);
        }

        [[nodiscard]] std::string_view LinuxPackageManager(const std::string_view format) noexcept
        {
            if (format == "deb")
            {
                constexpr std::array candidates{std::string_view{"/usr/bin/dpkg"}, std::string_view{"/usr/sbin/dpkg"}};
                return FirstLinuxExecutable(candidates);
            }
            if (format == "rpm")
            {
                constexpr std::array candidates{std::string_view{"/usr/bin/dnf"}, std::string_view{"/usr/sbin/dnf"},
                                                std::string_view{"/usr/bin/zypper"},
                                                std::string_view{"/usr/sbin/zypper"}};
                return FirstLinuxExecutable(candidates);
            }
            return {};
        }
#endif

        [[nodiscard]] HubStatus LaunchFailure(const std::filesystem::path& installer, std::string details)
        {
            return HubStatus::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                       .Message = "The verified native Hub installer could not be opened.",
                                       .Retryable = true,
                                       .AffectedItem = installer.filename().string(),
                                       .TechnicalDetails = std::move(details),
                                       .LogReference = {}});
        }

#if defined(_WIN32)
        [[nodiscard]] std::optional<std::string_view> ArgumentValue(const HubUpdateLaunch& launch,
                                                                    const std::string_view option) noexcept
        {
            for (std::size_t index = 0; index + 1 < launch.Arguments.size(); ++index)
            {
                if (launch.Arguments[index] == option)
                    return launch.Arguments[index + 1];
            }
            return std::nullopt;
        }

        [[nodiscard]] HubResult<std::vector<std::string>> WindowsInstallerArguments(const HubUpdateLaunch& launch)
        {
            const auto installRoot = ArgumentValue(launch, "--install-root");
            const auto resumeToken = ArgumentValue(launch, "--resume-token");
            const auto waitProcess = ArgumentValue(launch, "--wait-process");
            const auto fromVersion = ArgumentValue(launch, "--from-version");
            const auto toVersion = ArgumentValue(launch, "--to-version");
            if (launch.Arguments.empty() || launch.Arguments.front() != "--keire-hub-update" || !installRoot ||
                !resumeToken || !waitProcess || !fromVersion || !toVersion)
            {
                return HubResult<std::vector<std::string>>::Failure(
                    {.Code = HubErrorCode::InvalidArgument,
                     .Message = "The native Hub installer handoff is incomplete.",
                     .AffectedItem = launch.Executable.filename().string(),
                     .TechnicalDetails = {},
                     .LogReference = {}});
            }
            return HubResult<std::vector<std::string>>::Success(
                {"/KEIRE_HUB_UPDATE=1", "/INSTALL_ROOT=" + std::string(*installRoot),
                 "/RESUME_TOKEN=" + std::string(*resumeToken), "/WAIT_PROCESS=" + std::string(*waitProcess),
                 "/FROM_VERSION=" + std::string(*fromVersion), "/TO_VERSION=" + std::string(*toVersion)});
        }
#endif
    } // namespace

    bool NativeHubUpdateHandoffAvailable() noexcept
    {
#if defined(_WIN32)
        return true;
#elif defined(__APPLE__)
        return false;
#elif defined(__linux__)
        const auto format = HubUpdateManager::HostPackageFormatIdentity();
        return !LinuxPrivilegeTool().empty() && !LinuxPackageManager(format).empty();
#else
        return false;
#endif
    }

    HubUpdatePlatformSignaturePolicy NativeHubUpdatePlatformSignaturePolicy() noexcept
    {
#if defined(_WIN32)
        return HubUpdatePlatformSignaturePolicy::ValidateIfPresent;
#elif defined(__APPLE__)
        return HubUpdatePlatformSignaturePolicy::Required;
#else
        return HubUpdatePlatformSignaturePolicy::NotRequired;
#endif
    }

    std::string NativeHubUpdateHandoffUnavailableMessage()
    {
#if defined(__linux__)
        const auto format = HubUpdateManager::HostPackageFormatIdentity();
        if (format == "rpm")
        {
            return "The verified RPM package is ready, but pkexec or a supported RPM package manager is unavailable. "
                   "Reveal it and use your system package manager.";
        }
        return "The verified Debian package is ready, but pkexec or dpkg is unavailable. Reveal it and use your "
               "system package manager.";
#elif defined(__APPLE__)
        return "The verified disk image is ready. Reveal it and complete the drag-to-Applications install manually; "
               "automatic macOS replacement is not yet available.";
#elif defined(_WIN32)
        return {};
#else
        return "Automatic Hub installer handoff is not supported on this platform.";
#endif
    }

    std::uint64_t HubCurrentProcessId() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        return static_cast<std::uint64_t>(getpid());
#endif
    }

    HubResult<HubUpdatePlatformSignatureState> VerifyNativeHubInstallerSignature(const std::filesystem::path& installer)
    {
#if defined(_WIN32)
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = installer.c_str();
        WINTRUST_DATA trust{};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &file;
        trust.dwStateAction = WTD_STATEACTION_VERIFY;
        trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
        GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        SetLastError(ERROR_SUCCESS);
        const auto verification = WinVerifyTrust(nullptr, &policy, &trust);
        const auto verificationDetails = GetLastError();
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        (void)WinVerifyTrust(nullptr, &policy, &trust);
        if (verification == ERROR_SUCCESS)
            return HubResult<HubUpdatePlatformSignatureState>::Success(HubUpdatePlatformSignatureState::Valid);
        if (verification == TRUST_E_NOSIGNATURE &&
            (verificationDetails == static_cast<DWORD>(TRUST_E_NOSIGNATURE) ||
             verificationDetails == static_cast<DWORD>(TRUST_E_SUBJECT_FORM_UNKNOWN) ||
             verificationDetails == static_cast<DWORD>(TRUST_E_PROVIDER_UNKNOWN)))
        {
            return HubResult<HubUpdatePlatformSignatureState>::Success(HubUpdatePlatformSignatureState::NotPresent);
        }
        return HubResult<HubUpdatePlatformSignatureState>::Failure(
            {.Code = HubErrorCode::CatalogSignatureInvalid,
             .Message = "Windows found a Hub installer publisher signature that is invalid or untrusted.",
             .AffectedItem = installer.filename().string(),
             .TechnicalDetails = std::to_string(verification) + ":" + std::to_string(verificationDetails),
             .LogReference = {}});
#elif defined(__APPLE__)
        const std::vector<std::string> arguments{
            "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=2", installer.string()};
        const auto result =
            Keire::Detail::RunProcess("/usr/sbin/spctl", arguments, installer.parent_path(), std::chrono::seconds(30));
        if (!result.TimedOut && result.ExitCode == 0)
            return HubResult<HubUpdatePlatformSignatureState>::Success(HubUpdatePlatformSignatureState::Valid);
        return HubResult<HubUpdatePlatformSignatureState>::Failure(
            {.Code = HubErrorCode::CatalogSignatureInvalid,
             .Message = "macOS could not verify the Hub installer signature.",
             .AffectedItem = installer.filename().string(),
             .TechnicalDetails = result.TimedOut ? "spctl timed out" : result.Output,
             .LogReference = {}});
#else
        (void)installer;
        return HubResult<HubUpdatePlatformSignatureState>::Success(HubUpdatePlatformSignatureState::NotPresent);
#endif
    }

    HubStatus LaunchNativeHubInstaller(const HubUpdateLaunch& launch)
    {
        if (!NativeHubUpdateHandoffAvailable())
            return LaunchFailure(launch.Executable, NativeHubUpdateHandoffUnavailableMessage());
#if defined(_WIN32)
        std::string diagnostic;
        auto arguments = WindowsInstallerArguments(launch);
        if (!arguments)
            return HubStatus::Failure(arguments.Error());
        if (!Keire::Detail::LaunchDetachedProcess(launch.Executable, arguments.Value(), launch.Executable.parent_path(),
                                                  diagnostic))
        {
            return LaunchFailure(launch.Executable, std::move(diagnostic));
        }
#elif defined(__APPLE__)
        return LaunchFailure(launch.Executable, NativeHubUpdateHandoffUnavailableMessage());
#elif defined(__linux__)
        std::string diagnostic;
        const auto format = HubUpdateManager::HostPackageFormatIdentity();
        const auto packageManager = LinuxPackageManager(format);
        const auto privilegeTool = LinuxPrivilegeTool();
        const auto isZypper = packageManager.ends_with("/zypper");
        std::vector<std::string> arguments;
        if (format == "deb")
        {
            arguments = {std::string(packageManager), "--install", launch.Executable.string()};
        }
        else if (isZypper)
        {
            arguments = {std::string(packageManager), "--non-interactive", "install", "--allow-unsigned-rpm",
                         launch.Executable.string()};
        }
        else
        {
            arguments = {std::string(packageManager), "install", "--assumeyes", launch.Executable.string()};
        }
        if (privilegeTool.empty() || packageManager.empty() ||
            !Keire::Detail::LaunchDetachedProcess(std::filesystem::path(privilegeTool), arguments,
                                                  launch.Executable.parent_path(), diagnostic))
        {
            return LaunchFailure(launch.Executable, std::move(diagnostic));
        }
#else
        return LaunchFailure(launch.Executable, "Unsupported platform.");
#endif
        return HubStatus::Success();
    }
} // namespace KeireHub
