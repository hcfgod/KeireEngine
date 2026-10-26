#include "KeireInternal/Environment/BloomEvaluation.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

TEST_CASE("Bloom defaults remain disabled and extraction preserves HDR hue")
{
    Keire::BloomSettings settings;
    CHECK(Keire::Detail::ExtractBloom(settings, {4.0, 2.0, 1.0}) == std::array<double, 3>{});
    settings.Enabled = true;
    settings.SoftKnee = 0.0;
    const auto result = Keire::Detail::ExtractBloom(settings, {4.0, 2.0, 1.0});
    CHECK(result == std::array<double, 3>{3.0, 1.5, 0.75});
    CHECK(Keire::Detail::ExtractBloom(settings, {1.0, 0.5, 0.0}) == std::array<double, 3>{});
    settings.Intensity = 16.0;
    CHECK(Keire::Detail::ExtractBloom(settings, {4.0, 2.0, 1.0}) == result);
}

TEST_CASE("Bloom soft knee is continuous and handles zero threshold and finite extremes")
{
    Keire::BloomSettings settings;
    settings.Enabled = true;
    CHECK(Keire::Detail::ExtractBloom(settings, {0.5, 0.0, 0.0})[0] == 0.0);
    CHECK(Keire::Detail::ExtractBloom(settings, {1.0, 0.0, 0.0})[0] == doctest::Approx(0.125));
    CHECK(Keire::Detail::ExtractBloom(settings, {1.5, 0.0, 0.0})[0] == doctest::Approx(0.5));
    for (const double boundary : {0.5, 1.5})
    {
        const double below = Keire::Detail::ExtractBloom(settings, {boundary - 1.0e-8, 0.0, 0.0})[0];
        const double above = Keire::Detail::ExtractBloom(settings, {boundary + 1.0e-8, 0.0, 0.0})[0];
        CHECK(std::abs(above - below) < 3.0e-8);
    }
    settings.Threshold = 0.0;
    CHECK(Keire::Detail::ExtractBloom(settings, {0.0, 0.0, 0.0}) == std::array<double, 3>{});
    CHECK(Keire::Detail::ExtractBloom(settings, {-1.0, 2.0, std::numeric_limits<double>::max()}) ==
          std::array<double, 3>{0.0, 2.0, 65504.0});
    settings.Threshold = std::numeric_limits<double>::denorm_min();
    settings.SoftKnee = 1.0;
    const auto tiny = Keire::Detail::ExtractBloom(settings, {settings.Threshold, 0.0, 0.0});
    CHECK(std::isfinite(tiny[0]));
    CHECK(tiny[0] >= 0.0);
}

TEST_CASE("Bloom Gaussian weights are symmetric normalized and bounded")
{
    Keire::BloomSettings settings;
    for (const double radius : {0.5, 1.0, 2.0, 4.0})
    {
        settings.Radius = radius;
        const auto weights = Keire::Detail::BuildBloomGaussianKernel(settings);
        CHECK(std::accumulate(weights.begin(), weights.end(), 0.0) == doctest::Approx(1.0).epsilon(1.0e-14));
        for (std::size_t index = 0; index < weights.size(); ++index)
        {
            CHECK(weights[index] == weights[weights.size() - 1 - index]);
            CHECK(std::isfinite(weights[index]));
            CHECK(weights[index] >= 0.0);
            CHECK(weights[index] <= weights[Keire::Detail::BloomKernelHalfWidth]);
        }
    }
}

TEST_CASE("Bloom rejects invalid settings and nonfinite input even while disabled")
{
    for (const double invalid :
         {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Keire::BloomSettings settings;
        settings.Intensity = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
        settings = {};
        settings.Threshold = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
        settings = {};
        settings.SoftKnee = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
        settings = {};
        settings.Radius = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
    }
    for (const double invalid : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS((void)Keire::Detail::ExtractBloom({}, {invalid, 0.0, 0.0}), std::invalid_argument);
    }
    Keire::BloomSettings settings;
    settings.Intensity = 16.01;
    CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
    settings = {};
    settings.Threshold = 65505.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
    settings = {};
    settings.SoftKnee = 1.01;
    CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
    settings = {};
    settings.Radius = 0.49;
    CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
    settings.Radius = 4.01;
    CHECK_THROWS_AS(Keire::Detail::ValidateBloomSettings(settings), std::invalid_argument);
}
