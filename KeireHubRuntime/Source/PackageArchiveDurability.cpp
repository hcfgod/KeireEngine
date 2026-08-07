#include "KeireHubRuntime/PackageArchive.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::filesystem::path NativeIoPath(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            auto value = std::filesystem::absolute(path).lexically_normal().native();
            if (value.starts_with(LR"(\\?\)"))
                return value;
            if (value.starts_with(LR"(\\)"))
                return LR"(\\?\UNC\)" + value.substr(2);
            return LR"(\\?\)" + value;
#else
            return path;
#endif
        }

        [[nodiscard]] HubStatus DurabilityError(const std::string& packageId, const std::error_code& error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The verified package staging tree could not be synchronized.",
                                       .Retryable = true,
                                       .AffectedItem = packageId,
                                       .TechnicalDetails = error.message()});
        }

        [[nodiscard]] HubStatus SyncFile(const std::filesystem::path& path, const std::string& packageId)
        {
#if defined(_WIN32)
            const HANDLE handle = CreateFileW(NativeIoPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return DurabilityError(packageId,
                                       std::error_code(static_cast<int>(GetLastError()), std::system_category()));
            const bool flushed = FlushFileBuffers(handle) != 0;
            const auto failure = flushed ? ERROR_SUCCESS : GetLastError();
            CloseHandle(handle);
            return flushed
                       ? HubStatus::Success()
                       : DurabilityError(packageId, std::error_code(static_cast<int>(failure), std::system_category()));
#else
            int flags = O_RDONLY;
#if defined(O_CLOEXEC)
            flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
            flags |= O_NOFOLLOW;
#endif
            const int descriptor = ::open(NativeIoPath(path).c_str(), flags);
            if (descriptor < 0)
                return DurabilityError(packageId, std::error_code(errno, std::generic_category()));
            const int result = ::fsync(descriptor);
            const int failure = result == 0 ? 0 : errno;
            ::close(descriptor);
            return failure == 0 ? HubStatus::Success()
                                : DurabilityError(packageId, std::error_code(failure, std::generic_category()));
#endif
        }

        [[nodiscard]] HubStatus SyncDirectory(const std::filesystem::path& path, const std::string& packageId)
        {
#if defined(_WIN32)
            static_cast<void>(path);
            static_cast<void>(packageId);
            return HubStatus::Success();
#else
            int flags = O_RDONLY;
#if defined(O_CLOEXEC)
            flags |= O_CLOEXEC;
#endif
#if defined(O_DIRECTORY)
            flags |= O_DIRECTORY;
#endif
            const int descriptor = ::open(NativeIoPath(path).c_str(), flags);
            if (descriptor < 0)
                return DurabilityError(packageId, std::error_code(errno, std::generic_category()));
            const int result = ::fsync(descriptor);
            const int failure = result == 0 ? 0 : errno;
            ::close(descriptor);
            return failure == 0 ? HubStatus::Success()
                                : DurabilityError(packageId, std::error_code(failure, std::generic_category()));
#endif
        }
    } // namespace

    HubStatus SealPackageTreeForPublish(const std::filesystem::path& root, const PackageManifest& manifest)
    {
        if (const auto status = ValidatePackageTree(root, manifest); !status)
            return status;
        try
        {
            const auto nativeRoot = NativeIoPath(root);
            std::vector<std::filesystem::path> directories{nativeRoot};
            for (std::filesystem::recursive_directory_iterator iterator(nativeRoot), end; iterator != end; ++iterator)
            {
                const auto status = iterator->symlink_status();
                if (std::filesystem::is_symlink(status))
                {
                    return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                               .Message = "The package staging tree changed before publication.",
                                               .AffectedItem = manifest.Id});
                }
                if (std::filesystem::is_directory(status))
                    directories.push_back(iterator->path());
                else if (std::filesystem::is_regular_file(status))
                {
                    if (const auto sync = SyncFile(iterator->path(), manifest.Id); !sync)
                        return sync;
                }
            }
            if (const auto status = ValidatePackageTree(root, manifest); !status)
                return status;
            std::ranges::sort(
                directories, [](const auto& left, const auto& right)
                { return std::distance(left.begin(), left.end()) > std::distance(right.begin(), right.end()); });
            for (const auto& directory : directories)
                if (const auto status = SyncDirectory(directory, manifest.Id); !status)
                    return status;
            return HubStatus::Success();
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            return DurabilityError(manifest.Id, error.code());
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The verified package staging tree could not be synchronized.",
                                       .Retryable = true,
                                       .AffectedItem = manifest.Id,
                                       .TechnicalDetails = error.what()});
        }
    }
} // namespace KeireHub
