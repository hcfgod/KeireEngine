#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace KeireHub::Detail
{
    [[nodiscard]] std::optional<std::filesystem::path>
    ResolveConfinedRegularFile(const std::filesystem::path& root, const std::filesystem::path& relative);
    [[nodiscard]] std::optional<std::string> ReadBoundedCatalogText(const std::filesystem::path& path,
                                                                    std::size_t maximumBytes);
    [[nodiscard]] bool ContainsCaseInsensitive(std::string_view text, std::string_view query) noexcept;
} // namespace KeireHub::Detail
