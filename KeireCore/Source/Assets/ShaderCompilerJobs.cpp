#include "KeireInternal/Assets/ShaderCompilerJobs.h"

#include "Keire/Assets/Asset.h"
#include "Keire/Assets/ShaderCompilation.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <ranges>
#include <span>
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

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] bool IsLowercaseSha256(const std::string_view value) noexcept
        {
            return value.size() == 64U &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f'); });
        }

        [[nodiscard]] bool IsIdentifier(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > 128U ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) != 0 || character == '_'; });
        }

        [[nodiscard]] bool IsVirtualPath(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > 1024U || value.front() == '/' ||
                value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos)
                return false;
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('/', begin);
                const auto segment =
                    value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (segment.empty() || segment == "." || segment == "..")
                    return false;
                if (end == std::string_view::npos)
                    break;
                begin = end + 1U;
            }
            return true;
        }

        [[nodiscard]] std::string_view StageName(const ShaderCompileStage stage)
        {
            switch (stage)
            {
            case ShaderCompileStage::Vertex:
                return "vertex";
            case ShaderCompileStage::Fragment:
                return "fragment";
            case ShaderCompileStage::Compute:
                return "compute";
            }
            throw std::invalid_argument("Shader compile stage is unsupported.");
        }

        [[nodiscard]] std::string_view PlatformName(const ShaderCompilePlatform platform)
        {
            switch (platform)
            {
            case ShaderCompilePlatform::Windows:
                return "windows";
            case ShaderCompilePlatform::Linux:
                return "linux";
            case ShaderCompilePlatform::MacOS:
                return "macos";
            }
            throw std::invalid_argument("Shader compile platform is unsupported.");
        }

        [[nodiscard]] std::string_view ArchitectureName(const ShaderCompileArchitecture architecture)
        {
            switch (architecture)
            {
            case ShaderCompileArchitecture::X86_64:
                return "x86_64";
            case ShaderCompileArchitecture::Arm64:
                return "arm64";
            }
            throw std::invalid_argument("Shader compile architecture is unsupported.");
        }

        [[nodiscard]] std::string_view FormatName(const ShaderCompileBinaryFormat format)
        {
            switch (format)
            {
            case ShaderCompileBinaryFormat::Dxil:
                return "dxil";
            case ShaderCompileBinaryFormat::SpirV:
                return "spirv";
            case ShaderCompileBinaryFormat::Msl:
                return "msl";
            case ShaderCompileBinaryFormat::Metallib:
                return "metallib";
            }
            throw std::invalid_argument("Shader compile binary format is unsupported.");
        }

        [[nodiscard]] std::string_view BackendName(const ShaderCompileBackend backend)
        {
            switch (backend)
            {
            case ShaderCompileBackend::D3D12:
                return "d3d12";
            case ShaderCompileBackend::Vulkan:
                return "vulkan";
            case ShaderCompileBackend::Metal:
                return "metal";
            case ShaderCompileBackend::Automatic:
                break;
            }
            throw std::invalid_argument("Shader compile backend is unsupported.");
        }

        [[nodiscard]] ShaderCompileBackend ResolveBackend(const ShaderCompileTarget& target)
        {
            if (target.Backend != ShaderCompileBackend::Automatic)
                return target.Backend;
            switch (target.Format)
            {
            case ShaderCompileBinaryFormat::Dxil:
                return ShaderCompileBackend::D3D12;
            case ShaderCompileBinaryFormat::SpirV:
                return ShaderCompileBackend::Vulkan;
            case ShaderCompileBinaryFormat::Msl:
            case ShaderCompileBinaryFormat::Metallib:
                return ShaderCompileBackend::Metal;
            }
            throw std::invalid_argument("Shader compile backend cannot be inferred from the binary format.");
        }

        [[nodiscard]] std::string_view OptimizationName(const ShaderCompileOptimization optimization)
        {
            switch (optimization)
            {
            case ShaderCompileOptimization::Debug:
                return "debug";
            case ShaderCompileOptimization::Development:
                return "development";
            case ShaderCompileOptimization::Shipping:
                return "shipping";
            }
            throw std::invalid_argument("Shader compile optimization policy is unsupported.");
        }

        [[nodiscard]] Json EncodeManifestDocument(const ShaderCompileManifest& canonical)
        {
            Json defines = Json::array();
            for (const auto& define : canonical.Defines)
                defines.push_back({{"name", define.Name}, {"value", define.Value}});
            Json dependencies = Json::array();
            for (const auto& dependency : canonical.Dependencies)
                dependencies.push_back({{"path", dependency.VirtualPath}, {"sha256", dependency.Sha256}});
            return {{"schemaVersion", canonical.SchemaVersion},
                    {"programAbiVersion", canonical.ProgramAbiVersion},
                    {"toolchainSha256", canonical.ToolchainSha256},
                    {"sourceSha256", canonical.SourceSha256},
                    {"passRole", canonical.PassRole},
                    {"stage", StageName(canonical.Stage)},
                    {"entryPoint", canonical.EntryPoint},
                    {"target",
                     {{"platform", PlatformName(canonical.Target.Platform)},
                      {"architecture", ArchitectureName(canonical.Target.Architecture)},
                      {"backend", BackendName(canonical.Target.Backend)},
                      {"format", FormatName(canonical.Target.Format)}}},
                    {"defines", std::move(defines)},
                    {"dependencies", std::move(dependencies)},
                    {"optimization", OptimizationName(canonical.Optimization)},
                    {"warningsAsErrors", canonical.WarningsAsErrors},
                    {"debugInformation", canonical.DebugInformation}};
        }
    } // namespace

    void ValidateShaderCompileManifest(const ShaderCompileManifest& manifest)
    {
        if (manifest.SchemaVersion != ShaderCompileManifestSchemaVersion ||
            manifest.ProgramAbiVersion != ShaderCompileProgramAbiVersion)
            throw std::invalid_argument("Shader compile manifest schema or program ABI is unsupported.");
        if (!IsLowercaseSha256(manifest.ToolchainSha256) || !IsLowercaseSha256(manifest.SourceSha256))
            throw std::invalid_argument("Shader compile manifest digests must be lowercase SHA-256 values.");
        (void)StageName(manifest.Stage);
        (void)PlatformName(manifest.Target.Platform);
        (void)ArchitectureName(manifest.Target.Architecture);
        const auto backend = ResolveBackend(manifest.Target);
        (void)BackendName(backend);
        (void)FormatName(manifest.Target.Format);
        (void)OptimizationName(manifest.Optimization);
        if (!IsIdentifier(manifest.PassRole) || !IsIdentifier(manifest.EntryPoint))
            throw std::invalid_argument("Shader compile pass role or entry point is invalid.");
        const bool d3d12 = backend == ShaderCompileBackend::D3D12 &&
                           manifest.Target.Platform == ShaderCompilePlatform::Windows &&
                           manifest.Target.Format == ShaderCompileBinaryFormat::Dxil;
        const bool vulkan = backend == ShaderCompileBackend::Vulkan &&
                            manifest.Target.Platform != ShaderCompilePlatform::MacOS &&
                            manifest.Target.Format == ShaderCompileBinaryFormat::SpirV;
        const bool metal = backend == ShaderCompileBackend::Metal &&
                           manifest.Target.Platform == ShaderCompilePlatform::MacOS &&
                           (manifest.Target.Format == ShaderCompileBinaryFormat::Msl ||
                            manifest.Target.Format == ShaderCompileBinaryFormat::Metallib);
        if (!d3d12 && !vulkan && !metal)
            throw std::invalid_argument("Shader compile platform, backend, and binary format are incompatible.");
        if (manifest.Defines.size() > 64U || manifest.Dependencies.size() > 256U)
            throw std::invalid_argument("Shader compile manifest exceeds define or dependency limits.");

        std::vector<std::string_view> defineNames;
        defineNames.reserve(manifest.Defines.size());
        for (const auto& define : manifest.Defines)
        {
            if (!IsIdentifier(define.Name) || define.Value.size() > 256U ||
                define.Value.find_first_of("\r\n") != std::string::npos)
                throw std::invalid_argument("Shader compile define is invalid.");
            defineNames.push_back(define.Name);
        }
        std::ranges::sort(defineNames);
        if (std::ranges::adjacent_find(defineNames) != defineNames.end())
            throw std::invalid_argument("Shader compile define names must be unique.");

        std::vector<std::string_view> dependencyPaths;
        dependencyPaths.reserve(manifest.Dependencies.size());
        for (const auto& dependency : manifest.Dependencies)
        {
            if (!IsVirtualPath(dependency.VirtualPath) || !IsLowercaseSha256(dependency.Sha256))
                throw std::invalid_argument("Shader compile dependency path or digest is invalid.");
            dependencyPaths.push_back(dependency.VirtualPath);
        }
        std::ranges::sort(dependencyPaths);
        if (std::ranges::adjacent_find(dependencyPaths) != dependencyPaths.end())
            throw std::invalid_argument("Shader compile dependency paths must be unique.");
    }

    void ValidateShaderCompilationRequest(const ShaderCompilationRequest& request)
    {
        ValidateShaderCompileManifest(request.Manifest);
        if (request.Policy > ShaderCompilationPolicy::RemoteRequired ||
            request.Priority > ShaderCompilationPriority::Background)
            throw std::invalid_argument("Shader compilation request policy or priority is unsupported.");
    }

    ShaderCompileManifest CanonicalizeShaderCompileManifest(ShaderCompileManifest manifest)
    {
        manifest.Target.Backend = ResolveBackend(manifest.Target);
        ValidateShaderCompileManifest(manifest);
        std::ranges::sort(manifest.Defines, {}, &ShaderCompileDefine::Name);
        std::ranges::sort(manifest.Dependencies, {}, &ShaderCompileDependency::VirtualPath);
        return manifest;
    }

    std::string EncodeShaderCompileManifest(const ShaderCompileManifest& manifest)
    {
        const auto canonical = CanonicalizeShaderCompileManifest(manifest);
        return EncodeManifestDocument(canonical).dump();
    }

    std::vector<std::byte> EncodeShaderCompileManifestCbor(const ShaderCompileManifest& manifest)
    {
        const auto canonical = CanonicalizeShaderCompileManifest(manifest);
        const auto encoded = Json::to_cbor(EncodeManifestDocument(canonical));
        std::vector<std::byte> result(encoded.size());
        std::memcpy(result.data(), encoded.data(), encoded.size());
        return result;
    }

    std::string ShaderCompileWorkKey(const ShaderCompileManifest& manifest)
    {
        constexpr std::string_view domain = "keire-shader-work-v2";
        const auto encoded = EncodeShaderCompileManifestCbor(manifest);
        std::vector<std::byte> payload;
        payload.reserve(domain.size() + 1U + encoded.size());
        const auto domainBytes = std::as_bytes(std::span(domain));
        payload.insert(payload.end(), domainBytes.begin(), domainBytes.end());
        payload.push_back(std::byte{0});
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        return Detail::DigestToString(Detail::Sha256(payload));
    }

    bool IsShaderCompileWorkKey(const std::string_view value) noexcept { return IsLowercaseSha256(value); }
} // namespace Keire
