#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>

namespace KeireEditor::Detail
{
    void CheckShaderGraphPreviewCancellation(const ShaderGraphPreviewRequest& request)
    {
        if (request.CancellationRequested && request.CancellationRequested())
            throw std::runtime_error("Shader Graph preview rendering was superseded.");
    }

    std::string LowerShaderGraphPreviewText(const std::string_view value)
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        return result;
    }

    std::optional<Keire::Vector4>
    SampleShaderGraphPreviewTexture(const std::span<const ShaderGraphPreviewTexture> textures,
                                    const Keire::AssetId asset, Keire::Vector2 uv) noexcept
    {
        const auto resolved = std::ranges::find(textures, asset, &ShaderGraphPreviewTexture::Asset);
        if (resolved == textures.end() || !resolved->Texture || resolved->Texture->Mips().empty())
            return std::nullopt;
        const auto& mip = resolved->Texture->Mips().front();
        if (mip.Width == 0 || mip.Height == 0 ||
            mip.Pixels.size() != static_cast<std::size_t>(mip.Width) * mip.Height * 4U)
            return std::nullopt;
        uv.X -= std::floor(uv.X);
        uv.Y -= std::floor(uv.Y);
        const auto x = std::min(static_cast<std::uint32_t>(uv.X * static_cast<float>(mip.Width)), mip.Width - 1U);
        const auto y = std::min(static_cast<std::uint32_t>(uv.Y * static_cast<float>(mip.Height)), mip.Height - 1U);
        const auto offset = (static_cast<std::size_t>(y) * mip.Width + x) * 4U;
        if (resolved->Texture->Settings().HighDynamicRange)
        {
            const auto exponent =
                std::ldexp(1.0F, static_cast<int>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 3U])) - 136);
            return Keire::Vector4{static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset])) * exponent,
                                  static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 1U])) * exponent,
                                  static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 2U])) * exponent,
                                  1.0F};
        }
        constexpr float byteScale = 1.0F / 255.0F;
        return Keire::Vector4{static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset])) * byteScale,
                              static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 1U])) * byteScale,
                              static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 2U])) * byteScale,
                              static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + 3U])) * byteScale};
    }
} // namespace KeireEditor::Detail
