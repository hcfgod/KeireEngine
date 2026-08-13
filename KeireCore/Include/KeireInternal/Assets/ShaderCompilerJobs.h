#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace Keire::Detail
{
    struct ShaderCompilerJobCleanupResult final
    {
        std::size_t Removed = 0;
        std::size_t Retained = 0;
        std::size_t Ignored = 0;
    };

    [[nodiscard]] ShaderCompilerJobCleanupResult CleanupStaleShaderCompilerJobs(const std::filesystem::path& root,
                                                                                std::filesystem::file_time_type now,
                                                                                std::chrono::seconds minimumAge);
    void WriteShaderCompilerJobLease(const std::filesystem::path& jobDirectory, std::uint64_t processId);
} // namespace Keire::Detail
