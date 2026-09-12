#pragma once

#include <algorithm>

namespace KeireEditor
{
    struct ShaderGraphPaneLayout final
    {
        float CanvasWidth;
        float PreviewWidth;
    };

    [[nodiscard]] constexpr ShaderGraphPaneLayout ResolveShaderGraphPaneLayout(const float width,
                                                                               const bool preview) noexcept
    {
        const float available = std::max(1.0F, width);
        const float previewWidth = preview && available >= 620.0F ? 248.0F : 0.0F;
        return {available - (previewWidth > 0.0F ? previewWidth + 8.0F : 0.0F), previewWidth};
    }
} // namespace KeireEditor
