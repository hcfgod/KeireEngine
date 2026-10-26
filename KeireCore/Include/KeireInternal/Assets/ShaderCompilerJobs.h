#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire
{
    struct AssetImporterRegistration;
    struct ShaderImporterSpecification;
} // namespace Keire

namespace Keire::Detail
{
    struct ShaderCompilerStatistics final
    {
        std::atomic<std::size_t> NativeStarts{0};
        std::atomic<std::size_t> CacheHits{0};
        std::atomic<std::size_t> CacheMisses{0};
    };

    [[nodiscard]] AssetImporterRegistration
    CreateCancellableShaderAssetImporter(ShaderImporterSpecification specification, std::stop_token cancellation);
    [[nodiscard]] AssetImporterRegistration
    CreateCancellableShaderAssetImporter(ShaderImporterSpecification specification, std::stop_token cancellation,
                                         std::shared_ptr<ShaderCompilerStatistics> statistics);

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
    void RunShaderCompilerProcess(const std::filesystem::path& compiler, std::span<const std::string> arguments,
                                  const std::filesystem::path& workingDirectory, std::chrono::milliseconds timeout,
                                  std::stop_token cancellation, ShaderCompilerStatistics* statistics = nullptr);
    [[nodiscard]] std::vector<std::byte>
    CompileShaderStage(const std::filesystem::path& compiler, const std::filesystem::path& source,
                       std::string_view destination, std::string_view stage, std::string_view entry,
                       const std::filesystem::path& output, std::span<const std::filesystem::path> includeRoots,
                       std::span<const std::pair<std::string, std::string>> defines,
                       const ShaderImporterSpecification& specification, const std::filesystem::path& workingDirectory,
                       std::stop_token cancellation, ShaderCompilerStatistics* statistics);
} // namespace Keire::Detail
