#include "Keire/Physics/PhysicsSystem.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

TEST_CASE("Authored physics batch rejects every invalid entry before writing an earlier body")
{
    using namespace Keire;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition dynamic;
    dynamic.Motion = PhysicsMotionType::Dynamic;
    dynamic.UseGravity = false;
    const auto first = world->CreateBody(dynamic);
    PhysicsBodyDefinition stationary;
    stationary.Position = {5.0F, 0.0F, 0.0F};
    const auto second = world->CreateBody(stationary);
    world->AddForce(first, {3.0F, 4.0F, 5.0F});
    const auto before = world->TryGetBody(first);
    REQUIRE(before);
    auto target = *before;
    target.Position = {2.0F, 0.0F, 0.0F};
    target.LinearVelocity = {1.0F, 2.0F, 3.0F};
    std::array<Detail::AuthoredPhysicsBodyUpdate, 2> updates{};
    updates[0].Body = first;
    updates[0].State = target;
    updates[0].UseGravity = true;
    updates[1].Body = second;
    updates[1].State = world->TryGetBody(second);
    REQUIRE(updates[1].State);
    bool removed = false;
    SUBCASE("invalid quaternion") { updates[1].State->Rotation = {0.0F, 0.0F, 0.0F, 0.0F}; }
    SUBCASE("invalid settings")
    {
        updates[1].MotionSettings = PhysicsMotionSettings{};
        updates[1].MotionSettings->MaximumLinearSpeed = -1.0F;
    }
    SUBCASE("force on static body") { updates[1].State->PendingForce = {1.0F, 0.0F, 0.0F}; }
    SUBCASE("duplicate identity") { updates[1].Body = first; }
    SUBCASE("missing later body")
    {
        world->DestroyBody(second);
        removed = true;
    }
    CHECK_THROWS_AS(Detail::PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates), std::invalid_argument);
    const auto after = world->TryGetBody(first);
    REQUIRE(after);
    CHECK(after->Body == before->Body);
    CHECK(after->Position == before->Position);
    CHECK(after->Rotation == before->Rotation);
    CHECK(after->LinearVelocity == before->LinearVelocity);
    CHECK(after->AngularVelocity == before->AngularVelocity);
    CHECK(after->PendingForce == before->PendingForce);
    CHECK(after->PendingTorque == before->PendingTorque);
    CHECK(after->Sleeping == before->Sleeping);
    CHECK(world->TryGetBody(second).has_value() == !removed);
    world->Close();
    CHECK_THROWS_AS(Detail::PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates), std::logic_error);
    physics->Close();
}

TEST_CASE("Authored physics batch updates dynamic and static poses while preserving queued loads and identities")
{
    using namespace Keire;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition dynamic;
    dynamic.Motion = PhysicsMotionType::Dynamic;
    dynamic.UseGravity = false;
    const auto first = world->CreateBody(dynamic);
    const auto second = world->CreateBody({});
    world->AddForce(first, {3.0F, 4.0F, 5.0F});
    auto firstState = world->TryGetBody(first);
    auto secondState = world->TryGetBody(second);
    REQUIRE(firstState);
    REQUIRE(secondState);
    firstState->Position = {2.0F, 0.0F, 0.0F};
    secondState->Position = {-2.0F, 0.0F, 0.0F};
    const std::array updates{Detail::AuthoredPhysicsBodyUpdate{first, firstState, PhysicsMotionSettings{}, false},
                             Detail::AuthoredPhysicsBodyUpdate{second, secondState, PhysicsMotionSettings{}, false}};
    Detail::PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates);
    const auto afterFirst = world->TryGetBody(first);
    const auto afterSecond = world->TryGetBody(second);
    REQUIRE(afterFirst);
    REQUIRE(afterSecond);
    CHECK(afterFirst->Body == first);
    CHECK(afterSecond->Body == second);
    CHECK(afterFirst->Position == firstState->Position);
    CHECK(afterSecond->Position == secondState->Position);
    CHECK(afterFirst->PendingForce == firstState->PendingForce);
    CHECK(afterFirst->PendingTorque == firstState->PendingTorque);
    const auto hits =
        world->RayCast({.Origin = {-5.0F, 0.0F, 0.0F}, .Direction = {1.0F, 0.0F, 0.0F}, .MaximumDistance = 10.0F});
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].Body == second);
    CHECK(hits[1].Body == first);
    world->Close();
    physics->Close();
}
