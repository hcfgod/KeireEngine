#include "KeireInternal/Assets/ShaderCompilerJobs.h"

#include "Keire/Assets/Asset.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::string_view LeaseFileName = ".keire-shader-job";
        constexpr std::size_t MaximumLeaseBytes = 32;

        [[nodiscard]] bool IsOwnedJobName(const std::filesystem::path& path) noexcept
        {
            try
            {
                const auto name = path.filename().string();
                return AssetId::Parse(name).ToString() == name;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] std::uint64_t ReadLeaseProcessId(const std::filesystem::path& jobDirectory) noexcept
        {
            const auto lease = jobDirectory / LeaseFileName;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(lease, error);
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                return 0;
            const auto size = std::filesystem::file_size(lease, error);
            if (error || size == 0 || size > MaximumLeaseBytes)
                return 0;

            std::ifstream input(lease, std::ios::binary);
            std::string value(static_cast<std::size_t>(size), '\0');
            if (!input.read(value.data(), static_cast<std::streamsize>(value.size())))
                return 0;
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                value.pop_back();
            std::uint64_t processId = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), processId);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? processId : 0;
        }

        [[nodiscard]] bool IsOrdinaryDirectory(const std::filesystem::path& path) noexcept
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
        }
    } // namespace

    ShaderCompilerJobCleanupResult CleanupStaleShaderCompilerJobs(const std::filesystem::path& root,
                                                                  const std::filesystem::file_time_type now,
                                                                  const std::chrono::seconds minimumAge)
    {
        if (root.empty())
            throw std::invalid_argument("Shader compiler job cleanup requires a root directory.");
        if (minimumAge < std::chrono::seconds::zero())
            throw std::invalid_argument("Shader compiler job cleanup age cannot be negative.");

        const auto normalizedRoot = std::filesystem::absolute(root).lexically_normal();
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(normalizedRoot, error);
        if (!error && std::filesystem::exists(rootStatus) &&
            (!std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus)))
        {
            throw std::runtime_error("Shader compiler job root is not an ordinary directory.");
        }
        if (error || !std::filesystem::exists(rootStatus))
            return {};

        ShaderCompilerJobCleanupResult result;
        std::filesystem::directory_iterator iterator(normalizedRoot, std::filesystem::directory_options::none, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end)
        {
            const auto candidate = iterator->path();
            iterator.increment(error);
            if (!IsOwnedJobName(candidate) || !IsOrdinaryDirectory(candidate))
            {
                ++result.Ignored;
                continue;
            }

            std::error_code timestampError;
            const auto modified = std::filesystem::last_write_time(candidate, timestampError);
            if (timestampError || modified > now || now - modified < minimumAge)
            {
                ++result.Retained;
                continue;
            }
            const auto processId = ReadLeaseProcessId(candidate);
            if (processId != 0 && IsProcessAlive(processId))
            {
                ++result.Retained;
                continue;
            }

            if (!IsOrdinaryDirectory(candidate) || candidate.parent_path() != normalizedRoot)
            {
                ++result.Ignored;
                continue;
            }
            std::error_code removalError;
            std::filesystem::remove_all(candidate, removalError);
            if (removalError)
                ++result.Retained;
            else
                ++result.Removed;
        }
        if (error)
            ++result.Ignored;
        return result;
    }

    void WriteShaderCompilerJobLease(const std::filesystem::path& jobDirectory, const std::uint64_t processId)
    {
        if (processId == 0)
            throw std::invalid_argument("Shader compiler job lease requires a process identity.");
        if (!IsOrdinaryDirectory(jobDirectory))
            throw std::invalid_argument("Shader compiler job lease requires an ordinary directory.");
        WriteTextFileAtomically(jobDirectory / LeaseFileName, std::to_string(processId) + "\n");
    }
} // namespace Keire::Detail
