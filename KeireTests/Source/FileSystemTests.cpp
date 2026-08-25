#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <winioctl.h>
#endif

namespace
{
    class TestDirectoryLink final
    {
      public:
        static std::unique_ptr<TestDirectoryLink> Create(const std::filesystem::path& target,
                                                         const std::filesystem::path& link)
        {
#if defined(_WIN32)
            std::filesystem::create_directory(link);
            const auto handle =
                CreateFileW(link.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Cannot open junction directory");
            const auto substitute = std::wstring(L"\\??\\") + std::filesystem::absolute(target).native();
            const auto print = std::filesystem::absolute(target).native();
            constexpr std::size_t maximumPathCharacters = 32U * 1024U;
            struct MountPointBuffer final
            {
                DWORD Tag = IO_REPARSE_TAG_MOUNT_POINT;
                WORD DataLength = 0;
                WORD Reserved = 0;
                WORD SubstituteOffset = 0;
                WORD SubstituteLength = 0;
                WORD PrintOffset = 0;
                WORD PrintLength = 0;
                std::array<wchar_t, maximumPathCharacters> Path{};
            } buffer;
            buffer.SubstituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
            buffer.PrintOffset = static_cast<WORD>(buffer.SubstituteLength + sizeof(wchar_t));
            buffer.PrintLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
            buffer.DataLength = static_cast<WORD>(8U + buffer.PrintOffset + buffer.PrintLength + sizeof(wchar_t));
            std::memcpy(buffer.Path.data(), substitute.data(), buffer.SubstituteLength);
            std::memcpy(reinterpret_cast<std::byte*>(buffer.Path.data()) + buffer.PrintOffset, print.data(),
                        buffer.PrintLength);
            DWORD returned = 0;
            const auto changed =
                DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, static_cast<DWORD>(buffer.DataLength + 8U),
                                nullptr, 0, &returned, nullptr);
            const auto error = changed ? ERROR_SUCCESS : GetLastError();
            if (!changed)
            {
                CloseHandle(handle);
                std::filesystem::remove(link);
                throw std::system_error(static_cast<int>(error), std::system_category(), "Cannot create test junction");
            }
            return std::unique_ptr<TestDirectoryLink>(new TestDirectoryLink(link, handle));
#else
            std::filesystem::create_directory_symlink(target, link);
            return std::unique_ptr<TestDirectoryLink>(new TestDirectoryLink(link));
#endif
        }

        ~TestDirectoryLink() { RemoveNoThrow(); }

        TestDirectoryLink(const TestDirectoryLink&) = delete;
        TestDirectoryLink& operator=(const TestDirectoryLink&) = delete;

        void Remove()
        {
            if (!m_Active)
                throw std::logic_error("Test directory link was already removed.");
#if defined(_WIN32)
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(m_Handle, FileAttributeTagInfo, &attributes, sizeof(attributes)))
                ThrowAndClose(GetLastError(), "Cannot query test junction reparse tag");
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 ||
                attributes.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
                ThrowAndClose(ERROR_REPARSE_TAG_MISMATCH, "Retained test handle is not a mount-point junction");
            struct ReparseHeader final
            {
                DWORD Tag = IO_REPARSE_TAG_MOUNT_POINT;
                WORD DataLength = 0;
                WORD Reserved = 0;
            } header;
            DWORD returned = 0;
            if (!DeviceIoControl(m_Handle, FSCTL_DELETE_REPARSE_POINT, &header, sizeof(header), nullptr, 0, &returned,
                                 nullptr))
                ThrowAndClose(GetLastError(), "Cannot delete test junction reparse point");
            CloseHandle(m_Handle);
            m_Handle = INVALID_HANDLE_VALUE;
            if (!RemoveDirectoryW(m_Link.c_str()))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Cannot remove test junction directory");
#else
            std::filesystem::remove(m_Link);
#endif
            m_Active = false;
        }

      private:
#if defined(_WIN32)
        TestDirectoryLink(std::filesystem::path link, const HANDLE handle) : m_Link(std::move(link)), m_Handle(handle)
        {
        }

        [[noreturn]] void ThrowAndClose(const DWORD error, const char* message)
        {
            CloseHandle(m_Handle);
            m_Handle = INVALID_HANDLE_VALUE;
            m_Active = false;
            throw std::system_error(static_cast<int>(error), std::system_category(), message);
        }
#else
        explicit TestDirectoryLink(std::filesystem::path link) : m_Link(std::move(link)) {}
#endif

        void RemoveNoThrow() noexcept
        {
            if (!m_Active)
                return;
            try
            {
                Remove();
            }
            catch (...)
            {
#if defined(_WIN32)
                if (m_Handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Handle);
                m_Handle = INVALID_HANDLE_VALUE;
#endif
                m_Active = false;
            }
        }

        std::filesystem::path m_Link;
#if defined(_WIN32)
        HANDLE m_Handle = INVALID_HANDLE_VALUE;
#endif
        bool m_Active = true;
    };
} // namespace

TEST_CASE("filesystem UTF-8 conversion preserves non-ASCII project paths")
{
    const std::string encoded = "KéireEngine/Assets/Créature.png";
    const auto path = Keire::Detail::PathFromUtf8(encoded);
    CHECK(Keire::Detail::PathToUtf8(path) == encoded);
    CHECK(path.filename() == Keire::Detail::PathFromUtf8("Créature.png"));
}

TEST_CASE("filesystem UTF-8 conversion composes and creates a non-ASCII Hub project destination")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireHubUtf8-" + suffix);
    const std::string parentText = "Kéire Projects";
    const std::string projectText = "Créature Demo";
    const auto parent = root / Keire::Detail::PathFromUtf8(parentText);

    const auto parentInput = Keire::Detail::PathToUtf8(parent);
    const auto destination = Keire::Detail::PathFromUtf8(parentInput) / Keire::Detail::PathFromUtf8(projectText);
    std::filesystem::create_directories(destination);

    CHECK(std::filesystem::is_directory(destination));
    CHECK(Keire::Detail::PathToUtf8(destination.parent_path().filename()) == parentText);
    CHECK(Keire::Detail::PathToUtf8(destination.filename()) == projectText);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("filesystem rename retries only transient failures with bounded backoff")
{
    std::size_t attempts = 0;
    std::vector<std::chrono::milliseconds> delays;
    std::error_code error;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& result)
    {
        ++attempts;
        result = attempts < 3 ? std::make_error_code(std::errc::permission_denied) : std::error_code{};
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds value) { delays.push_back(value); };

    CHECK(Keire::Detail::TryRenamePathWithRetry("source", "destination", error, operation, delay));
    CHECK(attempts == 3);
    CHECK(delays == std::vector{std::chrono::milliseconds(10), std::chrono::milliseconds(20)});
    CHECK_FALSE(error);
}

TEST_CASE("filesystem rename fails nontransient errors immediately and reports resolved paths")
{
    std::size_t attempts = 0;
    std::size_t delays = 0;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error)
    {
        ++attempts;
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds) { ++delays; };

    std::error_code error;
    CHECK_FALSE(Keire::Detail::TryRenamePathWithRetry("missing-source", "destination", error, operation, delay));
    CHECK(attempts == 1);
    CHECK(delays == 0);
    CHECK(error == std::errc::no_such_file_or_directory);

    const auto source = Keire::Detail::PathToUtf8(std::filesystem::absolute("missing-source").lexically_normal());
    const auto destination = Keire::Detail::PathToUtf8(std::filesystem::absolute("destination").lexically_normal());
    try
    {
        Keire::Detail::RenamePathWithRetry("missing-source", "destination", operation, delay);
        FAIL("Expected a resolved-path rename diagnostic.");
    }
    catch (const std::runtime_error& exception)
    {
        CHECK(std::string(exception.what()).find(source) != std::string::npos);
        CHECK(std::string(exception.what()).find(destination) != std::string::npos);
    }
}

TEST_CASE("filesystem rename bounds persistent transient failures")
{
    std::size_t attempts = 0;
    std::vector<std::chrono::milliseconds> delays;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error)
    {
        ++attempts;
        error = std::make_error_code(std::errc::permission_denied);
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds value) { delays.push_back(value); };

    std::error_code error;
    CHECK_FALSE(Keire::Detail::TryRenamePathWithRetry("source", "destination", error, operation, delay));
    CHECK(attempts == 5);
    CHECK(delays == std::vector{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                std::chrono::milliseconds(160)});
}

#if defined(_WIN32)
TEST_CASE("filesystem rename supports extended-length Windows destinations")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireLongRename-" + suffix);
    auto directory = root;
    for (std::size_t index = 0; index < 5; ++index)
        directory /= std::string(30, static_cast<char>('a' + index));
    std::filesystem::create_directories(directory);
    const auto source = directory / "source.bin";
    const auto destination = directory / (std::string(100, 'd') + ".bin");
    REQUIRE(source.native().size() < 260);
    REQUIRE(destination.native().size() >= 260);
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "long-path rename";
        REQUIRE(stream.good());
    }

    CHECK_NOTHROW(Keire::Detail::RenamePathWithRetry(source, destination));
    CHECK_NOTHROW(Keire::Detail::RenamePathWithRetry(destination, source));
    CHECK(Keire::Detail::ReadTextFile(source, 1024) == "long-path rename");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("atomic file publication supports Windows paths whose temporary name exceeds MAX_PATH")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireLongAtomic-" + suffix);
    std::filesystem::create_directories(root);
    REQUIRE(root.native().size() < 240);
    const auto filenameLength = std::size_t{245} - root.native().size() - 1;
    REQUIRE(filenameLength > 4);
    REQUIRE(filenameLength < 255);
    const auto destination = root / (std::string(filenameLength - 4, 'a') + ".bin");
    REQUIRE(destination.native().size() == 245);
    REQUIRE(destination.native().size() + std::string_view(".tmp.18446744073709551615").size() > 260);

    CHECK_NOTHROW(Keire::Detail::WriteTextFileAtomically(destination, "long-path atomic publication"));
    CHECK(Keire::Detail::ReadTextFile(destination, 1024) == "long-path atomic publication");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
#endif

TEST_CASE("atomic file publication replaces complete text and binary contents")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("KeireAtomicPublication-" + suffix);
    std::filesystem::create_directories(directory);
    const auto textPath = directory / "settings.json";
    const auto binaryPath = directory / "thumbnail.rgba";

    Keire::Detail::WriteTextFileAtomically(textPath, "old");
    Keire::Detail::WriteTextFileAtomically(textPath, "replacement");
    CHECK(Keire::Detail::ReadTextFile(textPath, 1024) == "replacement");

    const std::array<std::byte, 3> bytes{std::byte{0x01}, std::byte{0x7f}, std::byte{0xff}};
    Keire::Detail::WriteFileAtomically(binaryPath, bytes);
    std::ifstream input(binaryPath, std::ios::binary);
    std::array<std::byte, 3> actual{};
    input.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    REQUIRE(input);
    CHECK(actual == bytes);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("conditional atomic file publication preserves unchanged files")
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("KeireConditionalPublication-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "workspace.sln";
    REQUIRE(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "solution"));
    const auto preservedTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(path, preservedTime);

    CHECK_FALSE(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "solution"));
    const bool timestampWasPreserved = std::filesystem::last_write_time(path) == preservedTime;
    CHECK(timestampWasPreserved);
    CHECK(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "updated solution"));
    CHECK(Keire::Detail::ReadTextFile(path, 1024) == "updated solution");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("anchored filesystem operations cannot be redirected by a parent-directory swap")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireAnchoredSwap-" + suffix);
    const auto outside = std::filesystem::temp_directory_path() / ("KeireAnchoredOutside-" + suffix);
    std::filesystem::create_directories(root / "parent");
    std::filesystem::create_directories(outside);
    Keire::Detail::WriteTextFileAtomically(root / "parent/value.txt", "inside");
    Keire::Detail::WriteTextFileAtomically(root / "parent/from.txt", "inside rename");
    Keire::Detail::WriteTextFileAtomically(outside / "value.txt", "outside");
    Keire::Detail::WriteTextFileAtomically(outside / "from.txt", "outside rename");

    Keire::Detail::AnchoredFileSystem files(root);
    bool swapped = false;
    std::unique_ptr<TestDirectoryLink> activeLink;
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const std::filesystem::path&)
        {
            if (swapped || operation == "exists" || operation == "signature")
                return;
            swapped = true;
            std::filesystem::rename(root / "parent", root / "retained");
            try
            {
                activeLink = TestDirectoryLink::Create(outside, root / "parent");
            }
            catch (...)
            {
                std::filesystem::rename(root / "retained", root / "parent");
                swapped = false;
                throw;
            }
        });

    try
    {
        const auto bytes = files.Read("parent/value.txt", 1024);
        CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "inside");
        CHECK(Keire::Detail::ReadTextFile(outside / "value.txt", 1024) == "outside");
    }
    catch (const std::system_error& error)
    {
        Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
        WARN_MESSAGE(false, "Skipping anchored swap test because directory links are unavailable: ", error.what());
        if (swapped)
        {
            activeLink->Remove();
            activeLink.reset();
            std::filesystem::rename(root / "retained", root / "parent");
        }
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::remove_all(outside, ignored);
        return;
    }
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    activeLink->Remove();
    activeLink.reset();
    std::filesystem::rename(root / "retained", root / "parent");

    const auto verifySwap = [&](const std::function<void()>& operation)
    {
        swapped = false;
        Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
            [&](const std::string_view, const std::filesystem::path&)
            {
                if (swapped)
                    return;
                swapped = true;
                std::filesystem::rename(root / "parent", root / "retained");
                try
                {
                    activeLink = TestDirectoryLink::Create(outside, root / "parent");
                }
                catch (...)
                {
                    std::filesystem::rename(root / "retained", root / "parent");
                    swapped = false;
                    throw;
                }
            });
        bool completed = false;
        std::exception_ptr unexpected;
        try
        {
            operation();
            completed = true;
        }
        catch (const std::system_error&)
        {
            // Failing closed is valid when a platform refuses a mutation after the visible namespace changes.
        }
        catch (...)
        {
            unexpected = std::current_exception();
        }
        Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
        if (swapped)
        {
            activeLink->Remove();
            activeLink.reset();
            std::filesystem::rename(root / "retained", root / "parent");
        }
        if (unexpected)
            std::rethrow_exception(unexpected);
        return completed;
    };

    const std::string replacement = "replacement";
    const bool writeCompleted =
        verifySwap([&] { files.WriteFileAtomically("parent/value.txt", std::as_bytes(std::span(replacement))); });
    CHECK(Keire::Detail::ReadTextFile(root / "parent/value.txt", 1024) == (writeCompleted ? replacement : "inside"));
    CHECK(Keire::Detail::ReadTextFile(outside / "value.txt", 1024) == "outside");

    const bool removeCompleted = verifySwap([&] { files.Remove("parent/value.txt"); });
    CHECK(std::filesystem::exists(root / "parent/value.txt") != removeCompleted);
    CHECK(std::filesystem::exists(outside / "value.txt"));

    const bool renameCompleted = verifySwap([&] { files.Rename("parent/from.txt", "parent/to.txt"); });
    CHECK(std::filesystem::exists(root / "parent/to.txt") == renameCompleted);
    CHECK(std::filesystem::exists(root / "parent/from.txt") != renameCompleted);
    CHECK(std::filesystem::exists(outside / "from.txt"));

    files.CreateDirectories("delete/child");
    files.WriteFileAtomically("delete/child/value.txt", std::as_bytes(std::span(replacement)));
    files.Remove("delete/child/value.txt");
    files.Remove("delete/child");
    files.Remove("delete");
    CHECK_FALSE(files.Exists("delete"));
    CHECK_FALSE(std::filesystem::exists(root / "delete"));

#if defined(_WIN32)
    std::filesystem::rename(root / "parent", root / "retained");
    activeLink = TestDirectoryLink::Create(outside, root / "parent");
    CHECK_THROWS(files.CreateDirectories("parent/new/deep"));
    activeLink->Remove();
    activeLink.reset();
    std::filesystem::rename(root / "retained", root / "parent");
    CHECK_FALSE(std::filesystem::exists(root / "parent/new"));
    CHECK_FALSE(std::filesystem::exists(outside / "new"));
#else
    const bool createCompleted = verifySwap([&] { files.CreateDirectories("parent/new/deep"); });
    CHECK(std::filesystem::is_directory(root / "parent/new/deep") == createCompleted);
    CHECK_FALSE(std::filesystem::exists(outside / "new"));
#endif

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(outside, ignored);
}

TEST_CASE("anchored no-replace mutations reject destinations created during the operation")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::absolute(std::filesystem::path("Build") / ("KeireAnchoredNoReplace-" + suffix));
    std::filesystem::create_directories(root);
    Keire::Detail::WriteTextFileAtomically(root / "rename-source.txt", "rename source");
    Keire::Detail::WriteTextFileAtomically(root / "copy-source.txt", "copy source");
    std::filesystem::create_directories(root / "rename-source-directory");
    Keire::Detail::AnchoredFileSystem files(root);

    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const std::filesystem::path&)
        {
            if (operation == "rename" && !std::filesystem::exists(root / "rename-destination.txt"))
                Keire::Detail::WriteTextFileAtomically(root / "rename-destination.txt", "raced rename");
        });
    try
    {
        files.Rename("rename-source.txt", "rename-destination.txt", false);
        FAIL("The raced rename destination was overwritten.");
    }
    catch (const std::system_error& error)
    {
        CHECK(error.code().default_error_condition() == std::errc::file_exists);
    }
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(Keire::Detail::ReadTextFile(root / "rename-source.txt", 1024) == "rename source");
    CHECK(Keire::Detail::ReadTextFile(root / "rename-destination.txt", 1024) == "raced rename");

    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const std::filesystem::path&)
        {
            if (operation == "rename" && !std::filesystem::exists(root / "rename-destination-directory"))
                std::filesystem::create_directories(root / "rename-destination-directory");
        });
    try
    {
        files.Rename("rename-source-directory", "rename-destination-directory", false);
        FAIL("The raced rename directory destination was overwritten.");
    }
    catch (const std::system_error& error)
    {
        CHECK(error.code().default_error_condition() == std::errc::file_exists);
    }
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(std::filesystem::is_directory(root / "rename-source-directory"));
    CHECK(std::filesystem::is_directory(root / "rename-destination-directory"));

    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const std::filesystem::path&)
        {
            if (operation == "write" && !std::filesystem::exists(root / "copy-destination.txt"))
                Keire::Detail::WriteTextFileAtomically(root / "copy-destination.txt", "raced copy");
        });
    try
    {
        files.Copy("copy-source.txt", "copy-destination.txt", false);
        FAIL("The raced copy destination was overwritten.");
    }
    catch (const std::system_error& error)
    {
        CHECK(error.code().default_error_condition() == std::errc::file_exists);
    }
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(Keire::Detail::ReadTextFile(root / "copy-source.txt", 1024) == "copy source");
    CHECK(Keire::Detail::ReadTextFile(root / "copy-destination.txt", 1024) == "raced copy");
    bool retainedTemporary = false;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (entry.path().filename().string().starts_with("copy-destination.txt.tmp."))
            retainedTemporary = true;
    }
    CHECK_FALSE(retainedTemporary);

    files.Rename("rename-source.txt", "rename-success.txt", false);
    std::filesystem::remove(root / "rename-destination-directory");
    files.Rename("rename-source-directory", "rename-success-directory", false);
    files.Copy("copy-source.txt", "copy-success.txt", false);
    CHECK(Keire::Detail::ReadTextFile(root / "rename-success.txt", 1024) == "rename source");
    CHECK(std::filesystem::is_directory(root / "rename-success-directory"));
    CHECK(Keire::Detail::ReadTextFile(root / "copy-success.txt", 1024) == "copy source");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
