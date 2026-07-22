#include "KeireInternal/FileSystem.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Keire::Detail
{
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
        [[nodiscard]] bool ContentsMatch(const std::filesystem::path& path, const std::string_view expected)
        {
            std::error_code error;
            if (std::filesystem::file_size(path, error) != expected.size() || error)
                return false;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return false;
            std::string contents(expected.size(), '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            return input && contents == expected;
        }

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
                                                      { std::filesystem::rename(from, to, result); }};
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

    void WriteTextFileAtomically(const std::filesystem::path& path, const std::string_view contents)
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
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("Cannot create temporary file: " + PathToUtf8(temporary));
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output)
            {
                std::filesystem::remove(temporary, error);
                throw std::runtime_error("Cannot write temporary file: " + PathToUtf8(temporary));
            }
        }

#if defined(_WIN32)
        const auto target = path.wstring();
        const auto source = temporary.wstring();
        DWORD lastError = ERROR_SUCCESS;
        constexpr std::size_t maximumAttempts = 6;
        for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            error.clear();
            const bool exists = std::filesystem::exists(path, error) && !error;
            const BOOL replaced = exists ? ReplaceFileW(target.c_str(), source.c_str(), nullptr,
                                                        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
                                         : MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
            if (replaced)
                return;

            lastError = GetLastError();
            // ReplaceFileW can publish the replacement and still report that it could not remove the old file.
            if (ContentsMatch(path, contents))
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
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("Cannot atomically replace '" + PathToUtf8(path) + "': " + diagnostic);
#else
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("Cannot atomically replace '" + PathToUtf8(path) + "': " + error.message());
        }
#endif
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
} // namespace Keire::Detail
