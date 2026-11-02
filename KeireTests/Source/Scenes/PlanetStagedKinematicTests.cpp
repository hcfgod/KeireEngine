#include "Keire/Physics/PhysicsSystem.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <thread>

TEST_CASE("Staged kinematic velocity rejects before any authored pose or pending force changes")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    definition.UseGravity = false;
    const auto first = world->CreateBody(definition);
    definition.Motion = PhysicsMotionType::Kinematic;
    definition.Position = {5.0F, 0.0F, 0.0F};
    const auto second = world->CreateBody(definition);
    world->AddForce(first, {2.0F, 3.0F, 4.0F});
    const auto beforeFirst = world->TryGetBody(first);
    const auto beforeSecond = world->TryGetBody(second);
    REQUIRE(beforeFirst);
    REQUIRE(beforeSecond);
    std::array<AuthoredPhysicsBodyUpdate, 2> updates{};
    updates[0].Body = first;
    updates[0].State = *beforeFirst;
    updates[0].State->Position.X = 1.0F;
    updates[1].Body = second;
    updates[1].State = *beforeSecond;
    updates[1].State->Position.X = -1.0e8F;
    constexpr float duration = 1.0e-32F;
    // The old live-to-target delta is zero. Only validation against the staged pose finds this overflow.
    CHECK_NOTHROW(PhysicsFrameAccess::ValidateKinematicMove(*world, second, beforeSecond->Position,
                                                            beforeSecond->Rotation, duration));
    updates[1].KinematicTarget = PhysicsKinematicTarget{beforeSecond->Position, beforeSecond->Rotation, duration};
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates), std::out_of_range);
    const auto afterFirst = world->TryGetBody(first);
    const auto afterSecond = world->TryGetBody(second);
    REQUIRE(afterFirst);
    REQUIRE(afterSecond);
    CHECK(afterFirst->Position == beforeFirst->Position);
    CHECK(afterFirst->LinearVelocity == beforeFirst->LinearVelocity);
    CHECK(afterFirst->PendingForce == beforeFirst->PendingForce);
    CHECK(afterFirst->PendingTorque == beforeFirst->PendingTorque);
    CHECK(afterSecond->Position == beforeSecond->Position);
    CHECK(afterSecond->LinearVelocity == beforeSecond->LinearVelocity);
    CHECK(afterSecond->AngularVelocity == beforeSecond->AngularVelocity);
    world->Close();
    physics->Close();
}

TEST_CASE("Staged frame target derives native velocity from the accepted authored start pose")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Kinematic;
    definition.UseGravity = false;
    const auto body = world->CreateBody(definition);
    AuthoredPhysicsBodyUpdate update;
    update.Body = body;
    update.State = world->TryGetBody(body);
    REQUIRE(update.State);
    update.State->Position = {1.0F, 0.0F, 0.0F};
    update.KinematicTarget = PhysicsKinematicTarget{{3.0F, 0.0F, 0.0F}, {}, 0.5F};
    const std::array updates{update};
    PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates);
    const auto started = world->TryGetBody(body);
    REQUIRE(started);
    CHECK(started->Position == update.State->Position);
    CHECK(started->LinearVelocity.X == doctest::Approx(4.0F));
    world->Step(0.5F);
    const auto arrived = world->TryGetBody(body);
    REQUIRE(arrived);
    CHECK(arrived->Position.X == doctest::Approx(3.0F));
    world->Close();
    physics->Close();
}

TEST_CASE("Staged frame targets accept converted statics and reject ordinary statics")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Static;
    const auto body = PhysicsFrameAccess::CreateFrameBody(*world, definition);
    const std::array bodies{body};
    AuthoredPhysicsBodyUpdate update;
    update.Body = body;
    update.KinematicTarget = PhysicsKinematicTarget{{1.0F, 0.0F, 0.0F}, {}, 0.5F};
    const std::array updates{update};
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates), std::invalid_argument);
    PhysicsFrameAccess::SetFrameKinematics(*world, bodies, true);
    CHECK_NOTHROW(PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*world, updates));
    const auto started = world->TryGetBody(body);
    REQUIRE(started);
    CHECK(started->Position.X == doctest::Approx(0.0F));
    CHECK(started->LinearVelocity.X == doctest::Approx(2.0F));
    world->Step(0.5F);
    const auto arrived = world->TryGetBody(body);
    REQUIRE(arrived);
    CHECK(arrived->Position.X == doctest::Approx(1.0F));
    world->Close();
    physics->Close();
}

TEST_CASE("physics clock preflight rejects exhausted ticks and mismatched loads without consuming state")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    definition.UseGravity = false;
    const auto body = world->CreateBody(definition);
    world->AddForce(body, {2.0F, 3.0F, 4.0F});
    const auto before = world->TryGetBody(body);
    REQUIRE(before);
    const auto original = PhysicsFrameAccess::CaptureLoadCheckpoint(*world);
    auto accepted = original;
    accepted.AcceptedWaterTick = accepted.Tick;
    accepted.AcceptedWaterDeltaSeconds = 0.1F;
    PhysicsFrameAccess::ValidateLoadCheckpoint(*world, accepted);
    PhysicsFrameAccess::RestoreLoadCheckpoint(*world, accepted);
    CHECK_THROWS_AS(PhysicsFrameAccess::ValidateStepClock(*world, 0.2F), std::invalid_argument);
    CHECK_NOTHROW(PhysicsFrameAccess::ValidateStepClock(*world, 0.1F));
    CHECK(PhysicsFrameAccess::CaptureLoadCheckpoint(*world).Tick == original.Tick);
    CHECK(world->TryGetBody(body)->PendingForce == before->PendingForce);
    auto exhausted = original;
    exhausted.Tick = std::numeric_limits<std::uint64_t>::max();
    PhysicsFrameAccess::ValidateLoadCheckpoint(*world, exhausted);
    PhysicsFrameAccess::RestoreLoadCheckpoint(*world, exhausted);
    CHECK_THROWS_AS(PhysicsFrameAccess::ValidateStepClock(*world, 0.1F), std::overflow_error);
    CHECK(PhysicsFrameAccess::CaptureLoadCheckpoint(*world).Tick == exhausted.Tick);
    CHECK(world->TryGetBody(body)->Position == before->Position);
    CHECK(world->TryGetBody(body)->PendingForce == before->PendingForce);
    PhysicsFrameAccess::RestoreLoadCheckpoint(*world, original);
    CHECK_THROWS_AS(PhysicsFrameAccess::ValidateStepClock(*world, 0.0F), std::invalid_argument);
    CHECK_THROWS_AS(PhysicsFrameAccess::ValidateStepClock(*world, std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                PhysicsFrameAccess::ValidateStepClock(*world, 0.1F);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    CHECK(PhysicsFrameAccess::CaptureLoadCheckpoint(*world).Tick == original.Tick);
    CHECK(world->TryGetBody(body)->PendingForce == before->PendingForce);
    CHECK_NOTHROW(world->Step(0.1F));
    CHECK(PhysicsFrameAccess::CaptureLoadCheckpoint(*world).Tick == original.Tick + 1);
    world->Close();
    physics->Close();
}
