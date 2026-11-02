#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Water/WaterBuoyancy.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

TEST_CASE("water buoyancy integrates dry half and fully submerged spherical probes")
{
    Keire::WaterSurfaceDefinition definition;
    definition.Shape = Keire::WaterSurfaceShape::InfinitePlane;
    const Keire::WaterSurface water(definition);
    Keire::WaterBuoyancyProbe probe;
    probe.RadiusMetres = 1.0;
    probe.Position.Y = 2.0;
    const auto dry = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(dry);
    CHECK(dry->SubmergedVolumeCubicMetres == 0.0);
    CHECK(dry->ForceNewtons == Keire::Vector3d{});
    probe.Position.Y = 0.0;
    const auto half = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(half);
    CHECK(half->SubmergedVolumeCubicMetres == doctest::Approx(2.0 * std::numbers::pi / 3.0));
    CHECK(half->ForceNewtons.Y == doctest::Approx(10'000.0 * half->SubmergedVolumeCubicMetres));
    CHECK(half->CenterOfBuoyancyOffset.Y == doctest::Approx(-0.375));
    probe.Position.Y = -2.0;
    const auto full = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(full);
    CHECK(full->SubmergedVolumeCubicMetres == doctest::Approx(4.0 * std::numbers::pi / 3.0));
    CHECK(full->CenterOfBuoyancyOffset == Keire::Vector3d{});
    CHECK(full->ForceNewtons.Y == doctest::Approx(2.0 * half->ForceNewtons.Y));
    probe.VolumeScale = 0.25;
    const auto weighted = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(weighted);
    CHECK(weighted->ForceNewtons.Y == doctest::Approx(full->ForceNewtons.Y * 0.25));
}

TEST_CASE("water buoyancy respects finite depth and rejects dry shore probes")
{
    Keire::WaterSurfaceDefinition definition;
    definition.DepthMetres = 1.0;
    const Keire::WaterSurface water(definition);
    Keire::WaterBuoyancyProbe probe;
    probe.RadiusMetres = 1.0;
    probe.Position.Y = -1.0;
    const auto bottom = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(bottom);
    CHECK(bottom->SubmergedVolumeCubicMetres == doctest::Approx(2.0 * std::numbers::pi / 3.0));
    CHECK(bottom->CenterOfBuoyancyOffset.Y == doctest::Approx(0.375));
    probe.Position.Y = -3.0;
    const auto below = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(below);
    CHECK(below->ForceNewtons == Keire::Vector3d{});
    probe.Position = {100.0, 0.0, 0.0};
    CHECK_FALSE(Keire::EvaluateWaterBuoyancy(water, probe, {}));
}

TEST_CASE("water buoyancy volume and centroid agree with independent slice integration")
{
    Keire::WaterSurfaceDefinition definition;
    definition.DepthMetres = 1.2;
    const Keire::WaterSurface water(definition);
    for (const double centerHeight : {0.75, 0.0, -0.5, -1.5})
    {
        CAPTURE(centerHeight);
        Keire::WaterBuoyancyProbe probe;
        probe.Position.Y = centerHeight;
        probe.RadiusMetres = 1.0;
        const auto sample = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
        REQUIRE(sample);
        constexpr int slices = 4096;
        const double lower = std::max(-1.0, -definition.DepthMetres - centerHeight);
        const double upper = std::min(1.0, -centerHeight);
        const double step = (upper - lower) / slices;
        double volume = 0.0;
        double moment = 0.0;
        for (int index = 0; index < slices; ++index)
        {
            const double height = lower + (index + 0.5) * step;
            const double sliceVolume = std::numbers::pi * (1.0 - height * height) * step;
            volume += sliceVolume;
            moment += height * sliceVolume;
        }
        CHECK(sample->SubmergedVolumeCubicMetres == doctest::Approx(volume).epsilon(0.000001));
        CHECK(sample->CenterOfBuoyancyOffset.Y == doctest::Approx(moment / volume).epsilon(0.000001));
    }
}

TEST_CASE("water buoyancy uses radial gravity and remains stable at distant planet origins")
{
    for (const double origin : {0.0, 1.0e12})
    {
        Keire::WaterSurfaceDefinition definition;
        definition.Shape = Keire::WaterSurfaceShape::Sphere;
        definition.Origin = {origin, -origin, origin};
        const Keire::WaterSurface ocean(definition);
        Keire::WaterBuoyancyProbe probe;
        probe.Position = {origin + definition.RadiusMetres, -origin, origin};
        const auto result = Keire::EvaluateWaterBuoyancy(ocean, probe, {-9.81, 0.0, 0.0});
        REQUIRE(result);
        CHECK(result->ForceNewtons.X == doctest::Approx(result->SubmergedVolumeCubicMetres * 9810.0));
        CHECK(result->ForceNewtons.Y == 0.0);
        CHECK(result->CenterOfBuoyancyOffset.X == doctest::Approx(-0.1875));
        const auto zeroGravity = Keire::EvaluateWaterBuoyancy(ocean, probe, {});
        REQUIRE(zeroGravity);
        CHECK(zeroGravity->ForceNewtons == Keire::Vector3d{});
        CHECK(zeroGravity->SubmergedVolumeCubicMetres > 0.0);
    }
}

TEST_CASE("water buoyancy force balances native body weight at far coordinates")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const Keire::Vector3d origin{1.0e12, 1.0e12, -1.0e12};
    const auto world = system->CreateWorld(origin);
    world->SetGravity({0.0F, -10.0F, 0.0F});
    Keire::WaterSurfaceDefinition waterDefinition;
    waterDefinition.Origin = origin;
    const Keire::WaterSurface water(waterDefinition);
    Keire::WaterBuoyancyProbe probe;
    probe.Position = origin;
    const auto buoyancy = Keire::EvaluateWaterBuoyancy(water, probe, Keire::Math::Widen(world->Gravity()));
    REQUIRE(buoyancy);
    Keire::PhysicsBodyDefinition bodyDefinition;
    bodyDefinition.Motion = Keire::PhysicsMotionType::Dynamic;
    bodyDefinition.Mass = static_cast<float>(buoyancy->SubmergedVolumeCubicMetres * 1000.0);
    bodyDefinition.MotionSettings.LinearDamping = 0.0F;
    const auto body = world->CreateBody(bodyDefinition);
    const auto force = Keire::Math::RelativeFloat(buoyancy->ForceNewtons, {}, 0.001);
    const auto point = Keire::Math::RelativeFloat(buoyancy->CenterOfBuoyancyOffset, {}, 0.001);
    world->AddForceAtPosition(body, force, point);
    world->Step(0.01F);
    const auto state = world->TryGetBody(body);
    REQUIRE(state);
    CHECK(state->LinearVelocity.Y == doctest::Approx(0.0).epsilon(0.0001));
    CHECK(state->AngularVelocity == Keire::Vector3{});
}

TEST_CASE("water buoyancy at an offset hull center does not invent torque around the entity origin")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const Keire::Vector3d origin{1.0e12, 0.0, 0.0};
    const auto world = system->CreateWorld(origin);
    Keire::CollisionCookInput mesh;
    for (const float x : {1.0F, 3.0F})
    {
        for (const float y : {-1.0F, 1.0F})
        {
            for (const float z : {-1.0F, 1.0F})
            {
                mesh.Vertices.push_back({x, y, z});
            }
        }
    }
    Keire::PhysicsBodyDefinition definition;
    definition.Motion = Keire::PhysicsMotionType::Dynamic;
    definition.Shape = Keire::ColliderShape::ConvexMesh;
    definition.Collision = Keire::CookCollisionMesh(std::move(mesh));
    definition.Position = {10.0F, 0.0F, 0.0F};
    definition.UseGravity = false;
    definition.Mass = 1000.0F;
    definition.MotionSettings.LinearDamping = 0.0F;
    const auto body = world->CreateBody(definition);
    Keire::WaterSurfaceDefinition waterDefinition;
    waterDefinition.Origin = origin;
    const Keire::WaterSurface water(waterDefinition);
    Keire::WaterBuoyancyProbe probe;
    probe.Position = {origin.X + 12.0, 0.0, 0.0};
    const auto buoyancy = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
    REQUIRE(buoyancy);
    const Keire::Vector3d point{12.0 + buoyancy->CenterOfBuoyancyOffset.X, buoyancy->CenterOfBuoyancyOffset.Y,
                                buoyancy->CenterOfBuoyancyOffset.Z};
    world->AddForceAtPosition(body, Keire::Math::RelativeFloat(buoyancy->ForceNewtons, {}, 0.001),
                              Keire::Math::RelativeFloat(point, {}, 0.001));
    world->Step(0.01F);
    const auto state = world->TryGetBody(body);
    REQUIRE(state);
    CHECK(state->LinearVelocity.Y == doctest::Approx(buoyancy->ForceNewtons.Y * 0.01 / definition.Mass));
    CHECK(state->AngularVelocity.X == doctest::Approx(0.0));
    CHECK(state->AngularVelocity.Y == doctest::Approx(0.0));
    CHECK(state->AngularVelocity.Z == doctest::Approx(0.0));
}

TEST_CASE("water buoyancy validates probes and rejects unrepresentable forces")
{
    const Keire::WaterSurface water(Keire::WaterSurfaceDefinition{});
    Keire::WaterBuoyancyProbe probe;
    SUBCASE("negative radius")
    {
        probe.RadiusMetres = -1.0;
        CHECK_THROWS_AS((void)Keire::EvaluateWaterBuoyancy(water, probe, {}), std::invalid_argument);
    }
    SUBCASE("invalid gravity")
    {
        CHECK_THROWS_AS(
            Keire::EvaluateWaterBuoyancy(water, probe, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}),
            std::invalid_argument);
    }
    SUBCASE("negative volume weight")
    {
        probe.VolumeScale = -1.0;
        CHECK_THROWS_AS((void)Keire::EvaluateWaterBuoyancy(water, probe, {}), std::invalid_argument);
    }
    SUBCASE("disabled probe")
    {
        probe.VolumeScale = 0.0;
        const auto disabled = Keire::EvaluateWaterBuoyancy(water, probe, {0.0, -10.0, 0.0});
        REQUIRE(disabled);
        CHECK(disabled->ForceNewtons == Keire::Vector3d{});
    }
    SUBCASE("force overflow")
    {
        CHECK_THROWS_AS(
            (void)Keire::EvaluateWaterBuoyancy(water, probe, {std::numeric_limits<double>::max(), 0.0, 0.0}),
            std::out_of_range);
    }
}
