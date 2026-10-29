#pragma once

#include "Keire/Rendering/StarField.h"
#include "KeireInternal/Spatial/FrameRotation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace Keire::RenderBackend
{
    // Apply before the existing artistic Y rotation. Stars live in the inertial root, independently of
    // planet weather rotation and camera radial up. Identity preserves legacy world-oriented star fields.
    [[nodiscard]] inline std::array<float, 4>
    MakeStarFieldOrientation(const Detail::FrameRotation& rootToObserver = {}) noexcept
    {
        const auto inverse = rootToObserver.Inverse();
        const auto& rotation = inverse.Components();
        return {static_cast<float>(rotation[0]), static_cast<float>(rotation[1]), static_cast<float>(rotation[2]),
                static_cast<float>(rotation[3])};
    }

    struct alignas(16) StarFieldUniforms
    {
        std::array<float, 4> Appearance{};
        std::array<float, 4> TintRotation{};
        std::array<float, 4> TwinkleFade{};
        std::array<std::uint32_t, 4> Identity{};
    };

    static_assert(sizeof(StarFieldUniforms) == 64);
    static_assert(offsetof(StarFieldUniforms, Identity) == 48);

    /// Called only after request validation. Preserve seed as integer bits, not lossy float conversion.
    [[nodiscard]] inline StarFieldUniforms MakeStarFieldUniforms(const StarFieldSettings& settings,
                                                                 const double phase) noexcept
    {
        return {{static_cast<float>(settings.Density), static_cast<float>(settings.Intensity),
                 static_cast<float>(settings.AngularRadius), static_cast<float>(settings.ColorVariation)},
                {static_cast<float>(settings.Tint[0]), static_cast<float>(settings.Tint[1]),
                 static_cast<float>(settings.Tint[2]),
                 static_cast<float>(settings.RotationDegrees * std::numbers::pi / 180.0)},
                {static_cast<float>(settings.TwinkleAmplitude), static_cast<float>(phase),
                 static_cast<float>(settings.FadeStartElevation), static_cast<float>(settings.FullVisibilityElevation)},
                {settings.Seed, settings.Enabled ? 1U : 0U, 0U, 0U}};
    }
} // namespace Keire::RenderBackend
