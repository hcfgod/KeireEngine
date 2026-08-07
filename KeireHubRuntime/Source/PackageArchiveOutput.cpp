#include "PackageArchiveOutput.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KeireHub::Detail
{
    namespace
    {
#if defined(_WIN32)
        class ScopedWindowsHandle final
        {
          public:
            explicit ScopedWindowsHandle(const HANDLE value) noexcept : m_Value(value) {}
            ~ScopedWindowsHandle()
            {
                if (m_Value != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Value);
            }

            ScopedWindowsHandle(const ScopedWindowsHandle&) = delete;
            ScopedWindowsHandle& operator=(const ScopedWindowsHandle&) = delete;

            [[nodiscard]] HANDLE Get() const noexcept { return m_Value; }

          private:
            HANDLE m_Value = INVALID_HANDLE_VALUE;
        };
#endif

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

        [[nodiscard]] HubError OutputError(const HubErrorCode code, std::string message, const std::string& item,
                                           const std::error_code& error = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = item,
                    .TechnicalDetails = error.message()};
        }
    } // namespace

    struct ExclusivePackageOutput::Implementation final
    {
        ~Implementation()
        {
#if defined(_WIN32)
            if (Handle != INVALID_HANDLE_VALUE)
            {
                if (OwnsPath)
                {
                    FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                    static_cast<void>(SetFileInformationByHandle(Handle, FileDispositionInfo, &disposition,
                                                                 static_cast<DWORD>(sizeof(disposition))));
                }
                CloseHandle(Handle);
            }
#else
            if (Handle >= 0)
            {
                if (OwnsPath)
                {
                    struct stat opened{};
                    struct stat named{};
                    if (::fstat(Handle, &opened) == 0 && ::lstat(Path.c_str(), &named) == 0 && S_ISREG(named.st_mode) &&
                        opened.st_dev == named.st_dev && opened.st_ino == named.st_ino)
                    {
                        static_cast<void>(::unlink(Path.c_str()));
                    }
                }
                ::close(Handle);
            }
#endif
        }

        std::filesystem::path Path;
        std::string Item;
        ExclusivePackageOutputTestHooks TestHooks;
#if defined(_WIN32)
        HANDLE Handle = INVALID_HANDLE_VALUE;
#else
        int Handle = -1;
#endif
        bool Finished = false;
        bool OwnsPath = false;
    };

    ExclusivePackageOutput::ExclusivePackageOutput(std::unique_ptr<Implementation> implementation)
        : m_Implementation(std::move(implementation))
    {
    }

    ExclusivePackageOutput::~ExclusivePackageOutput() = default;

    HubResult<std::unique_ptr<ExclusivePackageOutput>>
    ExclusivePackageOutput::Create(const std::filesystem::path& output, std::string item,
                                   ExclusivePackageOutputTestHooks testHooks)
    {
        auto implementation = std::make_unique<Implementation>();
        implementation->Item = std::move(item);
        implementation->TestHooks = std::move(testHooks);
        try
        {
            std::random_device random;
            for (std::size_t attempt = 0; attempt < 128U; ++attempt)
            {
                implementation->Path = output;
                implementation->Path += ".tmp-" + std::to_string(random()) + '-' + std::to_string(random()) + '-' +
                                        std::to_string(random()) + '-' + std::to_string(random());
#if defined(_WIN32)
                implementation->Handle = CreateFileW(NativeIoPath(implementation->Path).c_str(), GENERIC_WRITE | DELETE,
                                                     0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (implementation->Handle != INVALID_HANDLE_VALUE)
                {
                    implementation->OwnsPath = true;
                    return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Success(
                        std::unique_ptr<ExclusivePackageOutput>(new ExclusivePackageOutput(std::move(implementation))));
                }
                const auto failure = GetLastError();
                if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS)
                {
                    return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Failure(OutputError(
                        HubErrorCode::IoWrite, "The package output could not be created.", implementation->Item,
                        std::error_code(static_cast<int>(failure), std::system_category())));
                }
#else
                int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
                flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
                flags |= O_NOFOLLOW;
#endif
                implementation->Handle = ::open(NativeIoPath(implementation->Path).c_str(), flags, 0600);
                if (implementation->Handle >= 0)
                {
                    implementation->OwnsPath = true;
                    return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Success(
                        std::unique_ptr<ExclusivePackageOutput>(new ExclusivePackageOutput(std::move(implementation))));
                }
                if (errno != EEXIST)
                {
                    return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Failure(
                        OutputError(HubErrorCode::IoWrite, "The package output could not be created.",
                                    implementation->Item, std::error_code(errno, std::generic_category())));
                }
#endif
            }
            return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Failure(
                OutputError(HubErrorCode::IoWrite, "A unique package output staging file could not be allocated.",
                            implementation->Item));
        }
        catch (const std::exception&)
        {
            return HubResult<std::unique_ptr<ExclusivePackageOutput>>::Failure(
                OutputError(HubErrorCode::IoWrite, "The package output could not be created.", implementation->Item,
                            std::make_error_code(std::errc::io_error), true));
        }
    }

    HubStatus ExclusivePackageOutput::Write(const std::span<const char> bytes)
    {
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
#if defined(_WIN32)
            const auto remaining = std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<DWORD>::max());
            DWORD written = 0;
            if (!WriteFile(m_Implementation->Handle, bytes.data() + offset, static_cast<DWORD>(remaining), &written,
                           nullptr) ||
                written == 0)
            {
                return HubStatus::Failure(OutputError(
                    HubErrorCode::IoWrite, "The package output could not be written.", m_Implementation->Item,
                    std::error_code(static_cast<int>(GetLastError()), std::system_category())));
            }
#else
            const auto written = ::write(m_Implementation->Handle, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
            {
                return HubStatus::Failure(OutputError(HubErrorCode::IoWrite, "The package output could not be written.",
                                                      m_Implementation->Item,
                                                      std::error_code(errno, std::generic_category())));
            }
#endif
            offset += static_cast<std::size_t>(written);
        }
        return HubStatus::Success();
    }

    HubStatus ExclusivePackageOutput::Finish()
    {
        if (m_Implementation->Finished)
            return HubStatus::Success();
#if defined(_WIN32)
        if (!FlushFileBuffers(m_Implementation->Handle))
        {
            return HubStatus::Failure(
                OutputError(HubErrorCode::IoWrite, "The package output could not be completed.", m_Implementation->Item,
                            std::error_code(static_cast<int>(GetLastError()), std::system_category())));
        }
#else
        if (::fchmod(m_Implementation->Handle, 0644) != 0 || ::fsync(m_Implementation->Handle) != 0)
        {
            return HubStatus::Failure(OutputError(HubErrorCode::IoWrite, "The package output could not be completed.",
                                                  m_Implementation->Item,
                                                  std::error_code(errno, std::generic_category())));
        }
#endif
        m_Implementation->Finished = true;
        return HubStatus::Success();
    }

    HubStatus ExclusivePackageOutput::Publish(const std::filesystem::path& output, const bool replaceExisting)
    {
        if (!m_Implementation->Finished)
        {
            return HubStatus::Failure(OutputError(HubErrorCode::InvalidTransition,
                                                  "The package output is not complete.", m_Implementation->Item));
        }
#if defined(_WIN32)
        std::error_code parentError;
        const auto temporaryParent = m_Implementation->Path.parent_path();
        const auto destinationParent = output.parent_path();
        if (output.filename().empty() ||
            !std::filesystem::equivalent(NativeIoPath(temporaryParent), NativeIoPath(destinationParent), parentError) ||
            parentError)
        {
            return HubStatus::Failure(OutputError(HubErrorCode::UnsafeInstallRoot,
                                                  "The package output parent directory changed.",
                                                  m_Implementation->Item, parentError));
        }
        // Holding the verified parent without delete sharing prevents it from being renamed or replaced while the
        // full native destination name is committed or rolled back.
        ScopedWindowsHandle directory(CreateFileW(NativeIoPath(destinationParent).c_str(),
                                                  FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FILE_ATTRIBUTE_TAG_INFO directoryAttributes{};
        if (directory.Get() == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandleEx(directory.Get(), FileAttributeTagInfo, &directoryAttributes,
                                          sizeof(directoryAttributes)) ||
            (directoryAttributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (directoryAttributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            const auto failure = GetLastError();
            return HubStatus::Failure(OutputError(
                HubErrorCode::UnsafeInstallRoot, "The package output parent directory is unsafe.",
                m_Implementation->Item, std::error_code(static_cast<int>(failure), std::system_category())));
        }
        const auto prepareRename = [&](const std::filesystem::path& destination,
                                       const bool replace) -> HubResult<std::vector<std::byte>>
        {
            auto name = NativeIoPath(destination).native();
            constexpr std::wstring_view win32ExtendedPrefix = LR"(\\?\)";
            constexpr std::wstring_view nativePrefix = LR"(\??\)";
            if (!name.starts_with(win32ExtendedPrefix))
            {
                return HubResult<std::vector<std::byte>>::Failure(OutputError(
                    HubErrorCode::InvalidArgument, "The package output path is invalid.", m_Implementation->Item));
            }
            name.replace(0, win32ExtendedPrefix.size(), nativePrefix);
            if (name.empty() || name.size() > std::numeric_limits<DWORD>::max() / sizeof(wchar_t) ||
                sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t) > std::numeric_limits<DWORD>::max())
            {
                return HubResult<std::vector<std::byte>>::Failure(OutputError(
                    HubErrorCode::InvalidArgument, "The package output path is too long.", m_Implementation->Item));
            }
            const auto nameBytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            // Windows requires the complete structure size in addition to the variable-length name bytes; using
            // offsetof(FileName) underallocates this buffer on x64 because it omits the structure's tail padding.
            std::vector<std::byte> storage(sizeof(FILE_RENAME_INFO) + nameBytes);
            auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            information->ReplaceIfExists = replace ? TRUE : FALSE;
            information->RootDirectory = nullptr;
            information->FileNameLength = nameBytes;
            std::memcpy(information->FileName, name.data(), nameBytes);
            return HubResult<std::vector<std::byte>>::Success(std::move(storage));
        };
        auto destinationRename = prepareRename(output, replaceExisting);
        if (!destinationRename)
            return HubStatus::Failure(destinationRename.Error());
        std::vector<std::byte> rollbackRename;
        if (!replaceExisting)
        {
            auto preparedRollback = prepareRename(m_Implementation->Path, false);
            if (!preparedRollback)
                return HubStatus::Failure(preparedRollback.Error());
            rollbackRename = std::move(preparedRollback).Value();
        }
        const auto renameHandle = [&](std::vector<std::byte>& storage)
        {
            auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            return SetFileInformationByHandle(m_Implementation->Handle, FileRenameInfo, information,
                                              static_cast<DWORD>(storage.size())) != 0;
        };
        if (!renameHandle(destinationRename.Value()))
        {
            const auto failure = GetLastError();
            const auto code = failure == ERROR_FILE_EXISTS || failure == ERROR_ALREADY_EXISTS
                                  ? HubErrorCode::DestinationConflict
                                  : HubErrorCode::IoWrite;
            return HubStatus::Failure(
                OutputError(code, "The package output could not be published.", m_Implementation->Item,
                            std::error_code(static_cast<int>(failure), std::system_category()), true));
        }
        bool injectedFailure = false;
        try
        {
            injectedFailure =
                m_Implementation->TestHooks.FailAfterCommit && m_Implementation->TestHooks.FailAfterCommit();
        }
        catch (...)
        {
            injectedFailure = true;
        }
        const bool synchronized = !injectedFailure && FlushFileBuffers(m_Implementation->Handle) != 0;
        const auto syncFailure = injectedFailure ? ERROR_WRITE_FAULT : (synchronized ? ERROR_SUCCESS : GetLastError());
        if (!synchronized && !replaceExisting && renameHandle(rollbackRename))
        {
            return HubStatus::Failure(OutputError(
                HubErrorCode::IoWrite, "The committed package output could not be finalized and was rolled back.",
                m_Implementation->Item, std::error_code(static_cast<int>(syncFailure), std::system_category()), true));
        }
        CloseHandle(m_Implementation->Handle);
        m_Implementation->Handle = INVALID_HANDLE_VALUE;
        m_Implementation->OwnsPath = false;
#else
        const auto temporaryPath = NativeIoPath(m_Implementation->Path);
        const auto publishedPath = NativeIoPath(output);
        const auto parentPath = NativeIoPath(output.parent_path());
        struct stat opened{};
        struct stat named{};
        if (::fstat(m_Implementation->Handle, &opened) != 0 || ::lstat(temporaryPath.c_str(), &named) != 0 ||
            !S_ISREG(named.st_mode) || opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
        {
            return HubStatus::Failure(OutputError(HubErrorCode::UnsafeInstallRoot,
                                                  "The package output staging file was replaced.",
                                                  m_Implementation->Item));
        }
        const int publishedResult = replaceExisting ? ::rename(temporaryPath.c_str(), publishedPath.c_str())
                                                    : ::link(temporaryPath.c_str(), publishedPath.c_str());
        if (publishedResult != 0)
        {
            const auto failure = errno;
            const auto code = failure == EEXIST ? HubErrorCode::DestinationConflict : HubErrorCode::IoWrite;
            return HubStatus::Failure(OutputError(code, "The package output could not be published.",
                                                  m_Implementation->Item,
                                                  std::error_code(failure, std::generic_category()), true));
        }
        if (replaceExisting)
            m_Implementation->OwnsPath = false;

        enum class RollbackResult
        {
            RolledBack,
            Committed,
            Unsafe
        };
        const auto rollbackNoReplace = [&]
        {
            struct stat published{};
            if (::lstat(publishedPath.c_str(), &published) != 0)
                return errno == ENOENT ? RollbackResult::RolledBack : RollbackResult::Unsafe;
            if (!S_ISREG(published.st_mode) || opened.st_dev != published.st_dev || opened.st_ino != published.st_ino)
                return RollbackResult::Unsafe;
            if (::unlink(publishedPath.c_str()) == 0 || errno == ENOENT)
                return RollbackResult::RolledBack;
            return RollbackResult::Committed;
        };
        const auto resolvePostCommitFailure = [&](const std::error_code& failure)
        {
            if (replaceExisting)
                return HubStatus::Success();
            switch (rollbackNoReplace())
            {
            case RollbackResult::RolledBack:
                return HubStatus::Failure(OutputError(
                    HubErrorCode::IoWrite, "The committed package output could not be finalized and was rolled back.",
                    m_Implementation->Item, failure, true));
            case RollbackResult::Committed:
                return HubStatus::Success();
            case RollbackResult::Unsafe:
                return HubStatus::Failure(OutputError(HubErrorCode::UnsafeInstallRoot,
                                                      "The committed package output identity changed.",
                                                      m_Implementation->Item));
            }
            return HubStatus::Failure(OutputError(
                HubErrorCode::IoWrite, "The committed package output state is invalid.", m_Implementation->Item));
        };

        if (!replaceExisting)
        {
            struct stat published{};
            if (::lstat(publishedPath.c_str(), &published) != 0 || !S_ISREG(published.st_mode) ||
                opened.st_dev != published.st_dev || opened.st_ino != published.st_ino)
            {
                return resolvePostCommitFailure(std::make_error_code(std::errc::state_not_recoverable));
            }
        }
        bool injectedFailure = false;
        try
        {
            injectedFailure =
                m_Implementation->TestHooks.FailAfterCommit && m_Implementation->TestHooks.FailAfterCommit();
        }
        catch (...)
        {
            injectedFailure = true;
        }
        if (injectedFailure)
            return resolvePostCommitFailure(std::make_error_code(std::errc::io_error));

        if (!replaceExisting)
        {
            struct stat temporary{};
            if (::lstat(temporaryPath.c_str(), &temporary) != 0)
            {
                if (errno != ENOENT)
                    return resolvePostCommitFailure(std::error_code(errno, std::generic_category()));
                m_Implementation->OwnsPath = false;
            }
            else if (!S_ISREG(temporary.st_mode) || opened.st_dev != temporary.st_dev ||
                     opened.st_ino != temporary.st_ino)
            {
                return resolvePostCommitFailure(std::make_error_code(std::errc::state_not_recoverable));
            }
            else
            {
                if (::unlink(temporaryPath.c_str()) != 0)
                    return resolvePostCommitFailure(std::error_code(errno, std::generic_category()));
                m_Implementation->OwnsPath = false;
            }
        }

        int flags = O_RDONLY;
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_DIRECTORY)
        flags |= O_DIRECTORY;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        const int directory = ::open(parentPath.c_str(), flags);
        if (directory < 0)
            return resolvePostCommitFailure(std::error_code(errno, std::generic_category()));
        const int synchronized = ::fsync(directory);
        const int failure = synchronized == 0 ? 0 : errno;
        ::close(directory);
        if (failure != 0)
            return resolvePostCommitFailure(std::error_code(failure, std::generic_category()));
#endif
        return HubStatus::Success();
    }
} // namespace KeireHub::Detail
