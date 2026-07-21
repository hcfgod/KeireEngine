#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>

namespace Keire::Detail
{
    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view value);
    [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path, std::size_t maximumBytes);
    void WriteTextFileAtomically(const std::filesystem::path& path, std::string_view contents);
    using RenamePathOperation =
        std::function<void(const std::filesystem::path&, const std::filesystem::path&, std::error_code&)>;
    using RenamePathDelay = std::function<void(std::size_t attempt, std::chrono::milliseconds delay)>;
    [[nodiscard]] bool TryRenamePathWithRetry(const std::filesystem::path& source,
                                              const std::filesystem::path& destination, std::error_code& error,
                                              const RenamePathOperation& operation = {},
                                              const RenamePathDelay& delay = {});
    void RenamePathWithRetry(const std::filesystem::path& source, const std::filesystem::path& destination,
                             const RenamePathOperation& operation = {}, const RenamePathDelay& delay = {});
    [[nodiscard]] std::filesystem::path CanonicalExistingPath(const std::filesystem::path& path);
} // namespace Keire::Detail
