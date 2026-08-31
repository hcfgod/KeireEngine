#include "KeireInternal/Build/PlayerSupport.h"

#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumManifestBytes = 4ULL * 1024ULL * 1024U;

        template <typename Enum>
        [[nodiscard]] Enum ParseEnum(const std::string& value,
                                     const std::initializer_list<std::pair<std::string_view, Enum>> values,
                                     const std::string_view name)
        {
            const auto found =
                std::ranges::find_if(values, [&](const auto& candidate) { return candidate.first == value; });
            if (found == values.end())
                throw std::runtime_error("Unknown " + std::string(name) + ": " + value);
            return found->second;
        }

        [[nodiscard]] bool IsConfinedRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            const auto normalized = path.lexically_normal();
            return normalized != "." && !normalized.empty() && *normalized.begin() != "..";
        }

        [[nodiscard]] bool IsSafeComponent(const std::string_view value)
        {
            if (value.empty() || value == "." || value == ".." || value.size() > 128 || value.front() == '.' ||
                value.back() == '.' || value.back() == ' ')
                return false;
            if (std::ranges::any_of(value,
                                    [](const unsigned char character)
                                    {
                                        return character < 0x20U || character == 0x7fU || character == '/' ||
                                               character == '\\' || character == '<' || character == '>' ||
                                               character == ':' || character == '"' || character == '|' ||
                                               character == '?' || character == '*';
                                    }))
            {
                return false;
            }

            const auto path = PathFromUtf8(value);
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory() || path.filename() != path)
                return false;

            auto stem = std::string(value.substr(0, value.find('.')));
            std::ranges::transform(stem, stem.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::toupper(character)); });
            static constexpr std::array reservedNames{"CON",  "PRN",  "AUX",  "NUL",  "CLOCK$", "COM1", "COM2", "COM3",
                                                      "COM4", "COM5", "COM6", "COM7", "COM8",   "COM9", "LPT1", "LPT2",
                                                      "LPT3", "LPT4", "LPT5", "LPT6", "LPT7",   "LPT8", "LPT9"};
            return std::ranges::find(reservedNames, stem) == reservedNames.end();
        }

        [[nodiscard]] std::string CaseFoldedPath(const std::filesystem::path& path)
        {
            auto result = PathToUtf8(path.lexically_normal());
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] std::string LogicalPathKey(const std::filesystem::path& path, const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? CaseFoldedPath(path) : PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] bool HasProhibitedRuntimeComponent(const std::filesystem::path& path)
        {
            static const std::unordered_set<std::string> prohibited{"artifacts",     "build",     "cache", "caches",
                                                                    "logs",          "metadata",  "packs", "sdk",
                                                                    "sdk-manifests", "templates", "temp",  "tools"};
            return std::ranges::any_of(path, [](const auto& component)
                                       { return prohibited.contains(CaseFoldedPath(component)); });
        }

        [[nodiscard]] std::optional<std::pair<std::string, std::string>> FfmpegAbi(const std::filesystem::path& path)
        {
            const auto filename = CaseFoldedPath(path.filename());
            constexpr std::array families{"avcodec", "avformat", "avutil", "swresample", "swscale"};
            for (const std::string_view family : families)
            {
                const auto position = filename.find(family);
                if (position == std::string::npos)
                    continue;
                auto index = position + family.size();
                while (index < filename.size() &&
                       (filename[index] == '-' || filename[index] == '.' || filename[index] == '_'))
                    ++index;
                if (filename.substr(index).starts_with("so."))
                    index += 3;
                const auto first = index;
                while (index < filename.size() && std::isdigit(static_cast<unsigned char>(filename[index])) != 0)
                    ++index;
                if (index != first)
                    return std::pair{std::string(family), filename.substr(first, index - first)};
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root,
                                    const PlayerPlatform platform)
        {
            const auto pathKey = LogicalPathKey(path, platform);
            const auto rootKey = LogicalPathKey(root, platform);
            return pathKey == rootKey || pathKey.starts_with(rootKey + "/");
        }

        [[nodiscard]] std::optional<std::string> FirstRelativeComponent(const std::filesystem::path& path,
                                                                        const std::filesystem::path& root,
                                                                        const PlayerPlatform platform)
        {
            if (!IsWithin(path, root, platform))
                return std::nullopt;
            const auto pathKey = LogicalPathKey(path, platform);
            const auto rootKey = LogicalPathKey(root, platform);
            if (pathKey.size() <= rootKey.size())
                return std::nullopt;
            const auto relative = std::string_view(pathKey).substr(rootKey.size() + 1);
            return std::string(relative.substr(0, relative.find('/')));
        }

        [[nodiscard]] std::filesystem::path ManagedRoot(const PlayerSupportManifest& manifest,
                                                        const PlayerSupportVariant& variant)
        {
            if (manifest.Platform == PlayerPlatform::MacOS)
                return variant.Root / variant.Bundle / "Contents/Resources/Managed";
            return variant.Root / "Managed";
        }

        [[nodiscard]] std::string_view NethostName(const PlayerPlatform platform)
        {
            switch (platform)
            {
            case PlayerPlatform::Windows:
                return "nethost.dll";
            case PlayerPlatform::Linux:
                return "libnethost.so";
            case PlayerPlatform::MacOS:
                return "libnethost.dylib";
            }
            throw std::invalid_argument("Player support manifest platform is invalid.");
        }

        [[nodiscard]] std::string_view HostFxrName(const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? "hostfxr.dll"
                   : platform == PlayerPlatform::Linux ? "libhostfxr.so"
                                                       : "libhostfxr.dylib";
        }

        [[nodiscard]] std::string_view CoreClrName(const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? "coreclr.dll"
                   : platform == PlayerPlatform::Linux ? "libcoreclr.so"
                                                       : "libcoreclr.dylib";
        }

        [[nodiscard]] std::string_view HostPolicyName(const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? "hostpolicy.dll"
                   : platform == PlayerPlatform::Linux ? "libhostpolicy.so"
                                                       : "libhostpolicy.dylib";
        }

        [[nodiscard]] std::string_view CreateDumpName(const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? "createdump.exe" : "createdump";
        }

        void RequireFile(const std::unordered_set<std::string>& files, const std::filesystem::path& path,
                         const PlayerPlatform platform)
        {
            const auto required = LogicalPathKey(path, platform);
            if (files.contains(required))
                return;

            std::string diagnostic;
            const auto filename = CaseFoldedPath(path.filename());
            for (const auto& candidate : files)
            {
                const auto separator = candidate.find_last_of("/\\");
                const auto candidateFilename =
                    separator == std::string::npos ? std::string_view(candidate) : std::string_view(candidate).substr(separator + 1);
                if (candidateFilename == filename)
                {
                    diagnostic = " (the manifest instead contains '" + candidate + "')";
                    break;
                }
            }
            throw std::invalid_argument("Player support manifest runtime closure is missing a required file: " +
                                        PathToUtf8(path) + diagnostic);
        }

        void ValidateRuntimeClosure(const PlayerSupportManifest& manifest)
        {
            if (manifest.Files.empty())
                return;

            static constexpr std::array RequiredManagedFiles{"Coral.Managed.dll", "Coral.Managed.deps.json",
                                                             "Coral.Managed.runtimeconfig.json", "Keire.Managed.dll"};
            static constexpr std::array RequiredWindowsRuntimeFiles{
                "MSVCP140.dll", "MSVCP140_ATOMIC_WAIT.dll", "MSVCP140_1.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll"};
            static constexpr std::array RequiredLicenseFiles{"Keire-LICENSE.txt",
                                                             "Keire-THIRD_PARTY_NOTICES.md",
                                                             "Coral-LICENSE.txt",
                                                             "dotnet-LICENSE.txt",
                                                             "dotnet-ThirdPartyNotices.txt",
                                                             "SDL-LICENSE.txt",
                                                             "assimp-LICENSE.txt",
                                                             "assimp-zlib-LICENSE.txt",
                                                             "stb-LICENSE.txt",
                                                             "Jolt-LICENSE.txt",
                                                             "Recast-LICENSE.txt",
                                                             "miniaudio-LICENSE.txt",
                                                             "spdlog-LICENSE.txt",
                                                             "fmt-LICENSE.rst",
                                                             "nlohmann-json-LICENSE.MIT.txt",
                                                             "dear-imgui-LICENSE.txt",
                                                             "zstandard-LICENSE.txt",
                                                             "entt-LICENSE.txt",
                                                             "glm-COPYING.txt"};

            std::unordered_set<std::string> logicalFiles;
            for (const auto& file : manifest.Files)
                logicalFiles.emplace(LogicalPathKey(file.Path, manifest.Platform));

            std::unordered_set<std::string> allowedExecutables;
            std::map<std::string, std::unordered_set<std::string>> ffmpegGenerations;
            for (const auto& variant : manifest.Variants)
            {
                const auto executable = (variant.Root / variant.Executable).lexically_normal();
                const auto managed = ManagedRoot(manifest, variant).lexically_normal();
                std::filesystem::path nativeRoot = variant.Root;
                if (manifest.Platform == PlayerPlatform::MacOS)
                {
                    const auto requiredExecutable =
                        (variant.Bundle / "Contents/MacOS" / variant.Executable.filename()).lexically_normal();
                    if (variant.Bundle.empty() || variant.Executable != requiredExecutable)
                        throw std::invalid_argument("Player support macOS variant layout is invalid.");
                    nativeRoot /= variant.Bundle / "Contents/MacOS";
                }
                else if (!variant.Bundle.empty() || variant.Executable.has_parent_path())
                    throw std::invalid_argument("Player support native variant layout is invalid.");

                RequireFile(logicalFiles, executable, manifest.Platform);
                RequireFile(logicalFiles, nativeRoot / NethostName(manifest.Platform), manifest.Platform);
                if (manifest.Platform == PlayerPlatform::Windows)
                {
                    for (const auto filename : RequiredWindowsRuntimeFiles)
                        RequireFile(logicalFiles, nativeRoot / filename, manifest.Platform);
                }
                for (const auto filename : RequiredManagedFiles)
                    RequireFile(logicalFiles, managed / filename, manifest.Platform);
                for (const auto filename : RequiredLicenseFiles)
                    RequireFile(logicalFiles, variant.Root / "Licenses" / filename, manifest.Platform);

                const auto hostRoot = managed / "Dotnet/host/fxr";
                const auto runtimeRoot = managed / "Dotnet/shared/Microsoft.NETCore.App";
                std::unordered_set<std::string> hostGenerations;
                std::unordered_set<std::string> runtimeGenerations;
                for (const auto& file : manifest.Files)
                {
                    if (const auto generation = FirstRelativeComponent(file.Path, hostRoot, manifest.Platform))
                        hostGenerations.emplace(*generation);
                    if (const auto generation = FirstRelativeComponent(file.Path, runtimeRoot, manifest.Platform))
                        runtimeGenerations.emplace(*generation);
                    if (IsWithin(file.Path, variant.Root, manifest.Platform))
                    {
                        if (const auto abi = FfmpegAbi(file.Path))
                            ffmpegGenerations[LogicalPathKey(variant.Root, manifest.Platform) + "/" + abi->first]
                                .emplace(abi->second);
                    }
                }
                if (hostGenerations.size() != 1 || runtimeGenerations.size() != 1)
                    throw std::invalid_argument(
                        "Player support manifest must contain exactly one .NET ABI generation.");
                RequireFile(logicalFiles, hostRoot / *hostGenerations.begin() / HostFxrName(manifest.Platform),
                            manifest.Platform);
                const auto runtimeDirectory = runtimeRoot / *runtimeGenerations.begin();
                RequireFile(logicalFiles, runtimeDirectory / CoreClrName(manifest.Platform), manifest.Platform);
                RequireFile(logicalFiles, runtimeDirectory / HostPolicyName(manifest.Platform), manifest.Platform);
                RequireFile(logicalFiles, runtimeDirectory / "System.Private.CoreLib.dll", manifest.Platform);

                allowedExecutables.emplace(LogicalPathKey(executable, manifest.Platform));
                const auto createDump = runtimeDirectory / CreateDumpName(manifest.Platform);
                if (logicalFiles.contains(LogicalPathKey(createDump, manifest.Platform)))
                    allowedExecutables.emplace(LogicalPathKey(createDump, manifest.Platform));
            }

            for (const auto& file : manifest.Files)
            {
                if (std::ranges::none_of(manifest.Variants, [&](const auto& variant)
                                         { return IsWithin(file.Path, variant.Root, manifest.Platform); }) ||
                    HasProhibitedRuntimeComponent(file.Path))
                    throw std::invalid_argument("Player support manifest contains non-runtime payload data.");

                const auto pathKey = LogicalPathKey(file.Path, manifest.Platform);
                const auto filename = CaseFoldedPath(file.Path.filename());
                if ((filename == "createdump" || filename == "createdump.exe") && !allowedExecutables.contains(pathKey))
                    throw std::invalid_argument("Player support manifest contains a misplaced createdump executable.");
                if (file.Mode == 0755U && !allowedExecutables.contains(pathKey))
                    throw std::invalid_argument("Player support manifest contains an unexpected executable file.");
                if (allowedExecutables.contains(pathKey) && file.Mode != 0755U)
                    throw std::invalid_argument("Player support manifest executable mode is invalid.");
                if (CaseFoldedPath(file.Path.extension()) == ".exe" && !allowedExecutables.contains(pathKey))
                    throw std::invalid_argument("Player support manifest contains an unexpected executable.");
            }
            if (std::ranges::any_of(ffmpegGenerations, [](const auto& entry) { return entry.second.size() > 1; }))
                throw std::invalid_argument("Player support manifest runtime closure is incomplete or ambiguous.");
        }

        [[nodiscard]] const PlayerSupportVariant& FindVariant(const PlayerSupportManifest& manifest,
                                                              const PlayerBuildConfiguration configuration)
        {
            const auto found =
                std::ranges::find(manifest.Variants, configuration, &PlayerSupportVariant::Configuration);
            if (found == manifest.Variants.end())
                throw std::runtime_error("The player support module does not contain the requested configuration.");
            return *found;
        }

        [[nodiscard]] std::vector<std::filesystem::path> CandidateManifestPaths(const std::filesystem::path& executable,
                                                                                const PlayerPlatform platform,
                                                                                const PlayerArchitecture architecture)
        {
            const auto& build = GetBuildInfo();
            const auto target = std::string(ToString(platform)) + "-" + std::string(ToString(architecture));
            std::vector<std::filesystem::path> roots{PlayerSupportStorageRoot() / std::string(build.Version),
                                                     executable.parent_path() / "BuildSupport" /
                                                         std::string(build.Version)};
            std::vector<std::filesystem::path> result;
            for (const auto& root : roots)
            {
                std::error_code error;
                if (!std::filesystem::is_directory(root, error) || error)
                    continue;
                for (const auto& entry : std::filesystem::directory_iterator(root, error))
                {
                    if (error)
                        break;
                    if (!entry.is_directory(error) || error)
                        continue;
                    const auto manifest = entry.path() / "manifest.json";
                    if (std::filesystem::is_regular_file(manifest, error) && !error &&
                        PathToUtf8(entry.path().filename()).find(target) != std::string::npos)
                        result.push_back(manifest);
                }
            }
            std::ranges::sort(result);
            return result;
        }

        [[nodiscard]] std::optional<ResolvedPlayerSupport>
        DevelopmentSupport(const std::filesystem::path& executable, const PlayerPlatform platform,
                           const PlayerArchitecture architecture, const PlayerBuildConfiguration configuration,
                           const std::string& moduleFingerprint)
        {
            if (platform != HostPlayerPlatform() || architecture != HostPlayerArchitecture())
                return std::nullopt;

            const auto configurationName = configuration == PlayerBuildConfiguration::Development ? std::string("Debug")
                                           : configuration == PlayerBuildConfiguration::Release ? std::string("Release")
                                                                                                : std::string("Dist");
            std::vector<std::filesystem::path> runtimeCandidates;
            if (GetBuildInfo().Configuration == configurationName)
            {
                try
                {
                    runtimeCandidates.push_back(ResolveCompanionExecutable(executable, "KeireRuntime").parent_path());
                }
                catch (const std::exception&)
                {
                }
            }
            auto root = executable;
            for (std::size_t index = 0; index < 5 && root.has_parent_path(); ++index)
                root = root.parent_path();
            runtimeCandidates.push_back(root / "Build" / "Bin" /
                                        (configurationName + "-" + std::string(ToString(platform)) + "-" +
                                         std::string(ToString(architecture))) /
                                        "KeireRuntime");

#if defined(_WIN32)
            constexpr std::string_view runtimeName = "KeireRuntime.exe";
#else
            constexpr std::string_view runtimeName = "KeireRuntime";
#endif
            for (const auto& candidate : runtimeCandidates)
            {
                if (!std::filesystem::is_regular_file(candidate / runtimeName))
                    continue;
                PlayerSupportVariant variant{
                    .Configuration = configuration, .Root = ".", .Executable = PathFromUtf8(runtimeName)};
#if defined(_WIN32)
                if (std::filesystem::is_regular_file(candidate / "KeireRuntime.pdb"))
                    variant.Symbols.push_back("KeireRuntime.pdb");
                if (std::filesystem::is_regular_file(candidate / "KeireRuntime.ilk"))
                    variant.Symbols.push_back("KeireRuntime.ilk");
#endif
                return ResolvedPlayerSupport{.Manifest = {.Id = "development-host-fallback",
                                                          .EngineVersion = std::string(GetBuildInfo().Version),
                                                          .Platform = platform,
                                                          .Architecture = architecture,
                                                          .ModuleFingerprint = moduleFingerprint,
                                                          .Variants = {variant}},
                                             .Variant = std::move(variant),
                                             .InstallationRoot = candidate,
                                             .DevelopmentFallback = true};
            }
            return std::nullopt;
        }
    } // namespace

    std::filesystem::path PlayerSupportStorageRoot() { return GetPreferenceDirectory() / "BuildSupport"; }

    PlayerSupportManifest LoadPlayerSupportManifest(const std::filesystem::path& path)
    {
        return DecodePlayerSupportManifest(ReadTextFile(path, MaximumManifestBytes));
    }

    PlayerSupportManifest DecodePlayerSupportManifest(const std::string_view text)
    {
        if (text.size() > MaximumManifestBytes)
            throw std::runtime_error("Player support manifest exceeds its size limit.");
        const auto document = Json::parse(text);
        if (!document.is_object() || !document.value("variants", Json::array()).is_array())
            throw std::runtime_error("Player support manifest root is invalid.");
        PlayerSupportManifest result;
        result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        result.PlayerAbi = document.at("playerAbi").get<std::uint32_t>();
        result.Id = document.at("id").get<std::string>();
        result.EngineVersion = document.at("engineVersion").get<std::string>();
        result.Platform = ParseEnum<PlayerPlatform>(
            document.at("platform").get<std::string>(),
            {{"windows", PlayerPlatform::Windows}, {"linux", PlayerPlatform::Linux}, {"macos", PlayerPlatform::MacOS}},
            "player support platform");
        result.Architecture = ParseEnum<PlayerArchitecture>(
            document.at("architecture").get<std::string>(),
            {{"x86_64", PlayerArchitecture::X86_64}, {"arm64", PlayerArchitecture::Arm64}},
            "player support architecture");
        result.ModuleFingerprint = document.at("moduleFingerprint").get<std::string>();
        result.SourceModules = document.value("sourceModules", std::vector<std::string>{});
        for (const auto& encoded : document.at("variants"))
        {
            PlayerSupportVariant variant;
            variant.Configuration =
                ParseEnum<PlayerBuildConfiguration>(encoded.at("configuration").get<std::string>(),
                                                    {{"development", PlayerBuildConfiguration::Development},
                                                     {"release", PlayerBuildConfiguration::Release},
                                                     {"dist", PlayerBuildConfiguration::Dist}},
                                                    "player support configuration");
            variant.Root = PathFromUtf8(encoded.at("root").get<std::string>());
            variant.Executable = PathFromUtf8(encoded.at("executable").get<std::string>());
            variant.Bundle = PathFromUtf8(encoded.value("bundle", std::string{}));
            for (const auto& symbol : encoded.value("symbols", Json::array()))
                variant.Symbols.push_back(PathFromUtf8(symbol.get<std::string>()));
            result.Variants.push_back(std::move(variant));
        }
        for (const auto& encoded : document.value("files", Json::array()))
        {
            result.Files.push_back({.Path = PathFromUtf8(encoded.at("path").get<std::string>()),
                                    .Size = encoded.at("size").get<std::uint64_t>(),
                                    .Sha256 = encoded.at("sha256").get<std::string>(),
                                    .Mode = encoded.value("mode", 0644U)});
        }
        for (const auto& encoded : document.value("brandingSlots", Json::array()))
        {
            result.BrandingSlots.push_back({.Path = PathFromUtf8(encoded.at("path").get<std::string>()),
                                            .Kind = encoded.at("kind").get<std::string>(),
                                            .Offset = encoded.at("offset").get<std::uint64_t>(),
                                            .Size = encoded.at("size").get<std::uint64_t>()});
        }
        ValidatePlayerSupportManifest(result);
        return result;
    }

    std::string EncodePlayerSupportManifest(const PlayerSupportManifest& manifest)
    {
        ValidatePlayerSupportManifest(manifest);
        Json variants = Json::array();
        for (const auto& variant : manifest.Variants)
        {
            Json symbols = Json::array();
            for (const auto& symbol : variant.Symbols)
                symbols.push_back(PathToUtf8(symbol));
            variants.push_back({{"configuration", ToString(variant.Configuration)},
                                {"root", PathToUtf8(variant.Root)},
                                {"executable", PathToUtf8(variant.Executable)},
                                {"bundle", PathToUtf8(variant.Bundle)},
                                {"symbols", std::move(symbols)}});
        }
        Json files = Json::array();
        for (const auto& file : manifest.Files)
            files.push_back(
                {{"path", PathToUtf8(file.Path)}, {"size", file.Size}, {"sha256", file.Sha256}, {"mode", file.Mode}});
        Json slots = Json::array();
        for (const auto& slot : manifest.BrandingSlots)
            slots.push_back(
                {{"path", PathToUtf8(slot.Path)}, {"kind", slot.Kind}, {"offset", slot.Offset}, {"size", slot.Size}});
        const Json document{{"schemaVersion", manifest.SchemaVersion},
                            {"playerAbi", manifest.PlayerAbi},
                            {"id", manifest.Id},
                            {"engineVersion", manifest.EngineVersion},
                            {"platform", ToString(manifest.Platform)},
                            {"architecture", ToString(manifest.Architecture)},
                            {"moduleFingerprint", manifest.ModuleFingerprint},
                            {"sourceModules", manifest.SourceModules},
                            {"variants", std::move(variants)},
                            {"files", std::move(files)},
                            {"brandingSlots", std::move(slots)}};
        return document.dump(2) + '\n';
    }

    void ValidatePlayerSupportManifest(const PlayerSupportManifest& manifest)
    {
        if (manifest.SchemaVersion != PlayerSupportManifestSchemaVersion ||
            manifest.PlayerAbi != PlayerBuildAbiVersion || !IsSafeComponent(manifest.Id) ||
            !IsSafeComponent(manifest.EngineVersion) || manifest.ModuleFingerprint.empty() || manifest.Variants.empty())
            throw std::invalid_argument("Player support manifest identity is invalid or incompatible.");
        std::unordered_set<std::string> sourceModules;
        for (const auto& module : manifest.SourceModules)
            if (!IsSafeComponent(module) || !sourceModules.emplace(module).second)
                throw std::invalid_argument("Player support source-module catalog is invalid.");
        std::unordered_set<unsigned> configurations;
        std::unordered_set<std::string> paths;
        for (const auto& variant : manifest.Variants)
        {
            if (!configurations.emplace(static_cast<unsigned>(variant.Configuration)).second ||
                !IsConfinedRelativePath(variant.Root) || !IsConfinedRelativePath(variant.Executable) ||
                (!variant.Bundle.empty() && !IsConfinedRelativePath(variant.Bundle)) ||
                std::ranges::any_of(variant.Symbols,
                                    [](const auto& symbol) { return !IsConfinedRelativePath(symbol); }))
                throw std::invalid_argument("Player support manifest paths or variants are invalid.");
        }
        for (const auto& file : manifest.Files)
        {
            const auto key = LogicalPathKey(file.Path, manifest.Platform);
            const bool validDigest =
                file.Sha256.size() == 64 && std::ranges::all_of(file.Sha256, [](const unsigned char character)
                                                                { return std::isxdigit(character) != 0; });
            if (!IsConfinedRelativePath(file.Path) || file.Size > 16ULL * 1024ULL * 1024ULL * 1024ULL || !validDigest ||
                (file.Mode != 0644U && file.Mode != 0755U) || !paths.emplace(key).second)
                throw std::invalid_argument("Player support manifest file records are invalid.");
        }
        for (const auto& slot : manifest.BrandingSlots)
        {
            if (!IsConfinedRelativePath(slot.Path) || slot.Kind.empty() || slot.Kind.size() > 64 || slot.Size == 0 ||
                slot.Offset > 16ULL * 1024ULL * 1024ULL * 1024ULL ||
                slot.Size > 16ULL * 1024ULL * 1024ULL * 1024ULL - slot.Offset)
                throw std::invalid_argument("Player support branding slots are invalid.");
            const auto slotKey = LogicalPathKey(slot.Path, manifest.Platform);
            const auto file =
                std::ranges::find_if(manifest.Files, [&](const auto& candidate)
                                     { return LogicalPathKey(candidate.Path, manifest.Platform) == slotKey; });
            if (!manifest.Files.empty() &&
                (file == manifest.Files.end() || slot.Offset > file->Size || slot.Size > file->Size - slot.Offset))
                throw std::invalid_argument("Player support branding slot exceeds its file.");
        }
        ValidateRuntimeClosure(manifest);
    }

    ResolvedPlayerSupport ResolvePlayerSupport(const std::filesystem::path& executable, const PlayerPlatform platform,
                                               const PlayerArchitecture architecture,
                                               const PlayerBuildConfiguration configuration,
                                               const std::string& moduleFingerprint)
    {
        std::string diagnostic;
        for (const auto& path : CandidateManifestPaths(executable, platform, architecture))
        {
            try
            {
                auto manifest = LoadPlayerSupportManifest(path);
                if (manifest.EngineVersion != GetBuildInfo().Version || manifest.PlayerAbi != PlayerBuildAbiVersion ||
                    manifest.Platform != platform || manifest.Architecture != architecture ||
                    manifest.ModuleFingerprint != moduleFingerprint)
                    continue;
                const auto& variant = FindVariant(manifest, configuration);
                const auto installation = path.parent_path();
                if (!std::filesystem::is_directory(installation / variant.Root) ||
                    !std::filesystem::is_regular_file(installation / variant.Root / variant.Executable))
                    throw std::runtime_error("Player support payload is incomplete.");
                auto selectedVariant = variant;
                return {.Manifest = std::move(manifest),
                        .Variant = std::move(selectedVariant),
                        .InstallationRoot = installation};
            }
            catch (const std::exception& error)
            {
                diagnostic = error.what();
            }
        }
        if (auto fallback = DevelopmentSupport(executable, platform, architecture, configuration, moduleFingerprint))
            return std::move(*fallback);
        const auto target = std::string(ToString(platform)) + " " + std::string(ToString(architecture));
        throw std::runtime_error("No compatible Build Support module is installed for " + target +
                                 (diagnostic.empty() ? "." : ": " + diagnostic));
    }
} // namespace Keire::Detail
