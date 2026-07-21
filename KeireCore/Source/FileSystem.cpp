#include "KeireInternal/FileSystem.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Keire::Detail
{
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
        throw std::runtime_error("Cannot rename '" + resolvedSource.string() + "' to '" + resolvedDestination.string() +
                                 "': " + error.message());
    }

    std::string ReadTextFile(const std::filesystem::path& path, const std::size_t maximumBytes)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            throw std::runtime_error("Cannot inspect file '" + path.string() + "': " + error.message());
        if (size > maximumBytes)
            throw std::runtime_error("File exceeds the supported size limit: " + path.string());

        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open file: " + path.string());
        std::string result{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
            throw std::runtime_error("Cannot read file: " + path.string());
        return result;
    }

    void WriteTextFileAtomically(const std::filesystem::path& path, const std::string_view contents)
    {
        if (path.filename().empty())
            throw std::invalid_argument("Atomic file writes require a filename.");

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            throw std::runtime_error("Cannot create directory '" + path.parent_path().string() +
                                     "': " + error.message());

        const auto uniqueValue =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
            static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        auto temporary = path;
        temporary += ".tmp." + std::to_string(uniqueValue);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("Cannot create temporary file: " + temporary.string());
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output)
            {
                std::filesystem::remove(temporary, error);
                throw std::runtime_error("Cannot write temporary file: " + temporary.string());
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
        throw std::runtime_error("Cannot atomically replace '" + path.string() + "': " + diagnostic);
#else
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("Cannot atomically replace '" + path.string() + "': " + error.message());
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
            throw std::invalid_argument("Path does not exist or cannot be resolved: " + path.string());
        return result.lexically_normal();
    }
} // namespace Keire::Detail
