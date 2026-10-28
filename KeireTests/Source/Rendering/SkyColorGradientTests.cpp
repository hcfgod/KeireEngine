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
