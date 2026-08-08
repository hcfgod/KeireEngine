#include "KeireHubRuntime/ProjectStatusProbe.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <optional>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError ProbeError(const std::filesystem::path& root, std::string details)
        {
            return {.Code = HubErrorCode::IoRead,
                    .Message = "The project state could not be checked.",
                    .Retryable = true,
                    .AffectedItem = Detail::PathToUtf8(root.filename()),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubResult<std::optional<std::filesystem::path>>
        ConfinedRegularFile(const std::filesystem::path& root, const std::filesystem::path& relative)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(root / relative, error);
            if ((error && error == std::errc::no_such_file_or_directory) ||
                (!error && !std::filesystem::exists(status)))
                return HubResult<std::optional<std::filesystem::path>>::Success(std::nullopt);
            if (error)
                return HubResult<std::optional<std::filesystem::path>>::Failure(ProbeError(root, error.message()));
            if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
            {
                return HubResult<std::optional<std::filesystem::path>>::Failure(
                    ProbeError(root, "The project state marker is not a regular file."));
            }
            auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error)
                return HubResult<std::optional<std::filesystem::path>>::Failure(ProbeError(root, error.message()));
            auto canonical = std::filesystem::weakly_canonical(root / relative, error);
            if (error)
                return HubResult<std::optional<std::filesystem::path>>::Failure(ProbeError(root, error.message()));
            const auto confined = canonical.lexically_relative(canonicalRoot);
            if (!Detail::IsSafeRelativePath(confined))
            {
                return HubResult<std::optional<std::filesystem::path>>::Failure(
                    ProbeError(root, "The project state marker escapes the project root."));
            }
            return HubResult<std::optional<std::filesystem::path>>::Success(std::move(canonical));
        }
    } // namespace

    HubResult<bool> ProbeProjectLock(const std::filesystem::path& root)
    {
        auto lockPath = ConfinedRegularFile(root, std::filesystem::path("Library") / "Editor.lock");
        if (!lockPath)
            return HubResult<bool>::Failure(lockPath.Error());
        if (!lockPath.Value())
            return HubResult<bool>::Success(false);
#if defined(_WIN32)
        const auto handle = CreateFileW(lockPath.Value()->c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
            return HubResult<bool>::Success(false);
        }
        const auto nativeError = GetLastError();
        if (nativeError == ERROR_SHARING_VIOLATION || nativeError == ERROR_LOCK_VIOLATION)
            return HubResult<bool>::Success(true);
        return HubResult<bool>::Failure(
            ProbeError(root, std::error_code(static_cast<int>(nativeError), std::system_category()).message()));
#else
        int flags = O_RDWR;
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
        const auto handle = open(lockPath.Value()->c_str(), flags);
        if (handle < 0)
            return HubResult<bool>::Failure(
                ProbeError(root, std::error_code(errno, std::generic_category()).message()));
#if !defined(O_CLOEXEC)
        (void)fcntl(handle, F_SETFD, FD_CLOEXEC);
#endif
        const bool locked = flock(handle, LOCK_EX | LOCK_NB) != 0;
        if (!locked)
            (void)flock(handle, LOCK_UN);
        close(handle);
        return HubResult<bool>::Success(locked);
#endif
    }

    HubResult<bool> ProbeProjectRecovery(const std::filesystem::path& root)
    {
        auto journal =
            ConfinedRegularFile(root, std::filesystem::path("Library") / "ProjectUpgrades" / "Active" / "journal.json");
        if (!journal)
            return HubResult<bool>::Failure(journal.Error());
        return HubResult<bool>::Success(journal.Value().has_value());
    }
} // namespace KeireHub
