#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <filesystem>
#include <span>

namespace KeireEditor
{
    struct ShaderGraphPublication final
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceDirectory;
        std::filesystem::path GraphRelativePath;
        Keire::AssetId Asset;
        std::span<const Keire::ShaderGraphShaderVariant> Variants;
        std::span<const std::byte> GraphBytes;
    };

    void PublishShaderGraph(const ShaderGraphPublication& publication);
} // namespace KeireEditor
