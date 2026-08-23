#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Keire::Detail
{
    namespace
    {
        std::mutex s_AnchoredHookMutex;
        AnchoredFileSystemOperationHook s_AnchoredHook;

        [[nodiscard]] std::vector<std::filesystem::path> ConfinedComponents(const std::filesystem::path& relative)
        {
            const auto normalized = relative.lexically_normal();
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
                relative.has_root_directory() || normalized.empty() || normalized == "..")
                throw std::invalid_argument("Path must be a confined relative path: " + PathToUtf8(relative));
            std::vector<std::filesystem::path> result;
            for (const auto& component : normalized)
            {
                if (component.empty() || component == ".")
                    continue;
                if (component == "..")
                    throw std::invalid_argument("Path must be a confined relative path: " + PathToUtf8(relative));
                result.push_back(component);
            }
            if (result.empty())
                throw std::invalid_argument("Path must name an entry below the anchored root.");
            return result;
        }

        void InvokeAnchoredHook(const std::string_view operation, const std::filesystem::path& relative)
        {
            AnchoredFileSystemOperationHook hook;
            {
                std::scoped_lock lock(s_AnchoredHookMutex);
                hook = s_AnchoredHook;
            }
            if (hook)
                hook(operation, relative);
        }
    } // namespace

    void SetAnchoredFileSystemOperationHookForTesting(AnchoredFileSystemOperationHook hook)
    {
        std::scoped_lock lock(s_AnchoredHookMutex);
        s_AnchoredHook = std::move(hook);
    }

    class AnchoredFileSystem::Impl final
    {
      public:
        explicit Impl(const std::filesystem::path& root) : m_Root(CanonicalExistingPath(root))
        {
#if defined(_WIN32)
            m_RootHandle = CreateFileW(m_Root.c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (m_RootHandle == INVALID_HANDLE_VALUE)
                ThrowWindows("Cannot anchor filesystem root");
            try
            {
                RejectReparsePoint(m_RootHandle);
            }
            catch (...)
            {
                CloseHandle(m_RootHandle);
                m_RootHandle = INVALID_HANDLE_VALUE;
                throw;
            }
#else
            m_RootDescriptor = open(m_Root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (m_RootDescriptor < 0)
                throw std::system_error(errno, std::generic_category(), "Cannot anchor filesystem root");
#endif
        }

        ~Impl()
        {
#if defined(_WIN32)
            if (m_RootHandle != INVALID_HANDLE_VALUE)
                CloseHandle(m_RootHandle);
#else
            if (m_RootDescriptor >= 0)
                close(m_RootDescriptor);
#endif
        }

        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_Root; }

#if defined(_WIN32)
        class Handle final
        {
          public:
            explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : m_Value(value) {}
            ~Handle()
            {
                if (m_Value != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Value);
            }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& other) noexcept : m_Value(std::exchange(other.m_Value, INVALID_HANDLE_VALUE)) {}
            Handle& operator=(Handle&& other) noexcept
            {
                if (this != &other)
                {
                    if (m_Value != INVALID_HANDLE_VALUE)
                        CloseHandle(m_Value);
                    m_Value = std::exchange(other.m_Value, INVALID_HANDLE_VALUE);
                }
                return *this;
            }
            [[nodiscard]] HANDLE Get() const noexcept { return m_Value; }

          private:
            HANDLE m_Value;
        };

        using NtCreateFileFunction = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                                                      PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
        using NtSetInformationFileFunction = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG,
                                                              FILE_INFORMATION_CLASS);
        static constexpr ULONG NtFileDirectory = 0x00000001UL;
        static constexpr ULONG NtFileSynchronousIoNonAlert = 0x00000020UL;
        static constexpr ULONG NtFileNonDirectory = 0x00000040UL;
        static constexpr ULONG NtFileOpenReparsePoint = 0x00200000UL;
        static constexpr ULONG NtOpenExisting = 1UL;
        static constexpr ULONG NtCreateNew = 2UL;
        static constexpr ULONG NtOpenOrCreate = 3UL;

        [[noreturn]] static void ThrowWindows(const std::string_view message, const DWORD error = GetLastError())
        {
            throw std::system_error(static_cast<int>(error), std::system_category(), std::string(message));
        }

        [[nodiscard]] static NtCreateFileFunction NtCreate()
        {
            static const auto function =
                reinterpret_cast<NtCreateFileFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
            if (!function)
                throw std::runtime_error("NtCreateFile is unavailable; anchored filesystem operations are unsafe.");
            return function;
        }

        [[nodiscard]] static NtSetInformationFileFunction NtSetInformation()
        {
            static const auto function = reinterpret_cast<NtSetInformationFileFunction>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
            if (!function)
                throw std::runtime_error(
                    "NtSetInformationFile is unavailable; anchored filesystem operations are unsafe.");
            return function;
        }

        [[nodiscard]] static DWORD DosError(const NTSTATUS status)
        {
            using ConvertFunction = ULONG(WINAPI*)(NTSTATUS);
            static const auto convert = reinterpret_cast<ConvertFunction>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
            return convert ? convert(status) : ERROR_GEN_FAILURE;
        }

        static void RejectReparsePoint(const HANDLE handle)
        {
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)))
                ThrowWindows("Cannot inspect anchored filesystem entry");
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                throw std::invalid_argument("Anchored filesystem paths may not traverse reparse points.");
        }

        [[nodiscard]] static Handle OpenRelative(const HANDLE parent, const std::filesystem::path& component,
                                                 const ACCESS_MASK access, const ULONG disposition, const ULONG options,
                                                 const ULONG attributes = FILE_ATTRIBUTE_NORMAL,
                                                 const bool retryTransient = false)
        {
            const auto name = component.native();
            if (name.empty() || name.find_first_of(L"\\/") != std::wstring::npos)
                throw std::invalid_argument("Anchored filesystem components must contain one filename.");
            UNICODE_STRING unicode{};
            if (name.size() > (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t))
                throw std::invalid_argument("Anchored filesystem component is too long.");
            unicode.Buffer = const_cast<PWSTR>(name.data());
            unicode.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
            unicode.MaximumLength = unicode.Length;
            OBJECT_ATTRIBUTES object{};
            InitializeObjectAttributes(&object, &unicode, OBJ_CASE_INSENSITIVE, parent, nullptr);
            constexpr std::array delays{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                        std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                        std::chrono::milliseconds(160)};
            const auto failure = "Cannot open anchored filesystem entry '" + PathToUtf8(component) +
                                 "' (access=" + std::to_string(access) +
                                 ", disposition=" + std::to_string(disposition) + ')';
            HANDLE value = INVALID_HANDLE_VALUE;
            DWORD error = ERROR_SUCCESS;
            for (const auto delay : delays)
            {
                IO_STATUS_BLOCK statusBlock{};
                value = INVALID_HANDLE_VALUE;
                const auto status =
                    NtCreate()(&value, access, &object, &statusBlock, nullptr, attributes,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
                               options | NtFileSynchronousIoNonAlert | NtFileOpenReparsePoint, nullptr, 0);
                if (status >= 0)
                    break;
                error = DosError(status);
                if (!retryTransient ||
                    (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION))
                    ThrowWindows(failure, error);
                std::this_thread::sleep_for(delay);
            }
            if (value == INVALID_HANDLE_VALUE)
                ThrowWindows(failure, error);
            Handle result(value);
            RejectReparsePoint(value);
            return result;
        }

        [[nodiscard]] Handle DuplicateRoot() const
        {
            HANDLE duplicate = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), m_RootHandle, GetCurrentProcess(), &duplicate, 0, FALSE,
                                 DUPLICATE_SAME_ACCESS))
                ThrowWindows("Cannot duplicate anchored filesystem root handle");
            return Handle(duplicate);
        }

        static void RenameRelative(const HANDLE file, const HANDLE destinationParent,
                                   const std::filesystem::path& destinationName, const bool replaceExisting,
                                   const std::string_view failureMessage)
        {
            const auto target = destinationName.native();
            const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + target.size() * sizeof(wchar_t);
            std::vector<std::byte> storage(bytes);
            auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            rename->ReplaceIfExists = replaceExisting ? TRUE : FALSE;
            rename->RootDirectory = destinationParent;
            rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
            std::memcpy(rename->FileName, target.data(), rename->FileNameLength);
            constexpr auto renameInformation = static_cast<FILE_INFORMATION_CLASS>(10);
            constexpr std::array delays{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                        std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                        std::chrono::milliseconds(160)};
            DWORD error = ERROR_SUCCESS;
            for (const auto delay : delays)
            {
                IO_STATUS_BLOCK statusBlock{};
                const auto status = NtSetInformation()(file, &statusBlock, rename, static_cast<ULONG>(storage.size()),
                                                       renameInformation);
                if (status >= 0)
                    return;
                error = DosError(status);
                if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
                    ThrowWindows(failureMessage, error);
                std::this_thread::sleep_for(delay);
            }
            ThrowWindows(failureMessage, error);
        }

        [[nodiscard]] Handle OpenParent(const std::vector<std::filesystem::path>& components,
                                        const bool createDirectories) const
        {
            auto current = DuplicateRoot();
            std::filesystem::path traversed;
            for (std::size_t index = 0; index + 1 < components.size(); ++index)
            {
                const auto disposition = createDirectories ? NtOpenOrCreate : NtOpenExisting;
                traversed /= components[index];
                try
                {
                    current = OpenRelative(current.Get(), components[index],
                                           FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                           disposition, NtFileDirectory, FILE_ATTRIBUTE_DIRECTORY, true);
                }
                catch (const std::system_error& error)
                {
                    throw std::system_error(error.code(), "Cannot traverse anchored filesystem parent '" +
                                                              PathToUtf8(traversed) + "'");
                }
            }
            return current;
        }
#else
        class Descriptor final
        {
          public:
            explicit Descriptor(const int value = -1) noexcept : m_Value(value) {}
            ~Descriptor()
            {
                if (m_Value >= 0)
                    close(m_Value);
            }
            Descriptor(const Descriptor&) = delete;
            Descriptor& operator=(const Descriptor&) = delete;
            Descriptor(Descriptor&& other) noexcept : m_Value(std::exchange(other.m_Value, -1)) {}
            Descriptor& operator=(Descriptor&& other) noexcept
            {
                if (this != &other)
                {
                    if (m_Value >= 0)
                        close(m_Value);
                    m_Value = std::exchange(other.m_Value, -1);
                }
                return *this;
            }
            [[nodiscard]] int Get() const noexcept { return m_Value; }

          private:
            int m_Value;
        };

        static void RenameRelative(const int sourceParent, const std::filesystem::path& sourceName,
                                   const int destinationParent, const std::filesystem::path& destinationName,
                                   const bool replaceExisting, const std::string_view failureMessage)
        {
            int result = -1;
            if (replaceExisting)
                result = renameat(sourceParent, sourceName.c_str(), destinationParent, destinationName.c_str());
#if defined(__APPLE__)
            else
                result = renameatx_np(sourceParent, sourceName.c_str(), destinationParent, destinationName.c_str(),
                                      RENAME_EXCL);
#elif defined(__linux__)
            else
                result = static_cast<int>(syscall(SYS_renameat2, sourceParent, sourceName.c_str(), destinationParent,
                                                  destinationName.c_str(), 1U));
#else
#error "Kéire anchored no-replace rename requires an atomic platform implementation."
#endif
            if (result != 0)
                throw std::system_error(errno, std::generic_category(), std::string(failureMessage));
        }

        [[nodiscard]] Descriptor OpenParent(const std::vector<std::filesystem::path>& components,
                                            const bool createDirectories) const
        {
            Descriptor current(dup(m_RootDescriptor));
            if (current.Get() < 0)
                throw std::system_error(errno, std::generic_category(), "Cannot duplicate anchored root descriptor");
            for (std::size_t index = 0; index + 1 < components.size(); ++index)
            {
                if (createDirectories && mkdirat(current.Get(), components[index].c_str(), 0755) != 0 &&
                    errno != EEXIST)
                    throw std::system_error(errno, std::generic_category(), "Cannot create anchored directory");
                const auto next =
                    openat(current.Get(), components[index].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0)
                    throw std::system_error(errno, std::generic_category(), "Cannot open anchored directory");
                current = Descriptor(next);
            }
            return current;
        }
#endif

        std::filesystem::path m_Root;
#if defined(_WIN32)
        HANDLE m_RootHandle = INVALID_HANDLE_VALUE;
#else
        int m_RootDescriptor = -1;
#endif
    };

    AnchoredFileSystem::AnchoredFileSystem(const std::filesystem::path& root) : m_Impl(std::make_unique<Impl>(root)) {}
    AnchoredFileSystem::~AnchoredFileSystem() = default;
    AnchoredFileSystem::AnchoredFileSystem(AnchoredFileSystem&&) noexcept = default;
    AnchoredFileSystem& AnchoredFileSystem::operator=(AnchoredFileSystem&&) noexcept = default;

    const std::filesystem::path& AnchoredFileSystem::Root() const noexcept { return m_Impl->Root(); }

    std::vector<std::byte> AnchoredFileSystem::Read(const std::filesystem::path& relative,
                                                    const std::size_t maximumBytes) const
    {
        std::vector<std::byte> result;
        (void)ReadChunks(relative, maximumBytes, [&](const std::span<const std::byte> chunk)
                         { result.insert(result.end(), chunk.begin(), chunk.end()); });
        return result;
    }

    AnchoredFileMetadata AnchoredFileSystem::ReadChunks(const std::filesystem::path& relative,
                                                        const std::uintmax_t maximumBytes,
                                                        const AnchoredFileChunkVisitor& visitor) const
    {
        if (!visitor)
            throw std::invalid_argument("Anchored streaming reads require a chunk visitor.");
        const auto components = ConfinedComponents(relative);
        auto parent = m_Impl->OpenParent(components, false);
        InvokeAnchoredHook("read", relative);
#if defined(_WIN32)
        auto file =
            Impl::OpenRelative(parent.Get(), components.back(), FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                               Impl::NtOpenExisting, Impl::NtFileNonDirectory);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.Get(), &size))
            Impl::ThrowWindows("Cannot inspect anchored file");
        if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > maximumBytes ||
            static_cast<std::uint64_t>(size.QuadPart) > (std::numeric_limits<std::size_t>::max)())
            throw std::runtime_error("Anchored file exceeds the configured maximum size: " + PathToUtf8(relative));
        std::vector<std::byte> buffer(256ULL * 1024U);
        std::uint64_t remaining = static_cast<std::uint64_t>(size.QuadPart);
        while (remaining != 0)
        {
            const auto count = static_cast<DWORD>((std::min)(remaining, buffer.size()));
            DWORD read = 0;
            if (!ReadFile(file.Get(), buffer.data(), count, &read, nullptr) || read != count)
                Impl::ThrowWindows("Cannot read anchored file");
            visitor(std::span(buffer).first(read));
            remaining -= read;
        }
        return {.Size = static_cast<std::uintmax_t>(size.QuadPart), .Permissions = std::filesystem::perms::unknown};
#else
        Impl::Descriptor file(openat(parent.Get(), components.back().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        if (file.Get() < 0)
            throw std::system_error(errno, std::generic_category(), "Cannot open anchored file");
        struct stat status{};
        if (fstat(file.Get(), &status) != 0 || !S_ISREG(status.st_mode))
            throw std::runtime_error("Anchored path is not a regular file: " + PathToUtf8(relative));
        if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > maximumBytes ||
            static_cast<std::uint64_t>(status.st_size) > (std::numeric_limits<std::size_t>::max)())
            throw std::runtime_error("Anchored file exceeds the configured maximum size: " + PathToUtf8(relative));
        std::vector<std::byte> buffer(256ULL * 1024U);
        std::uint64_t remaining = static_cast<std::uint64_t>(status.st_size);
        while (remaining != 0)
        {
            const auto count = read(file.Get(), buffer.data(), (std::min)(remaining, buffer.size()));
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::system_error(errno, std::generic_category(), "Cannot read anchored file");
            }
            if (count == 0)
                throw std::runtime_error("Anchored file changed size while being read: " + PathToUtf8(relative));
            visitor(std::span(buffer).first(static_cast<std::size_t>(count)));
            remaining -= static_cast<std::size_t>(count);
        }
        return {.Size = static_cast<std::uintmax_t>(status.st_size),
                .Permissions = static_cast<std::filesystem::perms>(status.st_mode & 07777)};
#endif
    }

    AnchoredFileSignature AnchoredFileSystem::Signature(const std::filesystem::path& relative) const
    {
        const auto components = ConfinedComponents(relative);
        auto parent = m_Impl->OpenParent(components, false);
        InvokeAnchoredHook("signature", relative);
#if defined(_WIN32)
        auto file = Impl::OpenRelative(parent.Get(), components.back(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                       Impl::NtOpenExisting, Impl::NtFileNonDirectory);
        FILE_BASIC_INFO basic{};
        FILE_STANDARD_INFO standard{};
        if (!GetFileInformationByHandleEx(file.Get(), FileBasicInfo, &basic, sizeof(basic)) ||
            !GetFileInformationByHandleEx(file.Get(), FileStandardInfo, &standard, sizeof(standard)))
            Impl::ThrowWindows("Cannot inspect anchored file signature");
        return {static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart),
                static_cast<std::uintmax_t>(standard.EndOfFile.QuadPart)};
#else
        Impl::Descriptor file(openat(parent.Get(), components.back().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        struct stat status{};
        if (file.Get() < 0 || fstat(file.Get(), &status) != 0 || !S_ISREG(status.st_mode))
            throw std::system_error(errno, std::generic_category(), "Cannot inspect anchored file signature");
#if defined(__APPLE__)
        const auto seconds = status.st_mtimespec.tv_sec;
        const auto nanoseconds = status.st_mtimespec.tv_nsec;
#else
        const auto seconds = status.st_mtim.tv_sec;
        const auto nanoseconds = status.st_mtim.tv_nsec;
#endif
        return {(static_cast<std::uint64_t>(seconds) * 1'000'000'000ULL) + static_cast<std::uint64_t>(nanoseconds),
                static_cast<std::uintmax_t>(status.st_size)};
#endif
    }

    bool AnchoredFileSystem::IsRegularFile(const std::filesystem::path& relative) const
    {
        const auto components = ConfinedComponents(relative);
        try
        {
            auto parent = m_Impl->OpenParent(components, false);
            InvokeAnchoredHook("is-regular-file", relative);
#if defined(_WIN32)
            auto entry = Impl::OpenRelative(parent.Get(), components.back(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                            Impl::NtOpenExisting, 0);
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(entry.Get(), FileAttributeTagInfo, &attributes, sizeof(attributes)))
                Impl::ThrowWindows("Cannot inspect anchored filesystem entry");
            return (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
            struct stat status{};
            if (fstatat(parent.Get(), components.back().c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
                throw std::system_error(errno, std::generic_category(), "Cannot inspect anchored filesystem entry");
            if (S_ISLNK(status.st_mode))
                throw std::invalid_argument("Anchored filesystem paths may not be symbolic links.");
            return S_ISREG(status.st_mode);
#endif
        }
        catch (const std::system_error& error)
        {
            if (error.code() == std::errc::no_such_file_or_directory)
                return false;
#if defined(_WIN32)
            if (error.code().category() == std::system_category() &&
                (error.code().value() == ERROR_FILE_NOT_FOUND || error.code().value() == ERROR_PATH_NOT_FOUND))
                return false;
#endif
            throw;
        }
    }

    bool AnchoredFileSystem::Exists(const std::filesystem::path& relative) const
    {
        const auto components = ConfinedComponents(relative);
        try
        {
            auto parent = m_Impl->OpenParent(components, false);
            InvokeAnchoredHook("exists", relative);
#if defined(_WIN32)
            (void)Impl::OpenRelative(parent.Get(), components.back(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                     Impl::NtOpenExisting, 0);
#else
            struct stat status{};
            if (fstatat(parent.Get(), components.back().c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
                throw std::system_error(errno, std::generic_category(), "Cannot inspect anchored entry");
            if (S_ISLNK(status.st_mode))
                throw std::invalid_argument("Anchored filesystem paths may not be symbolic links.");
#endif
            return true;
        }
        catch (const std::system_error& error)
        {
            if (error.code() == std::errc::no_such_file_or_directory)
                return false;
#if defined(_WIN32)
            if (error.code().category() == std::system_category() &&
                (error.code().value() == ERROR_FILE_NOT_FOUND || error.code().value() == ERROR_PATH_NOT_FOUND))
                return false;
#endif
            throw;
        }
    }

    void AnchoredFileSystem::CreateDirectories(const std::filesystem::path& relative) const
    {
        const auto components = ConfinedComponents(relative);
#if defined(_WIN32)
        auto current = m_Impl->DuplicateRoot();
        for (std::size_t index = 0; index < components.size(); ++index)
        {
            if (index + 1 == components.size())
                InvokeAnchoredHook("create-directories", relative);
            current = Impl::OpenRelative(current.Get(), components[index],
                                         FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                         Impl::NtOpenOrCreate, Impl::NtFileDirectory, FILE_ATTRIBUTE_DIRECTORY);
        }
#else
        Impl::Descriptor current(dup(m_Impl->m_RootDescriptor));
        if (current.Get() < 0)
            throw std::system_error(errno, std::generic_category(), "Cannot duplicate anchored root descriptor");
        for (std::size_t index = 0; index < components.size(); ++index)
        {
            if (index + 1 == components.size())
                InvokeAnchoredHook("create-directories", relative);
            if (mkdirat(current.Get(), components[index].c_str(), 0755) != 0 && errno != EEXIST)
                throw std::system_error(errno, std::generic_category(), "Cannot create anchored directory");
            const auto next =
                openat(current.Get(), components[index].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0)
                throw std::system_error(errno, std::generic_category(), "Cannot open anchored directory");
            current = Impl::Descriptor(next);
        }
#endif
    }

    void AnchoredFileSystem::WriteFileAtomically(const std::filesystem::path& relative,
                                                 const std::span<const std::byte> contents,
                                                 const bool replaceExisting) const
    {
        std::size_t offset = 0;
        WriteFileAtomically(
            relative, contents.size(),
            [&](const std::span<std::byte> destination)
            {
                std::ranges::copy(contents.subspan(offset, destination.size()), destination.begin());
                offset += destination.size();
            },
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                std::filesystem::perms::group_read | std::filesystem::perms::others_read,
            replaceExisting);
    }

    void AnchoredFileSystem::WriteFileAtomically(const std::filesystem::path& relative, const std::uint64_t size,
                                                 const AnchoredFileChunkReader& reader,
                                                 const std::filesystem::perms permissions,
                                                 const bool replaceExisting) const
    {
        if (!reader)
            throw std::invalid_argument("Anchored streaming writes require a chunk reader.");
#if defined(_WIN32)
        (void)permissions;
#endif
        const auto components = ConfinedComponents(relative);
        auto parent = m_Impl->OpenParent(components, true);
        InvokeAnchoredHook("write", relative);
        const auto unique = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                            static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        const auto temporary = PathWithSuffix(components.back(), ".tmp." + std::to_string(unique));
#if defined(_WIN32)
        auto file =
            Impl::OpenRelative(parent.Get(), temporary,
                               FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
                               Impl::NtCreateNew, Impl::NtFileNonDirectory, FILE_ATTRIBUTE_NORMAL, true);
        try
        {
            std::vector<std::byte> buffer(256ULL * 1024U);
            std::uint64_t remaining = size;
            while (remaining != 0)
            {
                const auto count = static_cast<std::size_t>((std::min)(remaining, buffer.size()));
                const auto chunk = std::span(buffer).first(count);
                reader(chunk);
                DWORD written = 0;
                if (!WriteFile(file.Get(), chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr) ||
                    written != chunk.size())
                    Impl::ThrowWindows("Cannot write anchored temporary file");
                remaining -= written;
            }
            if (!FlushFileBuffers(file.Get()))
                Impl::ThrowWindows("Cannot flush anchored temporary file");
            const auto failure = "Cannot publish anchored file '" + PathToUtf8(relative) + "'";
            Impl::RenameRelative(file.Get(), parent.Get(), components.back(), replaceExisting, failure);
        }
        catch (...)
        {
            FILE_DISPOSITION_INFO disposition{TRUE};
            (void)SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition, sizeof(disposition));
            throw;
        }
#else
        Impl::Descriptor file(
            openat(parent.Get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (file.Get() < 0)
            throw std::system_error(errno, std::generic_category(), "Cannot create anchored temporary file");
        try
        {
            std::vector<std::byte> buffer(256ULL * 1024U);
            std::uint64_t remaining = size;
            while (remaining != 0)
            {
                const auto count = static_cast<std::size_t>((std::min)(remaining, buffer.size()));
                const auto chunk = std::span(buffer).first(count);
                reader(chunk);
                std::size_t offset = 0;
                while (offset < chunk.size())
                {
                    const auto written = write(file.Get(), chunk.data() + offset, chunk.size() - offset);
                    if (written < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        throw std::system_error(errno, std::generic_category(), "Cannot write anchored temporary file");
                    }
                    offset += static_cast<std::size_t>(written);
                }
                remaining -= chunk.size();
            }
            if (fchmod(file.Get(), static_cast<mode_t>(permissions) & 0777) != 0 || fsync(file.Get()) != 0)
                throw std::system_error(errno, std::generic_category(), "Cannot flush anchored temporary file");
            Impl::RenameRelative(parent.Get(), temporary, parent.Get(), components.back(), replaceExisting,
                                 "Cannot publish anchored file");
            (void)fsync(parent.Get());
        }
        catch (...)
        {
            (void)unlinkat(parent.Get(), temporary.c_str(), 0);
            throw;
        }
#endif
    }

    void AnchoredFileSystem::Remove(const std::filesystem::path& relative) const
    {
        const auto components = ConfinedComponents(relative);
        auto parent = m_Impl->OpenParent(components, false);
        InvokeAnchoredHook("remove", relative);
#if defined(_WIN32)
        auto entry = Impl::OpenRelative(parent.Get(), components.back(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                        Impl::NtOpenExisting, 0, FILE_ATTRIBUTE_NORMAL, true);
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(entry.Get(), FileDispositionInfo, &disposition, sizeof(disposition)))
            Impl::ThrowWindows("Cannot remove anchored filesystem entry");
        entry = Impl::Handle{};
        constexpr std::array delays{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                    std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                    std::chrono::milliseconds(160)};
        for (const auto delay : delays)
        {
            try
            {
                (void)Impl::OpenRelative(parent.Get(), components.back(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                         Impl::NtOpenExisting, 0);
            }
            catch (const std::system_error& error)
            {
                if (error.code().category() == std::system_category() &&
                    (error.code().value() == ERROR_FILE_NOT_FOUND || error.code().value() == ERROR_PATH_NOT_FOUND))
                    return;
                if (error.code().category() != std::system_category() ||
                    (error.code().value() != ERROR_ACCESS_DENIED && error.code().value() != ERROR_SHARING_VIOLATION &&
                     error.code().value() != ERROR_LOCK_VIOLATION))
                    throw;
            }
            std::this_thread::sleep_for(delay);
        }
        throw std::runtime_error("Anchored filesystem entry remained pending deletion: " + PathToUtf8(relative));
#else
        struct stat status{};
        if (fstatat(parent.Get(), components.back().c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            throw std::system_error(errno, std::generic_category(), "Cannot inspect anchored filesystem entry");
        if (S_ISLNK(status.st_mode))
            throw std::invalid_argument("Anchored filesystem paths may not be symbolic links.");
        if (unlinkat(parent.Get(), components.back().c_str(), S_ISDIR(status.st_mode) ? AT_REMOVEDIR : 0) != 0)
            throw std::system_error(errno, std::generic_category(), "Cannot remove anchored filesystem entry");
#endif
    }

    void AnchoredFileSystem::Rename(const std::filesystem::path& source, const std::filesystem::path& destination,
                                    const bool replaceExisting) const
    {
        RenameTo(source, *this, destination, replaceExisting);
    }

    void AnchoredFileSystem::RenameTo(const std::filesystem::path& source,
                                      const AnchoredFileSystem& destinationFileSystem,
                                      const std::filesystem::path& destination, const bool replaceExisting) const
    {
        const auto sourceComponents = ConfinedComponents(source);
        const auto destinationComponents = ConfinedComponents(destination);
        auto sourceParent = m_Impl->OpenParent(sourceComponents, false);
        auto destinationParent = destinationFileSystem.m_Impl->OpenParent(destinationComponents, true);
        InvokeAnchoredHook("rename", source);
#if defined(_WIN32)
        auto entry =
            Impl::OpenRelative(sourceParent.Get(), sourceComponents.back(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                               Impl::NtOpenExisting, 0, FILE_ATTRIBUTE_NORMAL, true);
        const auto failure =
            "Cannot rename anchored filesystem entry '" + PathToUtf8(source) + "' to '" + PathToUtf8(destination) + "'";
        Impl::RenameRelative(entry.Get(), destinationParent.Get(), destinationComponents.back(), replaceExisting,
                             failure);
#else
        struct stat status{};
        if (fstatat(sourceParent.Get(), sourceComponents.back().c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            throw std::system_error(errno, std::generic_category(), "Cannot inspect anchored rename source");
        if (S_ISLNK(status.st_mode))
            throw std::invalid_argument("Anchored filesystem paths may not be symbolic links.");
        Impl::RenameRelative(sourceParent.Get(), sourceComponents.back(), destinationParent.Get(),
                             destinationComponents.back(), replaceExisting, "Cannot rename anchored filesystem entry");
        (void)fsync(sourceParent.Get());
        if (destinationParent.Get() != sourceParent.Get())
            (void)fsync(destinationParent.Get());
#endif
    }

    void AnchoredFileSystem::Copy(const std::filesystem::path& source, const std::filesystem::path& destination,
                                  const bool replaceExisting) const
    {
        WriteFileAtomically(destination, Read(source, (std::numeric_limits<std::size_t>::max)()), replaceExisting);
    }

    class InterprocessMutex::Impl final
    {
      public:
        explicit Impl(const std::filesystem::path& path)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
                throw std::runtime_error("Cannot create asset-operation lock directory: " + error.message());
#if defined(_WIN32)
            m_Handle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_Handle == INVALID_HANDLE_VALUE)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Cannot open asset-operation lock file");
#else
            m_Descriptor = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (m_Descriptor < 0)
                throw std::system_error(errno, std::generic_category(), "Cannot open asset-operation lock file");
#endif
        }

        ~Impl()
        {
            Unlock();
#if defined(_WIN32)
            if (m_Handle != INVALID_HANDLE_VALUE)
                CloseHandle(m_Handle);
#else
            if (m_Descriptor >= 0)
                close(m_Descriptor);
#endif
        }

        void Lock()
        {
            m_Local.lock();
            try
            {
#if defined(_WIN32)
                OVERLAPPED overlap{};
                while (!LockFileEx(m_Handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap))
                {
                    const auto error = GetLastError();
                    if (error != ERROR_LOCK_VIOLATION && error != ERROR_SHARING_VIOLATION)
                        throw std::system_error(static_cast<int>(error), std::system_category(),
                                                "Cannot acquire asset-operation lock");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
#else
                while (flock(m_Descriptor, LOCK_EX) != 0)
                    if (errno != EINTR)
                        throw std::system_error(errno, std::generic_category(), "Cannot acquire asset-operation lock");
#endif
                m_Locked = true;
            }
            catch (...)
            {
                m_Local.unlock();
                throw;
            }
        }

        void Unlock() noexcept
        {
            if (!m_Locked)
                return;
#if defined(_WIN32)
            OVERLAPPED overlap{};
            (void)UnlockFileEx(m_Handle, 0, 1, 0, &overlap);
#else
            (void)flock(m_Descriptor, LOCK_UN);
#endif
            m_Locked = false;
            m_Local.unlock();
        }

      private:
        std::mutex m_Local;
#if defined(_WIN32)
        HANDLE m_Handle = INVALID_HANDLE_VALUE;
#else
        int m_Descriptor = -1;
#endif
        bool m_Locked = false;
    };

    InterprocessMutex::InterprocessMutex(const std::filesystem::path& path) : m_Impl(std::make_unique<Impl>(path)) {}
    InterprocessMutex::~InterprocessMutex() = default;
    void InterprocessMutex::lock() { m_Impl->Lock(); }
    void InterprocessMutex::unlock() noexcept { m_Impl->Unlock(); }

    namespace
    {
#if defined(_WIN32)
        [[nodiscard]] std::wstring ExtendedLengthPath(const std::filesystem::path& path)
        {
            auto value = std::filesystem::absolute(path).lexically_normal().native();
            if (value.starts_with(LR"(\\?\)"))
                return value;
            if (value.starts_with(LR"(\\)"))
                return LR"(\\?\UNC\)" + value.substr(2);
            return LR"(\\?\)" + value;
        }

        [[nodiscard]] bool FilesMatch(const std::filesystem::path& first, const std::filesystem::path& second)
        {
            std::error_code error;
            const auto firstSize = std::filesystem::file_size(first, error);
            if (error)
                return false;
            const auto secondSize = std::filesystem::file_size(second, error);
            if (error || firstSize != secondSize)
                return false;
            std::ifstream left(std::filesystem::path(ExtendedLengthPath(first)), std::ios::binary);
            std::ifstream right(std::filesystem::path(ExtendedLengthPath(second)), std::ios::binary);
            if (!left || !right)
                return false;
            std::array<char, std::size_t{64} * 1024U> leftBytes{};
            std::array<char, leftBytes.size()> rightBytes{};
            while (left && right)
            {
                left.read(leftBytes.data(), static_cast<std::streamsize>(leftBytes.size()));
                right.read(rightBytes.data(), static_cast<std::streamsize>(rightBytes.size()));
                if (left.gcount() != right.gcount() ||
                    !std::equal(leftBytes.begin(), leftBytes.begin() + left.gcount(), rightBytes.begin()))
                    return false;
            }
            return left.eof() && right.eof();
        }
#endif

        void FlushFileToStorage(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            const auto nativePath = ExtendedLengthPath(path);
            const auto handle =
                CreateFileW(nativePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Cannot open temporary file for durable publication");
            const auto flushed = FlushFileBuffers(handle);
            const auto error = flushed ? ERROR_SUCCESS : GetLastError();
            CloseHandle(handle);
            if (!flushed)
                throw std::system_error(static_cast<int>(error), std::system_category(),
                                        "Cannot flush temporary file for durable publication");
#else
            const auto descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (descriptor < 0)
                throw std::system_error(errno, std::generic_category(),
                                        "Cannot open temporary file for durable publication");
            const auto flushed = fsync(descriptor);
            const auto error = errno;
            close(descriptor);
            if (flushed != 0)
                throw std::system_error(error, std::generic_category(),
                                        "Cannot flush temporary file for durable publication");
#endif
        }

#if defined(_WIN32)
        void RenameNativePath(const std::filesystem::path& source, const std::filesystem::path& destination,
                              std::error_code& error)
        {
            const auto extendedSource = ExtendedLengthPath(source);
            const auto extendedDestination = ExtendedLengthPath(destination);
            if (MoveFileExW(extendedSource.c_str(), extendedDestination.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                error.clear();
                return;
            }
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }
#endif

        [[nodiscard]] bool IsTransientRenameError(const std::error_code& error) noexcept
        {
            if (error == std::errc::permission_denied || error == std::errc::device_or_resource_busy)
                return true;
#if defined(_WIN32)
            if (error.category() == std::system_category())
            {
                return error.value() == ERROR_ACCESS_DENIED || error.value() == ERROR_SHARING_VIOLATION ||
                       error.value() == ERROR_LOCK_VIOLATION;
            }
#endif
            return false;
        }
    } // namespace

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    std::filesystem::path PathFromUtf8(const std::string_view value)
    {
        std::u8string utf8;
        utf8.reserve(value.size());
        for (const char character : value)
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
        return std::filesystem::path(utf8);
    }

    std::filesystem::path PathWithSuffix(const std::filesystem::path& path, const std::string_view suffix)
    {
        auto result = path;
        result += PathFromUtf8(suffix).native();
        return result;
    }

    bool IsTransientFile(const std::filesystem::path& path)
    {
        const auto filename = PathToUtf8(path.filename());
        if (filename.empty() || filename.ends_with('~') || path.extension() == ".tmp")
            return true;

        const auto marker = filename.rfind(".tmp.");
        if (marker == std::string::npos)
            return false;
        const auto uniqueSuffix = std::string_view(filename).substr(marker + 5);
        return !uniqueSuffix.empty() &&
               std::ranges::all_of(uniqueSuffix, [](const unsigned char character) { return std::isdigit(character); });
    }

    bool TryRenamePathWithRetry(const std::filesystem::path& source, const std::filesystem::path& destination,
                                std::error_code& error, const RenamePathOperation& operation,
                                const RenamePathDelay& delay)
    {
        if (source.empty() || destination.empty())
        {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        const auto rename = operation
                                ? operation
                                : RenamePathOperation{[](const auto& from, const auto& to, std::error_code& result)
                                                      {
#if defined(_WIN32)
                                                          RenameNativePath(from, to, result);
#else
                                                          std::filesystem::rename(from, to, result);
#endif
                                                      }};
        constexpr std::array delays{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                    std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                    std::chrono::milliseconds(160)};
        for (std::size_t attempt = 0; attempt < delays.size(); ++attempt)
        {
            error.clear();
            rename(source, destination, error);
            if (!error)
                return true;
            if (!IsTransientRenameError(error))
                return false;
            if (delay)
                delay(attempt, delays[attempt]);
            else
                std::this_thread::sleep_for(delays[attempt]);
        }
        return false;
    }

    void RenamePathWithRetry(const std::filesystem::path& source, const std::filesystem::path& destination,
                             const RenamePathOperation& operation, const RenamePathDelay& delay)
    {
        std::error_code error;
        if (TryRenamePathWithRetry(source, destination, error, operation, delay))
            return;
        const auto resolvedSource = std::filesystem::absolute(source).lexically_normal();
        const auto resolvedDestination = std::filesystem::absolute(destination).lexically_normal();
        throw std::runtime_error("Cannot rename '" + PathToUtf8(resolvedSource) + "' to '" +
                                 PathToUtf8(resolvedDestination) + "': " + error.message());
    }

    std::string ReadTextFile(const std::filesystem::path& path, const std::size_t maximumBytes)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            throw std::runtime_error("Cannot inspect file '" + PathToUtf8(path) + "': " + error.message());
        if (size > maximumBytes)
            throw std::runtime_error("File exceeds the supported size limit: " + PathToUtf8(path));

        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open file: " + PathToUtf8(path));
        std::string result{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
            throw std::runtime_error("Cannot read file: " + PathToUtf8(path));
        return result;
    }

    void PublishFileAtomically(const std::filesystem::path& temporary, const std::filesystem::path& destination)
    {
        if (temporary.empty() || destination.filename().empty())
            throw std::invalid_argument("Atomic publication requires source and destination filenames.");
        if (std::filesystem::absolute(temporary).root_name() != std::filesystem::absolute(destination).root_name())
            throw std::invalid_argument("Atomic publication requires source and destination on the same volume.");

        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("Cannot create directory '" + PathToUtf8(destination.parent_path()) +
                                     "': " + error.message());
        FlushFileToStorage(temporary);

#if defined(_WIN32)
        const auto target = ExtendedLengthPath(destination);
        const auto source = ExtendedLengthPath(temporary);
        DWORD lastError = ERROR_SUCCESS;
        constexpr std::size_t maximumAttempts = 6;
        for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            error.clear();
            const bool exists = std::filesystem::exists(destination, error) && !error;
            const BOOL replaced = exists ? ReplaceFileW(target.c_str(), source.c_str(), nullptr,
                                                        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
                                         : MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
            if (replaced)
                return;

            lastError = GetLastError();
            // ReplaceFileW can publish the replacement and still report that it could not remove the old file.
            if (FilesMatch(temporary, destination))
            {
                std::filesystem::remove(temporary, error);
                return;
            }
            const bool retryable = lastError == ERROR_ACCESS_DENIED || lastError == ERROR_SHARING_VIOLATION ||
                                   lastError == ERROR_LOCK_VIOLATION || lastError == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
                                   lastError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 ||
                                   lastError == ERROR_UNABLE_TO_REMOVE_REPLACED;
            if (!retryable || attempt + 1 == maximumAttempts || !std::filesystem::exists(temporary))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5U << attempt));
        }

        const auto diagnostic = std::error_code(static_cast<int>(lastError), std::system_category()).message();
        throw std::runtime_error("Cannot atomically replace '" + PathToUtf8(destination) + "': " + diagnostic);
#else
        std::filesystem::rename(temporary, destination, error);
        if (error)
            throw std::runtime_error("Cannot atomically replace '" + PathToUtf8(destination) + "': " + error.message());
        const auto directory = open(destination.parent_path().c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
        if (directory >= 0)
        {
            (void)fsync(directory);
            close(directory);
        }
#endif
    }

    void WriteFileAtomically(const std::filesystem::path& path, const std::span<const std::byte> contents)
    {
        if (path.filename().empty())
            throw std::invalid_argument("Atomic file writes require a filename.");

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            throw std::runtime_error("Cannot create directory '" + PathToUtf8(path.parent_path()) +
                                     "': " + error.message());

        const auto uniqueValue =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
            static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        auto temporary = path;
        temporary += ".tmp." + std::to_string(uniqueValue);
        try
        {
#if defined(_WIN32)
            std::ofstream output(std::filesystem::path(ExtendedLengthPath(temporary)),
                                 std::ios::binary | std::ios::trunc);
#else
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
#endif
            if (!output)
                throw std::runtime_error("Cannot create temporary file: " + PathToUtf8(temporary));
            output.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output)
                throw std::runtime_error("Cannot write temporary file: " + PathToUtf8(temporary));
            output.close();
            PublishFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }

    void WriteTextFileAtomically(const std::filesystem::path& path, const std::string_view contents)
    {
        WriteFileAtomically(path, std::as_bytes(std::span(contents.data(), contents.size())));
    }

    bool WriteFileAtomicallyIfChanged(const std::filesystem::path& path, const std::span<const std::byte> contents)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error &&
            std::filesystem::file_size(path, error) == contents.size() && !error)
        {
            std::ifstream stream(path, std::ios::binary);
            if (stream)
            {
                std::array<std::byte, 64U * 1024U> buffer{};
                std::size_t offset = 0;
                bool equal = true;
                while (offset < contents.size())
                {
                    const auto count = (std::min)(buffer.size(), contents.size() - offset);
                    stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                    if (stream.gcount() != static_cast<std::streamsize>(count) ||
                        !std::equal(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count),
                                    contents.begin() + static_cast<std::ptrdiff_t>(offset)))
                    {
                        equal = false;
                        break;
                    }
                    offset += count;
                }
                if (equal)
                    return false;
            }
        }
        WriteFileAtomically(path, contents);
        return true;
    }

    bool WriteTextFileAtomicallyIfChanged(const std::filesystem::path& path, const std::string_view contents)
    {
        return WriteFileAtomicallyIfChanged(path, std::as_bytes(std::span(contents.data(), contents.size())));
    }

    std::filesystem::path CanonicalExistingPath(const std::filesystem::path& path)
    {
        if (path.empty())
            throw std::invalid_argument("Path must not be empty.");
        std::error_code error;
        auto result = std::filesystem::canonical(path, error);
        if (error)
            throw std::invalid_argument("Path does not exist or cannot be resolved: " + PathToUtf8(path));
        return result.lexically_normal();
    }

    std::filesystem::path ResolveConfinedPath(const std::filesystem::path& root, const std::filesystem::path& relative)
    {
        const auto normalized = relative.lexically_normal();
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
            normalized.empty() || normalized == ".." || *normalized.begin() == "..")
        {
            throw std::invalid_argument("Path must be a confined relative path: " + PathToUtf8(relative));
        }

        const auto canonicalRoot = CanonicalExistingPath(root);
        auto candidate = canonicalRoot;
        for (const auto& component : normalized)
        {
            candidate /= component;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(candidate, error);
            if (error)
            {
                if (error == std::errc::no_such_file_or_directory)
                    continue;
                throw std::invalid_argument("Confined path cannot be inspected: " + PathToUtf8(candidate));
            }
            if (std::filesystem::is_symlink(status))
                throw std::invalid_argument("Confined paths may not traverse symbolic links.");
#if defined(_WIN32)
            if (status.type() != std::filesystem::file_type::not_found)
            {
                const auto attributes = GetFileAttributesW(ExtendedLengthPath(candidate).c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                    throw std::invalid_argument("Confined path cannot be inspected: " + PathToUtf8(candidate));
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    throw std::invalid_argument("Confined paths may not traverse reparse points.");
            }
#endif
        }

        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(candidate, error);
        if (error)
            throw std::invalid_argument("Confined path cannot be resolved: " + PathToUtf8(candidate));
        const auto mismatch = std::ranges::mismatch(canonicalRoot, resolved);
        if (mismatch.in1 != canonicalRoot.end())
            throw std::invalid_argument("Path escapes its configured root.");
        return resolved.lexically_normal();
    }
} // namespace Keire::Detail
