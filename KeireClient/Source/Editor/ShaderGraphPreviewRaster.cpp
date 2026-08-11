#include "KeireClient/Editor/ShaderGraphPreviewRaster.h"

#include <algorithm>
#include <cmath>

namespace KeireEditor::Detail
{
    namespace
    {
        [[nodiscard]] float Clamp01(const float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

        [[nodiscard]] float Dot(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
        {
            return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
        }

        [[nodiscard]] Keire::Vector3 Normalize(const Keire::Vector3 value,
                                               const Keire::Vector3 fallback = {0.0F, 0.0F, 1.0F}) noexcept
        {
            const float lengthSquared = Dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }
    } // namespace

    void WritePreviewPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                           const std::uint32_t y, const std::array<float, 4> color)
    {
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
        for (std::size_t channel = 0; channel < 4; ++channel)
            pixels[offset + channel] = static_cast<std::byte>(std::lround(Clamp01(color[channel]) * 255.0F));
    }

    std::array<float, 3> PreviewBackground(const std::uint32_t x, const std::uint32_t y) noexcept
    {
        const float checker = ((x / 14U + y / 14U) & 1U) == 0 ? 0.115F : 0.145F;
        const float vignette = 1.0F - std::min(0.28F, std::abs(static_cast<float>(x % 256U) - 128.0F) / 900.0F);
        return {checker * vignette, (checker + 0.012F) * vignette, (checker + 0.025F) * vignette};
    }

    std::array<float, 4> ShadePreviewMaterial(const PreviewMaterial& material, const Keire::Vector3 normal,
                                              const Keire::Vector2 uv, const float exposure,
                                              const float environmentIntensity)
    {
        const auto n = Normalize(material.HasNormal ? material.Normal : normal, Normalize(normal));
        const auto light = Normalize(Keire::Vector3{-0.45F, 0.62F, 0.68F});
        const auto view = Keire::Vector3{0.0F, 0.0F, 1.0F};
        const auto halfVector = Normalize({light.X + view.X, light.Y + view.Y, light.Z + view.Z});
        const float noL = Clamp01(Dot(n, light));
        const float noH = Clamp01(Dot(n, halfVector));
        const float noV = Clamp01(Dot(n, view));
        const float texture =
            material.HasBaseTexture
                ? (((static_cast<int>(std::floor(uv.X * 10.0F)) + static_cast<int>(std::floor(uv.Y * 10.0F))) & 1) == 0
                       ? 0.88F
                       : 0.58F)
                : 1.0F;
        const float glossExponent = 4.0F + (1.0F - material.Roughness) * 124.0F;
        const float dielectric = 0.08F * material.Specular;
        const float specular = std::pow(noH, glossExponent) * (dielectric + material.Metallic * (1.0F - dielectric));
        const float clearCoatExponent = 4.0F + (1.0F - material.ClearCoatRoughness) * 252.0F;
        const float clearCoat = std::pow(noH, clearCoatExponent) * material.ClearCoat * 0.32F;
        const float sheen = std::pow(1.0F - noV, 2.0F + material.SheenRoughness * 4.0F);
        const float diffuse = material.Unlit ? 1.0F : 0.08F * environmentIntensity * material.Occlusion + noL * 0.82F;
        const float diffuseWeight = material.Unlit ? 1.0F : 1.0F - material.Metallic * 0.72F;
        const auto channel = [&](const float base, const float emission, const float sheenChannel)
        {
            const float linear =
                base * texture * diffuse * diffuseWeight + specular + clearCoat + sheenChannel * sheen + emission;
            return 1.0F - std::exp(-std::max(linear, 0.0F) * exposure);
        };
        return {channel(material.BaseColor.X, material.Emission.X, material.SheenColor.X),
                channel(material.BaseColor.Y, material.Emission.Y, material.SheenColor.Y),
                channel(material.BaseColor.Z, material.Emission.Z, material.SheenColor.Z),
                material.BaseColor.W * material.Opacity};
    }

    Keire::Vector3 RotatePreviewVector(const Keire::Vector3 value, const float rotationDegrees) noexcept
    {
        constexpr float radiansPerDegree = 0.01745329251994329577F;
        const float yaw = rotationDegrees * radiansPerDegree;
        const float cosineYaw = std::cos(yaw);
        const float sineYaw = std::sin(yaw);
        constexpr float cosinePitch = 0.97133797F;
        constexpr float sinePitch = -0.23770263F;
        const float x = cosineYaw * value.X + sineYaw * value.Z;
        const float z = -sineYaw * value.X + cosineYaw * value.Z;
        return {x, cosinePitch * value.Y - sinePitch * z, sinePitch * value.Y + cosinePitch * z};
    }
} // namespace KeireEditor::Detail
