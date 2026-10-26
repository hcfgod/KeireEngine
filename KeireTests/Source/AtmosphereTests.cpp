#include "Keire/Rendering/Atmosphere.h"
#include "KeireInternal/Environment/AtmosphereGeometry.h"
#include "KeireInternal/Environment/AtmosphereRecord.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("atmosphere defaults are opt in and preserve all serialized parameters")
{
    Keire::AtmosphereSettings settings;
    CHECK_FALSE(settings.Enabled);
    CHECK_NOTHROW(Keire::Detail::ValidateAtmosphere(settings));
    settings.Enabled = true;
    settings.PlanetCenter = {1.0e12, -1.0e12, 123.25};
    settings.MieAnisotropy = -0.5;
    settings.SolarIrradiance = {12.0, 13.0, 14.0};
    settings.ViewSamples = 64;
    settings.LightSamples = 32;
    const auto encoded = Keire::Detail::EncodeAtmosphere(settings);
    CHECK(encoded.size() == 17);
    CHECK(Keire::Detail::DecodeAtmosphere(nlohmann::json::parse(encoded.dump())) == settings);
}

TEST_CASE("atmosphere validation rejects nonfinite scalar and spectral inputs even when disabled")
{
    constexpr std::array scalarMembers{
        &Keire::AtmosphereSettings::PlanetRadius,        &Keire::AtmosphereSettings::Height,
        &Keire::AtmosphereSettings::RayleighScaleHeight, &Keire::AtmosphereSettings::MieScaleHeight,
        &Keire::AtmosphereSettings::MieScattering,       &Keire::AtmosphereSettings::MieExtinction,
        &Keire::AtmosphereSettings::MieAnisotropy,       &Keire::AtmosphereSettings::AbsorptionPeakAltitude,
        &Keire::AtmosphereSettings::AbsorptionHalfWidth, &Keire::AtmosphereSettings::SunAngularRadius};
    constexpr std::array vectorMembers{
        &Keire::AtmosphereSettings::PlanetCenter, &Keire::AtmosphereSettings::RayleighScattering,
        &Keire::AtmosphereSettings::AbsorptionExtinction, &Keire::AtmosphereSettings::SolarIrradiance};
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()})
    {
        for (const auto member : scalarMembers)
        {
            Keire::AtmosphereSettings settings;
            settings.*member = invalid;
            CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
        }
        for (const auto member : vectorMembers)
        {
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                Keire::AtmosphereSettings settings;
                (settings.*member)[channel] = invalid;
                CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
            }
        }
    }
}

TEST_CASE("atmosphere physical ordering and sample budgets are enforced")
{
    Keire::AtmosphereSettings settings;
    settings.MieExtinction = settings.MieScattering * 0.5;
    CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
    settings = {};
    settings.Height = settings.PlanetRadius + 1.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
    settings = {};
    settings.RayleighScaleHeight = settings.Height + 1.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
    for (const auto count : {0U, 3U, 65U, std::numeric_limits<unsigned>::max()})
    {
        settings = {};
        settings.ViewSamples = count;
        CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
    }
    for (const auto count : {0U, 1U, 33U, std::numeric_limits<unsigned>::max()})
    {
        settings = {};
        settings.LightSamples = count;
        CHECK_THROWS_AS(Keire::Detail::ValidateAtmosphere(settings), std::invalid_argument);
    }
    settings = {};
    settings.ViewSamples = 4;
    settings.LightSamples = 2;
    CHECK_NOTHROW(Keire::Detail::ValidateAtmosphere(settings));
}

TEST_CASE("atmosphere records reject missing unknown and incorrectly typed fields")
{
    const auto baseline = Keire::Detail::EncodeAtmosphere({});
    for (const auto& [name, value] : baseline.items())
    {
        CAPTURE(name);
        auto missing = baseline;
        missing.erase(name);
        CHECK_THROWS_AS((void)Keire::Detail::DecodeAtmosphere(missing), std::invalid_argument);
        auto wrongType = baseline;
        wrongType[name] = "not a number";
        CHECK_THROWS_AS((void)Keire::Detail::DecodeAtmosphere(wrongType), std::invalid_argument);
    }
    auto unknown = baseline;
    unknown["unknown"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeAtmosphere(unknown), std::invalid_argument);
    for (const auto& value :
         {nlohmann::json(-1), nlohmann::json(8.5), nlohmann::json(true), nlohmann::json(4294967296ULL)})
    {
        auto invalid = baseline;
        invalid["lightSamples"] = value;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeAtmosphere(invalid), std::invalid_argument);
    }
    for (const auto& value :
         {nlohmann::json::array({1, 2}), nlohmann::json::array({1, 2, 3, 4}), nlohmann::json::array({1, true, 3})})
    {
        auto invalid = baseline;
        invalid["planetCenter"] = value;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeAtmosphere(invalid), std::invalid_argument);
    }
}

TEST_CASE("atmosphere ray intervals distinguish ground orbit and empty space")
{
    using Keire::Detail::IntersectAtmosphere;
    const auto up = IntersectAtmosphere({0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, 10.0, 12.0);
    REQUIRE(up);
    CHECK(up->Begin == 0.0);
    CHECK(up->End == doctest::Approx(2.0));
    CHECK_FALSE(up->GroundOccluded);
    const auto down = IntersectAtmosphere({0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 10.0, 12.0);
    REQUIRE(down);
    CHECK(down->End == 0.0);
    CHECK(down->GroundOccluded);
    const auto orbit = IntersectAtmosphere({0.0, 20.0, 0.0}, {0.0, -1.0, 0.0}, 10.0, 12.0);
    REQUIRE(orbit);
    CHECK(orbit->Begin == doctest::Approx(8.0));
    CHECK(orbit->End == doctest::Approx(10.0));
    CHECK(orbit->GroundOccluded);
    CHECK_FALSE(IntersectAtmosphere({0.0, 20.0, 0.0}, {0.0, 1.0, 0.0}, 10.0, 12.0));
    CHECK_FALSE(IntersectAtmosphere({0.0, 20.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 12.0));
    const auto below = IntersectAtmosphere({0.0, 9.0, 0.0}, {0.0, 1.0, 0.0}, 10.0, 12.0);
    REQUIRE(below);
    CHECK(below->GroundOccluded);
    CHECK(below->End == 0.0);
}

TEST_CASE("atmosphere tangent and limb intervals retain double precision near the surface")
{
    using Keire::Detail::IntersectAtmosphere;
    const auto limb = IntersectAtmosphere({-20.0, 11.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 12.0);
    REQUIRE(limb);
    CHECK_FALSE(limb->GroundOccluded);
    CHECK(limb->Begin == doctest::Approx(20.0 - std::sqrt(23.0)));
    CHECK(limb->End == doctest::Approx(20.0 + std::sqrt(23.0)));
    const auto tangent = IntersectAtmosphere({-20.0, 12.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 12.0);
    REQUIRE(tangent);
    CHECK(tangent->Begin == doctest::Approx(20.0));
    CHECK(tangent->End == doctest::Approx(20.0));
    CHECK_FALSE(tangent->GroundOccluded);
    CHECK_FALSE(IntersectAtmosphere({-20.0, 12.000001, 0.0}, {1.0, 0.0, 0.0}, 10.0, 12.0));
    constexpr double radius = 1.0e9;
    constexpr double altitude = 0.125;
    const auto nearGround =
        IntersectAtmosphere({radius + altitude, 0.0, 0.0}, {-1.0, 0.0, 0.0}, radius, radius + 100'000.0);
    REQUIRE(nearGround);
    CHECK(nearGround->End == doctest::Approx(altitude).epsilon(1.0e-12));
    CHECK(nearGround->GroundOccluded);
}

TEST_CASE("atmosphere ray geometry rejects invalid and unnormalized inputs")
{
    using Keire::Detail::IntersectAtmosphere;
    for (const auto direction : {std::array{0.0, 0.0, 0.0}, std::array{0.0, 2.0, 0.0},
                                 std::array{0.0, std::numeric_limits<double>::quiet_NaN(), 0.0}})
    {
        CHECK_THROWS_AS((void)IntersectAtmosphere({0.0, 10.0, 0.0}, direction, 10.0, 12.0), std::invalid_argument);
    }
    CHECK_THROWS_AS((void)IntersectAtmosphere({0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, 12.0, 10.0), std::invalid_argument);
    CHECK_THROWS_AS((void)IntersectAtmosphere({0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, 0.0, 12.0), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)IntersectAtmosphere({0.0, std::numeric_limits<double>::infinity(), 0.0}, {0.0, 1.0, 0.0}, 10.0, 12.0),
        std::invalid_argument);
}
