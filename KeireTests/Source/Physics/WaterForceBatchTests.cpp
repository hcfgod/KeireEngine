#include "Keire/Water/WaterForceBatch.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

TEST_CASE("water force preparation preserves pending native loads even if a later body fails")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    const auto first = world->CreateBody(definition);
    const auto second = world->CreateBody(definition);
    world->AddForce(first, {1.0F, 2.0F, 3.0F});
    world->AddTorque(first, {4.0F, 5.0F, 6.0F});
    const auto before = world->TryGetBody(first);
    REQUIRE(before);
    Keire::WaterForceBodySnapshot snapshot;
    snapshot.Entity = Keire::EntityId(Keire::AssetId(1, 1));
    snapshot.Body = first;
    snapshot.Kinematics.CenterOfMass = {1.0e12, -1.0, 0.0};
    snapshot.ProbeGravity = {{0.0, -9.81, 0.0}};
    Keire::WaterSurfaceDefinition water;
    water.Origin.X = 1.0e12;
    const std::array media{Keire::WaterMedium(Keire::AssetId(2, 1), water, {})};
    const Keire::WaterSimulationStamp stamp{7, 8, 9, {1.0e12, 0.0, 0.0}};
    const std::array bodies{snapshot};
    const Keire::PreparedWaterForceBatch prepared(stamp, bodies, media, 0.0, 1.0 / 60.0);
    REQUIRE(prepared.Bodies().size() == 1);
    CHECK(prepared.Stamp() == stamp);
    CHECK(prepared.TimeSeconds() == 0.0);
    CHECK(prepared.DeltaSeconds() == 1.0 / 60.0);
    CHECK(prepared.Bodies()[0].Body == first);
    CHECK(prepared.Bodies()[0].Loads.ForceNewtons.Y > 0.0);
    CHECK(world->TryGetBody(first)->PendingForce == before->PendingForce);
    CHECK(world->TryGetBody(first)->PendingTorque == before->PendingTorque);
    const Keire::PreparedWaterForceBatch repeated(stamp, bodies, media, 0.0, 1.0 / 60.0);
    CHECK(repeated.Bodies()[0].Loads.ForceNewtons == prepared.Bodies()[0].Loads.ForceNewtons);
    CHECK(world->TryGetBody(first)->PendingForce == before->PendingForce);
    auto invalid = snapshot;
    invalid.Entity = Keire::EntityId(Keire::AssetId(1, 2));
    invalid.Body = second;
    invalid.Kinematics.InverseMass = 0.0;
    const std::array invalidBodies{snapshot, invalid};
    CHECK_THROWS_AS(Keire::PreparedWaterForceBatch(stamp, invalidBodies, media, 0.0, 1.0 / 60.0),
                    std::invalid_argument);
    CHECK(world->TryGetBody(first)->PendingForce == before->PendingForce);
    CHECK(world->TryGetBody(first)->PendingTorque == before->PendingTorque);
    const std::array duplicates{snapshot, snapshot};
    CHECK_THROWS_AS(Keire::PreparedWaterForceBatch(stamp, duplicates, media, 0.0, 1.0 / 60.0), std::invalid_argument);
}
