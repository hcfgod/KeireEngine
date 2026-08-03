#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846F;

        struct WeightedDirection final
        {
            Vector3 Direction;
            float Weight = 0.0F;
        };

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (!std::isfinite(lengthSquared) || lengthSquared <= std::numeric_limits<float>::epsilon())
                return {0.0F, 1.0F, 0.0F};
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        [[nodiscard]] std::array<float, 9> Basis(const Vector3 direction) noexcept
        {
            const auto [x, y, z] = Normalize(direction);
            return {0.282095F,
                    0.488603F * y,
                    0.488603F * z,
                    0.488603F * x,
                    1.092548F * x * y,
                    1.092548F * y * z,
                    0.315392F * (3.0F * y * y - 1.0F),
                    1.092548F * x * z,
                    0.546274F * (z * z - x * x)};
        }

        [[nodiscard]] Vector3 DecodePixel(const TextureMipLevel& mip, const std::uint32_t x, const std::uint32_t y,
                                          const bool rgbe) noexcept
        {
            const auto index = (static_cast<std::size_t>(y) * mip.Width + x) * 4U;
            const auto red = std::to_integer<std::uint8_t>(mip.Pixels[index]);
            const auto green = std::to_integer<std::uint8_t>(mip.Pixels[index + 1U]);
            const auto blue = std::to_integer<std::uint8_t>(mip.Pixels[index + 2U]);
            if (!rgbe)
                return {red / 255.0F, green / 255.0F, blue / 255.0F};
            const auto exponent = std::to_integer<std::uint8_t>(mip.Pixels[index + 3U]);
            if (exponent == 0U)
                return {};
            const float scale = std::ldexp(1.0F, static_cast<int>(exponent) - 136);
            return {red * scale, green * scale, blue * scale};
        }

        template <typename Callback> void VisitEquirectangular(const TextureMipLevel& mip, Callback&& callback)
        {
            for (std::uint32_t y = 0; y < mip.Height; ++y)
            {
                const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(mip.Height);
                const float latitude = (0.5F - v) * Pi;
                const float latitudeCosine = std::cos(latitude);
                for (std::uint32_t x = 0; x < mip.Width; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(mip.Width);
                    const float longitude = (u - 0.5F) * 2.0F * Pi;
                    callback(x, y,
                             WeightedDirection{{std::sin(longitude) * latitudeCosine, std::sin(latitude),
                                                std::cos(longitude) * latitudeCosine},
                                               latitudeCosine});
                }
            }
        }

        [[nodiscard]] Vector3 CubemapDirection(const std::uint32_t face, const float x, const float y) noexcept
        {
            switch (face)
            {
            case 0:
                return Normalize({1.0F, -y, -x});
            case 1:
                return Normalize({-1.0F, -y, x});
            case 2:
                return Normalize({x, 1.0F, y});
            case 3:
                return Normalize({x, -1.0F, -y});
            case 4:
                return Normalize({x, -y, 1.0F});
            case 5:
                return Normalize({-x, -y, -1.0F});
            default:
                return {0.0F, 1.0F, 0.0F};
            }
        }

        template <typename Callback>
        void VisitCubemapAtlas(const TextureMipLevel& mip, const TextureEnvironmentLayout layout, Callback&& callback)
        {
            std::uint32_t columns = 0;
            std::uint32_t rows = 0;
            std::array<std::pair<std::uint32_t, std::uint32_t>, 6> cells{};
            switch (layout)
            {
            case TextureEnvironmentLayout::HorizontalCross:
                columns = 4;
                rows = 3;
                cells = {{{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}}};
                break;
            case TextureEnvironmentLayout::VerticalCross:
                columns = 3;
                rows = 4;
                cells = {{{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {1, 3}}};
                break;
            case TextureEnvironmentLayout::HorizontalStrip:
                columns = 6;
                rows = 1;
                cells = {{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}}};
                break;
            case TextureEnvironmentLayout::VerticalStrip:
                columns = 1;
                rows = 6;
                cells = {{{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}}};
                break;
            default:
                throw std::invalid_argument("Diffuse irradiance baking requires a resolved environment layout.");
            }
            if (mip.Width % columns != 0U || mip.Height % rows != 0U || mip.Width / columns != mip.Height / rows)
                throw std::invalid_argument("Cubemap atlas dimensions do not match the selected environment layout.");
            const std::uint32_t faceSize = mip.Width / columns;
            for (std::uint32_t face = 0; face < cells.size(); ++face)
            {
                const auto [cellX, cellY] = cells[face];
                for (std::uint32_t y = 0; y < faceSize; ++y)
                    for (std::uint32_t x = 0; x < faceSize; ++x)
                    {
                        const float localX =
                            (static_cast<float>(x) + 0.5F) * 2.0F / static_cast<float>(faceSize) - 1.0F;
                        const float localY =
                            (static_cast<float>(y) + 0.5F) * 2.0F / static_cast<float>(faceSize) - 1.0F;
                        const float weight = 1.0F / std::pow(1.0F + localX * localX + localY * localY, 1.5F);
                        callback(cellX * faceSize + x, cellY * faceSize + y,
                                 WeightedDirection{CubemapDirection(face, localX, localY), weight});
                    }
            }
        }

        [[nodiscard]] float RadicalInverse(std::uint32_t bits) noexcept
        {
            bits = (bits << 16U) | (bits >> 16U);
            bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xaaaaaaaaU) >> 1U);
            bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xccccccccU) >> 2U);
            bits = ((bits & 0x0f0f0f0fU) << 4U) | ((bits & 0xf0f0f0f0U) >> 4U);
            bits = ((bits & 0x00ff00ffU) << 8U) | ((bits & 0xff00ff00U) >> 8U);
            return static_cast<float>(bits) * 2.3283064365386963e-10F;
        }

        [[nodiscard]] Vector3 ImportanceSampleGgx(const float x, const float y, const float roughness) noexcept
        {
            const float alpha = roughness * roughness;
            const float phi = 2.0F * Pi * x;
            const float cosine = std::sqrt((1.0F - y) / (1.0F + (alpha * alpha - 1.0F) * y));
            const float sine = std::sqrt(std::max(1.0F - cosine * cosine, 0.0F));
            return {std::cos(phi) * sine, std::sin(phi) * sine, cosine};
        }

        [[nodiscard]] float GeometrySchlickGgx(const float normalDirection, const float roughness) noexcept
        {
            const float factor = roughness * roughness * 0.5F;
            return normalDirection / (normalDirection * (1.0F - factor) + factor);
        }

        [[nodiscard]] std::pair<float, float> IntegrateBrdf(const float normalView, const float roughness,
                                                            const std::uint32_t sampleCount) noexcept
        {
            const Vector3 view{std::sqrt(std::max(1.0F - normalView * normalView, 0.0F)), 0.0F, normalView};
            float scale = 0.0F;
            float bias = 0.0F;
            for (std::uint32_t sample = 0; sample < sampleCount; ++sample)
            {
                const Vector3 halfway =
                    ImportanceSampleGgx(static_cast<float>(sample) / sampleCount, RadicalInverse(sample), roughness);
                const float viewHalfway = std::max(view.X * halfway.X + view.Z * halfway.Z, 0.0F);
                const Vector3 light{2.0F * viewHalfway * halfway.X - view.X, 2.0F * viewHalfway * halfway.Y,
                                    2.0F * viewHalfway * halfway.Z - view.Z};
                const float normalLight = std::max(light.Z, 0.0F);
                const float normalHalfway = std::max(halfway.Z, 0.0F);
                if (normalLight <= 0.0F || normalHalfway <= 0.0F || viewHalfway <= 0.0F)
                    continue;
                const float geometry =
                    GeometrySchlickGgx(normalView, roughness) * GeometrySchlickGgx(normalLight, roughness);
                const float visibility = geometry * viewHalfway / (normalHalfway * normalView);
                const float fresnel = std::pow(1.0F - viewHalfway, 5.0F);
                scale += (1.0F - fresnel) * visibility;
                bias += fresnel * visibility;
            }
            return {scale / sampleCount, bias / sampleCount};
        }
    } // namespace

    DiffuseIrradiance BakeDiffuseIrradiance(const Texture2DAsset& environment)
    {
        if (environment.Settings().Semantic != TextureSemantic::Environment || environment.Mips().empty())
            throw std::invalid_argument("Diffuse irradiance baking requires an environment texture.");
        const auto& mip = environment.Mips().front();
        DiffuseIrradiance result;
        float totalWeight = 0.0F;
        const auto accumulate = [&](const std::uint32_t x, const std::uint32_t y, const WeightedDirection weighted)
        {
            const Vector3 radiance = DecodePixel(mip, x, y, environment.Settings().HighDynamicRange);
            const auto basis = Basis(weighted.Direction);
            totalWeight += weighted.Weight;
            for (std::size_t index = 0; index < basis.size(); ++index)
            {
                result.Coefficients[index].X += radiance.X * basis[index] * weighted.Weight;
                result.Coefficients[index].Y += radiance.Y * basis[index] * weighted.Weight;
                result.Coefficients[index].Z += radiance.Z * basis[index] * weighted.Weight;
            }
        };
        if (environment.Settings().EnvironmentLayout == TextureEnvironmentLayout::Auto ||
            environment.Settings().EnvironmentLayout == TextureEnvironmentLayout::Equirectangular)
        {
            VisitEquirectangular(mip, accumulate);
        }
        else
        {
            VisitCubemapAtlas(mip, environment.Settings().EnvironmentLayout, accumulate);
        }
        if (!std::isfinite(totalWeight) || totalWeight <= 0.0F)
            throw std::invalid_argument("Environment texture has no finite solid-angle samples.");

        constexpr std::array<float, 9> convolution{Pi,        2.0F * Pi / 3.0F, 2.0F * Pi / 3.0F, 2.0F * Pi / 3.0F,
                                                   Pi / 4.0F, Pi / 4.0F,        Pi / 4.0F,        Pi / 4.0F,
                                                   Pi / 4.0F};
        const float normalization = 4.0F * Pi / totalWeight;
        for (std::size_t index = 0; index < result.Coefficients.size(); ++index)
        {
            const float factor = normalization * convolution[index];
            result.Coefficients[index].X *= factor;
            result.Coefficients[index].Y *= factor;
            result.Coefficients[index].Z *= factor;
        }
        return result;
    }

    Vector3 EvaluateDiffuseIrradiance(const DiffuseIrradiance& irradiance, const Vector3 direction) noexcept
    {
        const auto basis = Basis(direction);
        Vector3 result;
        for (std::size_t index = 0; index < basis.size(); ++index)
        {
            result.X += irradiance.Coefficients[index].X * basis[index];
            result.Y += irradiance.Coefficients[index].Y * basis[index];
            result.Z += irradiance.Coefficients[index].Z * basis[index];
        }
        return {std::max(result.X, 0.0F), std::max(result.Y, 0.0F), std::max(result.Z, 0.0F)};
    }

    Ref<Texture2DAsset> CreateBrdfIntegrationLut(const std::uint32_t resolution, const std::uint32_t sampleCount)
    {
        if (resolution < 2U || resolution > 1024U || sampleCount < 16U || sampleCount > 4096U)
            throw std::invalid_argument("BRDF integration LUT settings are outside supported bounds.");
        TextureMipLevel mip;
        mip.Width = resolution;
        mip.Height = resolution;
        mip.Pixels.resize(static_cast<std::size_t>(resolution) * resolution * 4U);
        for (std::uint32_t y = 0; y < resolution; ++y)
            for (std::uint32_t x = 0; x < resolution; ++x)
            {
                const float normalView = (static_cast<float>(x) + 0.5F) / static_cast<float>(resolution);
                const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(resolution);
                const auto [scale, bias] = IntegrateBrdf(normalView, roughness, sampleCount);
                const auto channel = [](const float value)
                { return std::byte(static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F)); };
                const auto index = (static_cast<std::size_t>(y) * resolution + x) * 4U;
                mip.Pixels[index] = channel(scale);
                mip.Pixels[index + 1U] = channel(bias);
                mip.Pixels[index + 2U] = std::byte{0};
                mip.Pixels[index + 3U] = std::byte{255};
            }
        TextureImportSettings settings;
        settings.Semantic = TextureSemantic::Data;
        settings.ColorSpace = TextureColorSpace::Linear;
        settings.Mips = TextureMipPolicy::None;
        settings.Sampler.AddressU = TextureAddressMode::Clamp;
        settings.Sampler.AddressV = TextureAddressMode::Clamp;
        settings.Sampler.AddressW = TextureAddressMode::Clamp;
        return CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{std::move(mip)});
    }
} // namespace Keire::RenderBackend
