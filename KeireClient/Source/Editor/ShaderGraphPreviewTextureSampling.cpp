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
    namespace
    {
        [[nodiscard]] double AddressCoordinate(const float coordinate, const Keire::TextureAddressMode mode) noexcept
        {
            const double value = coordinate;
            if (mode == Keire::TextureAddressMode::Clamp)
                return std::clamp(value, 0.0, 1.0);
            if (mode == Keire::TextureAddressMode::Mirror)
            {
                const double wrapped = value - 2.0 * std::floor(value / 2.0);
                return wrapped <= 1.0 ? wrapped : 2.0 - wrapped;
            }
            return value - std::floor(value);
        }

        [[nodiscard]] std::uint32_t AddressTexel(std::int64_t index, const std::uint32_t extent,
                                                 const Keire::TextureAddressMode mode) noexcept
        {
            if (mode != Keire::TextureAddressMode::Repeat)
                return static_cast<std::uint32_t>(std::clamp(index, std::int64_t{0}, std::int64_t{extent} - 1));
            index %= extent;
            if (index < 0)
                index += extent;
            return static_cast<std::uint32_t>(index);
        }

        [[nodiscard]] float DecodeSrgb(const float value) noexcept
        {
            return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
        }

        [[nodiscard]] Keire::Vector4 Blend(const Keire::Vector4 first, const Keire::Vector4 second,
                                           const float factor) noexcept
        {
            return {std::lerp(first.X, second.X, factor), std::lerp(first.Y, second.Y, factor),
                    std::lerp(first.Z, second.Z, factor), std::lerp(first.W, second.W, factor)};
        }
    } // namespace

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
        if (!std::isfinite(uv.X) || !std::isfinite(uv.Y))
            return std::nullopt;
        const auto resolved = std::ranges::find(textures, asset, &ShaderGraphPreviewTexture::Asset);
        if (resolved == textures.end() || !resolved->Texture || resolved->Texture->Mips().empty())
            return std::nullopt;
        const auto& mip = resolved->Texture->Mips().front();
        if (mip.Width == 0 || mip.Height == 0 ||
            mip.Pixels.size() != static_cast<std::size_t>(mip.Width) * mip.Height * 4U)
            return std::nullopt;
        const auto& settings = resolved->Texture->Settings();
        const auto sample = [&](const std::int64_t x, const std::int64_t y)
        {
            const auto column = AddressTexel(x, mip.Width, settings.Sampler.AddressU);
            const auto row = AddressTexel(y, mip.Height, settings.Sampler.AddressV);
            const auto offset = (static_cast<std::size_t>(row) * mip.Width + column) * 4U;
            const auto channel = [&](const std::size_t index)
            { return static_cast<float>(std::to_integer<std::uint8_t>(mip.Pixels[offset + index])); };
            if (settings.HighDynamicRange)
            {
                const auto encodedExponent = std::to_integer<std::uint8_t>(mip.Pixels[offset + 3U]);
                const float exponent = encodedExponent == 0U ? 0.0F : std::ldexp(1.0F, encodedExponent - 136);
                return Keire::Vector4{channel(0) * exponent, channel(1) * exponent, channel(2) * exponent, 1.0F};
            }
            constexpr float byteScale = 1.0F / 255.0F;
            Keire::Vector4 color{channel(0) * byteScale, channel(1) * byteScale, channel(2) * byteScale,
                                 channel(3) * byteScale};
            if (settings.ColorSpace == Keire::TextureColorSpace::Srgb)
            {
                color.X = DecodeSrgb(color.X);
                color.Y = DecodeSrgb(color.Y);
                color.Z = DecodeSrgb(color.Z);
            }
            return color;
        };
        const double x = AddressCoordinate(uv.X, settings.Sampler.AddressU) * mip.Width;
        const double y = AddressCoordinate(uv.Y, settings.Sampler.AddressV) * mip.Height;
        if (settings.Sampler.Magnification == Keire::TextureFilter::Nearest)
            return sample(static_cast<std::int64_t>(x), static_cast<std::int64_t>(y));

        // The CPU preview samples mip zero without screen-space derivatives. Decode each texel before filtering.
        const auto left = static_cast<std::int64_t>(std::floor(x - 0.5));
        const auto top = static_cast<std::int64_t>(std::floor(y - 0.5));
        const float horizontal = static_cast<float>(x - 0.5 - static_cast<double>(left));
        const float vertical = static_cast<float>(y - 0.5 - static_cast<double>(top));
        return Blend(Blend(sample(left, top), sample(left + 1, top), horizontal),
                     Blend(sample(left, top + 1), sample(left + 1, top + 1), horizontal), vertical);
    }
} // namespace KeireEditor::Detail
