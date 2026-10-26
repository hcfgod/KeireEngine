#include "KeireInternal/Assets/ShaderCompileIdentity.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/ShaderSourceSnapshot.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumToolchainFiles = 64;
        constexpr std::size_t MaximumImportUnits = 32U * 3U * 2U;

        [[nodiscard]] std::filesystem::path BundleFileName(const std::filesystem::path& path)
        {
            const auto name = PathToUtf8(path);
            if (path.empty() || path.has_root_path() || path != path.filename() || name == "." || name == ".." ||
                name.size() > 255U || name.find_first_of("/\\:") != std::string::npos ||
                std::ranges::any_of(name, [](const unsigned char value) { return value < 32U || value == 127U; }))
            {
                throw std::invalid_argument("Shader toolchain files must have bounded flat bundle names.");
            }
            return path;
        }

        [[nodiscard]] std::string HashDocument(const nlohmann::json& document)
        {
            const auto bytes = nlohmann::json::to_cbor(document);
            return DigestToString(Sha256(std::as_bytes(std::span(bytes))));
        }

        [[nodiscard]] std::string ToolchainIdentity(const ShaderToolchainSnapshot& snapshot)
        {
            std::map<std::string, std::string, std::less<>> sorted;
            for (const auto& file : snapshot.Files)
            {
                sorted.emplace(PathToUtf8(file.Name), DigestToString(Sha256(file.Bytes)));
            }
            nlohmann::json records = nlohmann::json::array();
            for (const auto& [name, digest] : sorted)
            {
                records.push_back({{"name", name}, {"sha256", digest}});
            }
            return HashDocument({{"domain", "keire-shader-toolchain-snapshot-v1"},
                                 {"executable", PathToUtf8(snapshot.Executable)},
                                 {"files", std::move(records)}});
        }
    } // namespace

    ShaderToolchainSnapshot CaptureShaderToolchain(const std::filesystem::path& directory,
                                                   const std::filesystem::path& executable,
                                                   const std::span<const std::filesystem::path> runtimeFiles,
                                                   const std::size_t maximumBytes)
    {
        if (maximumBytes == 0U || runtimeFiles.size() >= MaximumToolchainFiles)
        {
            throw std::invalid_argument("Shader toolchain snapshot exceeds its file or byte bound.");
        }
        ShaderToolchainSnapshot snapshot;
        snapshot.Executable = BundleFileName(executable);
        std::set<std::filesystem::path> names{snapshot.Executable};
        for (const auto& file : runtimeFiles)
        {
            if (!names.insert(BundleFileName(file)).second)
            {
                throw std::invalid_argument("Shader toolchain bundle file names must be unique.");
            }
        }
        AnchoredFileSystem fileSystem(directory, AnchoredRootPolicy::RejectLink);
        std::size_t remaining = maximumBytes;
        for (const auto& name : names)
        {
            auto bytes = fileSystem.Read(name, remaining);
            if (bytes.empty())
            {
                throw std::invalid_argument("Shader toolchain bundle files must not be empty.");
            }
            remaining -= bytes.size();
            snapshot.Files.push_back({name, std::move(bytes)});
        }
        snapshot.Sha256 = ToolchainIdentity(snapshot);
        return snapshot;
    }

    std::filesystem::path StageShaderToolchain(const ShaderToolchainSnapshot& snapshot,
                                               const std::filesystem::path& directory)
    {
        AnchoredFileSystem fileSystem(directory, AnchoredRootPolicy::RejectLink);
        std::set<std::filesystem::path> names;
        bool hasExecutable = false;
        for (const auto& file : snapshot.Files)
        {
            const auto name = BundleFileName(file.Name);
            if (file.Bytes.empty() || !names.insert(name).second)
            {
                throw std::invalid_argument("Shader toolchain staging requires unique non-empty files.");
            }
            hasExecutable |= name == snapshot.Executable;
        }
        if (!hasExecutable || snapshot.Files.size() > MaximumToolchainFiles ||
            !IsShaderCompileWorkKey(snapshot.Sha256) || ToolchainIdentity(snapshot) != snapshot.Sha256)
        {
            throw std::invalid_argument("Shader toolchain staging requires the complete unmodified snapshot.");
        }
        for (const auto& file : snapshot.Files)
        {
            std::size_t offset = 0;
            const auto permissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec;
            fileSystem.WriteFileAtomically(
                file.Name, file.Bytes.size(),
                [&file, &offset](const std::span<std::byte> chunk)
                {
                    if (offset > file.Bytes.size() || chunk.size() > file.Bytes.size() - offset)
                    {
                        throw std::runtime_error("Shader toolchain staging read exceeded captured bytes.");
                    }
                    std::memcpy(chunk.data(), file.Bytes.data() + offset, chunk.size());
                    offset += chunk.size();
                },
                permissions);
        }
        return fileSystem.Root() / snapshot.Executable;
    }

    std::string ShaderImportWorkKey(const std::span<const ShaderCompileManifest> units,
                                    const std::filesystem::path& source,
                                    const std::span<const std::filesystem::path> includeRoots,
                                    const std::string_view sourceManifestSha256)
    {
        if (units.empty() || units.size() > MaximumImportUnits || includeRoots.size() > MaximumShaderIncludeRoots ||
            !IsShaderCompileWorkKey(sourceManifestSha256))
        {
            throw std::invalid_argument("Shader import work identity exceeds its bounds or has an invalid digest.");
        }
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& unit : units)
        {
            keys.push_back(ShaderCompileWorkKey(unit));
        }
        nlohmann::json roots = nlohmann::json::array();
        for (const auto& root : includeRoots)
        {
            roots.push_back(PathToUtf8(ConfinedShaderSourcePath(root, true)));
        }
        return HashDocument({{"domain", "keire-shader-import-work-v1"},
                             {"source", PathToUtf8(ConfinedShaderSourcePath(source))},
                             {"sourceManifestSha256", sourceManifestSha256},
                             {"orderedIncludeRoots", std::move(roots)},
                             {"units", std::move(keys)}});
    }
} // namespace Keire::Detail
