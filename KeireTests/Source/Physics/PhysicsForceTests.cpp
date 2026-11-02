#include "Keire/Physics/PhysicsSystem.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
    Keire::Ref<Keire::PhysicsSystem> CreateForcePhysics()
    {
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        return Keire::CreateRef<Keire::PhysicsSystem>(specification);
    }

    Keire::PhysicsBodyDefinition ForceBody()
    {
        Keire::PhysicsBodyDefinition definition;
        definition.Motion = Keire::PhysicsMotionType::Dynamic;
        definition.UseGravity = false;
        definition.Mass = 2.0F;
        definition.MotionSettings.LinearDamping = 0.0F;
        definition.MotionSettings.AngularDamping = 0.0F;
        return definition;
    }
} // namespace

TEST_CASE("physics forces accumulate for one step and preserve mass response at far origins")
{
    const auto system = CreateForcePhysics();
    for (const double origin : {0.0, 1.0e12})
    {
        for (const float mass : {1.0F, 2.0F, 10.0F})
        {
            CAPTURE(origin);
            CAPTURE(mass);
            const auto world = system->CreateWorld({origin, -origin, origin});
            auto definition = ForceBody();
            definition.Mass = mass;
            const auto body = world->CreateBody(definition);
            world->AddForce(body, {2.0F, 0.0F, 0.0F});
            world->AddForce(body, {3.0F, 0.0F, 0.0F});
            world->Step(0.01F);
            const auto first = world->TryGetBody(body);
            REQUIRE(first);
            CHECK(first->LinearVelocity.X == doctest::Approx(0.05F / mass));
            CHECK(first->AngularVelocity == Keire::Vector3{});
            world->Step(0.01F);
            CHECK(world->TryGetBody(body)->LinearVelocity.X == doctest::Approx(first->LinearVelocity.X));
        }
    }
}

TEST_CASE("physics off-center forces and torque survive translation rebasing")
{
    const auto system = CreateForcePhysics();
    for (const bool rebase : {false, true})
    {
        const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
        const auto body = world->CreateBody(ForceBody());
        world->AddForceAtPosition(body, {0.0F, 10.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
        world->AddTorque(body, {0.0F, 0.0F, 2.0F});
        if (rebase)
        {
            world->RebaseOrigin({1.0e12 + 32.0, 0.0, 0.0});
        }
        world->Step(0.01F);
        const auto state = world->TryGetBody(body);
        REQUIRE(state);
        CHECK(state->LinearVelocity.Y == doctest::Approx(0.05F));
        // Unit cube inertia is mass / 6; moment is 10 + 2 Nm.
        CHECK(state->AngularVelocity.Z == doctest::Approx(0.36F));
        CHECK(state->LinearVelocity.X == 0.0F);
        CHECK(state->AngularVelocity.X == 0.0F);
    }
}

TEST_CASE("physics force rejection is atomic and zero submissions preserve sleep")
{
    const auto system = CreateForcePhysics();
    const auto world = system->CreateWorld();
    const auto body = world->CreateBody(ForceBody());
    auto sleeping = *world->TryGetBody(body);
    sleeping.Sleeping = true;
    world->SetBodyState(body, sleeping);
    world->AddForce(body, {});
    world->AddTorque(body, {});
    world->AddForceAtPosition(body, {}, {100.0F, 0.0F, 0.0F});
    CHECK(world->TryGetBody(body)->Sleeping);
    const float huge = std::numeric_limits<float>::max();
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(world->AddForce(body, {invalid, 0.0F, 0.0F}), std::invalid_argument);
    CHECK_THROWS_AS(world->AddTorque(body, {0.0F, invalid, 0.0F}), std::invalid_argument);
    CHECK_THROWS_AS(world->AddForceAtPosition(body, {1.0F, 0.0F, 0.0F}, {invalid, 0.0F, 0.0F}), std::invalid_argument);
    CHECK_THROWS_AS(world->AddForceAtPosition(body, {0.0F, huge, 0.0F}, {huge, 0.0F, 0.0F}), std::out_of_range);
    CHECK(world->TryGetBody(body)->Sleeping);
    world->AddForce(body, {4.0F, 0.0F, 0.0F});
    CHECK_FALSE(world->TryGetBody(body)->Sleeping);
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity.X == doctest::Approx(0.02F));
    CHECK(world->TryGetBody(body)->LinearVelocity.Y == 0.0F);
    CHECK(world->TryGetBody(body)->AngularVelocity == Keire::Vector3{});
}

TEST_CASE("physics force accumulator overflow rejects both force and torque without partial mutation")
{
    const auto system = CreateForcePhysics();
    const auto world = system->CreateWorld();
    const auto body = world->CreateBody(ForceBody());
    const float huge = std::numeric_limits<float>::max();
    world->AddForce(body, {huge, 0.0F, 0.0F});
    CHECK_THROWS_AS(world->AddForce(body, {huge, 0.0F, 0.0F}), std::out_of_range);
    world->AddForce(body, {-huge, 0.0F, 0.0F});
    world->AddTorque(body, {0.0F, 0.0F, huge});
    CHECK_THROWS_AS(world->AddForceAtPosition(body, {0.0F, huge, 0.0F}, {1.0F, 0.0F, 0.0F}), std::out_of_range);
    world->AddTorque(body, {0.0F, 0.0F, -huge});
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity == Keire::Vector3{});
    CHECK(world->TryGetBody(body)->AngularVelocity == Keire::Vector3{});
}

TEST_CASE("physics forces reject invalid bodies threads and closed worlds")
{
    const auto system = CreateForcePhysics();
    const auto world = system->CreateWorld();
    for (const auto motion : {Keire::PhysicsMotionType::Static, Keire::PhysicsMotionType::Kinematic})
    {
        auto definition = ForceBody();
        definition.Motion = motion;
        const auto body = world->CreateBody(definition);
        CHECK_THROWS_AS(world->AddForce(body, {1.0F, 0.0F, 0.0F}), std::invalid_argument);
        CHECK_THROWS_AS(world->AddTorque(body, {1.0F, 0.0F, 0.0F}), std::invalid_argument);
        CHECK_THROWS_AS(world->AddForceAtPosition(body, {1.0F, 0.0F, 0.0F}, {}), std::invalid_argument);
        world->DestroyBody(body);
    }
    CHECK_THROWS_AS(world->AddForce({}, {}), std::invalid_argument);
    const auto body = world->CreateBody(ForceBody());
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                world->AddForce(body, {1.0F, 0.0F, 0.0F});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity == Keire::Vector3{});
    world->DestroyBody(body);
    CHECK_THROWS_AS(world->AddForce(body, {}), std::invalid_argument);
    world->Close();
    CHECK_THROWS_AS(world->AddForce(body, {}), std::logic_error);
    CHECK_THROWS_AS(world->AddTorque(body, {}), std::logic_error);
    CHECK_THROWS_AS(world->AddForceAtPosition(body, {}, {}), std::logic_error);
}

TEST_CASE("physics source gravity and external forces reject combined overflow before submission")
{
    const auto system = CreateForcePhysics();
    const auto world = system->CreateWorld();
    world->SetGravity({});
    world->SetGravitySources({{{10.0, 0.0, 0.0}, 3.0e38, 1.0}});
    auto definition = ForceBody();
    definition.Mass = 1.0F;
    definition.UseGravity = true;
    const auto body = world->CreateBody(definition);
    const float huge = std::numeric_limits<float>::max();
    world->AddForce(body, {huge, 0.0F, 0.0F});
    CHECK_THROWS_AS(world->Step(0.01F), std::out_of_range);
    const auto rejected = world->TryGetBody(body);
    REQUIRE(rejected);
    CHECK(rejected->Position == Keire::Vector3{});
    CHECK(rejected->LinearVelocity == Keire::Vector3{});
    world->AddForce(body, {-huge, 0.0F, 0.0F});
    world->SetGravitySources({});
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity == Keire::Vector3{});
}

TEST_CASE("physics body state restores pending loads and rejects invalid loads atomically")
{
    const auto system = CreateForcePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    const auto body = world->CreateBody(ForceBody());
    const auto unloaded = *world->TryGetBody(body);
    world->AddForce(body, {4.0F, 0.0F, 0.0F});
    world->AddTorque(body, {0.0F, 0.0F, 2.0F});
    const auto loaded = *world->TryGetBody(body);
    CHECK(loaded.PendingForce == Keire::Vector3{4.0F, 0.0F, 0.0F});
    CHECK(loaded.PendingTorque == Keire::Vector3{0.0F, 0.0F, 2.0F});
    auto invalid = loaded;
    invalid.Position.X = 10.0F;
    invalid.PendingTorque.Z = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(world->SetBodyState(body, invalid), std::invalid_argument);
    CHECK(world->TryGetBody(body)->Position == loaded.Position);
    CHECK(world->TryGetBody(body)->PendingForce == loaded.PendingForce);
    world->SetBodyState(body, unloaded);
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity == Keire::Vector3{});
    CHECK(world->TryGetBody(body)->AngularVelocity == Keire::Vector3{});
    world->SetBodyState(body, loaded);
    world->Step(0.01F);
    CHECK(world->TryGetBody(body)->LinearVelocity.X == doctest::Approx(0.02F));
    CHECK(world->TryGetBody(body)->AngularVelocity.Z == doctest::Approx(0.06F));
    CHECK(world->TryGetBody(body)->PendingForce == Keire::Vector3{});
    CHECK(world->TryGetBody(body)->PendingTorque == Keire::Vector3{});
}
