#include "KeireInternal/Rendering/DirectionalShadowInternal.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>

TEST_CASE("practical directional cascade splits are bounded and deterministic")
{
    const auto uniform = Keire::RenderBackend::BuildPracticalCascadeSplits(0.1F, 100.0F, 4, 0.0F);
    const auto logarithmic = Keire::RenderBackend::BuildPracticalCascadeSplits(0.1F, 100.0F, 4, 1.0F);
    const auto practical = Keire::RenderBackend::BuildPracticalCascadeSplits(0.1F, 100.0F, 4, 0.65F);
    REQUIRE(practical.size() == 4);
    CHECK(practical.back() == doctest::Approx(100.0F));
    for (std::size_t index = 0; index + 1 < practical.size(); ++index)
    {
        CHECK(practical[index] > logarithmic[index]);
        CHECK(practical[index] < uniform[index]);
        CHECK(practical[index] < practical[index + 1]);
    }
    CHECK_THROWS_AS((void)Keire::RenderBackend::BuildPracticalCascadeSplits(0.0F, 100.0F, 4, 0.5F),
                    std::invalid_argument);
}

TEST_CASE("directional cascade centers snap to stable shadow texels")
{
    const auto snapped = Keire::RenderBackend::StabilizeShadowCenter({1.234F, -5.678F}, 64.0F, 2048);
    constexpr float texel = 64.0F / 2048.0F;
    CHECK(snapped.X / texel == doctest::Approx(std::round(1.234F / texel)));
    CHECK(snapped.Y / texel == doctest::Approx(std::round(-5.678F / texel)));
    const auto world = Keire::RenderBackend::StabilizeShadowCenter({1.234F, -5.678F, 9.0F}, {1.0F, 0.0F, 0.0F},
                                                                   {0.0F, 1.0F, 0.0F}, 64.0F, 2048);
    CHECK(world.X == doctest::Approx(snapped.X));
    CHECK(world.Y == doctest::Approx(snapped.Y));
    CHECK(world.Z == doctest::Approx(9.0F));
    CHECK_THROWS_AS((void)Keire::RenderBackend::StabilizeShadowCenter({}, 0.0F, 2048), std::invalid_argument);
}

TEST_CASE("directional shadow raster bias offsets flat and sloped self-shadowing")
{
    CHECK(Keire::RenderBackend::DirectionalShadowDepthBiasConstant > 0.0F);
    CHECK(Keire::RenderBackend::DirectionalShadowDepthBiasSlope > 0.0F);
}
