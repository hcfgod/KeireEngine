#pragma once

#include "KeireHubRuntime/HubError.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace KeireHub::Detail
{
    using Json = nlohmann::json;

    [[nodiscard]] HubResult<std::string> ReadTextFile(const std::filesystem::path& path, std::size_t maximumBytes);
    [[nodiscard]] HubResult<Json> ReadJsonFile(const std::filesystem::path& path, std::size_t maximumBytes);
    [[nodiscard]] HubStatus WriteTextFileAtomically(const std::filesystem::path& path, std::string_view text);
    [[nodiscard]] HubStatus WriteJsonFileAtomically(const std::filesystem::path& path, const Json& document);
    [[nodiscard]] HubStatus QuarantineCorruptFile(const std::filesystem::path& path);
    [[nodiscard]] bool TryRenamePathWithRetry(const std::filesystem::path& source,
                                              const std::filesystem::path& destination, std::error_code& error);

    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view path);
    [[nodiscard]] bool IsSha256(std::string_view value) noexcept;
    [[nodiscard]] bool IsBoundedIdentifier(std::string_view value, std::size_t maximumBytes = 128) noexcept;
    [[nodiscard]] bool IsSafeRelativePath(const std::filesystem::path& path);
} // namespace KeireHub::Detail
