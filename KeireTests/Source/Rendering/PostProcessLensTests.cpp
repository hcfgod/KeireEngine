#include "KeireInternal/Rendering/PostProcessLens.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

TEST_CASE("GPU lens coefficients match the signed thin lens reference across camera distances")
{
    using namespace Keire::Detail;
    for (const double focus : {0.2, 10.0, 10000000.0})
    {
        for (const double focal : {20.0, 50.0, 100.0})
        {
            PostProcessLens lens;
            lens.FocusDistanceMetres = focus;
            lens.FocalLengthMillimetres = focal;
            const auto coefficients = MakePostProcessLensCoefficients(lens, 2160.0);
            for (const double distance : {0.01, 0.2, 1.0, 10.0, 10000000.0, std::numeric_limits<double>::infinity()})
            {
                const double gpuRadius =
                    std::clamp(coefficients.ReciprocalDepthScale / distance - coefficients.Bias,
                               -coefficients.MaximumRadiusPixels, coefficients.MaximumRadiusPixels);
                CHECK(gpuRadius ==
                      doctest::Approx(PostProcessCircleOfConfusion(lens, distance, 2160.0)).epsilon(1e-10));
            }
        }
    }
}

TEST_CASE("GPU lens coefficients reject invalid authored focus and preserve disabled strength")
{
    using namespace Keire::Detail;
    PostProcessLens lens;
    lens.Strength = 0.0;
    const auto disabled = MakePostProcessLensCoefficients(lens, 1080.0);
    CHECK(disabled.ReciprocalDepthScale == 0.0);
    CHECK(disabled.Bias == 0.0);
    lens.FocusDistanceMetres = 0.05;
    CHECK_THROWS_AS(MakePostProcessLensCoefficients(lens, 1080.0), std::invalid_argument);
    lens.FocusDistanceMetres = 10.0;
    CHECK_THROWS_AS(MakePostProcessLensCoefficients(lens, 0.0), std::invalid_argument);
    lens.ApertureFNumber = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(MakePostProcessLensCoefficients(lens, 1080.0), std::invalid_argument);
}

TEST_CASE("DoF source coverage crosses focus silhouettes but respects foreground occlusion")
{
    using namespace Keire::Detail;
    CHECK(PostProcessDofNearWeight(8.0, 0.0, 4.0, 8.0) == doctest::Approx(1.0));
    CHECK(PostProcessDofNearWeight(8.0, -16.0, 4.0, 8.0) == doctest::Approx(1.0));
    CHECK(PostProcessDofNearWeight(8.0, 16.0, 4.0, 8.0) == 0.0);
    CHECK(PostProcessDofNearWeight(-8.0, 0.0, 4.0, 8.0) == 0.0);
    CHECK(PostProcessDofNearWeight(8.0, 0.0, 9.0, 8.0) == 0.0);
    CHECK(PostProcessDofNearWeight(8.0, 0.0, 8.5, 8.0) == doctest::Approx(0.5));
    CHECK_THROWS_AS(PostProcessDofNearWeight(8.0, 0.0, -1.0, 8.0), std::invalid_argument);
    // Constant near-field radiance retains unit opacity independent of blur radius.
    for (const double radius : {2.0, 8.0, 32.0})
    {
        double coverage = 0.0;
        for (int sample = 0; sample < 24; ++sample)
        {
            const double distance = std::sqrt((sample + 0.5) / 24.0) * radius;
            coverage += PostProcessDofNearWeight(radius, radius, distance, radius) / 24.0;
        }
        CHECK(coverage == doctest::Approx(1.0));
    }
}

TEST_CASE("DoF reduced resources retain odd image edges and one pixel surfaces")
{
    using Keire::Detail::PostProcessDofReducedExtent;
    CHECK_THROWS_AS(PostProcessDofReducedExtent(1, 0), std::invalid_argument);
    CHECK(PostProcessDofReducedExtent(1, 8) == 1);
    CHECK(PostProcessDofReducedExtent(2561, 2) == 1281);
    CHECK(PostProcessDofReducedExtent(1441, 4) == 361);
    CHECK(PostProcessDofReducedExtent(1441, 8) == 181);
    CHECK(PostProcessDofReducedExtent(std::numeric_limits<std::uint32_t>::max(), 2) == 2147483648U);
}

TEST_CASE("DoF UV mapping aligns every reduced block center including partial final blocks")
{
    using namespace Keire::Detail;
    for (const std::uint32_t extent : {1U, 2U, 3U, 5U, 8U, 9U, 17U, 1441U, 2560U, 2561U})
    {
        for (const std::uint32_t divisor : {2U, 4U, 8U})
        {
            const auto reduced = PostProcessDofReducedExtent(extent, divisor);
            for (std::uint32_t pixel = 0; pixel < reduced; ++pixel)
            {
                const double reducedUv = (pixel + 0.5) / reduced;
                const double first = static_cast<double>(pixel) * divisor;
                const double width = std::min<double>(divisor, extent - first);
                const double expected = (first + width * 0.5) / extent;
                const double fullUv = PostProcessDofReducedToFullUv(reducedUv, extent, divisor);
                CHECK(fullUv == doctest::Approx(expected).epsilon(1e-12));
                CHECK(PostProcessDofFullToReducedUv(fullUv, extent, divisor) ==
                      doctest::Approx(reducedUv).epsilon(1e-12));
            }
        }
    }
    // Explicit edge regression: final 1px block of a 5px image is centered at4.5, not5.
    CHECK(PostProcessDofReducedToFullUv(2.5 / 3.0, 5, 2) == doctest::Approx(0.9));
    CHECK(PostProcessDofFullToReducedUv(0.9, 5, 2) == doctest::Approx(2.5 / 3.0));
    for (const double uv : {0.125, 0.25, 0.5, 0.75, 0.875})
    {
        CHECK(PostProcessDofFullToReducedUv(uv, 2560, 8) == doctest::Approx(uv));
        CHECK(PostProcessDofReducedToFullUv(uv, 2560, 8) == doctest::Approx(uv));
    }
    CHECK(PostProcessDofFullToReducedUv(0.5, 1, 8) == 0.5);
    CHECK(PostProcessDofReducedToFullUv(0.5, 1, 8) == 0.5);
    CHECK_THROWS_AS(PostProcessDofFullToReducedUv(0.5, 0, 2), std::invalid_argument);
    CHECK_THROWS_AS(PostProcessDofReducedToFullUv(0.5, 1, 0), std::invalid_argument);
}
