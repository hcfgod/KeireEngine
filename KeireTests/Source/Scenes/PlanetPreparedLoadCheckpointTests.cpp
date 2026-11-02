#include "Keire/Core.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>

TEST_CASE("Scene physics checkpoint restores prepared-load tick and accepted water without duplication")
{
    using namespace Keire;
    using namespace Keire::Detail;
    auto assets = CreateRef<AssetSystem>(AssetSystemSpecification{.Mode = AssetMode::Development});
    auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Checkpoint body");
    (void)entity.AddComponent<ColliderComponent>();
    entity.AddComponent<RigidBodyComponent>()->SetUseGravity(false);
    auto session = CreateRef<SceneRuntimeSession>(scene, assets, Ref<AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == ScenePlayState::Playing);
    session->Pause();
    const auto world = session->Physics();
    const auto hit = world->RayCast({{0.0F, 5.0F, 0.0F}, {0.0F, -1.0F, 0.0F}, 10.0F});
    REQUIRE(hit.size() == 1);
    const std::array ids{hit.front().Body};
    const PhysicsLoadFrameContext context{2, 3, 1.0F / 60.0F};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    const std::array loads{PreparedPhysicsBodyLoad{hit.front().Body, {3.0, 0.0, 0.0}, {0.0, 2.0, 0.0}}};
    PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads);
    const auto checkpoint = session->CapturePhysicsWorldCheckpoint();
    REQUIRE(checkpoint.Loads);
    REQUIRE(checkpoint.Bodies.size() == 1);
    CHECK(checkpoint.Loads->AcceptedWaterTick == checkpoint.Loads->Tick);
    world->Step(context.DeltaSeconds);
    session->RestorePhysicsWorldCheckpoint(checkpoint);
    const auto restored = session->CapturePhysicsWorldCheckpoint();
    REQUIRE(restored.Loads);
    CHECK(restored.Loads->Tick == checkpoint.Loads->Tick);
    CHECK(restored.Loads->AcceptedWaterTick == checkpoint.Loads->AcceptedWaterTick);
    CHECK(restored.Loads->AcceptedWaterDeltaSeconds == context.DeltaSeconds);
    CHECK(restored.Bodies.front().PendingForce == checkpoint.Bodies.front().PendingForce);
    CHECK(restored.Bodies.front().PendingTorque == checkpoint.Bodies.front().PendingTorque);
    const auto duplicate = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, duplicate, loads), std::logic_error);
    CHECK_THROWS_AS(world->Step(context.DeltaSeconds * 2.0F), std::invalid_argument);
    world->Step(context.DeltaSeconds);
    const auto pending = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    const auto before = session->CapturePhysicsWorldCheckpoint();

    SUBCASE("restoration invalidates an otherwise equivalent capture")
    {
        session->RestorePhysicsWorldCheckpoint(before);
        CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, pending, loads), std::logic_error);
        const auto fresh = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
        CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, fresh, loads));
    }
    SUBCASE("legacy metadata omission preserves destination tick and latch")
    {
        auto legacy = before;
        legacy.Loads.reset();
        session->RestorePhysicsWorldCheckpoint(legacy);
        const auto after = session->CapturePhysicsWorldCheckpoint();
        REQUIRE(after.Loads);
        CHECK(after.Loads->Tick == before.Loads->Tick);
        CHECK(after.Loads->AcceptedWaterTick == before.Loads->AcceptedWaterTick);
        CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, pending, loads), std::logic_error);
    }
    SUBCASE("malformed metadata rejects before body gravity or bookkeeping changes")
    {
        auto invalid = before;
        invalid.Gravity = Vector3{9.0F, 8.0F, 7.0F};
        invalid.Bodies.front().Position.X += 10.0F;
        invalid.Bodies.front().PendingForce = {42.0F, 0.0F, 0.0F};
        SUBCASE("future accepted tick") { invalid.Loads->AcceptedWaterTick = invalid.Loads->Tick + 1; }
        SUBCASE("nonfinite duration")
        {
            invalid.Loads->AcceptedWaterDeltaSeconds = std::numeric_limits<float>::quiet_NaN();
        }
        SUBCASE("duration without accepted source") { invalid.Loads->AcceptedWaterTick.reset(); }
        CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::invalid_argument);
        const auto after = session->CapturePhysicsWorldCheckpoint();
        CHECK(after.Gravity == before.Gravity);
        CHECK(after.Bodies.front().Position == before.Bodies.front().Position);
        CHECK(after.Bodies.front().PendingForce == before.Bodies.front().PendingForce);
        CHECK(after.Bodies.front().PendingTorque == before.Bodies.front().PendingTorque);
        CHECK(after.Loads->Tick == before.Loads->Tick);
        CHECK(after.Loads->AcceptedWaterTick == before.Loads->AcceptedWaterTick);
        CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, pending, loads));
    }
    session->Stop();
    scene->Close();
    physics->Close();
    assets->Close();
}
