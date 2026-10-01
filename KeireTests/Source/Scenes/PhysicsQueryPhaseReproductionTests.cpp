#include "Keire/Core.h"

#include <doctest/doctest.h>

#include "KeireInternal/SceneState.h"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
    struct QueryPhaseFixture final
    {
        Keire::Ref<Keire::Scene> Scene;
        Keire::Ref<Keire::PhysicsSystem> Physics;
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        ~QueryPhaseFixture()
        {
            if (Session)
                Session->Stop();
            if (Scene)
                Scene->Close();
            if (Physics)
                Physics->Close();
        }
    };
} // namespace

TEST_CASE("Authored physics queries synchronize static and kinematic poses before a fixed step")
{
    using namespace Keire;
    for (const bool kinematic : {false, true})
    {
        for (const bool parented : {false, true})
        {
            CAPTURE(kinematic);
            CAPTURE(parented);
            QueryPhaseFixture fixture;
            fixture.Scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition("Query phase baseline"));
            auto parent = fixture.Scene->CreateEntity("Support parent");
            auto support = fixture.Scene->CreateEntity("Support", parented ? parent : Entity{});
            support.AddComponent<ColliderComponent>()->SetHalfExtent({.6F, .25F, .15F});
            if (kinematic)
                support.AddComponent<RigidBodyComponent>()->SetMotion(PhysicsMotionType::Kinematic);
            PhysicsSystemSpecification specification;
            specification.Mode = PhysicsMode::Enabled;
            fixture.Physics = CreateRef<PhysicsSystem>(specification);
            fixture.Session =
                CreateRef<SceneRuntimeSession>(fixture.Scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, fixture.Physics);
            fixture.Session->Play();
            REQUIRE(fixture.Session->State() == ScenePlayState::Playing);
            const auto runtimeSupport = fixture.Session->RuntimeScene()->FindEntity(support.Id());
            const auto mover = fixture.Session->RuntimeScene()->FindEntity(parented ? parent.Id() : support.Id());
            const auto transform = mover.GetComponent<TransformComponent>();
            const auto ray = [&](const Vector3 origin)
            { return fixture.Session->RayCast({.Origin = origin, .Direction = {0, -1, 0}, .MaximumDistance = 5}); };
            const auto capsule = [&](const Vector3 origin)
            {
                return fixture.Session->CastCapsule(
                    {.Origin = origin, .Radius = .05F, .Height = .2F, .Displacement = {0, -4, 0}});
            };
            const auto initialRay = ray({0, 3, 0});
            const auto initialCapsule = capsule({0, 3, 0});
            REQUIRE(initialRay.size() == 1);
            REQUIRE(initialCapsule);
            REQUIRE(initialRay.front().Entity == support.Id());
            REQUIRE(initialCapsule->Entity == support.Id());
            CHECK(initialRay.front().Hit.Position.Y == doctest::Approx(.25F));

            // Equivalent native seam to a script changing Transform and immediately querying through
            // either editor/player bridge: both call these scene-session APIs before StepPhysics.
            transform->SetLocalPosition({0, .2F, 0});
            CHECK(Math::TransformPoint(runtimeSupport.GetComponent<TransformComponent>()->WorldMatrix(), {}).Y ==
                  doctest::Approx(.2F).epsilon(.001));
            const auto staleRay = ray({0, 3, 0});
            const auto staleCapsule = capsule({0, 3, 0});
            REQUIRE(staleRay.size() == 1);
            REQUIRE(staleCapsule);
            CHECK(staleRay.front().Hit.Position.Y == doctest::Approx(.45F));
            CHECK(initialCapsule->Hit.Distance - staleCapsule->Hit.Distance == doctest::Approx(.2F).epsilon(.001));
            CHECK(staleRay.front().Hit.Body == initialRay.front().Hit.Body);
            CHECK(staleCapsule->Hit.Body == initialCapsule->Hit.Body);
            for (int repeat = 0; repeat < 20; ++repeat)
                CHECK(ray({0, 3, 0}).front().Hit.Body == initialRay.front().Hit.Body);
            fixture.Session->FixedUpdate(1.0F / 60);
            REQUIRE(fixture.Session->State() == ScenePlayState::Playing);
            const auto synchronizedRay = ray({0, 3, 0});
            const auto synchronizedCapsule = capsule({0, 3, 0});
            REQUIRE(synchronizedRay.size() == 1);
            REQUIRE(synchronizedCapsule);
            CHECK(synchronizedRay.front().Hit.Position.Y == doctest::Approx(.45F));
            CHECK(initialCapsule->Hit.Distance - synchronizedCapsule->Hit.Distance ==
                  doctest::Approx(.2F).epsilon(.001));
            MESSAGE("Authored query sync kinematic="
                    << kinematic << " parented=" << parented << " authoredTop=.45 beforeStepRayY="
                    << staleRay.front().Hit.Position.Y << " afterStepRayY=" << synchronizedRay.front().Hit.Position.Y
                    << " beforeStepCapsuleDistance=" << staleCapsule->Hit.Distance
                    << " afterStepCapsuleDistance=" << synchronizedCapsule->Hit.Distance);

            transform->SetLocalPosition({2, .2F, 0});
            transform->SetLocalEulerAngles({0, 90, 0});
            CHECK_FALSE(ray({2, 3, .4F}).empty());
            CHECK(capsule({2, 3, .4F}));
            CHECK(ray({0, 3, 0}).empty());
            fixture.Session->FixedUpdate(1.0F / 60);
            REQUIRE(fixture.Session->State() == ScenePlayState::Playing);
            const auto rotatedRay = ray({2, 3, .4F});
            const auto rotatedCapsule = capsule({2, 3, .4F});
            REQUIRE(rotatedRay.size() == 1);
            REQUIRE(rotatedCapsule);
            CHECK(rotatedRay.front().Entity == support.Id());
            CHECK(rotatedCapsule->Entity == support.Id());
            CHECK(ray({0, 3, 0}).empty());
        }
    }
}

TEST_CASE("Authored physics query revision excludes unrelated mutations and follows collider hierarchy")
{
    using namespace Keire;
    auto state = CreateRef<Detail::SceneState>(AssetId::Generate(), SceneAsset::EmptyDefinition(),
                                               ComponentRegistry::CreateDefault());
    state->Initialize(state);
    auto camera = state->Create("Camera");
    auto parent = state->Create("Parent");
    auto child = state->Create("Collider", parent.Id());
    auto collider = child.AddComponent<ColliderComponent>();
    auto revision = state->PhysicsRevision();
    camera.GetComponent<TransformComponent>()->SetLocalPosition({1, 2, 3});
    camera.SetName("Renamed camera");
    parent.SetName("Renamed support parent");
    CHECK(state->PhysicsRevision() == revision);
    parent.GetComponent<TransformComponent>()->SetLocalPosition({0, .1F, 0});
    CHECK(state->PhysicsRevision() > revision);
    revision = state->PhysicsRevision();
    (void)child.GetComponent<TransformComponent>()->WorldMatrix();
    state->MarkSaved();
    CHECK(state->PhysicsRevision() == revision);
    collider->SetEnabled(false);
    CHECK(state->PhysicsRevision() > revision);
    revision = state->PhysicsRevision();
    child.SetParent(camera, false);
    CHECK(state->PhysicsRevision() > revision);
    revision = state->PhysicsRevision();
    parent.GetComponent<TransformComponent>()->SetLocalPosition({0, .2F, 0});
    CHECK(state->PhysicsRevision() == revision);
    camera.GetComponent<TransformComponent>()->SetLocalPosition({1, 3, 3});
    CHECK(state->PhysicsRevision() > revision);
    state->Close();
}

TEST_CASE("Authored physics queries preserve pending controllers dynamics and reject invalid readers before mutation")
{
    using namespace Keire;
    QueryPhaseFixture fixture;
    fixture.Scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto support = fixture.Scene->CreateEntity("Support");
    support.AddComponent<ColliderComponent>()->SetHalfExtent({1, .25F, 1});
    auto motorEntity = fixture.Scene->CreateEntity("Controller");
    motorEntity.GetComponent<TransformComponent>()->SetLocalPosition({5, 3, 0});
    (void)motorEntity.AddComponent<CharacterControllerComponent>();
    auto dynamic = fixture.Scene->CreateEntity("Dynamic");
    dynamic.GetComponent<TransformComponent>()->SetLocalPosition({10, 3, 0});
    (void)dynamic.AddComponent<ColliderComponent>();
    auto body = dynamic.AddComponent<RigidBodyComponent>();
    body->SetMotion(PhysicsMotionType::Dynamic);
    body->SetUseGravity(false);
    body->SetLinearVelocity({0, 0, 1});
    auto child = fixture.Scene->CreateEntity("Authored child", dynamic);
    child.GetComponent<TransformComponent>()->SetLocalPosition({2, 0, 0});
    (void)child.AddComponent<ColliderComponent>();
    PhysicsSystemSpecification specification;
    specification.Mode = PhysicsMode::Enabled;
    fixture.Physics = CreateRef<PhysicsSystem>(specification);
    fixture.Session =
        CreateRef<SceneRuntimeSession>(fixture.Scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, fixture.Physics);
    fixture.Session->Play();
    auto runtimeSupport = fixture.Session->RuntimeScene()->FindEntity(support.Id());
    const auto supportTransform = runtimeSupport.GetComponent<TransformComponent>();
    const PhysicsRayQuery ray{.Origin = {0, 3, 0}, .Direction = {0, -1, 0}, .MaximumDistance = 5};
    const auto installed = fixture.Session->RayCast(ray).front().Hit.Body;
    supportTransform->SetLocalPosition({0, .2F, 0});
    std::atomic<bool> rejected = false;
    std::thread reader(
        [&]
        {
            try
            {
                (void)fixture.Session->RayCast(ray);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    reader.join();
    CHECK(rejected.load());
    CHECK(fixture.Session->Physics()->TryGetBody(installed)->Position.Y == 0);
    auto invalid = ray;
    invalid.Direction = {};
    CHECK_THROWS_AS((void)fixture.Session->RayCast(invalid), std::invalid_argument);
    CHECK_THROWS_AS((void)fixture.Session->CastCapsule({.Radius = -1}), std::invalid_argument);
    CHECK_THROWS_AS((void)fixture.Session->OverlapSphere({.Radius = -1}), std::invalid_argument);
    CHECK(fixture.Session->Physics()->TryGetBody(installed)->Position.Y == 0);
    const auto motor =
        fixture.Session->RuntimeScene()->FindEntity(motorEntity.Id()).GetComponent<CharacterControllerComponent>();
    REQUIRE(motor->QueueDesiredMovement({0, 0, .1F}));
    const auto dynamicHit = fixture.Session->RayCast({.Origin = {10, 6, 0}, .Direction = {0, -1, 0}}).front();
    const auto before = fixture.Session->Physics()->TryGetBody(dynamicHit.Hit.Body);
    REQUIRE(before);
    for (int repeat = 0; repeat < 20; ++repeat)
    {
        CHECK(fixture.Session->RayCast(ray).front().Hit.Position.Y == doctest::Approx(.45F));
        CHECK_FALSE(fixture.Session->OverlapSphere({.Center = {0, .45F, 0}, .Radius = .1F}).empty());
    }
    CHECK(motor->ConsumeDesiredMovement() == Vector3{0, 0, .1F});
    const auto after = fixture.Session->Physics()->TryGetBody(dynamicHit.Hit.Body);
    REQUIRE(after);
    CHECK(after->Position == before->Position);
    CHECK(after->LinearVelocity == before->LinearVelocity);
    auto runtimeDynamic = fixture.Session->RuntimeScene()->FindEntity(dynamic.Id());
    runtimeDynamic.GetComponent<TransformComponent>()->SetLocalPosition({20, 3, 0});
    const auto childHits = fixture.Session->RayCast({.Origin = {22, 6, 0}, .Direction = {0, -1, 0}});
    REQUIRE(childHits.size() == 1);
    CHECK(childHits.front().Entity == child.Id());
    CHECK(fixture.Session->Physics()->TryGetBody(dynamicHit.Hit.Body)->Position == before->Position);

    auto collider = runtimeSupport.GetComponent<ColliderComponent>();
    collider->SetEnabled(false);
    CHECK(fixture.Session->RayCast(ray).empty());
    collider->SetEnabled(true);
    REQUIRE_FALSE(fixture.Session->RayCast(ray).empty());
    const auto validBody = fixture.Session->RayCast(ray).front().Hit.Body;
    collider->SetShape(ColliderShape::TriangleMesh); // Missing asset: rejected installation preserves old native body.
    CHECK_THROWS((void)fixture.Session->RayCast(ray));
    REQUIRE(fixture.Session->Physics()->TryGetBody(validBody));
    collider->SetShape(ColliderShape::Box);
    CHECK(fixture.Session->RayCast(ray).front().Hit.Body == validBody);
    collider->SetHalfExtent({1, .5F, 1});
    CHECK(fixture.Session->RayCast(ray).front().Hit.Position.Y == doctest::Approx(.7F));
    CHECK_FALSE(fixture.Session->Physics()->TryGetBody(validBody));
    runtimeSupport.SetActive(false);
    CHECK(fixture.Session->RayCast(ray).empty());
}

TEST_CASE("Authored physics queries remove inactive simulation bodies and honor disabled component fallbacks")
{
    using namespace Keire;
    QueryPhaseFixture fixture;
    fixture.Scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto controller = fixture.Scene->CreateEntity("Controller with collider");
    controller.GetComponent<TransformComponent>()->SetLocalPosition({0, 2, 0});
    controller.AddComponent<ColliderComponent>()->SetHalfExtent({.4F, .25F, .4F});
    (void)controller.AddComponent<CharacterControllerComponent>();
    auto directController = fixture.Scene->CreateEntity("Controller only");
    directController.GetComponent<TransformComponent>()->SetLocalPosition({3, 2, 0});
    (void)directController.AddComponent<CharacterControllerComponent>();
    auto dynamic = fixture.Scene->CreateEntity("Dynamic");
    dynamic.GetComponent<TransformComponent>()->SetLocalPosition({6, 2, 0});
    (void)dynamic.AddComponent<ColliderComponent>();
    auto rigid = dynamic.AddComponent<RigidBodyComponent>();
    rigid->SetMotion(PhysicsMotionType::Dynamic);
    rigid->SetUseGravity(false);
    rigid->SetLinearVelocity({0, 0, .5F});
    PhysicsSystemSpecification specification;
    specification.Mode = PhysicsMode::Enabled;
    fixture.Physics = CreateRef<PhysicsSystem>(specification);
    fixture.Session =
        CreateRef<SceneRuntimeSession>(fixture.Scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, fixture.Physics);
    fixture.Session->Play();
    const auto ray = [&](float x)
    { return fixture.Session->RayCast({.Origin = {x, 5, 0}, .Direction = {0, -1, 0}, .MaximumDistance = 5}); };
    auto runtimeController = fixture.Session->RuntimeScene()->FindEntity(controller.Id());
    const auto controllerBody = ray(0).front().Hit.Body;
    runtimeController.GetComponent<CharacterControllerComponent>()->SetEnabled(false);
    const auto fallback = ray(0);
    REQUIRE(fallback.size() == 1);
    CHECK(fallback.front().Hit.Position.Y == doctest::Approx(2.25F));
    CHECK(fallback.front().Hit.Body != controllerBody);
    runtimeController.SetActive(false);
    CHECK(ray(0).empty());
    runtimeController.SetActive(true);
    CHECK_FALSE(ray(0).empty());
    auto only = fixture.Session->RuntimeScene()->FindEntity(directController.Id());
    REQUIRE_FALSE(ray(3).empty());
    only.GetComponent<CharacterControllerComponent>()->SetEnabled(false);
    CHECK(ray(3).empty());

    auto runtimeDynamic = fixture.Session->RuntimeScene()->FindEntity(dynamic.Id());
    auto runtimeRigid = runtimeDynamic.GetComponent<RigidBodyComponent>();
    auto dynamicCollider = runtimeDynamic.GetComponent<ColliderComponent>();
    REQUIRE_FALSE(ray(6).empty());
    runtimeDynamic.SetActive(false);
    CHECK(ray(6).empty());
    runtimeDynamic.SetActive(true);
    fixture.Session->FixedUpdate(1.0F / 60);
    REQUIRE_FALSE(ray(6).empty());
    dynamicCollider->SetEnabled(false);
    CHECK(ray(6).empty());
    dynamicCollider->SetEnabled(true);
    fixture.Session->FixedUpdate(1.0F / 60);
    const auto movingBody = ray(6).front().Hit.Body;
    const auto retainedVelocity = runtimeRigid->LinearVelocity();
    runtimeRigid->SetEnabled(false);
    const auto staticBody = ray(6).front().Hit.Body;
    CHECK(staticBody != movingBody);
    const auto staticPose = fixture.Session->Physics()->TryGetBody(staticBody);
    REQUIRE(staticPose);
    fixture.Session->FixedUpdate(1.0F / 60);
    CHECK(fixture.Session->Physics()->TryGetBody(staticBody)->Position == staticPose->Position);
    CHECK(runtimeRigid->LinearVelocity() == retainedVelocity);
    runtimeRigid->SetEnabled(true);
    CHECK(ray(6).front().Hit.Body == staticBody); // Enabling simulation resumes at the next fixed boundary.
    CHECK(fixture.Session->Physics()->TryGetBody(staticBody)->Position == staticPose->Position);
    CHECK(runtimeRigid->LinearVelocity() == retainedVelocity);

    // Compare identical integration, including backend damping, rather than requiring undamped velocity.
    auto control = fixture.Session->RuntimeScene()->CreateEntity("Matched dynamic integration control");
    control.GetComponent<TransformComponent>()->SetLocalPosition({20, 2, 0});
    (void)control.AddComponent<ColliderComponent>();
    auto controlRigid = control.AddComponent<RigidBodyComponent>();
    controlRigid->SetMotion(PhysicsMotionType::Dynamic);
    controlRigid->SetUseGravity(false);
    controlRigid->SetLinearVelocity(retainedVelocity);
    fixture.Session->FixedUpdate(1.0F / 60);
    const auto controlHits = ray(20);
    REQUIRE(controlHits.size() == 1);
    const auto controlPose = fixture.Session->Physics()->TryGetBody(controlHits.front().Hit.Body);
    REQUIRE(controlPose);
    const auto resumed = ray(6).front().Hit.Body;
    CHECK(resumed != staticBody);
    const auto resumedPose = fixture.Session->Physics()->TryGetBody(resumed);
    REQUIRE(resumedPose);
    CHECK(resumedPose->LinearVelocity == controlPose->LinearVelocity);
    CHECK(runtimeRigid->LinearVelocity() == controlRigid->LinearVelocity());
    CHECK(resumedPose->Position.Z > staticPose->Position.Z);
}
