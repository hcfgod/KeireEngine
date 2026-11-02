#include "KeireInternal/Spatial/PlanetCharacterKinematicPath.h"
#include "KeireInternal/Spatial/PlanetCharacterMovingClearance.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Planet moving clearance detects crossing obstacles without changing native poses")
{
    using namespace Keire;
    const auto system = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = system->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Position = {-2.0F, 0.0F, 0.0F};
    definition.HalfExtent = {0.1F, 0.1F, 0.1F};
    const auto body = world->CreateBody(definition);
    PhysicsCapsuleOverlapQuery characterShape;
    characterShape.Radius = 0.2F;
    characterShape.Height = 1.0F;
    Detail::PlanetCharacterFramePath character;
    character.Start = characterShape;
    PhysicsCapsuleOverlapQuery obstaclePose;
    obstaclePose.Center = definition.Position;
    const auto moving =
        Detail::MakePlanetCharacterKinematicPath(obstaclePose, definition.Position, {2.0F, 0.0F, 0.0F}, {});
    const std::array obstacles{Detail::PlanetCharacterMovingObstacle{body, moving}};
    const std::array ids{body};
    CHECK_FALSE(Detail::PhysicsFrameAccess::HasCapsuleContact(*world, characterShape));
    CHECK_FALSE(Detail::PlanetCharacterMovingClearance(*world, character, obstacles, ids));
    const auto unchanged = world->TryGetBody(body);
    REQUIRE(unchanged);
    CHECK(unchanged->Position == definition.Position);
    CHECK(unchanged->LinearVelocity == Vector3{});

    auto away = obstacles;
    away[0].Path = Detail::MakePlanetCharacterKinematicPath(obstaclePose, definition.Position, {-4.0F, 0.0F, 0.0F}, {});
    CHECK(Detail::PlanetCharacterMovingClearance(*world, character, away, ids));
    auto staged = away;
    auto stagedPose = obstaclePose;
    stagedPose.Center = {2.0F, 0.0F, 0.0F};
    staged[0].Path = Detail::MakePlanetCharacterKinematicPath(stagedPose, stagedPose.Center, {4.0F, 0.0F, 0.0F}, {});
    staged[0].NativeReference = obstaclePose;
    CHECK(Detail::PlanetCharacterMovingClearance(*world, character, staged, ids));

    CHECK_FALSE(Detail::PlanetCharacterMovingClearance(*world, character, away, ids, 1));
    CHECK_THROWS_AS((void)Detail::PlanetCharacterMovingClearance(*world, character, away, {}), std::invalid_argument);
    CHECK_THROWS_AS((void)Detail::PlanetCharacterMovingClearance(*world, character, away, ids, 0),
                    std::invalid_argument);
    world->Close();
    system->Close();
}
