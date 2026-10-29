#include "KeireInternal/Spatial/PlanetDefinitionRecord.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

TEST_CASE("Planet resolution cancels translated and sector-scale placement before narrowing")
{
    using namespace Keire::Detail;
    for (const auto sector : {std::int64_t{0}, std::int64_t{1'000'000'000'000}, std::int64_t{-1'000'000'000'000}})
    {
        for (const double translation : {0.0, 1.0e10, -1.0e10, 1.0e12, -1.0e12})
        {
            FrameRegistry registry(1);
            const auto root = registry.Find(1);
            const auto planetFrame = registry.Create(2, root);
            PlanetDefinition planet;
            planet.Planet = Keire::AssetId(1, 2);
            planet.Frame = 2;
            planet.ParentCenter = SectorPosition({1, 1}, {sector, -sector, sector}, {translation, 0.0, 0.0});
            planet.Orientation = FrameRotation::Normalize(0.0, 0.0, 1.0, 1.0);
            const std::array<double, 3> local{0.125, planet.RadiusMetres + 123.25, -0.5};
            const auto observer = planet.ParentCenter.Translated(planet.Orientation.Apply(local));
            const std::array evaluations{EvaluatePlanetFrame(planet, 5.0, registry.Revision())};
            const auto published = registry.Publish(evaluations, 5.0);
            const auto resolved = ResolvePlanetFrame(registry, planetFrame, root, observer, published, 5.0, {},
                                                     planet.Planet, planet.RadiusMetres, 7);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                CHECK(resolved.ObserverFromCenterPlanetMetres[axis] == doctest::Approx(local[axis]).epsilon(1e-8));
            }
            CHECK(resolved.DiscontinuityRevision == 7);
            CHECK(resolved.Planet == planet.Planet);
            const auto up = resolved.PlanetToObserver.Apply({0.0, 1.0, 0.0});
            CHECK(up[0] == doctest::Approx(-1.0));
            CHECK(up[1] == doctest::Approx(0.0).epsilon(1e-12));
            auto next = registry.Publish(evaluations, 5.0);
            CHECK_THROWS_AS(ResolvePlanetFrame(registry, planetFrame, root, observer, published, 5.0, {}, planet.Planet,
                                               planet.RadiusMetres, 7),
                            std::invalid_argument);
        }
    }
}

TEST_CASE("Frame body conversion preserves pose and both velocities across rotating frames")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    const auto source = registry.Create(2, root);
    const auto target = registry.Create(3, root);
    const std::array<FrameSnapshot, 2> evaluations{
        FrameSnapshot({1, 2}, SectorPosition({1, 1}, {1'000'000'000'000, 0, 0}, {0.125, 0.0, 0.0}),
                      FrameRotation::Normalize(0.0, 0.0, 1.0, 1.0), {2.0, 3.0, 4.0}, {0.0, 0.0, 0.5}, 4.0,
                      registry.Revision()),
        FrameSnapshot({1, 3}, SectorPosition({1, 1}, {1'000'000'000'000, 0, 0}, {-2.0, 5.0, 0.0}),
                      FrameRotation::Normalize(1.0, 0.0, 0.0, 1.0), {-1.0, 2.0, 3.0}, {0.25, 0.0, 0.0}, 4.0,
                      registry.Revision())};
    const auto snapshots = registry.Publish(evaluations, 4.0);
    const SpatialBodyState body{{SectorPosition({1, 2}, {}, {1.0, 2.0, 3.0}), {4.0, 5.0, 6.0}, {0.1, 0.2, 0.3}},
                                FrameRotation::Normalize(1.0, 2.0, 3.0, 4.0),
                                {0.1, 0.2, 0.3}};
    const auto converted = ConvertFrameBodyState(registry, source, target, body, snapshots, 4.0);
    const auto restored = ConvertFrameBodyState(registry, target, source, converted, snapshots, 4.0);
    const auto error = restored.Motion.Position.RelativeTo(body.Motion.Position);
    const auto originalForward = body.Orientation.Apply({0.0, 0.0, 1.0});
    const auto restoredForward = restored.Orientation.Apply({0.0, 0.0, 1.0});
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(error[axis] == doctest::Approx(0.0).epsilon(1e-9));
        CHECK(restored.Motion.Velocity[axis] == doctest::Approx(body.Motion.Velocity[axis]));
        CHECK(restored.Motion.Acceleration[axis] == doctest::Approx(body.Motion.Acceleration[axis]));
        CHECK(restored.AngularVelocity[axis] == doctest::Approx(body.AngularVelocity[axis]));
        CHECK(restoredForward[axis] == doctest::Approx(originalForward[axis]));
    }
    FrameRegistry other(1);
    CHECK_THROWS_AS(ConvertFrameBodyState(registry, source, other.Find(1), body, snapshots, 4.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(ConvertFrameBodyState(registry, source, target, body, snapshots, 5.0), std::invalid_argument);
    auto invalid = body;
    invalid.AngularVelocity[0] = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(ConvertFrameBodyState(registry, source, target, invalid, snapshots, 4.0), std::invalid_argument);
}

TEST_CASE("Planet definition records preserve sector identity and reject malformed placement")
{
    using namespace Keire::Detail;
    PlanetDefinition planet;
    planet.Planet = Keire::AssetId(123, 456);
    planet.Frame = 2;
    planet.ParentCenter = SectorPosition({1, 1}, {INT64_MAX, INT64_MIN, 42}, {0.125, -0.25, 0.5});
    planet.AngularVelocity = {0.0, 0.25, 0.0};
    planet.GravitationalParameter = 123.5;
    const auto record = PlanetDefinitionRecord::Encode(planet);
    const auto decoded = PlanetDefinitionRecord::Decode(record);
    CHECK(decoded.ParentCenter == planet.ParentCenter);
    CHECK(decoded.Planet == planet.Planet);
    CHECK(decoded.AngularVelocity == planet.AngularVelocity);
    CHECK(decoded.GravitationalParameter == planet.GravitationalParameter);
    for (const char* field : {"frame", "parentFrame", "world"})
    {
        auto invalid = record;
        invalid[field] = -1;
        CHECK_THROWS_AS(PlanetDefinitionRecord::Decode(invalid), std::invalid_argument);
    }
    auto invalid = record;
    invalid["sectors"][0] = std::numeric_limits<std::uint64_t>::max();
    CHECK_THROWS_AS(PlanetDefinitionRecord::Decode(invalid), std::invalid_argument);
    invalid = record;
    invalid["radiusMetres"] = 0;
    CHECK_THROWS_AS(PlanetDefinitionRecord::Decode(invalid), std::invalid_argument);
    const auto rotated = EvaluatePlanetFrame(planet, 2.0 * std::numbers::pi, 1).Rotation().Apply({1.0, 0.0, 0.0});
    CHECK(rotated[0] == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(rotated[2] == doctest::Approx(-1.0));
}
