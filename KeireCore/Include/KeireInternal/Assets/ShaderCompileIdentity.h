#pragma once

#include "Keire/Assets/ShaderCompilation.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    struct ShaderToolchainFile final
    {
        std::filesystem::path Name;
        std::vector<std::byte> Bytes;
    };

    struct ShaderToolchainSnapshot final
    {
        std::filesystem::path Executable;
        std::vector<ShaderToolchainFile> Files;
        std::string Sha256;
    };

    // The caller supplies the complete flat runtime bundle, not just the compiler executable.
    [[nodiscard]] ShaderToolchainSnapshot
    CaptureShaderToolchain(const std::filesystem::path& directory, const std::filesystem::path& executable,
                           std::span<const std::filesystem::path> runtimeFiles,
                           std::size_t maximumBytes = std::size_t{512} * 1024U * 1024U);

    [[nodiscard]] std::filesystem::path StageShaderToolchain(const ShaderToolchainSnapshot& snapshot,
                                                             const std::filesystem::path& directory);

    // Schema-2 service manifests do not contain ordered roots or source virtual paths. The local import key
    // wraps their complete unit identities without changing the public manifest ABI or aliasing root orders.
    [[nodiscard]] std::string ShaderImportWorkKey(std::span<const ShaderCompileManifest> units,
                                                  const std::filesystem::path& source,
                                                  std::span<const std::filesystem::path> includeRoots,
                                                  std::string_view sourceManifestSha256);
} // namespace Keire::Detail
