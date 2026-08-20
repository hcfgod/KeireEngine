#pragma once

#include "KeireClient/Editor/ShaderGraphPreview.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor::Detail
{
    [[nodiscard]] std::optional<Keire::Vector4>
    SampleShaderGraphPreviewTexture(std::span<const ShaderGraphPreviewTexture> textures, Keire::AssetId asset,
                                    Keire::Vector2 uv) noexcept;
    void CheckShaderGraphPreviewCancellation(const ShaderGraphPreviewRequest& request);
    [[nodiscard]] std::string LowerShaderGraphPreviewText(std::string_view value);
} // namespace KeireEditor::Detail
