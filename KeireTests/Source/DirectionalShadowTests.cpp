#include "KeireInternal/Rendering/DirectionalShadowInternal.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>

TEST_CASE("planet scale shadow transforms retain surface depth near the render origin")
{
    Keire::Matrix4 model;
    model.Elements[0] = 12720000.0F;
    model.Elements[5] = 12720000.0F;
    model.Elements[10] = 12720000.0F;
    model.Elements[13] = -6360000.0F;
    Keire::Matrix4 light;
    light.Elements[5] = 0.00317F;
    light.Elements[13] = 0.413F;
    const auto anchored = Keire::RenderBackend::BuildAnchoredDepthTransform(light, model);
    CHECK(anchored.LocalAnchor.Y == doctest::Approx(0.5F));
    // The north-pole vertex must retain the small translation instead of cancelling
    // two planet-sized projected values in the GPU's float arithmetic.
    const float surfaceY =
        anchored.Transform.Elements[5] * (0.5F - anchored.LocalAnchor.Y) + anchored.Transform.Elements[13];
    CHECK(surfaceY == doctest::Approx(0.413F).epsilon(0.000001));
    const auto ordinary = Keire::RenderBackend::BuildAnchoredDepthTransform(light, Keire::Matrix4{});
    CHECK(ordinary.LocalAnchor.X == 0.0F);
    CHECK(ordinary.LocalAnchor.Y == 0.0F);
    CHECK(ordinary.LocalAnchor.Z == 0.0F);
    CHECK(ordinary.Transform.Elements == light.Elements);
    model.Elements[0] = 0.0F;
    const auto singular = Keire::RenderBackend::BuildAnchoredDepthTransform(light, model);
    CHECK(singular.LocalAnchor.Y == 0.0F);
    CHECK(singular.Transform.Elements == Keire::Math::Multiply(light, model).Elements);
}

TEST_CASE("shadow slices remain finite with planetary camera clipping distances")
{
    const Keire::Matrix4 view{};
    const auto ordinary = Keire::Math::Perspective(90.0F, 1.0F, 0.05F, 1000.0F);
    const auto planetary = Keire::Math::Perspective(90.0F, 1.0F, 0.05F, 10000000.0F);
    const auto expected = Keire::RenderBackend::BuildShadowSliceCorners(ordinary, view, 100.0F);
    const auto actual = Keire::RenderBackend::BuildShadowSliceCorners(planetary, view, 100.0F);
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        CHECK(std::isfinite(actual[index].X));
        CHECK(std::isfinite(actual[index].Y));
        CHECK(actual[index].X == doctest::Approx(expected[index].X));
        CHECK(actual[index].Y == doctest::Approx(expected[index].Y));
        CHECK(actual[index].Z == doctest::Approx(100.0F));
        CHECK(std::abs(actual[index].X) == doctest::Approx(100.0F));
    }

    const auto orthographic = Keire::Math::Orthographic(20.0F, 1.0F, 0.05F, 10000000.0F);
    const auto parallel = Keire::RenderBackend::BuildShadowSliceCorners(orthographic, view, 100.0F);
    CHECK(std::abs(parallel.front().X) == doctest::Approx(10.0F));
    CHECK(std::abs(parallel.front().Y) == doctest::Approx(10.0F));
    CHECK(parallel.front().Z == doctest::Approx(100.0F));

    const auto elevatedView = Keire::Math::LookAt({0.0F, 150000.0F, 0.0F}, {0.0F, 150000.0F, 1.0F}, {0.0F, 1.0F, 0.0F});
    const auto elevated = Keire::RenderBackend::BuildShadowSliceCorners(planetary, elevatedView, 100.0F);
    CHECK(elevated.front().Y == doctest::Approx(149900.0F));
    CHECK(elevated.front().Z == doctest::Approx(100.0F));
}

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
