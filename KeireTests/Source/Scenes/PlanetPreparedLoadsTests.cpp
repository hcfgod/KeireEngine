#include "Keire/Physics/PhysicsSystem.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>

TEST_CASE("Prepared loads add to live external loads exactly once per physics tick")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    definition.UseGravity = false;
    const auto body = world->CreateBody(definition);
    const std::array ids{body};
    const PhysicsLoadFrameContext context{3, 9, 1.0F / 60.0F};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    REQUIRE(capture->Bodies().size() == 1);
    CHECK(capture->Bodies().front().InverseMass > 0.0F);
    world->AddForce(body, {2.0F, 0.0F, 0.0F});
    world->AddTorque(body, {0.0F, 3.0F, 0.0F});
    const std::array loads{PreparedPhysicsBodyLoad{body, {5.0, 0.0, 0.0}, {0.0, 7.0, 0.0}}};
    PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads);
    const auto committed = world->TryGetBody(body);
    REQUIRE(committed);
    CHECK(committed->PendingForce == Vector3{7.0F, 0.0F, 0.0F});
    CHECK(committed->PendingTorque == Vector3{0.0F, 10.0F, 0.0F});
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads), std::logic_error);
    const auto recaptured = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, recaptured, loads), std::logic_error);
    CHECK_THROWS_AS(world->Step(1.0F / 30.0F), std::invalid_argument);
    CHECK(world->TryGetBody(body)->PendingForce == committed->PendingForce);
    world->Step(context.DeltaSeconds);
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads), std::logic_error);
    const auto next = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    CHECK(next->PhysicsTick() == capture->PhysicsTick() + 1);
    CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, next, loads));
    world->Close();
    physics->Close();
}

TEST_CASE("Prepared load rejection preserves all accumulators and leaves its tick slot available")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    definition.UseGravity = false;
    const auto first = world->CreateBody(definition);
    definition.Position = {5.0F, 0.0F, 0.0F};
    const auto second = world->CreateBody(definition);
    const std::array ids{first, second};
    const PhysicsLoadFrameContext context{3, 9, 1.0F / 60.0F};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    world->AddForce(first, {2.0F, 0.0F, 0.0F});
    const float maximum = std::numeric_limits<float>::max();
    world->AddForce(second, {maximum, 0.0F, 0.0F});
    std::array loads{PreparedPhysicsBodyLoad{first, {5.0, 0.0, 0.0}, {}},
                     PreparedPhysicsBodyLoad{second, {maximum, 0.0, 0.0}, {}}};
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads), std::out_of_range);
    CHECK(world->TryGetBody(first)->PendingForce == Vector3{2.0F, 0.0F, 0.0F});
    CHECK(world->TryGetBody(second)->PendingForce == Vector3{maximum, 0.0F, 0.0F});
    loads[1].Force = {};
    CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads));
    CHECK(world->TryGetBody(first)->PendingForce == Vector3{7.0F, 0.0F, 0.0F});
    world->Close();
    physics->Close();
}

TEST_CASE("Prepared loads reject changed context body pose origin and world identity")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    const auto other = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    const auto body = world->CreateBody(definition);
    const std::array ids{body};
    PhysicsLoadFrameContext context{3, 9, 1.0F / 60.0F};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    const std::array loads{PreparedPhysicsBodyLoad{body, {1.0, 0.0, 0.0}, {}}};
    SUBCASE("frame epoch") { ++context.FrameEpoch; }
    SUBCASE("body pose")
    {
        auto state = *world->TryGetBody(body);
        state.Position.X = 1.0F;
        world->SetBodyState(body, state);
    }
    SUBCASE("rebase and return to the same origin")
    {
        world->RebaseOrigin({128.0, 0.0, 0.0});
        world->RebaseOrigin({});
    }
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads), std::logic_error);
    CHECK(world->TryGetBody(body)->PendingForce == Vector3{});
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*other, context, capture, loads), std::logic_error);
    world->Close();
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, loads), std::logic_error);
    other->Close();
    physics->Close();
}

TEST_CASE("Prepared load capture rejects duplicate and nondynamic body identities")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Motion = PhysicsMotionType::Dynamic;
    const auto dynamic = world->CreateBody(definition);
    const auto stationary = world->CreateBody({});
    const PhysicsLoadFrameContext context{0, 0, 1.0F / 60.0F};
    const std::array duplicate{dynamic, dynamic};
    CHECK_THROWS_AS((void)PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, duplicate),
                    std::invalid_argument);
    const std::array nondynamic{stationary};
    CHECK_THROWS_AS(
        (void)PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, nondynamic),
        std::invalid_argument);
    const std::array ids{dynamic};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, ids);
    const std::array duplicateLoads{PreparedPhysicsBodyLoad{dynamic, {1.0, 0.0, 0.0}, {}},
                                    PreparedPhysicsBodyLoad{dynamic, {2.0, 0.0, 0.0}, {}}};
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, duplicateLoads),
                    std::invalid_argument);
    CHECK(world->TryGetBody(dynamic)->PendingForce == Vector3{});
    const std::array foreignLoad{PreparedPhysicsBodyLoad{stationary, {1.0, 0.0, 0.0}, {}}};
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, foreignLoad),
                    std::invalid_argument);
    CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, {}));
    world->Close();
    physics->Close();
}

TEST_CASE("Empty prepared load batch consumes one tick slot and empty-world steps advance it")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = physics->CreateWorld();
    const PhysicsLoadFrameContext context{0, 0, 1.0F / 60.0F};
    const auto capture = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, {});
    REQUIRE(capture->Bodies().empty());
    PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, {});
    CHECK_THROWS_AS(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, capture, {}), std::logic_error);
    world->Step(context.DeltaSeconds);
    const auto next = PhysicsFrameAccess::CapturePreparedLoads(world, context, PhysicsLoadSource::Water, {});
    CHECK(next->PhysicsTick() == capture->PhysicsTick() + 1);
    CHECK_NOTHROW(PhysicsFrameAccess::ApplyPreparedLoads(*world, context, next, {}));
    world->Close();
    physics->Close();
}
