#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumShaderIncludeRoots = 16;

    struct ShaderSourceFile final
    {
        AssetSourceDependency Dependency;
        std::vector<std::byte> Bytes;
    };

    [[nodiscard]] std::filesystem::path ConfinedShaderSourcePath(const std::filesystem::path& path,
                                                                 bool allowProjectRoot = false);

    // Literal includes only, in local-file then ordered include-root precedence. The same retained bytes are used
    // for traversal, hashing and compiler staging; this is not a conditional/macro preprocessor or a compile cache.
    [[nodiscard]] std::vector<ShaderSourceFile>
    CaptureShaderSourceSnapshot(const AssetImportContext& context, const std::filesystem::path& source,
                                std::span<const std::filesystem::path> projectRelativeIncludeRoots);
} // namespace Keire::Detail
