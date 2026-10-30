#include "KeireInternal/Rendering/PlanetAtmosphereComposition.h"
#include "KeireInternal/Spatial/SectorPosition.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace Keire::Detail;

    [[nodiscard]] PlanetAtmosphereCoefficients Medium(double extinction, double source)
    {
        return {{extinction, extinction, extinction}, {source, source, source}};
    }
} // namespace

TEST_CASE("Planet atmosphere composition orders separated media and skips interplanetary vacuum")
{
    const std::array bodies{PlanetAtmosphereRayBody{{0.0, 0.0, -2.0}, 1.0, 0.0},
                            PlanetAtmosphereRayBody{{0.0, 0.0, -1.0e12}, 1.0, 0.0}};
    const auto result = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 2.0e12, 1, [](std::size_t index, double)
                                                   { return index == 0 ? Medium(0.5, 1.0) : Medium(1.0, 3.0); });
    CHECK_FALSE(result.OpaqueBody);
    CHECK(result.MediumSamples == 2);
    CHECK(result.Transmittance[0] == doctest::Approx(std::exp(-3.0)));
    const double expected = 2.0 * (1.0 - std::exp(-1.0)) + std::exp(-1.0) * 3.0 * (1.0 - std::exp(-2.0));
    CHECK(result.Radiance[0] == doctest::Approx(expected));
}

TEST_CASE("Planet atmosphere composition is invariant under common sector translation")
{
    for (const std::int64_t sector :
         {std::int64_t{0}, std::int64_t{1'000'000'000'000}, std::int64_t{-1'000'000'000'000}})
    {
        const SectorPosition observer({1, 1}, {sector, sector, sector}, {0.125, -0.25, 0.5});
        const auto center = observer.Translated({0.0, 0.0, 3.0});
        const std::array bodies{PlanetAtmosphereRayBody{observer.RelativeTo(center), 2.0, 1.0}};
        const auto result = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, 4,
                                                       [](std::size_t, double) { return Medium(1.0, 1.0); });
        CHECK(result.EndDistance == doctest::Approx(2.0));
        CHECK(result.Transmittance[0] == doctest::Approx(std::exp(-1.0)));
        CHECK(result.Radiance[0] == doctest::Approx(1.0 - std::exp(-1.0)));
    }
}

TEST_CASE("Planet atmosphere overlaps combine coefficients before attenuation")
{
    const std::array bodies{PlanetAtmosphereRayBody{{0.0, 0.0, -2.0}, 1.0, 0.0},
                            PlanetAtmosphereRayBody{{0.0, 0.0, -3.0}, 1.0, 0.0}};
    for (const std::size_t samples : {1U, 7U, 64U})
    {
        const auto result =
            IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, samples, [](std::size_t index, double)
                                       { return index == 0 ? Medium(0.5, 1.0) : Medium(1.0, 3.0); });
        const double expected = 2.0 * (1.0 - std::exp(-0.5)) + std::exp(-0.5) * (4.0 / 1.5) * (1.0 - std::exp(-1.5)) +
                                std::exp(-2.0) * 3.0 * (1.0 - std::exp(-1.0));
        CHECK(result.Radiance[0] == doctest::Approx(expected));
        CHECK(result.Transmittance[0] == doctest::Approx(std::exp(-3.0)));
    }
    const std::array reversed{bodies[1], bodies[0]};
    const auto forward = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, 8, [](std::size_t index, double)
                                                    { return index == 0 ? Medium(0.5, 1.0) : Medium(1.0, 3.0); });
    const auto reverse = IntegratePlanetAtmospheres(reversed, {0.0, 0.0, 1.0}, 10.0, 8, [](std::size_t index, double)
                                                    { return index == 1 ? Medium(0.5, 1.0) : Medium(1.0, 3.0); });
    CHECK(forward.Radiance == reverse.Radiance);
    CHECK(forward.Transmittance == reverse.Transmittance);
}

TEST_CASE("Planet atmosphere composition samples varying density along the ordered ray")
{
    const std::array bodies{PlanetAtmosphereRayBody{{}, 2.0, 0.0}};
    const auto result = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, 32, [](std::size_t, double distance)
                                                   { return Medium(distance, distance); });
    // Integral of distance over [0, 2] is 2; source/extinction is identically one.
    CHECK(result.Transmittance[0] == doctest::Approx(std::exp(-2.0)));
    CHECK(result.Radiance[0] == doctest::Approx(1.0 - std::exp(-2.0)));
}

TEST_CASE("Planet atmosphere opaque hits stop background media and respect inside and surface rays")
{
    const std::array bodies{PlanetAtmosphereRayBody{{0.0, 0.0, -3.0}, 2.0, 1.0},
                            PlanetAtmosphereRayBody{{0.0, 0.0, -6.0}, 1.0, 0.0}};
    const auto result = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 100.0, 4,
                                                   [](std::size_t index, double)
                                                   {
                                                       CHECK(index == 0);
                                                       return Medium(1.0, 1.0);
                                                   });
    REQUIRE(result.OpaqueBody);
    CHECK(*result.OpaqueBody == 0);
    CHECK(result.EndDistance == doctest::Approx(2.0));
    CHECK(result.Radiance[0] == doctest::Approx(1.0 - std::exp(-1.0)));
    CHECK(result.Transmittance[0] == doctest::Approx(std::exp(-1.0)));
    const std::array surface{PlanetAtmosphereRayBody{{0.0, 1.0, 0.0}, 2.0, 1.0}};
    const auto outward = IntegratePlanetAtmospheres(surface, {0.0, 1.0, 0.0}, 10.0, 2,
                                                    [](std::size_t, double) { return Medium(1.0, 1.0); });
    CHECK_FALSE(outward.OpaqueBody);
    CHECK(outward.Transmittance[0] == doctest::Approx(std::exp(-1.0)));
    const auto inward = IntegratePlanetAtmospheres(surface, {0.0, -1.0, 0.0}, 10.0, 2,
                                                   [](std::size_t, double) { return Medium(1.0, 1.0); });
    CHECK(inward.OpaqueBody.has_value());
    CHECK(inward.EndDistance == 0.0);
    CHECK(inward.MediumSamples == 0);
    const std::array inside{PlanetAtmosphereRayBody{{0.0, 0.0, 0.0}, 2.0, 1.0}};
    CHECK(IntegratePlanetAtmospheres(inside, {0.0, 1.0, 0.0}, 10.0, 2,
                                     [](std::size_t, double) { return Medium(1.0, 1.0); })
              .EndDistance == 0.0);
}

TEST_CASE("Planet atmosphere tangency and validation do not invent a visible medium")
{
    const std::array tangent{PlanetAtmosphereRayBody{{0.0, 1.0, -2.0}, 1.0, 0.0}};
    const auto result = IntegratePlanetAtmospheres(tangent, {0.0, 0.0, 1.0}, 10.0, 2,
                                                   [](std::size_t, double) { return Medium(1.0, 1.0); });
    CHECK(result.MediumSamples == 0);
    CHECK(result.Radiance[0] == 0.0);
    CHECK(result.Transmittance[0] == 1.0);
    CHECK_THROWS_AS(IntegratePlanetAtmospheres(tangent, {0.0, 0.0, 2.0}, 10.0, 2,
                                               [](std::size_t, double) { return Medium(1.0, 1.0); }),
                    std::invalid_argument);
    const std::array inside{PlanetAtmosphereRayBody{{}, 1.0, 0.0}};
    CHECK_THROWS_AS(IntegratePlanetAtmospheres(inside, {0.0, 0.0, 1.0}, 10.0, 2, [](std::size_t, double)
                                               { return Medium(std::numeric_limits<double>::quiet_NaN(), 1.0); }),
                    std::invalid_argument);
}

TEST_CASE("Planet atmosphere rejects finite geometry whose intermediate distances exceed double range")
{
    const double largest = std::numeric_limits<double>::max();
    const std::array bodies{PlanetAtmosphereRayBody{{largest, largest, largest}, 1.0, 0.0}};
    CHECK_THROWS_AS(IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, 2,
                                               [](std::size_t, double) { return Medium(1.0, 1.0); }),
                    std::out_of_range);
    const std::array hugeSphere{PlanetAtmosphereRayBody{{}, largest, 0.0}};
    CHECK_THROWS_AS(IntegratePlanetAtmospheres(hugeSphere, {0.0, 0.0, 1.0}, 10.0, 2,
                                               [](std::size_t, double) { return Medium(1.0, 1.0); }),
                    std::out_of_range);
}

TEST_CASE("Planet atmosphere equal opaque hits retain stable first candidate")
{
    const std::array bodies{PlanetAtmosphereRayBody{{0.0, 0.0, -3.0}, 2.0, 1.0},
                            PlanetAtmosphereRayBody{{0.0, 0.0, -3.0}, 2.0, 1.0}};
    const auto result = IntegratePlanetAtmospheres(bodies, {0.0, 0.0, 1.0}, 10.0, 2,
                                                   [](std::size_t, double) { return Medium(1.0, 1.0); });
    REQUIRE(result.OpaqueBody);
    CHECK(*result.OpaqueBody == 0);
}
