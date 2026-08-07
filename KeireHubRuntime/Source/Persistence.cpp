#include "Persistence.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub::Detail
{
    namespace
    {
#if defined(_WIN32)
        class WindowsFileHandle final
        {
          public:
            explicit WindowsFileHandle(HANDLE handle) noexcept : m_Handle(handle) {}
            ~WindowsFileHandle()
            {
                if (m_Handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Handle);
            }

            WindowsFileHandle(const WindowsFileHandle&) = delete;
            WindowsFileHandle& operator=(const WindowsFileHandle&) = delete;

            [[nodiscard]] HANDLE Get() const noexcept { return m_Handle; }

          private:
            HANDLE m_Handle = INVALID_HANDLE_VALUE;
        };
#endif

        [[nodiscard]] HubError IoError(const HubErrorCode code, const std::filesystem::path& path,
                                       const std::string& details)
        {
            return {.Code = code,
                    .Message = code == HubErrorCode::IoRead ? "The Hub could not read its saved data."
                                                            : "The Hub could not save its data.",
                    .Retryable = true,
                    .AffectedItem = PathToUtf8(path.filename()),
                    .TechnicalDetails = details};
        }

        [[nodiscard]] bool ReplaceFile(const std::filesystem::path& source, const std::filesystem::path& destination,
                                       std::error_code& error) noexcept
        {
#if defined(_WIN32)
            for (std::uint32_t attempt = 0; attempt < 16; ++attempt)
            {
                if (MoveFileExW(source.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    return true;
                }
                const auto code = GetLastError();
                const bool retryable =
                    code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
                if (!retryable || attempt == 15)
                {
                    error = std::error_code(static_cast<int>(code), std::system_category());
                    return false;
                }
                // Readers and security filters may retain a replace-sensitive handle briefly after their read.
                Sleep(attempt < 4 ? 0U : 1U);
            }
            return false;
#else
            std::filesystem::rename(source, destination, error);
            return !error;
#endif
        }
    } // namespace

    HubResult<std::string> ReadTextFile(const std::filesystem::path& path, const std::size_t maximumBytes)
    {
#if defined(_WIN32)
        const WindowsFileHandle file(CreateFileW(path.c_str(), GENERIC_READ,
                                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.Get() == INVALID_HANDLE_VALUE)
        {
            const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
            return HubResult<std::string>::Failure(IoError(HubErrorCode::IoRead, path, error.message()));
        }
        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file.Get(), &fileSize) || fileSize.QuadPart < 0)
        {
            const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
            return HubResult<std::string>::Failure(IoError(HubErrorCode::IoRead, path, error.message()));
        }
        const auto size = static_cast<std::uint64_t>(fileSize.QuadPart);
#else
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return HubResult<std::string>::Failure(IoError(HubErrorCode::IoRead, path, error.message()));
#endif
        if (size > maximumBytes)
        {
            return HubResult<std::string>::Failure({.Code = HubErrorCode::InvalidData,
                                                    .Message = "The saved Hub data is larger than its allowed limit.",
                                                    .AffectedItem = PathToUtf8(path.filename()),
                                                    .TechnicalDetails = "File exceeds the configured byte limit."});
        }

#if defined(_WIN32)
        std::string result(static_cast<std::size_t>(size), '\0');
        std::size_t offset = 0;
        while (offset < result.size())
        {
            const auto remaining = result.size() - offset;
            const auto requested = static_cast<DWORD>(
                std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD read = 0;
            if (!ReadFile(file.Get(), result.data() + offset, requested, &read, nullptr))
            {
                const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
                return HubResult<std::string>::Failure(IoError(HubErrorCode::IoRead, path, error.message()));
            }
            if (read == 0)
            {
                return HubResult<std::string>::Failure(
                    IoError(HubErrorCode::IoRead, path, "Could not read complete file."));
            }
            offset += read;
        }
#else
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return HubResult<std::string>::Failure(IoError(HubErrorCode::IoRead, path, "Could not open file."));
        std::string result(static_cast<std::size_t>(size), '\0');
        if (!result.empty())
            stream.read(result.data(), static_cast<std::streamsize>(result.size()));
        if (!stream && !result.empty())
            return HubResult<std::string>::Failure(
                IoError(HubErrorCode::IoRead, path, "Could not read complete file."));
#endif
        return HubResult<std::string>::Success(std::move(result));
    }

    HubResult<Json> ReadJsonFile(const std::filesystem::path& path, const std::size_t maximumBytes)
    {
        auto text = ReadTextFile(path, maximumBytes);
        if (!text)
            return HubResult<Json>::Failure(text.Error());
        try
        {
            return HubResult<Json>::Success(Json::parse(text.Value()));
        }
        catch (const std::exception& error)
        {
            return HubResult<Json>::Failure({.Code = HubErrorCode::InvalidData,
                                             .Message = "The saved Hub data is malformed.",
                                             .AffectedItem = PathToUtf8(path.filename()),
                                             .TechnicalDetails = error.what()});
        }
    }

    HubStatus WriteTextFileAtomically(const std::filesystem::path& path, const std::string_view text)
    {
        try
        {
            const auto parent = path.parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                if (!stream)
                    return HubStatus::Failure(IoError(HubErrorCode::IoWrite, path, "Could not create temporary file."));
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
                stream.flush();
                if (!stream)
                {
                    stream.close();
                    std::error_code ignored;
                    std::filesystem::remove(temporary, ignored);
                    return HubStatus::Failure(
                        IoError(HubErrorCode::IoWrite, path, "Could not write complete temporary file."));
                }
            }
            std::error_code error;
            if (!ReplaceFile(temporary, path, error))
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return HubStatus::Failure(IoError(HubErrorCode::IoWrite, path, error.message()));
            }
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(IoError(HubErrorCode::IoWrite, path, error.what()));
        }
    }

    HubStatus WriteJsonFileAtomically(const std::filesystem::path& path, const Json& document)
    {
        return WriteTextFileAtomically(path, document.dump(2) + '\n');
    }

    HubStatus QuarantineCorruptFile(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return HubStatus::Success();
        for (std::size_t suffix = 0; suffix < 1000; ++suffix)
        {
            auto destination = path;
            destination += suffix == 0 ? ".corrupt" : ".corrupt." + std::to_string(suffix);
            if (std::filesystem::exists(destination))
                continue;
            std::error_code error;
            std::filesystem::rename(path, destination, error);
            if (!error)
                return HubStatus::Success();
            return HubStatus::Failure(IoError(HubErrorCode::IoWrite, path, error.message()));
        }
        return HubStatus::Failure(
            IoError(HubErrorCode::IoWrite, path, "Could not allocate a corrupt-file quarantine name."));
    }

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const auto encoded = path.generic_u8string();
        return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    }

    std::filesystem::path PathFromUtf8(const std::string_view path)
    {
        const auto* begin = reinterpret_cast<const char8_t*>(path.data());
        return std::filesystem::path(std::u8string(begin, begin + path.size()));
    }

    bool IsSha256(const std::string_view value) noexcept
    {
        if (value.size() != 64)
            return false;
        for (const auto character : value)
        {
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
                return false;
        }
        return true;
    }

    bool IsBoundedIdentifier(const std::string_view value, const std::size_t maximumBytes) noexcept
    {
        if (value.empty() || value.size() > maximumBytes || value.front() == '.' || value.back() == '.')
            return false;
        bool previousSeparator = false;
        for (const auto character : value)
        {
            const bool separator = character == '.' || character == '-' || character == '_';
            if (!(character >= 'a' && character <= 'z') && !(character >= 'A' && character <= 'Z') &&
                !(character >= '0' && character <= '9') && !separator)
                return false;
            if (separator && previousSeparator)
                return false;
            previousSeparator = separator;
        }
        return true;
    }

    bool IsSafeRelativePath(const std::filesystem::path& path)
    {
        if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
            return false;
        for (const auto& component : path)
        {
            if (component == ".." || component == "." || component.empty())
                return false;
        }
        return true;
    }
} // namespace KeireHub::Detail
