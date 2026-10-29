#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Rendering/Lighting.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Keire
{
    enum class EnvironmentDirectionalFrame : std::uint8_t
    {
        LegacyWorld,
        PlanetFixed,
        InertialRoot
    };

    struct EnvironmentShadowSettings
    {
        ShadowQuality Quality = ShadowQuality::Disabled;
        ShadowResolutionHint Resolution = ShadowResolutionHint::High;
        float Strength = 1.0F;
        float Bias = 0.005F;

        bool operator==(const EnvironmentShadowSettings&) const noexcept = default;
    };

    struct EnvironmentDirectionalLight
    {
        std::array<float, 3> Direction{0.0F, -1.0F, 0.0F};
        std::array<float, 3> LinearColor{1.0F, 1.0F, 1.0F};
        float Intensity = 1.0F;
        EnvironmentShadowSettings Shadows;
    };

    struct EnvironmentAmbientLight
    {
        std::array<float, 3> LinearColor{0.20F, 0.22F, 0.26F};
        float Intensity = 0.75F;

        bool operator==(const EnvironmentAmbientLight&) const noexcept = default;
    };

    /// Split calendar sample retains local-time precision at distant dates; no total-seconds conversion is implied.
    struct EnvironmentTimeSample
    {
        std::int64_t Day = 0;
        double DayFraction = 0.0;
        double SecondsPerDay = 1200.0;

        bool operator==(const EnvironmentTimeSample&) const noexcept = default;
    };

    /// Art-directed linear HDR gradient, independent of the selected directional light.
    struct EnvironmentSkyColors
    {
        std::array<float, 3> Horizon{};
        std::array<float, 3> Zenith{};
        float BlendWeight = 1.0F;
        float GradientExponent = 1.0F;

        bool operator==(const EnvironmentSkyColors&) const noexcept = default;
    };

    /// Immutable value input for one view. Source identifies the world/preview lifetime, not an asset or entity.
    struct EnvironmentLightingSnapshot
    {
        AssetId Source;
        /// Changes on seeks, profile edits and reference-frame discontinuities, not normal time advancement.
        std::uint64_t Revision = 0;
        /// Empty preserves authored directional-light selection while still tracking environment continuity.
        std::optional<EnvironmentDirectionalLight> Directional;
        /// Empty retains the project's ambient settings; this does not replace baked or image-based lighting.
        std::optional<EnvironmentAmbientLight> Ambient;
        /// Shared world or Edit-preview clock for environment animation, independent of directional ownership.
        std::optional<EnvironmentTimeSample> Time;
        /// Empty retains the physical sky or authored environment texture.
        std::optional<EnvironmentSkyColors> SkyColors;
        /// Daily-track direction basis. Resolve planetary modes into the view frame before submission.
        EnvironmentDirectionalFrame DirectionFrame = EnvironmentDirectionalFrame::LegacyWorld;
    };
} // namespace Keire
