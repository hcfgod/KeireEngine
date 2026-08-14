#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    inline constexpr std::array<std::string_view, 6> EditorRendererCapabilityNames = {
        "surface", "compute", "pbr", "shader-graph", "material-graph", "vfx-graph"};

    [[nodiscard]] inline std::vector<std::string> EditorRendererCapabilities()
    {
        return {EditorRendererCapabilityNames.begin(), EditorRendererCapabilityNames.end()};
    }
} // namespace KeireEditor
