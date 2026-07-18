#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view value);
    [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path, std::size_t maximumBytes);
    void WriteTextFileAtomically(const std::filesystem::path& path, std::string_view contents);
    [[nodiscard]] std::filesystem::path CanonicalExistingPath(const std::filesystem::path& path);
} // namespace Keire::Detail
