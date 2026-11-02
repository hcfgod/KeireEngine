#include "KeireInternal/Spatial/PlanetCharacterFramePath.h"
#include "KeireInternal/Spatial/PlanetCharacterPoseClearance.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

TEST_CASE("Planet character pose envelopes contain the swept centerline and preserve query filters")
{
    Keire::PhysicsCapsuleOverlapQuery capsule;
    capsule.Radius = 0.3F;
    capsule.Height = 2.0F;
    capsule.Mask = 7;
    capsule.Layer = 2;
    capsule.IncludeTriggers = false;
    const double halfSegment = static_cast<double>(capsule.Height) * 0.5 - capsule.Radius;
    for (const double angle : {0.0, 0.01, 0.5, std::numbers::pi})
    {
        const double translationBound = 0.125;
        const auto envelope = Keire::Detail::PlanetCharacterPoseEnvelope(capsule, angle, translationBound);
        CHECK(envelope.Mask == capsule.Mask);
        CHECK(envelope.Layer == capsule.Layer);
        CHECK_FALSE(envelope.IncludeTriggers);
        CHECK(static_cast<double>(envelope.Height) - 2.0 * envelope.Radius >= 2.0 * halfSegment);
        for (int sample = -100; sample <= 100; ++sample)
        {
            const double rotation = angle * sample / 100.0;
            const double lateral = halfSegment * std::sin(rotation);
            const double vertical = halfSegment * (std::cos(rotation) - 1.0);
            const double excursion = std::hypot(lateral, vertical) + translationBound;
            CHECK(static_cast<double>(envelope.Radius) >= capsule.Radius + excursion);
        }
    }
}

TEST_CASE("Planet character pose envelopes reject invalid geometry and unbounded motion")
{
    Keire::PhysicsCapsuleOverlapQuery capsule;
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPoseEnvelope(capsule, -0.1, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPoseEnvelope(capsule, 0.0, -0.1), std::invalid_argument);
    capsule.Height = 0.5F;
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPoseEnvelope(capsule, 0.0, 0.0), std::invalid_argument);
    capsule.Height = 1.0F;
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPoseEnvelope(capsule, 0.0, std::numeric_limits<double>::max()),
                    std::overflow_error);
}

TEST_CASE("Planet character clearance subdivides conservative overlap and rejects unresolved paths")
{
    const auto path = [](const double begin, const double end)
    {
        Keire::Detail::PlanetCharacterPoseInterval interval;
        interval.Midpoint.Center.X = static_cast<float>((begin + end) * 0.5);
        interval.CenterExcursion = (end - begin) * 0.5;
        return interval;
    };
    std::size_t queries = 0;
    const auto clearSmallEnvelope = [&](const Keire::PhysicsCapsuleOverlapQuery& envelope)
    {
        ++queries;
        return envelope.Radius <= 0.625F;
    };
    CHECK(Keire::Detail::PlanetCharacterPathClear(path, clearSmallEnvelope, 7, 2));
    CHECK(queries == 7);
    queries = 0;
    CHECK_FALSE(Keire::Detail::PlanetCharacterPathClear(path, clearSmallEnvelope, 6, 2));
    CHECK(queries == 6);
    queries = 0;
    CHECK_FALSE(Keire::Detail::PlanetCharacterPathClear(path, clearSmallEnvelope, 7, 1));
    CHECK(queries == 2);

    // A thin obstacle late in the motion must reject the whole move after earlier intervals passed.
    const auto obstacle = [](const Keire::PhysicsCapsuleOverlapQuery& envelope)
    {
        const double excursion = static_cast<double>(envelope.Radius) - 0.5;
        return std::abs(static_cast<double>(envelope.Center.X) - 0.875) > excursion;
    };
    CHECK_FALSE(Keire::Detail::PlanetCharacterPathClear(path, obstacle));
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPathClear(path, obstacle, 0), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::PlanetCharacterPathClear(path, obstacle, 1, 21), std::invalid_argument);
}

TEST_CASE("Planet character native clearance detects intermediate rotation collisions at distant origins")
{
    using namespace Keire;
    const auto system = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    for (const double origin : {0.0, 1.0e12, -1.0e12})
    {
        CAPTURE(origin);
        const auto world = system->CreateWorld({origin, -origin, origin});
        PhysicsBodyDefinition obstacle;
        obstacle.Position = {0.0F, 0.9F, 0.0F};
        obstacle.HalfExtent = {0.05F, 0.05F, 0.5F};
        const auto obstacleBody = world->CreateBody(obstacle);
        const auto path = [](const double begin, const double end)
        {
            Detail::PlanetCharacterPoseInterval interval;
            interval.Midpoint.Radius = 0.2F;
            interval.Midpoint.Height = 2.0F;
            const double angle = std::numbers::pi * ((begin + end) * 0.5 - 0.5);
            interval.Midpoint.Rotation = {0.0F, 0.0F, static_cast<float>(std::sin(angle * 0.5)),
                                          static_cast<float>(std::cos(angle * 0.5))};
            interval.AngularExcursion = std::numbers::pi * (end - begin) * 0.5;
            return interval;
        };
        const auto query = [&](const PhysicsCapsuleOverlapQuery& envelope)
        {
            const bool clear = Detail::PlanetCharacterPoseIntervalClear(*world, envelope, 0.0, 0.0);
            CHECK(clear == world->OverlapCapsule(envelope).empty());
            return clear;
        };
        CHECK(query(path(0.0, 0.0).Midpoint));
        CHECK(query(path(1.0, 1.0).Midpoint));
        CHECK_FALSE(Detail::PlanetCharacterPathClear(path, query));
        const auto unchanged = world->TryGetBody(obstacleBody);
        REQUIRE(unchanged);
        CHECK(unchanged->Position == obstacle.Position);
        world->DestroyBody(obstacleBody);
        CHECK(Detail::PlanetCharacterPathClear(path, query));
        world->Close();
    }
    system->Close();
}

TEST_CASE("Planet character native clearance detects thin obstacles between clear translation endpoints")
{
    using namespace Keire;
    const auto system = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    PhysicsBodyDefinition wall;
    wall.Position = {0.875F, 0.0F, 0.0F};
    wall.HalfExtent = {0.005F, 2.0F, 2.0F};
    const auto wallBody = world->CreateBody(wall);
    const auto path = [](const double begin, const double end)
    {
        Detail::PlanetCharacterPoseInterval interval;
        interval.Midpoint.Radius = 0.1F;
        interval.Midpoint.Height = 1.0F;
        interval.Midpoint.Center.X = static_cast<float>(begin + end);
        interval.CenterExcursion = end - begin;
        return interval;
    };
    const auto query = [&](const PhysicsCapsuleOverlapQuery& envelope)
    { return Detail::PlanetCharacterPoseIntervalClear(*world, envelope, 0.0, 0.0); };
    CHECK(query(path(0.0, 0.0).Midpoint));
    CHECK(query(path(1.0, 1.0).Midpoint));
    CHECK_FALSE(Detail::PlanetCharacterPathClear(path, query));
    world->DestroyBody(wallBody);
    CHECK(Detail::PlanetCharacterPathClear(path, query));
    world->Close();
    CHECK_THROWS_AS(Detail::PlanetCharacterPathClear(path, query), std::logic_error);
    system->Close();
}

TEST_CASE("Planet character support path encloses pivot arcs translation and capsule orientation")
{
    using namespace Keire::Detail;
    PlanetCharacterFramePath path;
    path.Start.Center = {4.0F, 1.0F, 0.0F};
    path.Start.Radius = 0.25F;
    path.Start.Height = 2.0F;
    path.Pivot = {2.0, 0.0, 0.0};
    path.PivotDisplacement = {3.0, -2.0, 1.0};
    constexpr double totalAngle = 2.0;
    path.Rotation = FrameRotation::Normalize(0.0, 0.0, std::sin(totalAngle * 0.5), std::cos(totalAngle * 0.5));
    for (const double width : {1.0, 0.25, 0.03125})
    {
        const double begin = 0.375;
        const double end = std::min(1.0, begin + width);
        const auto interval = path(begin, end);
        const auto envelope =
            PlanetCharacterPoseEnvelope(interval.Midpoint, interval.AngularExcursion, interval.CenterExcursion);
        const auto orientation = FrameRotation::Normalize(envelope.Rotation.X, envelope.Rotation.Y, envelope.Rotation.Z,
                                                          envelope.Rotation.W);
        const auto up = orientation.Apply({0.0, 1.0, 0.0});
        const double halfSegment = static_cast<double>(envelope.Height) * 0.5 - envelope.Radius;
        for (int sample = 0; sample <= 100; ++sample)
        {
            const double fraction = begin + (end - begin) * sample / 100.0;
            const double angle = totalAngle * fraction;
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            for (const double tip : {-0.75, 0.75})
            {
                // Independent analytic Z rotation of the start center and capsule endpoint about the pivot.
                const double x = 2.0 + 3.0 * fraction + 2.0 * cosine - (1.0 + tip) * sine;
                const double y = -2.0 * fraction + 2.0 * sine + (1.0 + tip) * cosine;
                const double z = fraction;
                const std::array relative{x - envelope.Center.X, y - envelope.Center.Y, z - envelope.Center.Z};
                const double projection = std::clamp(relative[0] * up[0] + relative[1] * up[1] + relative[2] * up[2],
                                                     -halfSegment, halfSegment);
                const double distance = std::hypot(relative[0] - projection * up[0], relative[1] - projection * up[1],
                                                   relative[2] - projection * up[2]);
                CHECK(distance + path.Start.Radius <= static_cast<double>(envelope.Radius));
            }
        }
    }
    CHECK_THROWS_AS(path(-0.1, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(path(0.7, 0.3), std::invalid_argument);
    path.PivotDisplacement[0] = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(path(0.0, 1.0), std::invalid_argument);
}
