#include "Keire/Core.h"
#include "Keire/ECS/Components/BuoyancyComponent.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"
#include "KeireInternal/Water/WaterAnchorVelocity.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace
{
    struct WaterRuntimeFixture
    {
        Keire::Ref<Keire::Scene> Edit;
        Keire::Ref<Keire::PhysicsSystem> Physics;
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::EntityId Hull;
        Keire::EntityId Water;
        float HullMass = static_cast<float>(1000.0 * (4.0 / 3.0) * std::numbers::pi_v<double> * 0.125);

        explicit WaterRuntimeFixture(const double origin = 0.0, const bool sphere = false)
        {
            Edit = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                  Keire::SceneAsset::EmptyDefinition("Water QA"));
            auto water = Edit->CreateEntity("Water");
            Water = water.Id();
            water.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise(
                {origin, sphere ? -100.0 : 0.0, 0.0});
            const auto medium = water.AddComponent<Keire::WaterBodyComponent>();
            if (sphere)
            {
                auto settings = medium->Settings();
                settings.Shape = Keire::WaterSurfaceShape::Sphere;
                settings.RadiusMetres = 100.0;
                medium->Configure(settings);
            }
            auto hull = Edit->CreateEntity("Floating hull");
            Hull = hull.Id();
            hull.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({origin, -2.0, 0.0});
            (void)hull.AddComponent<Keire::ColliderComponent>();
            const auto buoyancy = hull.AddComponent<Keire::BuoyancyComponent>();
            auto hydrodynamics = buoyancy->Settings();
            hydrodynamics.DragRatePerSecond = 0.0;
            buoyancy->Configure(hydrodynamics);
            const auto rigid = hull.GetComponent<Keire::RigidBodyComponent>();
            rigid->SetMass(HullMass);
            rigid->SetMotionSettings({0.0F, 0.0F, 500.0F, 100.0F});
            Keire::PhysicsSystemSpecification specification;
            specification.Mode = Keire::PhysicsMode::Enabled;
            Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
            Session = Keire::CreateRef<Keire::SceneRuntimeSession>(Edit, Keire::Ref<Keire::AssetSystem>{},
                                                                   Keire::Ref<Keire::AudioSystem>{}, Physics);
            Session->Play();
        }

        Keire::ScenePhysicsCheckpointBody HullState() const
        {
            const auto checkpoint = Session->CapturePhysicsWorldCheckpoint();
            REQUIRE(checkpoint.Bodies.size() == 1);
            CHECK(checkpoint.Bodies.front().Entity == Hull);
            return checkpoint.Bodies.front();
        }

        Keire::PhysicsBodyId HullBody() const
        {
            const auto hits = Session->Physics()->OverlapSphere(HullState().Position, 2.0F);
            REQUIRE(hits.size() == 1);
            return hits.front();
        }
    };
} // namespace

TEST_CASE("authored buoyancy runs automatically at near and trillion metre origins")
{
    for (const double origin : {0.0, 1.0e12})
    {
        WaterRuntimeFixture fixture(origin);
        for (int tick = 0; tick < 20; ++tick)
        {
            fixture.Session->FixedUpdate(0.01F);
            REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
        }
        CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);
        const auto water =
            fixture.Session->RuntimeScene()->FindEntity(fixture.Water).GetComponent<Keire::WaterBodyComponent>();
        REQUIRE(water->SimulationTimeSeconds());
        CHECK(*water->SimulationTimeSeconds() == doctest::Approx(0.2));
        CHECK_FALSE(
            fixture.Edit->FindEntity(fixture.Water).GetComponent<Keire::WaterBodyComponent>()->SimulationTimeSeconds());
    }
}

TEST_CASE("authored water preserves external forces and applies one contribution per tick")
{
    WaterRuntimeFixture fixture;
    fixture.Session->Physics()->AddForce(fixture.HullBody(), {fixture.HullMass * 2.0F, 0.0F, 0.0F});
    fixture.Session->FixedUpdate(0.01F);
    REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
    CHECK(fixture.HullState().LinearVelocity.X == doctest::Approx(0.02F));
    CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);
    fixture.Session->FixedUpdate(0.01F);
    CHECK(fixture.HullState().LinearVelocity.X == doctest::Approx(0.02F));
    CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);
}

TEST_CASE("disabled buoyancy and water layer masks stop fluid forces on the next fixed step")
{
    WaterRuntimeFixture fixture;
    const auto baseline = fixture.Session->CapturePhysicsWorldCheckpoint();
    const auto hull = fixture.Session->RuntimeScene()->FindEntity(fixture.Hull);
    const auto buoyancy = hull.GetComponent<Keire::BuoyancyComponent>();
    buoyancy->SetEnabled(false);
    fixture.Session->FixedUpdate(0.01F);
    CHECK(fixture.HullState().LinearVelocity.Y < -0.09F);
    fixture.Session->RestorePhysicsWorldCheckpoint(baseline);
    buoyancy->SetEnabled(true);
    auto settings = buoyancy->Settings();
    settings.WaterLayerMask = 0;
    buoyancy->Configure(settings);
    fixture.Session->FixedUpdate(0.01F);
    CHECK(fixture.HullState().LinearVelocity.Y < -0.09F);
}

TEST_CASE("water checkpoint restores phase and native latch without double applying restored loads")
{
    WaterRuntimeFixture fixture;
    fixture.Session->FixedUpdate(0.01F);
    const auto saved = fixture.Session->CapturePhysicsWorldCheckpoint();
    REQUIRE(saved.Loads);
    REQUIRE(saved.WaterTimeSeconds);
    fixture.Session->FixedUpdate(0.01F);
    fixture.Session->RestorePhysicsWorldCheckpoint(saved);
    fixture.Session->FixedUpdate(0.01F);
    CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);
    CHECK(*fixture.Session->CapturePhysicsWorldCheckpoint().WaterTimeSeconds == doctest::Approx(0.02));

    // Simulate a checkpoint made after atomic water acceptance but before native integration.
    auto pending = fixture.Session->CapturePhysicsWorldCheckpoint();
    pending.Loads->AcceptedWaterTick = pending.Loads->Tick;
    pending.Loads->AcceptedWaterDeltaSeconds = 0.01F;
    pending.Bodies.front().PendingForce.Y = fixture.HullMass * 9.81F;
    fixture.Session->RestorePhysicsWorldCheckpoint(pending);
    fixture.Session->FixedUpdate(0.01F);
    REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
    CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);

    auto invalid = fixture.Session->CapturePhysicsWorldCheckpoint();
    invalid.WaterTimeSeconds = std::numeric_limits<double>::quiet_NaN();
    const auto before = fixture.HullState();
    CHECK_THROWS_AS(fixture.Session->RestorePhysicsWorldCheckpoint(invalid), std::invalid_argument);
    CHECK(fixture.HullState().Position == before.Position);
}

TEST_CASE("spherical still water is coupled rather than silently omitted")
{
    WaterRuntimeFixture fixture(1.0e12, true);
    fixture.Session->FixedUpdate(0.01F);
    REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
    CHECK(std::abs(fixture.HullState().LinearVelocity.Y) < 0.0001F);
}

TEST_CASE("water native anchor motion measures velocity at its world point")
{
    WaterRuntimeFixture fixture(1.0e12);
    const auto world = fixture.Session->Physics();
    const auto id = fixture.HullBody();
    auto body = world->TryGetBody(id);
    REQUIRE(body);
    body->LinearVelocity = {1.0F, 0.0F, 0.0F};
    body->AngularVelocity = {0.0F, 0.0F, 3.0F};
    world->SetBodyState(id, *body);
    const auto origin = world->ToWorldPosition(body->Position);
    const auto point = Keire::Detail::CaptureWaterAnchorVelocity(*world, id, {origin.X + 2.0, origin.Y, origin.Z});
    CHECK(point.Linear.X == doctest::Approx(1.0F));
    CHECK(point.Linear.Y == doctest::Approx(6.0F));
    CHECK(point.Angular.Z == doctest::Approx(3.0F));
}
