#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <compare>
#include <cstdint>

namespace Keire::RenderBackend
{
    enum class MaterialBlendFactor : std::uint8_t
    {
        Zero,
        One,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationColor
    };

    struct MaterialBlendPolicy final
    {
        MaterialBlendFactor SourceColor = MaterialBlendFactor::One;
        MaterialBlendFactor DestinationColor = MaterialBlendFactor::Zero;
        MaterialBlendFactor SourceAlpha = MaterialBlendFactor::One;
        MaterialBlendFactor DestinationAlpha = MaterialBlendFactor::Zero;
        bool Enabled = false;
        bool WritesDepth = true;

        auto operator<=>(const MaterialBlendPolicy&) const noexcept = default;
    };

    [[nodiscard]] constexpr MaterialBlendPolicy MaterialBlending(const MaterialAlphaMode mode) noexcept
    {
        using enum MaterialBlendFactor;
        switch (mode)
        {
        case MaterialAlphaMode::Blend:
            return {SourceAlpha, OneMinusSourceAlpha, One, OneMinusSourceAlpha, true, false};
        case MaterialAlphaMode::Additive:
            return {SourceAlpha, One, One, One, true, false};
        case MaterialAlphaMode::Modulate:
            return {DestinationColor, Zero, Zero, One, true, false};
        case MaterialAlphaMode::AlphaComposite:
            return {One, OneMinusSourceAlpha, One, OneMinusSourceAlpha, true, false};
        case MaterialAlphaMode::AlphaHoldout:
            return {Zero, OneMinusSourceAlpha, Zero, OneMinusSourceAlpha, true, false};
        case MaterialAlphaMode::Opaque:
        case MaterialAlphaMode::Mask:
        default:
            return {};
        }
    }
} // namespace Keire::RenderBackend
