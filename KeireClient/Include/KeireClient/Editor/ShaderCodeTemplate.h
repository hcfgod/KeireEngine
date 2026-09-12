#pragma once

#include <filesystem>
#include <string>

namespace KeireEditor
{
    struct ShaderCodeTemplate
    {
        std::string Hlsl;
        std::string Manifest;
    };

    /// Emits editable code using the same property, vertex and material-pass contracts as an Unlit graph.
    [[nodiscard]] ShaderCodeTemplate CreateUnlitShaderCodeTemplate(const std::filesystem::path& projectSource);
} // namespace KeireEditor
