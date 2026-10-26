#include "KeireInternal/Assets/ShaderImportCache.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/RenderingAssetValidation.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string_view>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::string_view EnvelopeMagic = "keire-shader-import-cache-v1\n";
        constexpr std::size_t DigestBytes = 64;
        constexpr std::size_t HeaderBytes = EnvelopeMagic.size() + DigestBytes * 2;
        constexpr std::string_view CacheDirectory = "Library/ShaderCompileCache/v1";

        [[nodiscard]] std::string Digest(const std::span<const std::byte> bytes)
        {
            return DigestToString(Sha256(bytes));
        }

        [[nodiscard]] bool EnvironmentSet(const char* name)
        {
#if defined(_WIN32)
            char* value = nullptr;
            std::size_t size = 0;
            const auto error = _dupenv_s(&value, &size, name);
            const bool present = error != 0 || (value && *value);
            std::free(value);
            return present;
#else
            const char* value = std::getenv(name);
            return value && *value;
#endif
        }

        [[nodiscard]] ShaderToolchainSnapshot CaptureBundledCompiler(const std::filesystem::path& compiler)
        {
            // Overrides can change the loaded runtime outside the flat bundle. Custom compilers still work,
            // but do not opt into reuse based on an incomplete executable-only identity.
            for (const char* name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                                     "DYLD_INSERT_LIBRARIES"})
            {
                if (EnvironmentSet(name))
                {
                    throw std::runtime_error("Shader runtime loader overrides disable persistent reuse.");
                }
            }
#if defined(_WIN32)
            constexpr std::string_view executableName = "KeireShaderCompiler.exe";
            constexpr std::string_view requiredRuntime = "dxcompiler.dll";
#elif defined(__APPLE__)
            constexpr std::string_view executableName = "KeireShaderCompiler";
            constexpr std::string_view requiredRuntime = "libdxcompiler.dylib";
#else
            constexpr std::string_view executableName = "KeireShaderCompiler";
            constexpr std::string_view requiredRuntime = "libdxcompiler.so";
#endif
            if (compiler.filename() != executableName)
            {
                throw std::runtime_error("Persistent shader reuse requires the flat bundled compiler.");
            }
            std::vector<std::filesystem::path> runtimes;
            for (const auto& entry : std::filesystem::directory_iterator(compiler.parent_path()))
            {
                const auto name = PathToUtf8(entry.path().filename());
                const bool runtime = name.ends_with(".dll") || name.find(".so") != std::string::npos ||
                                     name.find(".dylib") != std::string::npos;
                if (runtime)
                {
                    runtimes.push_back(entry.path().filename());
                }
                if (runtimes.size() > 63U)
                {
                    throw std::runtime_error("Shader runtime bundle exceeds its file bound.");
                }
            }
            if (std::ranges::find(runtimes, requiredRuntime) == runtimes.end())
            {
                throw std::runtime_error("Shader runtime bundle is incomplete.");
            }
#if defined(_WIN32)
            if (std::ranges::find(runtimes, "dxil.dll") == runtimes.end())
            {
                throw std::runtime_error("Shader runtime bundle is missing the DXIL validator.");
            }
#endif
            return CaptureShaderToolchain(compiler.parent_path(), compiler.filename(), runtimes);
        }

        [[nodiscard]] ShaderCompileTarget Target(const ShaderBinaryFormat format)
        {
            ShaderCompileTarget target;
#if defined(__aarch64__) || defined(_M_ARM64)
            target.Architecture = ShaderCompileArchitecture::Arm64;
#endif
            if (format == ShaderBinaryFormat::Dxil)
            {
                target.Platform = ShaderCompilePlatform::Windows;
                target.Format = ShaderCompileBinaryFormat::Dxil;
            }
            else if (format == ShaderBinaryFormat::Msl)
            {
                target.Platform = ShaderCompilePlatform::MacOS;
                target.Format = ShaderCompileBinaryFormat::Msl;
            }
            else
            {
#if defined(_WIN32)
                target.Platform = ShaderCompilePlatform::Windows;
#else
                target.Platform = ShaderCompilePlatform::Linux;
#endif
                target.Format = ShaderCompileBinaryFormat::SpirV;
            }
            return target;
        }

        [[nodiscard]] bool ContainsVolatileText(const std::string_view text)
        {
            std::string logical;
            logical.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\\' && index + 1 < text.size())
                {
                    if (text[index + 1] == '\n')
                    {
                        ++index;
                        continue;
                    }
                    if (text[index + 1] == '\r' && index + 2 < text.size() && text[index + 2] == '\n')
                    {
                        index += 2;
                        continue;
                    }
                }
                logical.push_back(text[index]);
            }
            // Token pasting can synthesize a volatile predefined macro. Bypass rather than guessing expansion.
            for (const auto macro : {"__DATE__", "__TIME__", "__TIMESTAMP__", "##"})
            {
                if (logical.find(macro) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool ContainsVolatileMacros(const std::span<const ShaderSourceFile> sources)
        {
            for (const auto& source : sources)
            {
                const std::string_view text(reinterpret_cast<const char*>(source.Bytes.data()), source.Bytes.size());
                if (ContainsVolatileText(text))
                {
                    return true;
                }
            }
            return false;
        }

        void Append(std::vector<std::byte>& destination, const std::string_view text)
        {
            const auto bytes = std::as_bytes(std::span(text));
            destination.insert(destination.end(), bytes.begin(), bytes.end());
        }
    } // namespace

    std::unique_ptr<ShaderImportCache> ShaderImportCache::TryCreate(
        const std::filesystem::path& compiler, const AssetImportContext& context,
        const ShaderAssetDefinition& definition, const ShaderImporterSpecification& specification,
        const std::span<const ShaderPassCompileLane> passes, const std::span<const ShaderSourceFile> sources,
        const std::span<const std::filesystem::path> includeRoots,
        const std::span<const std::pair<std::string, std::string>> defines,
        const std::span<const std::byte> authoredManifest)
    {
        try
        {
            const std::string_view manifestText(reinterpret_cast<const char*>(authoredManifest.data()),
                                                authoredManifest.size());
            if (context.ProjectRoot.empty() || ContainsVolatileMacros(sources) || ContainsVolatileText(manifestText))
            {
                return {};
            }
            auto cache = std::unique_ptr<ShaderImportCache>(new ShaderImportCache());
            cache->m_Project = context.ProjectRoot;
            cache->m_Toolchain = CaptureBundledCompiler(compiler);
            ShaderCompileManifest base;
            base.ToolchainSha256 = cache->m_Toolchain.Sha256;
            for (const auto& source : sources)
            {
                base.Dependencies.push_back({PathToUtf8(source.Dependency.RelativePath), source.Dependency.Digest});
                if (source.Dependency.RelativePath == ConfinedShaderSourcePath(definition.Source))
                {
                    base.SourceSha256 = source.Dependency.Digest;
                }
            }
            for (const auto& [name, value] : defines)
            {
                base.Defines.push_back({name, value});
            }
            std::vector<ShaderCompileManifest> units;
            for (const auto& pass : passes)
            {
                auto lane = base;
                lane.PassRole = pass.Role;
                if (pass.Define)
                {
                    lane.Defines.push_back({*pass.Define, "1"});
                }
                for (const auto format : specification.Formats)
                {
                    lane.Target = Target(format);
                    lane.Stage = ShaderCompileStage::Vertex;
                    lane.EntryPoint = definition.VertexEntry;
                    units.push_back(lane);
                    lane.Stage = ShaderCompileStage::Fragment;
                    lane.EntryPoint = definition.FragmentEntry;
                    units.push_back(lane);
                }
            }
            const auto workKey = ShaderImportWorkKey(units, definition.Source, includeRoots, Digest(authoredManifest));
            // Version the whole-import/reflection contract separately from the public unit manifest ABI.
            const auto policy = "keire-validated-graphics-import-v11:" + workKey + ":" +
                                std::to_string(specification.MaximumOutputBytes);
            cache->m_Key = Digest(std::as_bytes(std::span(policy)));
            const auto slot = std::stoul(cache->m_Key.substr(0, 2), nullptr, 16) % ShaderImportCacheSlotCount;
            cache->m_Slot = std::filesystem::path(CacheDirectory) / (std::to_string(slot) + ".bin");
            return cache;
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    std::vector<std::byte> ShaderImportCache::TryRead(const ShaderAssetDefinition& expected,
                                                      const ShaderImporterSpecification& specification,
                                                      const std::span<const ShaderPassCompileLane> passes) const
    {
        try
        {
            const AnchoredFileSystem files(m_Project, AnchoredRootPolicy::RejectLink);
            const auto encoded = files.Read(m_Slot, ShaderImportCacheMaximumEntryBytes);
            if (encoded.size() <= HeaderBytes)
            {
                return {};
            }
            const std::string_view header(reinterpret_cast<const char*>(encoded.data()), HeaderBytes);
            const auto payload = std::span(encoded).subspan(HeaderBytes);
            if (!header.starts_with(EnvelopeMagic) || header.substr(EnvelopeMagic.size(), DigestBytes) != m_Key ||
                header.substr(EnvelopeMagic.size() + DigestBytes) != Digest(payload))
            {
                return {};
            }
            const auto shader = ShaderAsset::Decode(payload);
            const auto& definition = shader->Definition();
            ValidateShaderDefinition(definition, specification.Formats.size() == 3);
            const bool sameDependencies = std::ranges::equal(
                definition.Dependencies, expected.Dependencies, [](const auto& left, const auto& right)
                { return left.RelativePath == right.RelativePath && left.Digest == right.Digest; });
            if (!sameDependencies || definition.Variants.size() != passes.size() * specification.Formats.size())
            {
                return {};
            }
            std::set<std::pair<std::string, ShaderBinaryFormat>> expectedVariants;
            for (const auto& pass : passes)
            {
                for (const auto format : specification.Formats)
                {
                    expectedVariants.emplace(pass.Role, format);
                }
            }
            for (const auto& variant : definition.Variants)
            {
                if (variant.Vertex.size() > specification.MaximumOutputBytes ||
                    variant.Fragment.size() > specification.MaximumOutputBytes ||
                    expectedVariants.erase({variant.PassRole, variant.Format}) != 1U)
                {
                    return {};
                }
            }
            return expectedVariants.empty() ? std::vector<std::byte>(payload.begin(), payload.end())
                                            : std::vector<std::byte>{};
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    void ShaderImportCache::TryStore(const std::span<const std::byte> validatedArtifact) const
    {
        if (validatedArtifact.empty() || validatedArtifact.size() > ShaderImportCacheMaximumEntryBytes - HeaderBytes)
        {
            return;
        }
        try
        {
            std::vector<std::byte> encoded;
            encoded.reserve(HeaderBytes + validatedArtifact.size());
            Append(encoded, EnvelopeMagic);
            Append(encoded, m_Key);
            Append(encoded, Digest(validatedArtifact));
            encoded.insert(encoded.end(), validatedArtifact.begin(), validatedArtifact.end());
            const AnchoredFileSystem files(m_Project, AnchoredRootPolicy::RejectLink);
            files.CreateDirectories(std::filesystem::path(CacheDirectory));
            files.WriteFileAtomically(m_Slot, encoded);
        }
        catch (const std::exception&)
        {
            // Cache storage never turns a successfully validated import into a failure.
        }
    }

    std::filesystem::path ShaderImportCache::StageCompiler(const std::filesystem::path& directory) const
    {
        const AnchoredFileSystem scratch(directory.parent_path(), AnchoredRootPolicy::RejectLink);
        scratch.CreateDirectories(directory.filename());
        return StageShaderToolchain(m_Toolchain, directory);
    }
} // namespace Keire::Detail
