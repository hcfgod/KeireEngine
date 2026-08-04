#pragma once

#include "Keire/Rendering/MaterialGraph.h"

#include <filesystem>
#include <span>

namespace KeireEditor
{
    struct MaterialGraphPublication final
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceDirectory;
        std::filesystem::path GraphRelativePath;
        Keire::AssetId Asset;
        std::span<const Keire::MaterialGraphShaderVariant> Variants;
        std::span<const std::byte> GraphBytes;
    };

    void PublishMaterialGraph(const MaterialGraphPublication& publication);
} // namespace KeireEditor
