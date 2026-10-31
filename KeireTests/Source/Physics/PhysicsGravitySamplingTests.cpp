#include "Keire/Physics/PhysicsSystem.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
    Keire::Ref<Keire::PhysicsSystem> CreateGravityPhysics()
    {
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        return Keire::CreateRef<Keire::PhysicsSystem>(specification);
    }
} // namespace

TEST_CASE("gravity sampling shares radial and combined body acceleration at distant origins")
{
    const auto system = CreateGravityPhysics();
    for (const double offset : {0.0, 1.0e12})
    {
        for (const bool combined : {false, true})
        {
            CAPTURE(offset);
            CAPTURE(combined);
            const auto world = system->CreateWorld({offset, offset, -offset});
            world->SetGravity(combined ? Keire::Vector3{0.0F, -2.0F, 0.0F} : Keire::Vector3{});
            world->SetGravitySources({{{offset, offset, -offset}, 1000.0, 10.0}});
            const auto local = world->ToLocalPosition({offset + 10.0, offset, -offset}, 0.001);
            const auto sampled = world->SampleGravity(local);
            CHECK(sampled.X == doctest::Approx(-10.0F));
            CHECK(sampled.Y == (combined ? -2.0F : 0.0F));
            CHECK(sampled.Z == 0.0F);

            Keire::PhysicsBodyDefinition definition;
            definition.Motion = Keire::PhysicsMotionType::Dynamic;
            definition.Position = local;
            definition.MotionSettings.LinearDamping = 0.0F;
            const auto body = world->CreateBody(definition);
            constexpr float delta = 0.01F;
            world->Step(delta);
            const auto state = world->TryGetBody(body);
            REQUIRE(state);
            CHECK(state->LinearVelocity.X / delta == doctest::Approx(sampled.X).epsilon(0.0001));
            CHECK(state->LinearVelocity.Y / delta == doctest::Approx(sampled.Y).epsilon(0.0001));
            CHECK(state->LinearVelocity.Z == 0.0F);

            world->RebaseOrigin({offset + 32.0, offset, -offset});
            CHECK(world->SampleGravity({-22.0F, 0.0F, 0.0F}) == sampled);
        }
    }
}

TEST_CASE("gravity sampling handles uniform zero center interior and opposing sources")
{
    const auto system = CreateGravityPhysics();
    const auto world = system->CreateWorld();
    world->SetGravity({1.0F, -2.0F, 3.0F});
    CHECK(world->SampleGravity({100.0F, 200.0F, -300.0F}) == world->Gravity());
    world->SetGravity({});
    CHECK(world->SampleGravity({}) == Keire::Vector3{});
    world->SetGravitySources({{{}, 1000.0, 10.0}});
    CHECK(world->SampleGravity({}) == Keire::Vector3{});
    CHECK(world->SampleGravity({5.0F, 0.0F, 0.0F}).X == doctest::Approx(-5.0F));
    world->SetGravitySources({{{-10.0, 0.0, 0.0}, 100.0, 1.0}, {{10.0, 0.0, 0.0}, 100.0, 1.0}});
    CHECK(world->SampleGravity({}) == Keire::Vector3{});
}

TEST_CASE("gravity sampling rejects invalid range thread and lifetime without mutating the field")
{
    const auto system = CreateGravityPhysics();
    const auto world = system->CreateWorld();
    const auto initial = world->SampleGravity({});
    CHECK_THROWS_AS((void)world->SampleGravity({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)world->SampleGravity({0.0F, std::numeric_limits<float>::infinity(), 0.0F}),
                    std::invalid_argument);
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                (void)world->SampleGravity({});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    CHECK(world->SampleGravity({}) == initial);
    world->SetGravitySources({{{1.0, 0.0, 0.0}, 1.0e100, 1.0}});
    CHECK_THROWS_AS((void)world->SampleGravity({}), std::out_of_range);
    world->SetGravitySources({});
    CHECK(world->SampleGravity({}) == initial);
    SUBCASE("closed world")
    {
        world->Close();
        CHECK_FALSE(world->IsOpen());
    }
    SUBCASE("closed service")
    {
        system->Close();
        CHECK_FALSE(system->IsOpen());
    }
    CHECK_THROWS_AS((void)world->SampleGravity({}), std::logic_error);
}
