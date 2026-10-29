#include "Keire/Core.h"
#include "KeireInternal/Math/RelativePositionTranslation.h"
#include "KeireInternal/Physics/GravityField.h"
#include "KeireInternal/Spatial/SimulationOriginPolicy.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] Keire::Ref<Keire::PhysicsSystem> CreatePhysics()
    {
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        return Keire::CreateRef<Keire::PhysicsSystem>(specification);
    }
} // namespace

TEST_CASE("physics project gravity defaults affect new worlds without replacing world overrides")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    specification.DefaultGravity = {6.0F, 0.0F, 0.0F};
    const auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    CHECK(world->Gravity() == specification.DefaultGravity);
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.MotionSettings.LinearDamping = 0.0F;
    const auto falling = world->CreateBody(definition);
    definition.Position = {0.0F, 10.0F, 0.0F};
    definition.UseGravity = false;
    const auto unaffected = world->CreateBody(definition);
    world->Step(1.0F / 60.0F);
    REQUIRE(world->TryGetBody(falling));
    REQUIRE(world->TryGetBody(unaffected));
    CHECK(world->TryGetBody(falling)->LinearVelocity.X == doctest::Approx(0.1F));
    CHECK(world->TryGetBody(unaffected)->LinearVelocity == Keire::Vector3{});

    world->SetGravity({0.0F, -2.0F, 0.0F});
    system->ConfigureDefaultGravity({});
    CHECK(world->Gravity() == Keire::Vector3{0.0F, -2.0F, 0.0F});
    CHECK(system->CreateWorld()->Gravity() == Keire::Vector3{});
    CHECK_THROWS_AS(system->ConfigureDefaultGravity({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}),
                    std::invalid_argument);
    CHECK(system->DefaultGravity() == Keire::Vector3{});
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                system->ConfigureDefaultGravity({1.0F, 0.0F, 0.0F});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    CHECK(system->DefaultGravity() == Keire::Vector3{});
    system->Close();
    CHECK_THROWS_AS(system->ConfigureDefaultGravity({}), std::logic_error);
    CHECK_THROWS_AS(system->DefaultGravity(), std::logic_error);
}

TEST_CASE("physics motion settings allow drag free travel and live speed limits")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    world->SetGravity({});
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.Shape = Keire::ColliderShape::Sphere;
    definition.LinearVelocity = {1000.0F, 0.0F, 0.0F};
    definition.MotionSettings = {0.0F, 0.0F, 2000.0F, 100.0F};
    const auto body = world->CreateBody(definition);
    auto initial = world->TryGetBody(body);
    REQUIRE(initial);
    initial->AngularVelocity = {0.0F, 60.0F, 0.0F};
    world->SetBodyState(body, *initial);
    for (int tick = 0; tick < 120; ++tick)
    {
        world->Step(1.0F / 60.0F);
    }
    const auto coast = world->TryGetBody(body);
    REQUIRE(coast);
    CHECK(coast->LinearVelocity.X == doctest::Approx(1000.0F));
    CHECK(coast->AngularVelocity.Y == doctest::Approx(60.0F));
    CHECK(coast->Position.X == doctest::Approx(2000.0F).epsilon(0.0001));
    world->SetMotionSettings(body, {0.0F, 0.0F, 100.0F, 5.0F});
    const auto limited = world->TryGetBody(body);
    REQUIRE(limited);
    CHECK(limited->Body == body);
    CHECK(limited->Position == coast->Position);
    CHECK(limited->LinearVelocity.X == doctest::Approx(100.0F));
    CHECK(limited->AngularVelocity.Y == doctest::Approx(5.0F));
    world->SetMotionSettings(body, {1.0F, 1.0F, 100.0F, 5.0F});
    world->Step(0.01F);
    REQUIRE(world->TryGetBody(body));
    CHECK(world->TryGetBody(body)->LinearVelocity.X < 100.0F);
    CHECK(world->TryGetBody(body)->AngularVelocity.Y < 5.0F);
}

TEST_CASE("physics motion settings reject invalid values before changing bodies")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld();
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.LinearVelocity = {10.0F, 0.0F, 0.0F};
    const auto body = world->CreateBody(definition);
    for (const auto settings :
         {Keire::PhysicsMotionSettings{-1.0F, 0.0F, 10.0F, 10.0F},
          Keire::PhysicsMotionSettings{0.0F, -1.0F, 10.0F, 10.0F},
          Keire::PhysicsMotionSettings{0.0F, 0.0F, 0.0F, 10.0F}, Keire::PhysicsMotionSettings{0.0F, 0.0F, 10.0F, 0.0F},
          Keire::PhysicsMotionSettings{std::numeric_limits<float>::quiet_NaN()},
          Keire::PhysicsMotionSettings{0.0F, 0.0F, std::numeric_limits<float>::infinity()}})
    {
        CHECK_THROWS_AS(world->SetMotionSettings(body, settings), std::invalid_argument);
        definition.MotionSettings = settings;
        CHECK_THROWS_AS((void)world->CreateBody(definition), std::invalid_argument);
        REQUIRE(world->TryGetBody(body));
        CHECK(world->TryGetBody(body)->LinearVelocity.X == 10.0F);
    }
    CHECK_THROWS_AS(world->SetMotionSettings({}, {}), std::invalid_argument);
    world->Close();
    CHECK_THROWS_AS(world->SetMotionSettings(body, {}), std::logic_error);
}

TEST_CASE("radial gravity follows inverse square falloff and finite sphere interiors")
{
    const Keire::PhysicsGravitySource source{{1.0e12, 0.0, 0.0}, 100.0, 2.0};
    const Keire::Vector3d origin{1.0e12, 0.0, 0.0};
    CHECK(Keire::Detail::EvaluateGravitySource(source, origin, {}).X == 0.0);
    CHECK(Keire::Detail::EvaluateGravitySource(source, origin, {1.0F, 0.0F, 0.0F}).X == doctest::Approx(-12.5));
    CHECK(Keire::Detail::EvaluateGravitySource(source, origin, {2.0F, 0.0F, 0.0F}).X == doctest::Approx(-25.0));
    CHECK(Keire::Detail::EvaluateGravitySource(source, origin, {4.0F, 0.0F, 0.0F}).X == doctest::Approx(-6.25));
    CHECK(Keire::Detail::EvaluateGravitySource(source, origin, {-4.0F, 0.0F, 0.0F}).X == doctest::Approx(6.25));
}

TEST_CASE("radial gravity drives solver bodies independently of mass and origin shifts")
{
    const auto system = CreatePhysics();
    for (const float mass : {1.0F, 3.0F})
    {
        for (const bool rebase : {false, true})
        {
            CAPTURE(mass);
            CAPTURE(rebase);
            const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
            world->SetGravity({});
            const std::vector<Keire::PhysicsGravitySource> sources{{{1.0e12, 0.0, 0.0}, 100.0, 2.0}};
            world->SetGravitySources(sources);
            Keire::PhysicsBodyDefinition definition;
            definition.Motion = Keire::PhysicsMotionType::Dynamic;
            definition.Position = {4.0F, 0.0F, 0.0F};
            definition.Mass = mass;
            const auto body = world->CreateBody(definition);
            definition.Position.X = -4.0F;
            definition.UseGravity = false;
            const auto disabled = world->CreateBody(definition);
            if (rebase)
            {
                world->RebaseOrigin({1.0e12 + 128.0, 0.0, 0.0});
            }
            world->Step(0.01F);
            REQUIRE(world->TryGetBody(body));
            CHECK(world->TryGetBody(body)->LinearVelocity.X == doctest::Approx(-0.0625F).epsilon(0.002));
            CHECK(world->TryGetBody(disabled)->LinearVelocity == Keire::Vector3{});
            CHECK(world->GravitySources() == sources);
            auto invalid = sources;
            invalid.front().RadiusMetres = 0.0;
            CHECK_THROWS_AS(world->SetGravitySources(invalid), std::invalid_argument);
            CHECK(world->GravitySources() == sources);
            CHECK_THROWS_AS(world->SetGravitySources(std::vector<Keire::PhysicsGravitySource>(65)),
                            std::invalid_argument);
            CHECK(world->GravitySources() == sources);
        }
    }
}

TEST_CASE("radial gravity combines distant sources with uniform gravity")
{
    const auto system = CreatePhysics();
    for (const double offset : {0.0, 1.0e12})
    {
        CAPTURE(offset);
        const auto world = system->CreateWorld({offset, 0.0, 0.0});
        world->SetGravity({0.0F, -2.0F, 0.0F});
        world->SetGravitySources({{{offset - 10.0, 0.0, 0.0}, 100.0, 1.0},
                                  {{offset + 10.0, 0.0, 0.0}, 100.0, 1.0},
                                  {{offset, 10.0, 0.0}, 500.0, 1.0}});
        Keire::PhysicsBodyDefinition definition;
        definition.Motion = Keire::PhysicsMotionType::Dynamic;
        const auto body = world->CreateBody(definition);
        world->Step(0.01F);
        const auto state = world->TryGetBody(body);
        REQUIRE(state);
        CHECK(state->LinearVelocity.X == doctest::Approx(0.0F).epsilon(0.00001));
        CHECK(state->LinearVelocity.Y == doctest::Approx(0.03F).epsilon(0.002));
        CHECK(state->LinearVelocity.Z == 0.0F);
    }
}

TEST_CASE("radial gravity preserves circular orbits across distant origins and rebases")
{
    constexpr double radius = 100.0;
    constexpr double gravitationalParameter = 10000.0;
    constexpr double speed = 10.0;
    constexpr double angularSpeed = speed / radius;
    constexpr double duration = 3.0 * 2.0 * std::numbers::pi / angularSpeed;
    constexpr double initialEnergy = -gravitationalParameter / (2.0 * radius);
    const auto system = CreatePhysics();

    for (const double offset : {0.0, 1.0e12})
    {
        for (const bool rebase : {false, true})
        {
            double previousRadiusError = 1.0;
            for (const int frequency : {60, 120})
            {
                CAPTURE(offset);
                CAPTURE(rebase);
                CAPTURE(frequency);
                const Keire::Vector3d center{offset, 0.0, 0.0};
                const auto world = system->CreateWorld(center);
                world->SetGravity({});
                world->SetGravitySources({{center, gravitationalParameter, 1.0}});
                Keire::PhysicsBodyDefinition definition;
                definition.Motion = Keire::PhysicsMotionType::Dynamic;
                definition.Shape = Keire::ColliderShape::Sphere;
                definition.Position = {static_cast<float>(radius), 0.0F, 0.0F};
                definition.LinearVelocity = {0.0F, static_cast<float>(speed), 0.0F};
                definition.MotionSettings = {0.0F, 0.0F, 100.0F, 100.0F};
                const auto body = world->CreateBody(definition);
                const float step = 1.0F / static_cast<float>(frequency);
                const int ticks = static_cast<int>(duration / step);
                double maximumRadiusError = 0.0;
                double maximumEnergyError = 0.0;

                for (int tick = 0; tick < ticks; ++tick)
                {
                    if (rebase && tick % frequency == 0)
                    {
                        const double shift = (tick / frequency) % 2 == 0 ? 128.0 : -128.0;
                        world->RebaseOrigin({offset + shift, 64.0, 0.0});
                    }
                    world->Step(step);
                    const auto state = world->TryGetBody(body);
                    REQUIRE(state);
                    const auto position = world->ToWorldPosition(state->Position);
                    const double x = position.X - center.X;
                    const double y = position.Y - center.Y;
                    const double distance = std::hypot(x, y);
                    const double velocitySquared =
                        static_cast<double>(state->LinearVelocity.X) * state->LinearVelocity.X +
                        static_cast<double>(state->LinearVelocity.Y) * state->LinearVelocity.Y;
                    const double energy = velocitySquared * 0.5 - gravitationalParameter / distance;
                    maximumRadiusError = std::max(maximumRadiusError, std::abs(distance / radius - 1.0));
                    maximumEnergyError =
                        std::max(maximumEnergyError, std::abs((energy - initialEnergy) / initialEnergy));
                    CHECK(state->Position.Z == 0.0F);
                }

                const auto finalState = world->TryGetBody(body);
                REQUIRE(finalState);
                const auto finalPosition = world->ToWorldPosition(finalState->Position);
                const double phase = std::atan2(finalPosition.Y - center.Y, finalPosition.X - center.X);
                const double expectedPhase = angularSpeed * static_cast<double>(step) * ticks;
                const double phaseError = std::abs(std::remainder(phase - expectedPhase, 2.0 * std::numbers::pi));
                CAPTURE(maximumRadiusError);
                CAPTURE(maximumEnergyError);
                CAPTURE(phaseError);
                CHECK(maximumRadiusError < 0.002);
                CHECK(maximumEnergyError < 0.0001);
                CHECK(phaseError < 0.01);
                if (frequency == 120)
                {
                    CHECK(maximumRadiusError < previousRadiusError * 0.75);
                }
                previousRadiusError = maximumRadiusError;
            }
        }
    }
}

TEST_CASE("radial gravity keeps unsupported bodies accelerating in weak fields")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    world->SetGravity({});
    world->SetGravitySources({{{1.0e12, 0.0, 0.0}, 0.001, 1.0}});
    SUBCASE("radial field") {}
    SUBCASE("uniform field")
    {
        world->SetGravitySources({});
        world->SetGravity({-0.00001F, 0.0F, 0.0F});
    }
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.Position = {10.0F, 0.0F, 0.0F};
    definition.MotionSettings.LinearDamping = 0.0F;
    const auto body = world->CreateBody(definition);
    for (int tick = 0; tick < 600; ++tick)
    {
        world->Step(1.0F / 60.0F);
    }
    const auto state = world->TryGetBody(body);
    REQUIRE(state);
    CAPTURE(state->LinearVelocity.X);
    CAPTURE(state->Position.X);
    CHECK_FALSE(state->Sleeping);
    CHECK(state->LinearVelocity.X < -0.00009F);
    CHECK(state->Position.X < 9.9998F);
}

TEST_CASE("radial gravity permits supported sleep but ignores trigger support")
{
    const auto system = CreatePhysics();
    for (const bool trigger : {false, true})
    {
        CAPTURE(trigger);
        const auto world = system->CreateWorld();
        world->SetGravity({});
        world->SetGravitySources({{{0.0, -100.0, 0.0}, 0.1, 1.0}});
        Keire::PhysicsBodyDefinition floor;
        floor.Position = {0.0F, -0.5F, 0.0F};
        floor.HalfExtent = {10.0F, 0.5F, 10.0F};
        floor.Trigger = trigger;
        const auto floorBody = world->CreateBody(floor);
        REQUIRE(floorBody);
        Keire::PhysicsBodyDefinition definition;
        definition.Motion = Keire::PhysicsMotionType::Dynamic;
        definition.Position = {0.0F, 0.499F, 0.0F};
        definition.MotionSettings.LinearDamping = 0.0F;
        const auto body = world->CreateBody(definition);
        for (int tick = 0; tick < 600; ++tick)
        {
            world->Step(1.0F / 60.0F);
        }
        const auto state = world->TryGetBody(body);
        REQUIRE(state);
        CHECK(state->Sleeping == !trigger);
        if (trigger)
        {
            CHECK(state->LinearVelocity.Y < -0.00009F);
        }
        else
        {
            CHECK(state->Position.Y == doctest::Approx(0.5F).epsilon(0.01));
            (void)world->DrainContactEvents();
            world->Step(1.0F / 60.0F);
            const auto contacts = world->DrainContactEvents();
            REQUIRE(contacts.size() == 1);
            CHECK(contacts.front().Normal.Y > 0.99F);
            CHECK(world->TryGetBody(body)->Sleeping);
        }
    }
}

TEST_CASE("radial gravity force overflow rejects the step without leaving queued forces")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    world->SetGravity({});
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.Position = {2.0F, 0.0F, 0.0F};
    const auto first = world->CreateBody(definition);
    definition.Position = {-2.0F, 0.0F, 0.0F};
    const auto second = world->CreateBody(definition);
    world->SetGravitySources({{{1.0e12, 0.0, 0.0}, 1.0e100, 1.0}});
    CHECK_THROWS_AS(world->Step(0.01F), std::out_of_range);
    for (const auto body : {first, second})
    {
        const auto state = world->TryGetBody(body);
        REQUIRE(state);
        CHECK(state->LinearVelocity == Keire::Vector3{});
        CHECK(std::abs(state->Position.X) == 2.0F);
    }
    world->SetGravitySources({});
    world->Step(0.01F);
    for (const auto body : {first, second})
    {
        const auto state = world->TryGetBody(body);
        REQUIRE(state);
        CHECK(state->LinearVelocity == Keire::Vector3{});
        CHECK(std::abs(state->Position.X) == 2.0F);
    }
}

TEST_CASE("physics region gravity changes wake affected bodies and survive origin translations")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    world->SetGravity({});
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    const auto affected = world->CreateBody(definition);
    definition.Position = {10.0F, 0.0F, 0.0F};
    definition.UseGravity = false;
    const auto unaffected = world->CreateBody(definition);
    for (int tick = 0; tick < 240; ++tick)
    {
        world->Step(1.0F / 60.0F);
    }
    REQUIRE(world->TryGetBody(affected));
    REQUIRE(world->TryGetBody(unaffected));
    REQUIRE(world->TryGetBody(affected)->Sleeping);
    REQUIRE(world->TryGetBody(unaffected)->Sleeping);
    world->SetGravity({});
    CHECK(world->TryGetBody(affected)->Sleeping);
    const Keire::Vector3 sideways{9.81F, 0.0F, 0.0F};
    world->SetGravity(sideways);
    CHECK(world->Gravity() == sideways);
    CHECK_FALSE(world->TryGetBody(affected)->Sleeping);
    CHECK(world->TryGetBody(unaffected)->Sleeping);
    world->RebaseOrigin({1.0e12 + 128.0, 64.0, -128.0});
    CHECK(world->Gravity() == sideways);
    world->Step(0.1F);
    CHECK(world->TryGetBody(affected)->LinearVelocity.X > 0.9F);
    CHECK(world->TryGetBody(affected)->LinearVelocity.Y == 0.0F);
    CHECK(world->TryGetBody(unaffected)->LinearVelocity == Keire::Vector3{});
    CHECK_THROWS_AS(world->SetGravity({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}), std::invalid_argument);
    CHECK(world->Gravity() == sideways);
    bool rejected = false;
    std::thread foreign(
        [&]
        {
            try
            {
                world->SetGravity({});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    foreign.join();
    CHECK(rejected);
    CHECK(world->Gravity() == sideways);
    world->Close();
    CHECK_THROWS_AS(world->SetGravity({}), std::logic_error);
    CHECK_THROWS_AS((void)world->Gravity(), std::logic_error);
}

TEST_CASE("simulation origin policy keeps a stable grid around a precise travel focus")
{
    using Keire::Detail::SelectSimulationOrigin;
    CHECK_FALSE(SelectSimulationOrigin({}, {8192.0, 0.0, 0.0}));
    const auto moved = SelectSimulationOrigin({}, {8192.125, 0.0, 0.0});
    REQUIRE(moved);
    CHECK(*moved == Keire::Vector3d{8192.0, 0.0, 0.0});
    CHECK_FALSE(SelectSimulationOrigin(*moved, {8191.875, 0.0, 0.0}));
    CHECK_FALSE(SelectSimulationOrigin(*moved, {8192.125, 0.0, 0.0}));
    CHECK(SelectSimulationOrigin({}, {-8192.125, 0.0, 0.0}) == std::optional<Keire::Vector3d>{{-8192.0, 0.0, 0.0}});
    const auto distant = SelectSimulationOrigin({}, {1.0e15 + 0.125, 0.0, -1.0e15});
    REQUIRE(distant);
    CHECK_FALSE(SelectSimulationOrigin(*distant, {1.0e15 + 0.125, 0.0, -1.0e15}));
    CHECK_THROWS_AS(SelectSimulationOrigin({}, {}, {.RebaseDistanceMetres = 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(SelectSimulationOrigin({}, {}, {.GridSpacingMetres = 16384.0}), std::invalid_argument);
    CHECK_THROWS_AS(SelectSimulationOrigin({}, {std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                    std::invalid_argument);
}

TEST_CASE("relative origin translation rejects precision lost before float conversion")
{
    const double enormous = std::ldexp(1.0, 60);
    CHECK_THROWS_AS(Keire::Detail::TranslateRelativePosition({0.015625F, 0.0F, 0.0F}, {enormous, 0.0, 0.0}, 0.001),
                    std::out_of_range);
    CHECK_THROWS_AS(
        Keire::Detail::TranslateRelativePosition({static_cast<float>(enormous), 0.0F, 0.0F}, {0.5, 0.0, 0.0}, 0.001),
        std::out_of_range);
    CHECK(Keire::Detail::TranslateRelativePosition({0.015625F, 0.0F, 0.0F}, {128.0, 0.0, 0.0}, 0.0) ==
          Keire::Vector3{128.015625F, 0.0F, 0.0F});
}

TEST_CASE("physics region origins preserve local collisions and queries at planetary distances")
{
    const auto system = CreatePhysics();
    for (const double distance : {0.0, 1000.0, -1000.0, 100000.0, 10000000.0, 1.0e12, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const Keire::Vector3d origin{distance, distance, -distance};
        const auto world = system->CreateWorld(origin);
        CHECK(world->WorldOrigin() == origin);
        const Keire::Vector3d authoredPosition{distance + 0.25, distance + 4.0, -distance + 0.125};
        const auto localPosition = world->ToLocalPosition(authoredPosition, 0.0);
        CHECK(localPosition == Keire::Vector3{0.25F, 4.0F, 0.125F});
        CHECK(world->ToWorldPosition(localPosition) == authoredPosition);

        Keire::PhysicsBodyDefinition floor;
        floor.Position = {0.0F, -0.5F, 0.0F};
        floor.HalfExtent = {10.0F, 0.5F, 10.0F};
        const auto floorBody = world->CreateBody(floor);
        Keire::PhysicsBodyDefinition falling;
        falling.Motion = Keire::PhysicsMotionType::Dynamic;
        falling.Position = localPosition;
        const auto fallingBody = world->CreateBody(falling);
        for (int step = 0; step < 240; ++step)
        {
            world->Step(1.0F / 60.0F);
        }
        const auto settled = world->TryGetBody(fallingBody);
        REQUIRE(settled);
        CHECK(settled->Position.Y == doctest::Approx(0.5F).epsilon(0.03));
        CHECK(std::abs(settled->LinearVelocity.Y) < 0.05F);
        const auto hits =
            world->RayCast({.Origin = {3.0F, 2.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 4.0F});
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().Body == floorBody);
        CHECK(hits.front().Distance == doctest::Approx(2.0F));
        world->ConfigureDebugCapture({.Enabled = true});
        const auto debug = world->CaptureDebugSnapshot();
        REQUIRE(debug);
        CHECK(debug->WorldOrigin == origin);
    }
}

TEST_CASE("physics origin translation preserves sub-double local detail and rejects partial mutation")
{
    const auto system = CreatePhysics();
    const Keire::Vector3d origin{1.0e15, -1.0e15, 0.0};
    const auto world = system->CreateWorld(origin);
    Keire::PhysicsBodyDefinition anchor;
    const auto anchorId = world->CreateBody(anchor);
    Keire::PhysicsBodyDefinition moving;
    moving.Motion = Keire::PhysicsMotionType::Dynamic;
    moving.UseGravity = false;
    moving.Position = {0.015625F, 2.0F, 0.03125F};
    moving.LinearVelocity = {1.0F, 2.0F, 3.0F};
    const auto movingId = world->CreateBody(moving);
    const auto before = world->TryGetBody(movingId);
    REQUIRE(before);
    world->RebaseOrigin({origin.X + 128.0, origin.Y - 64.0, 256.0});
    const auto shifted = world->TryGetBody(movingId);
    REQUIRE(shifted);
    CHECK(shifted->Position == Keire::Vector3{-127.984375F, 66.0F, -255.96875F});
    CHECK(shifted->LinearVelocity == before->LinearVelocity);
    CHECK(shifted->AngularVelocity == before->AngularVelocity);
    CHECK(shifted->Rotation == before->Rotation);
    CHECK(shifted->Sleeping == before->Sleeping);
    const auto shiftedOrigin = world->WorldOrigin();
    const auto shiftedAnchor = world->TryGetBody(anchorId);
    REQUIRE(shiftedAnchor);
    CHECK_THROWS_AS(world->RebaseOrigin({origin.X + 16777216.0, origin.Y, 0.0}), std::out_of_range);
    CHECK(world->WorldOrigin() == shiftedOrigin);
    CHECK(world->TryGetBody(anchorId)->Position == shiftedAnchor->Position);
    CHECK(world->TryGetBody(movingId)->Position == shifted->Position);
    CHECK_THROWS_AS(world->RebaseOrigin({std::ldexp(1.0, 60), 0.0, 0.0}), std::out_of_range);
    CHECK(world->WorldOrigin() == shiftedOrigin);
    CHECK(world->TryGetBody(movingId)->Position == shifted->Position);
    CHECK_THROWS_AS(world->RebaseOrigin(origin, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(world->RebaseOrigin({std::numeric_limits<double>::infinity(), 0.0, 0.0}), std::invalid_argument);
    world->RebaseOrigin(origin);
    CHECK(world->TryGetBody(movingId)->Position == before->Position);
    world->Close();
    CHECK_THROWS_AS(world->RebaseOrigin(origin), std::logic_error);
}

TEST_CASE("physics origin translation retains queued contacts and immutable debug snapshots")
{
    const auto system = CreatePhysics();
    const auto world = system->CreateWorld({1.0e12, 0.0, 0.0});
    Keire::PhysicsBodyDefinition first;
    first.Trigger = true;
    const auto firstId = world->CreateBody(first);
    auto second = first;
    second.Trigger = false;
    second.Motion = Keire::PhysicsMotionType::Dynamic;
    second.UseGravity = false;
    second.Position = {0.25F, 0.0F, 0.0F};
    const auto secondId = world->CreateBody(second);
    world->ConfigureDebugCapture({.Enabled = true});
    world->Step(1.0F / 60.0F);
    (void)world->RayCast({.Origin = {0.0F, 3.0F, 0.0F},
                          .Direction = {0.0F, -1.0F, 0.0F},
                          .MaximumDistance = 5.0F,
                          .IncludeTriggers = true});
    const auto original = world->CaptureDebugSnapshot();
    REQUIRE(original);
    REQUIRE(original->Contacts.size() == 1);
    REQUIRE(original->Queries.size() == 1);
    world->RebaseOrigin({1.0e12 + 128.0, 0.0, 0.0});
    const auto shifted = world->CaptureDebugSnapshot();
    REQUIRE(shifted);
    REQUIRE(shifted->Contacts.size() == 1);
    CHECK(shifted->Contacts.front().Point.X + 128.0F == doctest::Approx(original->Contacts.front().Point.X));
    CHECK(shifted->Queries.front().Ray.Origin.X == -128.0F);
    CHECK(original->WorldOrigin == Keire::Vector3d{1.0e12, 0.0, 0.0});
    CHECK(original->Queries.front().Ray.Origin.X == 0.0F);
    const auto queued = world->DrainContactEvents();
    REQUIRE(queued.size() == 1);
    CHECK(queued.front().Phase == Keire::ContactPhase::Enter);
    CHECK(queued.front().Point == shifted->Contacts.front().Point);
    world->Step(1.0F / 60.0F);
    const auto continued = world->DrainContactEvents();
    REQUIRE(continued.size() == 1);
    CHECK(continued.front().Phase == Keire::ContactPhase::Stay);
    CHECK(continued.front().First == firstId);
    CHECK(continued.front().Second == secondId);
}

TEST_CASE("physics region conversion rejects invalid input and closed worlds")
{
    const auto system = CreatePhysics();
    const auto infinity = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)system->CreateWorld({infinity, 0.0, 0.0}), std::invalid_argument);
    const auto world = system->CreateWorld();
    CHECK(world->WorldOrigin() == Keire::Vector3d{});
    CHECK_THROWS_AS((void)world->ToLocalPosition({16777217.0, 0.0, 0.0}, 0.0), std::out_of_range);
    CHECK_THROWS_AS((void)world->ToLocalPosition({}, -1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)world->ToLocalPosition({infinity, 0.0, 0.0}, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)world->ToWorldPosition({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}),
                    std::invalid_argument);
    world->Close();
    CHECK_THROWS_AS((void)world->WorldOrigin(), std::logic_error);
    CHECK_THROWS_AS((void)world->ToLocalPosition({}, 0.0), std::logic_error);
    CHECK_THROWS_AS((void)world->ToWorldPosition({}), std::logic_error);
}

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
    CHECK(world->OverlapSphere({.Center = {0.9F, 0.0F, 0.0F}, .Radius = 0.2F, .Mask = 1U, .IgnoreBody = sphereBody})
              .empty());
    CHECK(world->OverlapSphere({.Center = {3.0F, 0.0F, 0.0F}, .Radius = 0.25F, .Mask = 2U, .IncludeTriggers = false})
              .empty());
    const auto triggerOverlap =
        world->OverlapSphere({.Center = {3.0F, 0.0F, 0.0F}, .Radius = 0.25F, .Mask = 2U, .IncludeTriggers = true});
    REQUIRE(triggerOverlap.size() == 1);
    CHECK(triggerOverlap.front() == triggerBody);
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
    CHECK_THROWS_AS((void)world->OverlapSphere({.Radius = -1.0F}), std::invalid_argument);

    character.Position = {-3.0F, 1.0F, 0.0F};
    character.Radius = 0.5F;
    character.Height = 1.0F;
    const auto sphereLikeCapsule = world->CreateBody(character);
    CHECK(world->TryGetBody(sphereLikeCapsule).has_value());
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

TEST_CASE("physics stacks retain resting stability across distant region origins")
{
    const auto system = CreatePhysics();
    std::vector<Keire::PhysicsBodyState> baseline;
    constexpr int BodyCount = 8;
    constexpr int SettlingSteps = 1200;
    for (const double distance : {0.0, 1000.0, -1000.0, 100000.0, 10000000.0, 1.0e12, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const Keire::Vector3d origin{distance, distance, -distance};
        const auto world = system->CreateWorld(origin);
        Keire::PhysicsBodyDefinition floor;
        floor.Position = {0.0F, -0.5F, 0.0F};
        floor.HalfExtent = {10.0F, 0.5F, 10.0F};
        const auto floorBody = world->CreateBody(floor);
        REQUIRE(floorBody);
        std::vector<Keire::PhysicsBodyId> bodies;
        for (int index = 0; index < BodyCount; ++index)
        {
            Keire::PhysicsBodyDefinition box;
            box.Motion = Keire::PhysicsMotionType::Dynamic;
            box.Position = world->ToLocalPosition({distance + 0.25, distance + 0.5 + index, -distance + 0.125}, 0.0);
            bodies.push_back(world->CreateBody(box));
        }
        for (int step = 0; step < SettlingSteps; ++step)
        {
            world->Step(1.0F / 60.0F);
        }
        for (int index = 0; index < BodyCount; ++index)
        {
            CAPTURE(index);
            const auto state = world->TryGetBody(bodies[index]);
            REQUIRE(state);
            CHECK(std::abs(state->Position.X - 0.25F) < 0.1F);
            CHECK(std::abs(state->Position.Z - 0.125F) < 0.1F);
            CHECK(std::abs(state->Position.Y - (0.5F + index)) < 0.15F);
            CHECK(std::abs(state->LinearVelocity.Y) < 0.05F);
            CHECK(state->Sleeping);
            if (distance == 0.0)
            {
                baseline.push_back(*state);
            }
            else
            {
                CHECK(state->Position.X == doctest::Approx(baseline[index].Position.X).epsilon(0.0001));
                CHECK(state->Position.Y == doctest::Approx(baseline[index].Position.Y).epsilon(0.0001));
                CHECK(state->Position.Z == doctest::Approx(baseline[index].Position.Z).epsilon(0.0001));
            }
        }
    }
}

TEST_CASE("physics sleeping stacks survive repeated live origin translations")
{
    const auto system = CreatePhysics();
    for (const double distance : {0.0, 1.0e7, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const Keire::Vector3d origin{distance, 0.0, -distance};
        const auto world = system->CreateWorld(origin);
        Keire::PhysicsBodyDefinition floor;
        floor.Position = {0.0F, -0.5F, 0.0F};
        floor.HalfExtent = {10.0F, 0.5F, 10.0F};
        REQUIRE(world->CreateBody(floor));
        std::vector<Keire::PhysicsBodyId> bodies;
        for (int index = 0; index < 8; ++index)
        {
            Keire::PhysicsBodyDefinition box;
            box.Motion = Keire::PhysicsMotionType::Dynamic;
            box.Position = {0.25F, 0.5F + index, 0.125F};
            bodies.push_back(world->CreateBody(box));
        }
        for (int step = 0; step < 1200; ++step)
        {
            world->Step(1.0F / 60.0F);
        }
        std::vector<Keire::PhysicsBodyState> settled;
        for (const auto body : bodies)
        {
            const auto state = world->TryGetBody(body);
            REQUIRE(state);
            REQUIRE(state->Sleeping);
            settled.push_back(*state);
        }
        for (int translation = 0; translation < 8; ++translation)
        {
            CAPTURE(translation);
            const float shift = translation % 2 == 0 ? 128.0F : 0.0F;
            world->RebaseOrigin({origin.X + shift, shift, origin.Z - shift});
            for (std::size_t index = 0; index < bodies.size(); ++index)
            {
                const auto state = world->TryGetBody(bodies[index]);
                REQUIRE(state);
                CHECK(state->Sleeping);
                CHECK(state->LinearVelocity == settled[index].LinearVelocity);
            }
            for (int step = 0; step < 60; ++step)
            {
                world->Step(1.0F / 60.0F);
            }
            for (std::size_t index = 0; index < bodies.size(); ++index)
            {
                const auto state = world->TryGetBody(bodies[index]);
                REQUIRE(state);
                CHECK(std::abs(state->Position.X + shift - settled[index].Position.X) < 0.001F);
                CHECK(std::abs(state->Position.Y + shift - settled[index].Position.Y) < 0.001F);
                CHECK(std::abs(state->Position.Z - shift - settled[index].Position.Z) < 0.001F);
                CHECK(state->Sleeping);
            }
        }
    }
}

TEST_CASE("physics continuous bodies cannot tunnel through thin walls at distant origins")
{
    const auto system = CreatePhysics();
    for (const double distance : {0.0, 1000.0, -1000.0, 100000.0, 10000000.0, 1.0e12, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const auto world = system->CreateWorld({distance, distance, -distance});
        Keire::PhysicsBodyDefinition wall;
        wall.HalfExtent = {0.05F, 5.0F, 5.0F};
        const auto wallBody = world->CreateBody(wall);
        REQUIRE(wallBody);
        Keire::PhysicsBodyDefinition projectile;
        projectile.Motion = Keire::PhysicsMotionType::Dynamic;
        projectile.Shape = Keire::ColliderShape::Sphere;
        projectile.Radius = 0.125F;
        projectile.Position = world->ToLocalPosition({distance - 5.0, distance + 0.25, -distance + 0.125}, 0.0);
        projectile.LinearVelocity = {600.0F, 0.0F, 0.0F};
        projectile.UseGravity = false;
        projectile.Continuous = true;
        const auto body = world->CreateBody(projectile);
        for (int step = 0; step < 30; ++step)
        {
            CAPTURE(step);
            world->Step(1.0F / 60.0F);
            const auto state = world->TryGetBody(body);
            REQUIRE(state);
            CHECK(std::isfinite(state->Position.X));
            CHECK(state->Position.X < 0.0F);
            CHECK(state->Position.X > -5.1F);
            CHECK(std::abs(state->LinearVelocity.X) < 1.0F);
        }
    }
}

TEST_CASE("physics fast flight and impact remain continuous across repeated origin changes")
{
    const auto system = CreatePhysics();
    for (const double distance : {0.0, 1.0e7, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const auto world = system->CreateWorld({distance, 0.0, 0.0});
        const auto control = system->CreateWorld();
        Keire::PhysicsBodyDefinition wall;
        wall.HalfExtent = {0.05F, 5.0F, 5.0F};
        REQUIRE(world->CreateBody(wall));
        REQUIRE(control->CreateBody(wall));
        Keire::PhysicsBodyDefinition projectile;
        projectile.Motion = Keire::PhysicsMotionType::Dynamic;
        projectile.Shape = Keire::ColliderShape::Sphere;
        projectile.Radius = 0.125F;
        projectile.Position = {-25.0F, 0.25F, 0.125F};
        projectile.LinearVelocity = {600.0F, 0.0F, 0.0F};
        projectile.UseGravity = false;
        projectile.Continuous = true;
        const auto body = world->CreateBody(projectile);
        const auto controlBody = control->CreateBody(projectile);
        float previousPosition = -25.0F;
        for (int step = 0; step < 12; ++step)
        {
            CAPTURE(step);
            const float shift = step % 2 == 0 ? 128.0F : 0.0F;
            world->RebaseOrigin({distance + shift, 0.0, 0.0});
            world->Step(1.0F / 60.0F);
            control->Step(1.0F / 60.0F);
            const auto state = world->TryGetBody(body);
            const auto controlState = control->TryGetBody(controlBody);
            REQUIRE(state);
            REQUIRE(controlState);
            const float position = state->Position.X + shift;
            CHECK(std::abs(position - controlState->Position.X) < 0.001F);
            CHECK(std::abs(state->LinearVelocity.X - controlState->LinearVelocity.X) < 0.001F);
            CHECK(std::isfinite(position));
            CHECK(position < 0.0F);
            CHECK(position >= previousPosition - 0.001F);
            if (step < 2)
            {
                // Jolt clamps the requested 600 m/s to its default 500 m/s body limit.
                CHECK(position > previousPosition + 8.0F);
                CHECK(state->LinearVelocity.X > 490.0F);
            }
            else
            {
                CHECK(position > -0.3F);
                CHECK(std::abs(state->LinearVelocity.X) < 1.0F);
            }
            previousPosition = position;
        }
    }
}
