#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    struct ProcessResult
    {
        int ExitCode = -1;
        bool TimedOut = false;
        std::string Output;
    };

    [[nodiscard]] ProcessResult RunProcess(const std::filesystem::path& executable,
                                           std::span<const std::string> arguments,
                                           const std::filesystem::path& workingDirectory,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] bool LaunchDetachedProcess(const std::filesystem::path& executable,
                                             std::span<const std::string> arguments,
                                             const std::filesystem::path& workingDirectory,
                                             std::string& diagnostic) noexcept;
    [[nodiscard]] bool RevealInFileManager(const std::filesystem::path& path, std::string& diagnostic) noexcept;
} // namespace Keire::Detail
#include <chrono>
