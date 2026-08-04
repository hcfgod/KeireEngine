#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <cmath>

namespace
{
    [[nodiscard]] Keire::Ref<Keire::PhysicsSystem> CreatePhysics()
    {
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        return Keire::CreateRef<Keire::PhysicsSystem>(specification);
    }
} // namespace

TEST_CASE("physics queries use narrow-phase geometry and bounded opt-in traces")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition sphere;
    sphere.Shape = Keire::ColliderShape::Sphere;
    sphere.Radius = 1.0F;
    sphere.Layer = 1U;
    const auto sphereBody = world->CreateBody(sphere);

    Keire::PhysicsBodyDefinition trigger = sphere;
    trigger.Position = {3.0F, 0.0F, 0.0F};
    trigger.Radius = 0.5F;
    trigger.Layer = 2U;
    trigger.Trigger = true;
    const auto triggerBody = world->CreateBody(trigger);

    const auto hits = world->RayCast(
        {.Origin = {-2.0F, 0.9F, 0.0F}, .Direction = {1.0F, 0.0F, 0.0F}, .MaximumDistance = 10.0F, .Mask = 1U});
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().Body == sphereBody);
    CHECK(hits.front().Distance == doctest::Approx(2.0F - std::sqrt(0.19F)).epsilon(0.001));
    CHECK(hits.front().Normal.Y == doctest::Approx(0.9F).epsilon(0.001));

    CHECK(world->OverlapSphere({0.9F, 0.9F, 0.0F}, 0.1F, 1U).empty());
    const auto overlaps = world->OverlapSphere({0.9F, 0.0F, 0.0F}, 0.2F, 1U);
    REQUIRE(overlaps.size() == 1);
    CHECK(overlaps.front() == sphereBody);
    CHECK(world
              ->RayCast({.Origin = {2.0F, 0.0F, 0.0F},
                         .Direction = {1.0F, 0.0F, 0.0F},
                         .MaximumDistance = 2.0F,
                         .Mask = 2U,
                         .IncludeTriggers = false})
              .empty());

    CHECK_FALSE(world->CaptureDebugSnapshot());
    world->ConfigureDebugCapture(
        {.Enabled = true, .MaximumBodies = 1, .MaximumContacts = 1, .MaximumQueryTraces = 2, .MaximumHitsPerQuery = 1});
    CHECK(world->RayCast({.Origin = {-2.0F, 0.0F, 0.0F}, .Direction = {1.0F, 0.0F, 0.0F}, .MaximumDistance = 10.0F})
              .size() == 2);
    (void)world->OverlapSphere({}, 0.25F);
    (void)world->OverlapSphere({3.0F, 0.0F, 0.0F}, 0.25F);

    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot);
    CHECK(snapshot->Bodies.size() == 1);
    CHECK(snapshot->DroppedBodies == 1);
    REQUIRE(snapshot->Queries.size() == 2);
    CHECK(snapshot->Queries.front().Sequence == 2);
    CHECK(snapshot->Queries.back().Sequence == 3);
    CHECK(snapshot->DroppedQueries == 1);
    REQUIRE(snapshot->Queries.back().Overlaps.size() == 1);
    CHECK(snapshot->Queries.back().Overlaps.front() == triggerBody);
}

TEST_CASE("capsule casts return walkable surface normals and can ignore the controller body")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition wall;
    wall.Position = {3.0F, 1.0F, 0.0F};
    wall.HalfExtent = {0.25F, 2.0F, 3.0F};
    const auto wallBody = world->CreateBody(wall);

    Keire::PhysicsBodyDefinition character;
    character.Motion = Keire::PhysicsMotionType::Kinematic;
    character.Shape = Keire::ColliderShape::Capsule;
    character.Position = {0.0F, 1.0F, 0.0F};
    character.Radius = 0.4F;
    character.Height = 1.0F;
    const auto characterBody = world->CreateBody(character);

    const auto hit = world->CastCapsule({.Origin = character.Position,
                                         .Radius = character.Radius,
                                         .Height = character.Height,
                                         .Displacement = {5.0F, 0.0F, 0.0F},
                                         .IgnoreBody = characterBody});
    REQUIRE(hit);
    CHECK(hit->Body == wallBody);
    CHECK(hit->Distance == doctest::Approx(2.35F).epsilon(0.01));
    CHECK(hit->Normal.X == doctest::Approx(-1.0F).epsilon(0.001));
    CHECK(hit->Normal.Y == doctest::Approx(0.0F).epsilon(0.001));

    const auto selfHit = world->CastCapsule({.Origin = character.Position,
                                             .Radius = character.Radius,
                                             .Height = character.Height,
                                             .Displacement = {0.1F, 0.0F, 0.0F}});
    REQUIRE(selfHit);
    CHECK(selfHit->Body == characterBody);
    CHECK_THROWS_AS((void)world->CastCapsule({.Radius = -1.0F, .Displacement = {1.0F, 0.0F}}), std::invalid_argument);
}

TEST_CASE("physics contacts expose narrow-phase data and enforce reciprocal masks")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition floor;
    floor.Position = {0.0F, -0.5F, 0.0F};
    floor.HalfExtent = {5.0F, 0.5F, 5.0F};
    floor.Layer = 1U;
    floor.Mask = 1U;
    (void)world->CreateBody(floor);

    Keire::PhysicsBodyDefinition body;
    body.Motion = Keire::PhysicsMotionType::Dynamic;
    body.Shape = Keire::ColliderShape::Sphere;
    body.Position = {0.0F, 0.45F, 0.0F};
    body.LinearVelocity = {0.0F, -2.0F, 0.0F};
    body.Radius = 0.5F;
    body.Layer = 1U;
    body.Mask = 1U;
    body.UseGravity = false;
    (void)world->CreateBody(body);

    world->Step(1.0F / 60.0F);
    const auto contacts = world->DrainContactEvents();
    REQUIRE(contacts.size() == 1);
    CHECK(contacts.front().Phase == Keire::ContactPhase::Enter);
    CHECK_FALSE(contacts.front().Trigger);
    CHECK(Keire::Math::IsFinite(contacts.front().Point));
    CHECK(Keire::Math::IsFinite(contacts.front().Normal));
    const auto normalLength = std::sqrt(contacts.front().Normal.X * contacts.front().Normal.X +
                                        contacts.front().Normal.Y * contacts.front().Normal.Y +
                                        contacts.front().Normal.Z * contacts.front().Normal.Z);
    CHECK(normalLength == doctest::Approx(1.0F).epsilon(0.001));
    CHECK(contacts.front().Impulse >= 0.0F);

    const auto filteredWorld = system->CreateWorld();
    floor.Layer = 2U;
    floor.Mask = 2U;
    (void)filteredWorld->CreateBody(floor);
    body.Layer = 1U;
    body.Mask = 1U;
    (void)filteredWorld->CreateBody(body);
    filteredWorld->Step(1.0F / 60.0F);
    CHECK(filteredWorld->DrainContactEvents().empty());
}

TEST_CASE("physics gravity can be authored and changed without recreating a body")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition body;
    body.Motion = Keire::PhysicsMotionType::Dynamic;
    body.Shape = Keire::ColliderShape::Sphere;
    body.Position = {0.0F, 5.0F, 0.0F};
    body.UseGravity = false;
    const auto id = world->CreateBody(body);

    world->Step(0.1F);
    const auto suspended = world->TryGetBody(id);
    REQUIRE(suspended);
    CHECK(suspended->Position.Y == doctest::Approx(5.0F));

    world->SetGravityEnabled(id, true);
    world->Step(0.1F);
    const auto falling = world->TryGetBody(id);
    REQUIRE(falling);
    CHECK(falling->Position.Y < suspended->Position.Y);
    CHECK(falling->LinearVelocity.Y < 0.0F);

    world->ConfigureDebugCapture({.Enabled = true});
    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot);
    REQUIRE(snapshot->Bodies.size() == 1);
    CHECK(snapshot->Bodies.front().UseGravity);
    CHECK_THROWS_AS(world->SetGravityEnabled({}, false), std::invalid_argument);
}

TEST_CASE("physics body checkpoint state restores transforms velocities and sleep state")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.Shape = Keire::ColliderShape::Sphere;
    definition.UseGravity = false;
    const auto body = world->CreateBody(definition);

    Keire::PhysicsBodyState checkpoint;
    checkpoint.Body = body;
    checkpoint.Position = {4.0F, 5.0F, 6.0F};
    checkpoint.Rotation = Keire::Math::Normalize(Keire::Quaternion{0.1F, 0.2F, 0.3F, 0.9F});
    checkpoint.LinearVelocity = {1.0F, 2.0F, 3.0F};
    checkpoint.AngularVelocity = {0.25F, 0.5F, 0.75F};
    checkpoint.Sleeping = false;
    world->SetBodyState(body, checkpoint);

    const auto restored = world->TryGetBody(body);
    REQUIRE(restored);
    CHECK(restored->Position.X == doctest::Approx(checkpoint.Position.X));
    CHECK(restored->Position.Y == doctest::Approx(checkpoint.Position.Y));
    CHECK(restored->Position.Z == doctest::Approx(checkpoint.Position.Z));
    CHECK(restored->LinearVelocity.X == doctest::Approx(checkpoint.LinearVelocity.X));
    CHECK(restored->AngularVelocity.Z == doctest::Approx(checkpoint.AngularVelocity.Z));
    CHECK_FALSE(restored->Sleeping);
    checkpoint.LinearVelocity = {};
    checkpoint.AngularVelocity = {};
    checkpoint.Sleeping = true;
    world->SetBodyState(body, checkpoint);
    REQUIRE(world->TryGetBody(body));
    CHECK(world->TryGetBody(body)->Sleeping);
    CHECK_THROWS_AS(world->SetBodyState({}, checkpoint), std::invalid_argument);
}

TEST_CASE("project collision matrix filters contacts and reciprocal queries inside each physics world")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    specification.CollisionMatrix[0] &= ~(1U << 1U);
    specification.CollisionMatrix[1] &= ~(1U << 0U);
    const auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    CHECK(system->CollisionMatrix() == specification.CollisionMatrix);
    const auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition first;
    first.Shape = Keire::ColliderShape::Sphere;
    first.Radius = 1.0F;
    first.Layer = 1U;
    first.Mask = ~0U;
    (void)world->CreateBody(first);

    auto second = first;
    second.Motion = Keire::PhysicsMotionType::Dynamic;
    second.Position = {0.5F, 0.0F, 0.0F};
    second.Layer = 2U;
    second.UseGravity = false;
    (void)world->CreateBody(second);

    world->Step(1.0F / 60.0F);
    CHECK(world->DrainContactEvents().empty());
    CHECK(world
              ->RayCast({.Origin = {3.0F, 0.0F, 0.0F},
                         .Direction = {-1.0F, 0.0F, 0.0F},
                         .MaximumDistance = 10.0F,
                         .Mask = 2U,
                         .IncludeTriggers = true,
                         .Layer = 1U})
              .empty());
    CHECK_THROWS_AS(system->ConfigureCollisionMatrix(specification.CollisionMatrix), std::logic_error);
}
