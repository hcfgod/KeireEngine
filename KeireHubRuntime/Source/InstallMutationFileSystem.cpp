#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>

#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>
#endif

namespace KeireHub::Detail
{
    namespace
    {
        [[nodiscard]] HubError MutationError(const HubErrorCode code, std::string message,
                                             const std::filesystem::path& path = {}, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = PathToUtf8(path),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::vector<std::filesystem::path> Components(const std::filesystem::path& relative)
        {
            if (!IsSafeRelativePath(relative))
                throw std::invalid_argument("Install mutation paths must be safe relative paths.");
            std::vector<std::filesystem::path> result;
            for (const auto& component : relative)
            {
                if (!component.empty() && component != ".")
                    result.push_back(component);
            }
            if (result.empty())
                throw std::invalid_argument("Install mutation paths must name an entry below the anchored root.");
            return result;
        }

#if defined(KEIRE_INSTALL_TRANSACTION_TESTING)
        std::atomic<InstallMutationHook> s_MutationHook = nullptr;

        void InvokeMutationHook(const std::string_view operation, const std::filesystem::path& relative)
        {
            if (const auto hook = s_MutationHook.load(std::memory_order_acquire))
                hook(operation, relative);
        }
#else
        void InvokeMutationHook(std::string_view, const std::filesystem::path&) {}
#endif
    } // namespace

    class InstallMutationFileSystem::Impl final
    {
      public:
        explicit Impl(const std::filesystem::path& root, const bool createIfMissing, const bool requireNew)
            : m_Root(std::filesystem::absolute(root).lexically_normal())
        {
            if (m_Root.empty() || m_Root.filename().empty())
                throw std::invalid_argument("An install mutation root must name an ordinary directory.");
#if defined(_WIN32)
            const auto access = RootAccess();
            std::error_code error;
            const auto status = std::filesystem::symlink_status(m_Root, error);
            const bool missing =
                error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
            const auto parentPath = m_Root.parent_path();
            m_ParentHandle = Handle(CreateFileW(parentPath.c_str(), ParentAccess(),
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                OPEN_EXISTING,
                                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!m_ParentHandle)
                ThrowWindows("Cannot anchor install mutation root parent");
            RejectReparsePoint(m_ParentHandle.Get());
            RejectNonDirectory(m_ParentHandle.Get());
            if (!missing)
            {
                if (requireNew)
                    throw std::invalid_argument("A new install mutation root collided with an existing path.");
                if (error)
                    throw std::system_error(error, "Cannot inspect install mutation root");
                m_RootHandle =
                    OpenRelative(m_ParentHandle.Get(), m_Root.filename(), access, NtOpenExisting, NtFileDirectory);
                RejectReparsePoint(m_RootHandle.Get());
                RejectNonDirectory(m_RootHandle.Get());
                return;
            }
            if (!createIfMissing)
                ThrowWindows("Install mutation root does not exist", ERROR_PATH_NOT_FOUND);

            m_RootHandle = OpenRelative(m_ParentHandle.Get(), m_Root.filename(), RootAccess(),
                                        requireNew ? NtCreateNew : NtOpenOrCreate, NtFileDirectory,
                                        FILE_ATTRIBUTE_DIRECTORY);
#else
            std::error_code error;
            if (createIfMissing)
            {
                const bool created = std::filesystem::create_directory(m_Root, error);
                if (requireNew && !error && !created)
                    throw std::invalid_argument("A new install mutation root collided with an existing path.");
            }
            if (error)
                throw std::system_error(error, "Cannot create install mutation root");
            const auto status = std::filesystem::symlink_status(m_Root, error);
            if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
                throw std::invalid_argument("Install mutation roots must be ordinary directories.");
#endif
        }

        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_Root; }

#if defined(_WIN32)
        class Handle final
        {
          public:
            explicit Handle(const HANDLE value = INVALID_HANDLE_VALUE) noexcept : m_Value(value) {}
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
            [[nodiscard]] explicit operator bool() const noexcept { return m_Value != INVALID_HANDLE_VALUE; }

          private:
            HANDLE m_Value = INVALID_HANDLE_VALUE;
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

        [[nodiscard]] static ACCESS_MASK RootAccess() noexcept
        {
            return FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE;
        }

        [[nodiscard]] static ACCESS_MASK ParentAccess() noexcept
        {
            return FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | FILE_ADD_SUBDIRECTORY | SYNCHRONIZE;
        }

        [[noreturn]] static void ThrowWindows(const std::string_view message, const DWORD error = GetLastError())
        {
            throw std::system_error(static_cast<int>(error), std::system_category(), std::string(message));
        }

        [[nodiscard]] static NtCreateFileFunction NtCreate()
        {
            static const auto function =
                reinterpret_cast<NtCreateFileFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
            if (!function)
                throw std::runtime_error("NtCreateFile is unavailable; install mutations are unsafe.");
            return function;
        }

        [[nodiscard]] static NtSetInformationFileFunction NtSetInformation()
        {
            static const auto function = reinterpret_cast<NtSetInformationFileFunction>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
            if (!function)
                throw std::runtime_error("NtSetInformationFile is unavailable; install mutations are unsafe.");
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
                ThrowWindows("Cannot inspect anchored install entry");
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                throw std::invalid_argument("Install mutations may not traverse reparse points.");
        }

        static void RejectNonDirectory(const HANDLE handle)
        {
            FILE_STANDARD_INFO information{};
            if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &information, sizeof(information)))
                ThrowWindows("Cannot inspect anchored install directory");
            if (!information.Directory)
                throw std::invalid_argument("Install mutation roots and parents must be directories.");
        }

        [[nodiscard]] static Handle OpenRelative(const HANDLE parent, const std::filesystem::path& component,
                                                 const ACCESS_MASK access, const ULONG disposition, const ULONG options,
                                                 const ULONG attributes = FILE_ATTRIBUTE_NORMAL)
        {
            const auto name = component.native();
            if (name.empty() || name.find_first_of(L"\\/") != std::wstring::npos ||
                name.size() > (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t))
            {
                throw std::invalid_argument("Anchored install components must contain one filename.");
            }
            UNICODE_STRING unicode{};
            unicode.Buffer = const_cast<PWSTR>(name.data());
            unicode.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
            unicode.MaximumLength = unicode.Length;
            OBJECT_ATTRIBUTES object{};
            InitializeObjectAttributes(&object, &unicode, OBJ_CASE_INSENSITIVE, parent, nullptr);
            IO_STATUS_BLOCK statusBlock{};
            HANDLE value = INVALID_HANDLE_VALUE;
            const auto status = NtCreate()(&value, access, &object, &statusBlock, nullptr, attributes,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
                                           options | NtFileSynchronousIoNonAlert | NtFileOpenReparsePoint, nullptr, 0);
            if (status < 0)
                ThrowWindows("Cannot open anchored install entry", DosError(status));
            Handle result(value);
            RejectReparsePoint(value);
            return result;
        }

        [[nodiscard]] Handle DuplicateRoot() const
        {
            HANDLE duplicate = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), m_RootHandle.Get(), GetCurrentProcess(), &duplicate, 0, FALSE,
                                 DUPLICATE_SAME_ACCESS))
            {
                ThrowWindows("Cannot duplicate anchored install root");
            }
            return Handle(duplicate);
        }

        [[nodiscard]] Handle OpenParent(const std::vector<std::filesystem::path>& components,
                                        const bool createDirectories) const
        {
            auto current = DuplicateRoot();
            for (std::size_t index = 0; index + 1 < components.size(); ++index)
            {
                current = OpenRelative(current.Get(), components[index], RootAccess(),
                                       createDirectories ? NtOpenOrCreate : NtOpenExisting, NtFileDirectory,
                                       FILE_ATTRIBUTE_DIRECTORY);
                RejectNonDirectory(current.Get());
            }
            return current;
        }

        [[nodiscard]] Handle OpenFile(const std::filesystem::path& relative, const ACCESS_MASK access) const
        {
            const auto components = Components(relative);
            auto parent = OpenParent(components, false);
            return OpenRelative(parent.Get(), components.back(), access | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                NtOpenExisting, NtFileNonDirectory);
        }

        static void RenameRelative(const HANDLE file, const HANDLE destinationParent,
                                   const std::filesystem::path& destinationName, const bool replaceExisting)
        {
            const auto name = destinationName.native();
            const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + name.size() * sizeof(wchar_t);
            std::vector<std::byte> storage(bytes);
            auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            rename->ReplaceIfExists = replaceExisting ? TRUE : FALSE;
            rename->RootDirectory = destinationParent;
            rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            std::memcpy(rename->FileName, name.data(), rename->FileNameLength);
            IO_STATUS_BLOCK statusBlock{};
            constexpr auto renameInformation = static_cast<FILE_INFORMATION_CLASS>(10);
            const auto status = NtSetInformation()(file, &statusBlock, rename, static_cast<ULONG>(storage.size()),
                                                   renameInformation);
            if (status < 0)
                ThrowWindows("Cannot rename anchored install file", DosError(status));
        }

        [[nodiscard]] static InstallOwnedFile DescribeHandle(const HANDLE handle, const std::filesystem::path& relative)
        {
            FILE_STANDARD_INFO information{};
            if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &information, sizeof(information)))
                ThrowWindows("Cannot inspect anchored install file");
            if (information.Directory || information.EndOfFile.QuadPart < 0 ||
                static_cast<std::uint64_t>(information.EndOfFile.QuadPart) > MaximumInstallFileBytes)
            {
                throw std::invalid_argument("An anchored install file has an invalid size or type.");
            }
            LARGE_INTEGER beginning{};
            if (!SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN))
                ThrowWindows("Cannot seek anchored install file");
            Sha256Builder builder;
            std::array<std::byte, std::size_t{64U} * 1024U> buffer{};
            std::uint64_t total = 0;
            while (true)
            {
                DWORD read = 0;
                if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
                    ThrowWindows("Cannot read anchored install file");
                if (read == 0)
                    break;
                total += read;
                builder.Update(std::span(buffer).first(read));
            }
            if (total != static_cast<std::uint64_t>(information.EndOfFile.QuadPart))
                throw std::runtime_error("An anchored install file changed while it was being read.");
            return {.Path = relative, .SizeBytes = total, .Sha256 = DigestToString(builder.Finish())};
        }

        static void VerifyExpected(const InstallOwnedFile& expected, const InstallOwnedFile& actual)
        {
            if (actual.SizeBytes != expected.SizeBytes || actual.Sha256 != expected.Sha256)
                throw std::runtime_error("An install file changed before its anchored mutation.");
        }

        [[nodiscard]] HubResult<InstallOwnedFile> Describe(const std::filesystem::path& relative,
                                                           const bool allowMissing) const
        {
            try
            {
                auto file = OpenFile(relative, FILE_READ_DATA);
                return HubResult<InstallOwnedFile>::Success(DescribeHandle(file.Get(), relative));
            }
            catch (const std::system_error& error)
            {
                if (allowMissing &&
                    (error.code().value() == ERROR_FILE_NOT_FOUND || error.code().value() == ERROR_PATH_NOT_FOUND))
                {
                    return HubResult<InstallOwnedFile>::Success({.Path = relative});
                }
                return HubResult<InstallOwnedFile>::Failure(MutationError(
                    HubErrorCode::IoRead, "An anchored install file could not be inspected.", relative, error.what()));
            }
            catch (const std::exception& error)
            {
                return HubResult<InstallOwnedFile>::Failure(MutationError(
                    HubErrorCode::InvalidData, "An anchored install file is unsafe.", relative, error.what()));
            }
        }

        [[nodiscard]] HubResult<std::string> ReadText(const std::filesystem::path& relative,
                                                      const std::size_t maximumBytes) const
        {
            try
            {
                auto file = OpenFile(relative, FILE_READ_DATA);
                FILE_STANDARD_INFO information{};
                if (!GetFileInformationByHandleEx(file.Get(), FileStandardInfo, &information, sizeof(information)))
                    ThrowWindows("Cannot inspect anchored install document");
                if (information.Directory || information.EndOfFile.QuadPart < 0 ||
                    static_cast<std::uint64_t>(information.EndOfFile.QuadPart) > maximumBytes)
                {
                    throw std::invalid_argument("An anchored install document exceeds its size limit.");
                }
                std::string result(static_cast<std::size_t>(information.EndOfFile.QuadPart), '\0');
                LARGE_INTEGER beginning{};
                if (!SetFilePointerEx(file.Get(), beginning, nullptr, FILE_BEGIN))
                    ThrowWindows("Cannot seek anchored install document");
                std::size_t offset = 0;
                while (offset != result.size())
                {
                    DWORD read = 0;
                    const auto remaining = (std::min)(result.size() - offset,
                                                      static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()));
                    if (!ReadFile(file.Get(), result.data() + offset, static_cast<DWORD>(remaining), &read, nullptr) ||
                        read == 0)
                    {
                        ThrowWindows("Cannot read anchored install document");
                    }
                    offset += read;
                }
                char trailing = '\0';
                DWORD read = 0;
                if (!ReadFile(file.Get(), &trailing, 1, &read, nullptr))
                    ThrowWindows("Cannot finish reading anchored install document");
                if (read != 0)
                    throw std::runtime_error("An anchored install document changed while it was being read.");
                return HubResult<std::string>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::string>::Failure(MutationError(
                    HubErrorCode::IoRead, "An anchored install document could not be read.", relative, error.what()));
            }
        }

        [[nodiscard]] HubStatus WriteTextAtomically(const std::filesystem::path& relative,
                                                    const std::string_view text, const bool replaceExisting) const
        {
            Handle file;
            try
            {
                const auto components = Components(relative);
                auto parent = OpenParent(components, true);
                const auto unique = static_cast<std::uint64_t>(
                                        std::chrono::steady_clock::now().time_since_epoch().count()) ^
                                    static_cast<std::uint64_t>(
                                        std::hash<std::thread::id>{}(std::this_thread::get_id()));
                auto temporary = components.back();
                temporary += ".tmp." + std::to_string(unique);
                file = OpenRelative(parent.Get(), temporary,
                                    FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | DELETE |
                                        SYNCHRONIZE,
                                    NtCreateNew, NtFileNonDirectory);
                InvokeMutationHook("write", relative);
                std::size_t offset = 0;
                while (offset != text.size())
                {
                    DWORD written = 0;
                    const auto remaining =
                        (std::min)(text.size() - offset, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()));
                    if (!WriteFile(file.Get(), text.data() + offset, static_cast<DWORD>(remaining), &written, nullptr) ||
                        written == 0)
                    {
                        ThrowWindows("Cannot write anchored install document");
                    }
                    offset += written;
                }
                if (!FlushFileBuffers(file.Get()))
                    ThrowWindows("Cannot flush anchored install document");
                RenameRelative(file.Get(), parent.Get(), components.back(), replaceExisting);
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                if (file)
                {
                    FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                    (void)SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition,
                                                     sizeof(disposition));
                }
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An anchored install document could not be published.",
                                                        relative, error.what()));
            }
        }

        [[nodiscard]] HubStatus CreateDirectories(const std::filesystem::path& relative) const
        {
            try
            {
                auto current = DuplicateRoot();
                for (const auto& component : Components(relative))
                {
                    current = OpenRelative(current.Get(), component, RootAccess(), NtOpenOrCreate, NtFileDirectory,
                                           FILE_ATTRIBUTE_DIRECTORY);
                    RejectNonDirectory(current.Get());
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An anchored install directory could not be created.", relative,
                                                        error.what()));
            }
        }

        [[nodiscard]] HubStatus CopyVerifiedTo(const InstallOwnedFile& expected, const Impl& destination) const
        {
            Handle output;
            std::string_view operation = "opening the anchored source";
            try
            {
                auto source = OpenFile(expected.Path, FILE_READ_DATA);
                operation = "verifying the anchored source";
                VerifyExpected(expected, DescribeHandle(source.Get(), expected.Path));
                const auto components = Components(expected.Path);
                operation = "creating anchored destination parents";
                if (const auto status = destination.CreateDirectories(expected.Path.parent_path());
                    !expected.Path.parent_path().empty() && !status)
                {
                    return status;
                }
                operation = "opening the anchored destination parent";
                auto destinationParent = destination.OpenParent(components, true);
                operation = "creating the anchored destination file";
                output = OpenRelative(destinationParent.Get(), components.back(),
                                      FILE_WRITE_DATA | FILE_READ_DATA | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES |
                                          DELETE | SYNCHRONIZE,
                                      NtCreateNew, NtFileNonDirectory);
                InvokeMutationHook("copy", expected.Path);
                operation = "copying anchored file bytes";
                LARGE_INTEGER beginning{};
                if (!SetFilePointerEx(source.Get(), beginning, nullptr, FILE_BEGIN))
                    ThrowWindows("Cannot seek anchored install source");
                std::array<std::byte, std::size_t{64U} * 1024U> buffer{};
                while (true)
                {
                    DWORD read = 0;
                    if (!ReadFile(source.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
                        ThrowWindows("Cannot read anchored install source");
                    if (read == 0)
                        break;
                    DWORD written = 0;
                    if (!WriteFile(output.Get(), buffer.data(), read, &written, nullptr) || written != read)
                        ThrowWindows("Cannot write anchored install destination");
                }
                if (!FlushFileBuffers(output.Get()))
                    ThrowWindows("Cannot flush anchored install destination");
                VerifyExpected(expected, DescribeHandle(output.Get(), expected.Path));
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                if (output)
                {
                    FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                    (void)SetFileInformationByHandle(output.Get(), FileDispositionInfo, &disposition,
                                                     sizeof(disposition));
                }
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An anchored install file could not be copied.", expected.Path,
                                                        std::string(operation) + ": " + error.what()));
            }
        }

        [[nodiscard]] HubStatus RenameVerifiedTo(const InstallOwnedFile& expected, const Impl& destination,
                                                 const bool allowMissing,
                                                 const std::filesystem::path& destinationPath) const
        {
            try
            {
                Handle source;
                try
                {
                    source = OpenFile(expected.Path, FILE_READ_DATA | DELETE);
                }
                catch (const std::system_error& error)
                {
                    if (allowMissing &&
                        (error.code().value() == ERROR_FILE_NOT_FOUND || error.code().value() == ERROR_PATH_NOT_FOUND))
                    {
                        return HubStatus::Success();
                    }
                    throw;
                }
                VerifyExpected(expected, DescribeHandle(source.Get(), expected.Path));
                const auto targetPath = destinationPath.empty() ? expected.Path : destinationPath;
                const auto components = Components(targetPath);
                if (!targetPath.parent_path().empty())
                {
                    if (const auto status = destination.CreateDirectories(targetPath.parent_path()); !status)
                        return status;
                }
                auto destinationParent = destination.OpenParent(components, true);
                InvokeMutationHook("rename", expected.Path);
                RenameRelative(source.Get(), destinationParent.Get(), components.back(), false);
                VerifyExpected(expected, DescribeHandle(source.Get(), expected.Path));
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An anchored install file could not be moved.", expected.Path,
                                                        error.what()));
            }
        }

        [[nodiscard]] HubStatus RemoveVerified(const InstallOwnedFile& expected) const
        {
            try
            {
                auto file = OpenFile(expected.Path, FILE_READ_DATA | DELETE);
                VerifyExpected(expected, DescribeHandle(file.Get(), expected.Path));
                InvokeMutationHook("remove", expected.Path);
                FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                if (!SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition, sizeof(disposition)))
                    ThrowWindows("Cannot remove anchored install file");
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An anchored install file could not be removed.", expected.Path,
                                                        error.what()));
            }
        }

        [[nodiscard]] HubStatus RemoveEmptyDirectory(const std::filesystem::path& relative) const
        {
            try
            {
                const auto components = Components(relative);
                auto parent = OpenParent(components, false);
                auto directory =
                    OpenRelative(parent.Get(), components.back(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 NtOpenExisting, NtFileDirectory);
                RejectNonDirectory(directory.Get());
                InvokeMutationHook("remove-directory", relative);
                FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                if (!SetFileInformationByHandle(directory.Get(), FileDispositionInfo, &disposition,
                                                sizeof(disposition)))
                    ThrowWindows("Cannot remove anchored install directory");
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(MutationError(HubErrorCode::DestinationConflict,
                                                        "A changed or non-empty install directory was preserved.",
                                                        relative, error.what()));
            }
        }

        [[nodiscard]] HubStatus RemoveRootIfEmpty() const
        {
            try
            {
                InvokeMutationHook("remove-root", {});
                RejectReparsePoint(m_RootHandle.Get());
                RejectNonDirectory(m_RootHandle.Get());
                FILE_DISPOSITION_INFO disposition{.DeleteFile = TRUE};
                if (!SetFileInformationByHandle(m_RootHandle.Get(), FileDispositionInfo, &disposition,
                                                sizeof(disposition)))
                {
                    ThrowWindows("Cannot remove anchored install root");
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(MutationError(HubErrorCode::DestinationConflict,
                                                        "A changed or non-empty install root was preserved.", m_Root,
                                                        error.what()));
            }
        }
#else
        [[nodiscard]] HubResult<InstallOwnedFile> Describe(const std::filesystem::path& relative,
                                                           const bool allowMissing) const
        {
            const auto path = m_Root / relative;
            std::error_code error;
            if (!std::filesystem::exists(path, error))
                return allowMissing ? HubResult<InstallOwnedFile>::Success({.Path = relative})
                                    : HubResult<InstallOwnedFile>::Failure(MutationError(
                                          HubErrorCode::NotFound, "An install file is missing.", relative));
            const auto size = std::filesystem::file_size(path, error);
            auto digest = Sha256File(path, MaximumInstallFileBytes);
            if (error || !digest)
                return HubResult<InstallOwnedFile>::Failure(MutationError(
                    HubErrorCode::IoRead, "An install file could not be inspected.", relative, error.message()));
            return HubResult<InstallOwnedFile>::Success(
                {.Path = relative, .SizeBytes = size, .Sha256 = std::move(digest).Value()});
        }

        [[nodiscard]] HubStatus CreateDirectories(const std::filesystem::path& relative) const
        {
            std::error_code error;
            std::filesystem::create_directories(m_Root / relative, error);
            return error ? HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                            "An install directory could not be created.", relative,
                                                            error.message()))
                         : HubStatus::Success();
        }

        [[nodiscard]] HubResult<std::string> ReadText(const std::filesystem::path& relative,
                                                      const std::size_t maximumBytes) const
        {
            const auto path = m_Root / relative;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
            {
                return HubResult<std::string>::Failure(MutationError(
                    HubErrorCode::IoRead, "An install document could not be read safely.", relative, error.message()));
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > maximumBytes)
            {
                return HubResult<std::string>::Failure(MutationError(
                    HubErrorCode::IoRead, "An install document exceeds its size limit.", relative, error.message()));
            }
            std::ifstream input(path, std::ios::binary);
            std::string result{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            return input.bad() ? HubResult<std::string>::Failure(MutationError(
                                     HubErrorCode::IoRead, "An install document could not be read.", relative))
                               : HubResult<std::string>::Success(std::move(result));
        }

        [[nodiscard]] HubStatus WriteTextAtomically(const std::filesystem::path& relative,
                                                    const std::string_view text, const bool replaceExisting) const
        {
            if (!relative.parent_path().empty())
                if (const auto created = CreateDirectories(relative.parent_path()); !created)
                    return created;
            auto temporary = m_Root / relative;
            temporary += ".tmp." + std::to_string(
                                       std::chrono::steady_clock::now().time_since_epoch().count());
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            if (!output)
                return HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                        "An install document could not be staged.", relative));
            std::error_code error;
            if (!replaceExisting && std::filesystem::exists(m_Root / relative, error))
            {
                std::filesystem::remove(temporary, error);
                return HubStatus::Failure(MutationError(HubErrorCode::DestinationConflict,
                                                        "An install document already exists.", relative));
            }
            std::filesystem::rename(temporary, m_Root / relative, error);
            return error ? HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                            "An install document could not be published.", relative,
                                                            error.message()))
                         : HubStatus::Success();
        }

        [[nodiscard]] HubStatus CopyVerifiedTo(const InstallOwnedFile& file, const Impl& destination) const
        {
            auto actual = Describe(file.Path, false);
            if (!actual || actual.Value().SizeBytes != file.SizeBytes || actual.Value().Sha256 != file.Sha256)
                return actual ? HubStatus::Failure(MutationError(HubErrorCode::InvalidData,
                                                                 "An install file changed before copy.", file.Path))
                              : HubStatus::Failure(actual.Error());
            if (!file.Path.parent_path().empty())
                if (const auto created = destination.CreateDirectories(file.Path.parent_path()); !created)
                    return created;
            std::error_code error;
            std::filesystem::copy_file(m_Root / file.Path, destination.m_Root / file.Path,
                                       std::filesystem::copy_options::none, error);
            return error
                       ? HubStatus::Failure(MutationError(HubErrorCode::IoWrite, "An install file could not be copied.",
                                                          file.Path, error.message()))
                       : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RenameVerifiedTo(const InstallOwnedFile& file, const Impl& destination,
                                                 const bool allowMissing,
                                                 const std::filesystem::path& destinationPath) const
        {
            auto actual = Describe(file.Path, allowMissing);
            if (!actual)
                return HubStatus::Failure(actual.Error());
            if (actual.Value().Sha256.empty())
                return HubStatus::Success();
            if (actual.Value().SizeBytes != file.SizeBytes || actual.Value().Sha256 != file.Sha256)
                return HubStatus::Failure(
                    MutationError(HubErrorCode::InvalidData, "An install file changed before move.", file.Path));
            const auto targetPath = destinationPath.empty() ? file.Path : destinationPath;
            if (!targetPath.parent_path().empty())
                if (const auto created = destination.CreateDirectories(targetPath.parent_path()); !created)
                    return created;
            std::error_code error;
            std::filesystem::rename(m_Root / file.Path, destination.m_Root / targetPath, error);
            return error
                       ? HubStatus::Failure(MutationError(HubErrorCode::IoWrite, "An install file could not be moved.",
                                                          file.Path, error.message()))
                       : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveVerified(const InstallOwnedFile& file) const
        {
            auto actual = Describe(file.Path, false);
            if (!actual || actual.Value().SizeBytes != file.SizeBytes || actual.Value().Sha256 != file.Sha256)
                return actual ? HubStatus::Failure(MutationError(HubErrorCode::InvalidData,
                                                                 "An install file changed before removal.", file.Path))
                              : HubStatus::Failure(actual.Error());
            std::error_code error;
            std::filesystem::remove(m_Root / file.Path, error);
            return error ? HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                            "An install file could not be removed.", file.Path,
                                                            error.message()))
                         : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveEmptyDirectory(const std::filesystem::path& relative) const
        {
            std::error_code error;
            const bool removed = std::filesystem::remove(m_Root / relative, error);
            return !removed || error
                       ? HubStatus::Failure(MutationError(HubErrorCode::DestinationConflict,
                                                          "A changed or non-empty install directory was preserved.",
                                                          relative, error.message()))
                       : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveRootIfEmpty() const
        {
            std::error_code error;
            const bool removed = std::filesystem::remove(m_Root, error);
            return !removed || error
                       ? HubStatus::Failure(MutationError(HubErrorCode::DestinationConflict,
                                                          "A changed or non-empty install root was preserved.", m_Root,
                                                          error.message()))
                       : HubStatus::Success();
        }
#endif

      private:
        std::filesystem::path m_Root;
#if defined(_WIN32)
        Handle m_ParentHandle;
        Handle m_RootHandle;
#endif
    };

    InstallMutationFileSystem::InstallMutationFileSystem(const std::filesystem::path& root, const bool createIfMissing,
                                                         const bool requireNew)
        : m_Impl(std::make_unique<Impl>(root, createIfMissing, requireNew))
    {
    }

    InstallMutationFileSystem::~InstallMutationFileSystem() = default;
    InstallMutationFileSystem::InstallMutationFileSystem(InstallMutationFileSystem&&) noexcept = default;
    InstallMutationFileSystem& InstallMutationFileSystem::operator=(InstallMutationFileSystem&&) noexcept = default;

    const std::filesystem::path& InstallMutationFileSystem::Root() const noexcept { return m_Impl->Root(); }

    HubResult<InstallOwnedFile> InstallMutationFileSystem::Describe(const std::filesystem::path& relative,
                                                                    const bool allowMissing) const
    {
        return m_Impl->Describe(relative, allowMissing);
    }

    HubResult<std::string> InstallMutationFileSystem::ReadText(const std::filesystem::path& relative,
                                                               const std::size_t maximumBytes) const
    {
        return m_Impl->ReadText(relative, maximumBytes);
    }

    HubStatus InstallMutationFileSystem::WriteTextAtomically(const std::filesystem::path& relative,
                                                             const std::string_view text,
                                                             const bool replaceExisting) const
    {
        return m_Impl->WriteTextAtomically(relative, text, replaceExisting);
    }

    HubStatus InstallMutationFileSystem::CreateDirectories(const std::filesystem::path& relative) const
    {
        if (relative.empty() || relative == ".")
            return HubStatus::Success();
        return m_Impl->CreateDirectories(relative);
    }

    HubStatus InstallMutationFileSystem::CopyVerifiedTo(const InstallOwnedFile& file,
                                                        const InstallMutationFileSystem& destination) const
    {
        return m_Impl->CopyVerifiedTo(file, *destination.m_Impl);
    }

    HubStatus InstallMutationFileSystem::RenameVerifiedTo(const InstallOwnedFile& file,
                                                          const InstallMutationFileSystem& destination,
                                                          const bool allowMissing,
                                                          const std::filesystem::path& destinationPath) const
    {
        return m_Impl->RenameVerifiedTo(file, *destination.m_Impl, allowMissing, destinationPath);
    }

    HubStatus InstallMutationFileSystem::RemoveVerified(const InstallOwnedFile& file) const
    {
        return m_Impl->RemoveVerified(file);
    }

    HubStatus InstallMutationFileSystem::RemoveEmptyDirectory(const std::filesystem::path& relative) const
    {
        return m_Impl->RemoveEmptyDirectory(relative);
    }

    HubStatus InstallMutationFileSystem::RemoveRootIfEmpty() const { return m_Impl->RemoveRootIfEmpty(); }

    HubResult<std::shared_ptr<InstallMutationFileSystem>>
    InstallMutationAuthority::Pin(const std::filesystem::path& root, const bool createIfMissing, const bool requireNew)
    {
        auto absolute = std::filesystem::absolute(root).lexically_normal();
        auto key = PathToUtf8(absolute);
#if defined(_WIN32)
        std::ranges::transform(key, key.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
        if (const auto iterator = m_Roots.find(key); iterator != m_Roots.end())
        {
            if (requireNew)
            {
                return HubResult<std::shared_ptr<InstallMutationFileSystem>>::Failure(MutationError(
                    HubErrorCode::DestinationConflict, "A new install mutation root was already pinned.", root));
            }
            return HubResult<std::shared_ptr<InstallMutationFileSystem>>::Success(iterator->second);
        }
        try
        {
            auto fileSystem = std::make_shared<InstallMutationFileSystem>(absolute, createIfMissing, requireNew);
            m_Roots.emplace(std::move(key), fileSystem);
            return HubResult<std::shared_ptr<InstallMutationFileSystem>>::Success(std::move(fileSystem));
        }
        catch (const std::exception& error)
        {
            return HubResult<std::shared_ptr<InstallMutationFileSystem>>::Failure(
                MutationError(HubErrorCode::UnsafeInstallRoot, "An install mutation root could not be anchored.",
                              absolute, error.what()));
        }
    }

    void InstallMutationAuthority::Unpin(const std::filesystem::path& root) noexcept
    {
        try
        {
            auto key = PathToUtf8(std::filesystem::absolute(root).lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
            m_Roots.erase(key);
        }
        catch (...)
        {
        }
    }

#if defined(KEIRE_INSTALL_TRANSACTION_TESTING)
    void SetInstallMutationHookForTesting(const InstallMutationHook hook) noexcept
    {
        s_MutationHook.store(hook, std::memory_order_release);
    }
#endif
} // namespace KeireHub::Detail
