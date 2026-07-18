#include "KeireInternal/FileSystem.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Keire::Detail
{
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

        auto temporary = path;
        temporary += ".tmp";
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
        const bool exists = std::filesystem::exists(path, error) && !error;
        const BOOL replaced =
            exists ? ReplaceFileW(target.c_str(), source.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
                   : MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!replaced)
        {
            const auto diagnostic = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("Cannot atomically replace '" + path.string() + "': " + diagnostic);
        }
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
