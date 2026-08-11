#pragma once

#include "Keire/Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace KeireEditor::Detail
{
    struct PreviewMaterial
    {
        Keire::Vector4 BaseColor{0.72F, 0.72F, 0.74F, 1.0F};
        Keire::Vector3 Emission;
        float Metallic = 0.0F;
        float Roughness = 0.45F;
        float Specular = 0.5F;
        float ClearCoat = 0.0F;
        float ClearCoatRoughness = 0.1F;
        Keire::Vector3 SheenColor;
        float SheenRoughness = 0.5F;
        float Opacity = 1.0F;
        float Occlusion = 1.0F;
        Keire::Vector3 Normal;
        bool HasBaseTexture = false;
        bool HasNormal = false;
        bool Unlit = false;
    };

    void WritePreviewPixel(std::vector<std::byte>& pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y,
                           std::array<float, 4> color);
    [[nodiscard]] std::array<float, 3> PreviewBackground(std::uint32_t x, std::uint32_t y) noexcept;
    [[nodiscard]] std::array<float, 4> ShadePreviewMaterial(const PreviewMaterial& material, Keire::Vector3 normal,
                                                            Keire::Vector2 uv, float exposure,
                                                            float environmentIntensity);
    [[nodiscard]] Keire::Vector3 RotatePreviewVector(Keire::Vector3 value, float rotationDegrees) noexcept;
} // namespace KeireEditor::Detail
