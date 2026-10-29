#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Rendering/SkyColorUniforms.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace
{
    using std::clamp;
    using std::lerp;
    using std::pow;

#include "../../../KeireCore/Shaders/SkyColorGradient.hlsli"
} // namespace

TEST_CASE("Actual sky gradient kernel preserves disabled radiance and HDR endpoints")
{
    CHECK(BlendSkyColorChannel(3.0F, 8.0F, 16.0F, 0.5F, 0.0F, 1.0F) == 3.0F);
    CHECK(BlendSkyColorChannel(3.0F, 8.0F, 16.0F, 0.0F, 1.0F, 1.0F) == 8.0F);
    CHECK(BlendSkyColorChannel(3.0F, 8.0F, 16.0F, 1.0F, 1.0F, 1.0F) == 16.0F);
    CHECK(BlendSkyColorChannel(3.0F, 8.0F, 16.0F, -1.0F, 1.0F, 1.0F) == 8.0F);
    CHECK(BlendSkyColorChannel(4.0F, 8.0F, 16.0F, 0.5F, 0.5F, 2.0F) == 7.0F);
    CHECK(BlendSkyColorChannel(0.0F, 65504.0F, 65504.0F, 0.5F, 1.0F, 0.1F) == 65504.0F);
}

TEST_CASE("Sky capture copies color values into the matching uniform lanes without retaining request state")
{
    using namespace Keire;
    CHECK(RenderBackend::CaptureSkyColors(std::nullopt).HorizonWeight[3] == 0.0F);
    EnvironmentLightingSnapshot snapshot;
    snapshot.SkyColors = EnvironmentSkyColors{{2.0F, 4.0F, 6.0F}, {8.0F, 10.0F, 12.0F}, 0.25F, 2.0F};
    const auto packed = RenderBackend::CaptureSkyColors(snapshot);
    snapshot.SkyColors->Horizon[0] = 0.0F;
    CHECK(packed.HorizonWeight == std::array<float, 4>{2.0F, 4.0F, 6.0F, 0.25F});
    CHECK(packed.ZenithExponent == std::array<float, 4>{8.0F, 10.0F, 12.0F, 2.0F});
}

TEST_CASE("Actual sky gradient fades continuously to physical space radiance")
{
    constexpr float radius = 6360.0F;
    constexpr float height = 100.0F;
    CHECK(SkyGradientAtmosphereWeight(radius, radius, height) == 1.0F);
    CHECK(SkyGradientAtmosphereWeight(radius + 50.0F, radius, height) == 0.5F);
    CHECK(SkyGradientAtmosphereWeight(radius + height, radius, height) == 0.0F);
    CHECK(SkyGradientAtmosphereWeight(1.0e9F, radius, height) == 0.0F);
    CHECK(SkyGradientAtmosphereWeight(radius - 1.0F, radius, height) == 0.0F);
    CHECK(SkyGradientAtmosphereWeight(radius, radius, 0.0F) == 0.0F);
    float previous = 1.0F;
    for (int altitude = 0; altitude <= 101; ++altitude)
    {
        const float weight = SkyGradientAtmosphereWeight(radius + static_cast<float>(altitude), radius, height);
        CHECK(weight >= 0.0F);
        CHECK(weight <= previous);
        previous = weight;
    }
    const float outside = SkyGradientAtmosphereWeight(radius + height + 1.0F, radius, height);
    for (const float direction : {-1.0F, 0.0F, 1.0F})
    {
        CHECK(BlendSkyColorChannel(0.125F, 8.0F, 16.0F, direction, outside, 1.0F) == 0.125F);
    }
}

TEST_CASE("Planet-relative sky gradient is invariant under distant world translations")
{
    Keire::AtmosphereSettings settings;
    settings.Enabled = true;
    for (const double offset : {0.0, 1.0e10, -1.0e10, 1.0e12, -1.0e12})
    {
        CAPTURE(offset);
        settings.PlanetCenter = {offset, offset - settings.PlanetRadius, offset};
        for (const float altitude : {0.0F, 2000.0F, 50000.0F, 100000.0F, 400000.0F})
        {
            CAPTURE(altitude);
            const auto translated = Keire::RenderBackend::MakeAtmosphereUniforms(
                settings, {0.0F, altitude, 0.0F}, {0.0F, -1.0F, 0.0F}, {offset, offset, offset});
            Keire::AtmosphereSettings localSettings;
            localSettings.Enabled = true;
            const auto local = Keire::RenderBackend::MakeAtmosphereUniforms(localSettings, {0.0F, altitude, 0.0F},
                                                                            {0.0F, -1.0F, 0.0F});
            REQUIRE(translated.ObserverRadius == local.ObserverRadius);
            const float weight = SkyGradientAtmosphereWeight(translated.ObserverRadius.Y, translated.ObserverRadius.W,
                                                             translated.SunHeight.W);
            CHECK(weight ==
                  SkyGradientAtmosphereWeight(local.ObserverRadius.Y, local.ObserverRadius.W, local.SunHeight.W));
            if (altitude == 0.0F)
            {
                CHECK(weight == 1.0F);
            }
            if (altitude >= 100000.0F)
            {
                CHECK(weight == 0.0F);
            }
        }
    }
}
