#include "Keire/Math/Curves.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("Curve1D validates keys and evaluates deterministic interpolation")
{
    const Keire::Curve1D curve({{0.0F, 0.0F, 0.0F, 0.0F, Keire::CurveInterpolation::Constant},
                                {1.0F, 2.0F, 0.0F, 2.0F, Keire::CurveInterpolation::Linear},
                                {2.0F, 4.0F, 2.0F, 0.0F, Keire::CurveInterpolation::Cubic},
                                {3.0F, 6.0F, 0.0F, 0.0F, Keire::CurveInterpolation::Linear}});

    CHECK(curve.Evaluate(-1.0F) == doctest::Approx(0.0F));
    CHECK(curve.Evaluate(0.5F) == doctest::Approx(0.0F));
    CHECK(curve.Evaluate(1.5F) == doctest::Approx(3.0F));
    CHECK(curve.Evaluate(2.5F) == doctest::Approx(5.0F));
    CHECK(curve.Evaluate(4.0F) == doctest::Approx(6.0F));

    CHECK_THROWS_AS(Keire::Curve1D({{1.0F, 0.0F}, {1.0F, 1.0F}}), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Curve1D({{0.0F, std::numeric_limits<float>::infinity()}}), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Curve1D({{0.0F, 0.0F, 0.0F, 0.0F, static_cast<Keire::CurveInterpolation>(255)}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)curve.Evaluate(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
}

TEST_CASE("ColorGradient clamps endpoints and supports constant and linear segments")
{
    const Keire::Color red{1.0F, 0.0F, 0.0F, 1.0F};
    const Keire::Color blue{0.0F, 0.0F, 1.0F, 0.5F};
    Keire::ColorGradient gradient({{0.0F, red}, {1.0F, blue}});

    CHECK(gradient.Evaluate(-1.0F) == red);
    const auto midpoint = gradient.Evaluate(0.5F);
    CHECK(midpoint.Red == doctest::Approx(0.5F));
    CHECK(midpoint.Green == doctest::Approx(0.0F));
    CHECK(midpoint.Blue == doctest::Approx(0.5F));
    CHECK(midpoint.Alpha == doctest::Approx(0.75F));
    CHECK(gradient.Evaluate(2.0F) == blue);

    gradient.SetInterpolation(Keire::GradientInterpolation::Constant);
    CHECK(gradient.Evaluate(0.75F) == red);
    CHECK_THROWS_AS(Keire::ColorGradient({{1.0F, red}, {0.5F, blue}}), std::invalid_argument);
    CHECK_THROWS_AS(gradient.SetInterpolation(static_cast<Keire::GradientInterpolation>(255)), std::invalid_argument);
    CHECK(gradient.Interpolation() == Keire::GradientInterpolation::Constant);
}

TEST_CASE("Curve interpolation avoids intermediate overflow for finite extreme endpoints and times")
{
    const float largest = std::numeric_limits<float>::max();
    const auto curve = Keire::Curve1D::Linear(-largest, largest);
    CHECK(curve.Evaluate(0.0F) == -largest);
    CHECK(curve.Evaluate(0.5F) == 0.0F);
    CHECK(curve.Evaluate(1.0F) == largest);
    CHECK(std::isfinite(curve.Evaluate(0.25F)));
    CHECK(curve.Evaluate(0.25F) == doctest::Approx(-largest / 2.0F));

    for (const auto interpolation : {Keire::CurveInterpolation::Linear, Keire::CurveInterpolation::Cubic})
    {
        const Keire::Curve1D wide({{-largest, 2, 0, 0, interpolation}, {largest, 6}});
        CHECK(wide.Evaluate(-largest) == 2.0F);
        CHECK(wide.Evaluate(0.0F) == 4.0F);
        CHECK(wide.Evaluate(largest) == 6.0F);
        CHECK(std::isfinite(wide.Evaluate(largest / 2.0F)));
    }
    const Keire::Curve1D largeCubic({{0, -largest, 0, 0, Keire::CurveInterpolation::Cubic}, {1, largest}});
    CHECK(largeCubic.Evaluate(0.5F) == 0.0F);
    const Keire::Curve1D overshooting(
        {{0, largest, 0, largest, Keire::CurveInterpolation::Cubic}, {1, largest, -largest, 0}});
    CHECK(overshooting.Evaluate(0.5F) == std::numeric_limits<float>::infinity());
    const Keire::Curve1D undershooting(
        {{0, -largest, 0, -largest, Keire::CurveInterpolation::Cubic}, {1, -largest, largest, 0}});
    CHECK(undershooting.Evaluate(0.5F) == -std::numeric_limits<float>::infinity());
}

TEST_CASE("Color interpolation and gradients retain finite extreme channels and wide key times")
{
    const float largest = std::numeric_limits<float>::max();
    const Keire::Color first{-largest, largest, -largest, largest};
    const Keire::Color last{largest, -largest, largest, -largest};
    CHECK(Keire::Math::Lerp(first, last, 0.0F) == first);
    CHECK(Keire::Math::Lerp(first, last, 1.0F) == last);
    CHECK(Keire::Math::Lerp(first, last, -1.0F) == first);
    CHECK(Keire::Math::Lerp(first, last, 2.0F) == last);
    CHECK(Keire::Math::Lerp(first, last, 0.5F) == Keire::Color{0, 0, 0, 0});
    CHECK(Keire::Math::IsFinite(Keire::Math::Lerp(first, last, 0.25F)));
    const Keire::ColorGradient gradient({{-largest, first}, {largest, last}});
    CHECK(gradient.Evaluate(0.0F) == Keire::Color{0, 0, 0, 0});
    CHECK(gradient.Evaluate(-largest) == first);
    CHECK(gradient.Evaluate(largest) == last);
    CHECK(Keire::Math::IsFinite(gradient.Evaluate(largest / 2.0F)));
}
