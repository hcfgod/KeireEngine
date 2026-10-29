#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterGrounding.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Scripting/ManagedRuntimePhysics.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("physics rigid body motion settings serialize with legacy defaults and atomic rejection")
{
    const auto registration = Keire::CreateRigidBodyComponentRegistration();
    Keire::RigidBodyComponent body;
    const Keire::PhysicsMotionSettings settings{0.0F, 0.0F, 20000.0F, 100.0F};
    body.SetMotionSettings(settings);
    body.SetMass(4.0F);
    const auto saved = registration.Serialize(body);
    Keire::RigidBodyComponent restored;
    registration.Deserialize(restored, saved, 1);
    CHECK(restored.MotionSettings() == settings);
    CHECK(restored.Mass() == 4.0F);
    auto invalid = saved;
    invalid["mass"] = 10.0;
    invalid["maximumLinearSpeed"] = -1.0;
    CHECK_THROWS_AS(registration.Deserialize(restored, invalid, 1), std::invalid_argument);
    CHECK(restored.MotionSettings() == settings);
    CHECK(restored.Mass() == 4.0F);
    invalid = saved;
    invalid["angularDamping"] = std::string("invalid");
    CHECK_THROWS_AS(registration.Deserialize(restored, invalid, 1), std::invalid_argument);
    CHECK(restored.MotionSettings() == settings);
    auto legacy = saved;
    legacy.erase("linearDamping");
    legacy.erase("angularDamping");
    legacy.erase("maximumLinearSpeed");
    legacy.erase("maximumAngularSpeed");
    registration.Deserialize(restored, legacy, 1);
    CHECK(restored.MotionSettings() == Keire::PhysicsMotionSettings{});
    CHECK_THROWS_AS(restored.SetMotionSettings({-1.0F}), std::invalid_argument);
    CHECK(restored.MotionSettings() == Keire::PhysicsMotionSettings{});
}

TEST_CASE("physics authored velocity and gravity changes preserve live body identity and spin")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Spinning body");
    (void)entity.AddComponent<Keire::ColliderComponent>();
    entity.AddComponent<Keire::RigidBodyComponent>()->SetUseGravity(false);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto world = session->Physics();
    REQUIRE(world);
    const auto hits = world->RayCast({.Origin = {0.0F, 5.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}});
    REQUIRE(hits.size() == 1);
    const auto bodyId = hits.front().Body;
    auto state = world->TryGetBody(bodyId);
    REQUIRE(state);
    state->AngularVelocity = {0.0F, 2.0F, 0.0F};
    world->SetBodyState(bodyId, *state);
    const auto authored = session->RuntimeScene()->FindEntity(entity.Id()).GetComponent<Keire::RigidBodyComponent>();
    for (int tick = 0; tick < 20; ++tick)
    {
        // Raise the limit and velocity together: settings must reach the native body first.
        authored->SetMotionSettings({0.0F, 0.0F, static_cast<float>((tick + 1) * 1000), 100.0F});
        authored->SetLinearVelocity({static_cast<float>((tick + 1) * 600), 0.0F, 0.0F});
        authored->SetUseGravity(tick % 2 == 0);
        session->FixedUpdate(0.01F);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        state = world->TryGetBody(bodyId);
        REQUIRE(state);
        CHECK(state->AngularVelocity.Y > 1.9F);
        CHECK(state->LinearVelocity.X == doctest::Approx(static_cast<float>((tick + 1) * 600)));
        if (tick % 2 == 0)
        {
            CHECK(state->LinearVelocity.Y < -0.09F);
        }
        else
        {
            CHECK(state->LinearVelocity.Y == 0.0F);
        }
    }
}

TEST_CASE("physics checkpoints restore installed motion settings before velocity and synchronization")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Checkpoint traveler");
    (void)entity.AddComponent<Keire::ColliderComponent>();
    entity.AddComponent<Keire::RigidBodyComponent>()->SetUseGravity(false);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto world = session->Physics();
    const auto hits = world->RayCast({.Origin = {0, 5, 0}, .Direction = {0, -1, 0}});
    REQUIRE(hits.size() == 1);
    const auto id = hits.front().Body;
    const Keire::PhysicsMotionSettings settings{0, 0, 20000, 100};
    world->SetMotionSettings(id, settings);
    auto state = world->TryGetBody(id);
    REQUIRE(state);
    state->LinearVelocity = {1000, 0, 0};
    world->SetBodyState(id, *state);
    const auto saved = session->CapturePhysicsWorldCheckpoint();
    REQUIRE(saved.Bodies.size() == 1);
    CHECK(saved.Bodies.front().MotionSettings == settings);
    world->SetMotionSettings(id, {1, 1, 10, 10});
    auto invalid = saved;
    invalid.Bodies.front().MotionSettings->MaximumLinearSpeed = -1;
    CHECK_THROWS(session->RestorePhysicsWorldCheckpoint(invalid));
    CHECK(world->GetMotionSettings(id).MaximumLinearSpeed == 10);
    CHECK(world->TryGetBody(id)->LinearVelocity.X == doctest::Approx(10));
    session->RestorePhysicsWorldCheckpoint(saved);
    CHECK(world->GetMotionSettings(id) == settings);
    CHECK(world->TryGetBody(id)->LinearVelocity.X == doctest::Approx(1000));
    const auto component = session->RuntimeScene()->FindEntity(entity.Id()).GetComponent<Keire::RigidBodyComponent>();
    CHECK(component->MotionSettings() == settings);
    session->FixedUpdate(0.01F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK(world->GetMotionSettings(id) == settings);
    CHECK(world->TryGetBody(id)->LinearVelocity.X == doctest::Approx(1000));
}

TEST_CASE("physics world checkpoints preserve gravity and sleep with atomic validation")
{
    for (const bool withBody : {false, true})
    {
        CAPTURE(withBody);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        if (withBody)
        {
            auto entity = scene->CreateEntity("Checkpoint sleeper");
            (void)entity.AddComponent<Keire::ColliderComponent>();
            (void)entity.AddComponent<Keire::RigidBodyComponent>();
        }
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                          Keire::Ref<Keire::AudioSystem>{}, physics);
        session->Play();
        REQUIRE(session->Physics());
        session->Physics()->SetGravity({});
        const std::vector<Keire::PhysicsGravitySource> sources{{{1000.0, 0.0, 0.0}, 0.0, 10.0}};
        session->Physics()->SetGravitySources(sources);
        for (int tick = 0; tick < 240; ++tick)
        {
            session->FixedUpdate(1.0F / 60.0F);
        }
        const auto checkpoint = session->CapturePhysicsWorldCheckpoint();
        REQUIRE(checkpoint.Gravity);
        CHECK(checkpoint.GravitySources == sources);
        CHECK(*checkpoint.Gravity == Keire::Vector3{});
        CHECK(checkpoint.Bodies.size() == (withBody ? 1U : 0U));
        if (withBody)
        {
            REQUIRE(checkpoint.Bodies.front().Sleeping);
        }
        session->Physics()->SetGravity({9.81F, 0.0F, 0.0F});
        session->Physics()->SetGravitySources({});
        session->RestorePhysicsWorldCheckpoint(checkpoint);
        CHECK(session->Physics()->GravitySources() == sources);
        CHECK(session->Physics()->Gravity() == Keire::Vector3{});
        if (withBody)
        {
            CHECK(session->CapturePhysicsWorldCheckpoint().Bodies.front().Sleeping);
        }
        auto invalid = checkpoint;
        invalid.GravitySources.front().RadiusMetres = 0.0;
        CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::invalid_argument);
        CHECK(session->Physics()->GravitySources() == sources);
        invalid = checkpoint;
        invalid.Gravity = Keire::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::invalid_argument);
        CHECK(session->Physics()->Gravity() == Keire::Vector3{});
        invalid.Gravity.reset();
        CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::runtime_error);
        CHECK(session->Physics()->Gravity() == Keire::Vector3{});
        bool foreignRejected = false;
        std::thread foreign(
            [&]
            {
                try
                {
                    session->RestorePhysicsWorldCheckpoint(checkpoint);
                }
                catch (const std::logic_error&)
                {
                    foreignRejected = true;
                }
            });
        foreign.join();
        CHECK(foreignRejected);
        CHECK(session->Physics()->Gravity() == Keire::Vector3{});
        if (withBody)
        {
            invalid = checkpoint;
            invalid.Gravity = Keire::Vector3{9.81F, 0.0F, 0.0F};
            invalid.Bodies.front().Position.X = std::numeric_limits<float>::infinity();
            CHECK_THROWS(session->RestorePhysicsWorldCheckpoint(invalid));
            CHECK(session->Physics()->Gravity() == Keire::Vector3{});
            CHECK(session->CapturePhysicsWorldCheckpoint().Bodies.front().Sleeping);
        }
        session->Stop();
        session->Play();
        REQUIRE(session->Physics());
        session->RestorePhysicsWorldCheckpoint(checkpoint);
        CHECK(session->Physics()->Gravity() == Keire::Vector3{});
        CHECK(session->Physics()->GravitySources() == sources);
        if (withBody)
        {
            CHECK(session->CapturePhysicsWorldCheckpoint().Bodies.front().Sleeping);
        }
    }
}

TEST_CASE("physics world checkpoints distinguish absent worlds from empty worlds")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene);
    session->Play();
    REQUIRE_FALSE(session->Physics());
    const auto checkpoint = session->CapturePhysicsWorldCheckpoint();
    CHECK_FALSE(checkpoint.Gravity);
    CHECK(checkpoint.Bodies.empty());
    CHECK_NOTHROW(session->RestorePhysicsWorldCheckpoint(checkpoint));
    auto invalid = checkpoint;
    invalid.Gravity = Keire::Vector3{};
    CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::runtime_error);
    invalid.Gravity.reset();
    invalid.Bodies.emplace_back();
    CHECK_THROWS_AS(session->RestorePhysicsWorldCheckpoint(invalid), std::runtime_error);
}

TEST_CASE("physics checkpoint origins convert before mutation and reject incompatible positions")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    for (int index = 0; index < 2; ++index)
    {
        auto entity = scene->CreateEntity("Checkpoint body");
        entity.GetComponent<Keire::TransformComponent>()->SetLocalPosition(
            {static_cast<float>(index) * 4.0F, 2.0F, 0.0F});
        (void)entity.AddComponent<Keire::ColliderComponent>();
    }
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    const auto original = session->CapturePhysicsCheckpoint();
    REQUIRE(original.size() == 2);
    auto shifted = original;
    for (auto& body : shifted)
    {
        CHECK(body.WorldOrigin == Keire::Vector3d{});
        body.WorldOrigin = {128.0, 0.0, 0.0};
        body.Position.X -= 128.0F;
    }
    session->RestorePhysicsCheckpoint(shifted);
    const auto restored = session->CapturePhysicsCheckpoint();
    REQUIRE(restored.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        CHECK(restored[index].Position == original[index].Position);
    }
    shifted.front().Position.X += 1.0F;
    SUBCASE("nonfinite origin") { shifted.back().WorldOrigin.X = std::numeric_limits<double>::infinity(); }
    SUBCASE("unrepresentable relative position") { shifted.back().WorldOrigin.X = 1e15 + 0.25; }
    SUBCASE("fractional offset lost during double addition")
    {
        shifted.back().WorldOrigin.X = std::ldexp(1.0, 60);
        shifted.back().Position.X = 0.015625F;
    }
    CHECK_THROWS(session->RestorePhysicsCheckpoint(shifted));
    const auto unchanged = session->CapturePhysicsCheckpoint();
    REQUIRE(unchanged.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        CHECK(unchanged[index].Position == original[index].Position);
    }
}

TEST_CASE("physics preserves local body offsets when distant parent translations cancel")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto parent = scene->CreateEntity("Distant parent");
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({1e12, 0.0, 0.0});
    auto entity = scene->CreateEntity("Body", parent);
    entity.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({-1e12 + 0.25, 2.0, 0.0});
    entity.AddComponent<Keire::ColliderComponent>();
    const auto rigid = entity.AddComponent<Keire::RigidBodyComponent>();
    rigid->SetMotion(Keire::PhysicsMotionType::Dynamic);
    rigid->SetUseGravity(false);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    session->FixedUpdate(1.0F / 60.0F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    const auto transform = session->RuntimeScene()->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>();
    CHECK(transform->WorldPositionPrecise() == Keire::Vector3d{0.25, 2.0, 0.0});
    CHECK(transform->LocalPositionPrecise() == Keire::Vector3d{-1e12 + 0.25, 2.0, 0.0});
    const auto checkpoint = session->CapturePhysicsCheckpoint();
    REQUIRE(checkpoint.size() == 1);
    CHECK(checkpoint.front().Position == Keire::Vector3{0.25F, 2.0F, 0.0F});
    const Keire::ScenePhysicsRayQueryPrecise query{
        .Origin = {0.25, 5.0, 0.0}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 10.0F};
    const auto hits = session->RayCastPrecise(query);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().Entity == entity.Id());
    CHECK(hits.front().Position.X == 0.25);
    CHECK(hits.front().Position.Y == doctest::Approx(2.5));
    CHECK(hits.front().Distance == doctest::Approx(2.5F));
    CHECK(session->RayCastPrecise(query, entity.Id()).empty());
    const Keire::ScenePhysicsCapsuleCastQueryPrecise capsule{
        .Origin = {0.25, 5.0, 0.0}, .Radius = 0.25F, .Height = 1.0F, .Displacement = {0.0F, -4.0F, 0.0F}};
    const auto capsuleHit = session->CastCapsulePrecise(capsule);
    REQUIRE(capsuleHit);
    CHECK(capsuleHit->Entity == entity.Id());
    CHECK(capsuleHit->Position.Y == doctest::Approx(2.5).epsilon(0.001));
    CHECK_FALSE(session->CastCapsulePrecise(capsule, entity.Id()));
    const Keire::ScenePhysicsSphereOverlapQueryPrecise overlap{.Center = {0.25, 2.0, 0.0}, .Radius = 0.25F};
    const auto overlapping = session->OverlapSpherePrecise(overlap);
    REQUIRE(overlapping.size() == 1);
    CHECK(overlapping.front() == entity.Id());
    CHECK(session->OverlapSpherePrecise(overlap, entity.Id()).empty());
    auto invalidCapsule = capsule;
    invalidCapsule.Radius = 0.0F;
    CHECK_THROWS_AS((void)session->CastCapsulePrecise(invalidCapsule), std::invalid_argument);
    auto invalidOverlap = overlap;
    invalidOverlap.Center.X = 16777217.0;
    CHECK_THROWS_AS((void)session->OverlapSpherePrecise(invalidOverlap), std::out_of_range);
    auto invalid = query;
    invalid.Origin.X = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)session->RayCastPrecise(invalid), std::invalid_argument);
    invalid = query;
    invalid.Direction = {std::numeric_limits<float>::max(), 0.0F, 0.0F};
    CHECK_THROWS_AS((void)session->RayCastPrecise(invalid), std::invalid_argument);
    invalid = query;
    invalid.Origin.X = 16777217.0;
    CHECK_THROWS_AS((void)session->RayCastPrecise(invalid), std::out_of_range);
}

TEST_CASE("character vertical grounding stops instead of creating downhill motion")
{
    constexpr Keire::Vector3 downward{0.0F, -0.5F, 0.0F};
    const Keire::Vector3 slopeNormal{0.0F, 0.8F, 0.6F};

    const auto sliding = Keire::Detail::ResolveCharacterCollisionRemainder(downward, slopeNormal, true);
    CHECK(sliding.Z == doctest::Approx(0.24F));
    CHECK(sliding.Y == doctest::Approx(-0.18F));
    CHECK(Keire::Detail::ResolveCharacterCollisionRemainder(downward, slopeNormal, false) == Keire::Vector3{});
}

TEST_CASE("character grounding and support carry retain offsets under distant cancelling parents")
{
    double worldOffset = 0.0;
    SUBCASE("near-origin physics region") {}
    SUBCASE("distant physics region") { worldOffset = 1.0e12; }
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto parent = scene->CreateEntity("Distant reference");
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({1e12 + worldOffset, 0.0, 0.0});
    auto platform = scene->CreateEntity("Support", parent);
    platform.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({-1e12 + 0.25, 0.0, 0.0});
    platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, 0.2F, 2.0F});
    auto rider = scene->CreateEntity("Rider", parent);
    rider.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({-1e12 + 0.25, 1.12, 0.0});
    rider.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    const auto runtimeRider = session->RuntimeScene()->FindEntity(rider.Id());
    const auto motor = runtimeRider.GetComponent<Keire::CharacterControllerComponent>();
    const auto transform = runtimeRider.GetComponent<Keire::TransformComponent>();
    for (int tick = 0; tick < 6; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(1.0F / 60.0F);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    }
    REQUIRE(motor->Grounded());
    CHECK(transform->WorldPositionPrecise().X == worldOffset + 0.25);
    const auto beforeRebase = transform->WorldPositionPrecise();
    session->RebasePhysicsOrigin({worldOffset + 128.0, 64.0, -128.0});
    CHECK(transform->WorldPositionPrecise() == beforeRebase);
    CHECK(motor->Grounded());
    const auto support = session->RuntimeScene()->FindEntity(platform.Id()).GetComponent<Keire::TransformComponent>();
    support->SetLocalPositionPrecise({-1e12 + 0.75, 0.0, 0.0});
    session->FixedUpdate(1.0F / 60.0F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK(motor->Grounded());
    CHECK(std::abs(transform->WorldPositionPrecise().X - (worldOffset + 0.75)) < 0.001);
}

TEST_CASE("ground snap follows walkable descents without catching jumps or walls")
{
    constexpr float minimumWalkableNormal = 0.6F;
    CHECK(Keire::Detail::ShouldSnapCharacterToGround(true, -0.05F, {0.0F, 0.8F, 0.6F}, minimumWalkableNormal));
    CHECK_FALSE(Keire::Detail::ShouldSnapCharacterToGround(true, 0.05F, {0.0F, 1.0F, 0.0F}, minimumWalkableNormal));
    CHECK_FALSE(Keire::Detail::ShouldSnapCharacterToGround(false, -0.05F, {0.0F, 1.0F, 0.0F}, minimumWalkableNormal));
    CHECK_FALSE(Keire::Detail::ShouldSnapCharacterToGround(true, -0.05F, {0.8F, 0.2F, 0.0F}, minimumWalkableNormal));
}

TEST_CASE("character grounding tolerates brief slope contact gaps without hiding jumps")
{
    std::uint32_t missedWalkableFrames = 2;
    CHECK(Keire::Detail::ResolveCharacterGrounded(true, false, -0.1F, missedWalkableFrames));
    CHECK(missedWalkableFrames == 0);

    CHECK(Keire::Detail::ResolveCharacterGrounded(false, true, -0.1F, missedWalkableFrames));
    CHECK(Keire::Detail::ResolveCharacterGrounded(false, true, -0.1F, missedWalkableFrames));
    CHECK(Keire::Detail::ResolveCharacterGrounded(false, true, -0.1F, missedWalkableFrames));
    CHECK_FALSE(Keire::Detail::ResolveCharacterGrounded(false, true, -0.1F, missedWalkableFrames));
    CHECK(missedWalkableFrames == 0);

    CHECK_FALSE(Keire::Detail::ResolveCharacterGrounded(false, true, 0.1F, missedWalkableFrames));
    CHECK(missedWalkableFrames == 0);
}

TEST_CASE("default component registry exposes production physics components")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    CHECK(registry->Contains(Keire::ColliderComponent::StaticType()));
    CHECK(registry->Contains(Keire::RigidBodyComponent::StaticType()));

    const auto rigidBody = registry->Find(Keire::RigidBodyComponent::StaticType());
    REQUIRE(rigidBody.has_value());
    REQUIRE(rigidBody->RequiredComponents.size() == 1);
    CHECK(rigidBody->RequiredComponents.front() == Keire::ColliderComponent::StaticType());
}

TEST_CASE("collider registration round trips production query settings")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registration = registry->Find(Keire::ColliderComponent::StaticType());
    REQUIRE(registration.has_value());
    const auto source = registration->Factory();
    auto& collider = dynamic_cast<Keire::ColliderComponent&>(*source);
    collider.SetShape(Keire::ColliderShape::Capsule);
    collider.SetCenter({1.0F, 2.0F, 3.0F});
    collider.SetRadius(0.75F);
    collider.SetHeight(2.5F);
    collider.SetLayer(8);
    collider.SetMask(0x00FF00FFU);
    collider.SetTrigger(true);
    const auto collisionMesh = Keire::AssetId::Parse("0f9b5088-1332-4c50-b4fb-8aa574633f21");
    const auto physicsMaterial = Keire::AssetId::Parse("ce4ad487-8d66-4dd2-895f-bec0c60be731");
    collider.SetCollisionMesh(collisionMesh);
    collider.SetPhysicsMaterial(physicsMaterial);

    const auto values = registration->Serialize(*source);
    const auto target = registration->Factory();
    registration->Deserialize(*target, values, registration->SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::ColliderComponent&>(*target);
    CHECK(restored.Shape() == Keire::ColliderShape::Capsule);
    CHECK(restored.Center() == Keire::Vector3{1.0F, 2.0F, 3.0F});
    CHECK(restored.Radius() == doctest::Approx(0.75F));
    CHECK(restored.Height() == doctest::Approx(2.5F));
    CHECK(restored.Layer() == 8);
    CHECK(restored.Mask() == 0x00FF00FFU);
    CHECK(restored.Trigger());
    CHECK(restored.CollisionMesh() == collisionMesh);
    CHECK(restored.PhysicsMaterial() == physicsMaterial);
    CHECK(registration->SchemaVersion == 2);

    auto legacy = values;
    legacy.erase("collisionMesh");
    legacy.erase("physicsMaterial");
    REQUIRE(registration->Migrate);
    const auto migrated = registration->Migrate(legacy, 1);
    const auto migratedTarget = registration->Factory();
    registration->Deserialize(*migratedTarget, migrated, registration->SchemaVersion);
    const auto& migratedCollider = dynamic_cast<const Keire::ColliderComponent&>(*migratedTarget);
    CHECK_FALSE(migratedCollider.CollisionMesh());
    CHECK_FALSE(migratedCollider.PhysicsMaterial());
    CHECK_THROWS_AS(collider.SetLayer(3), std::invalid_argument);
}

TEST_CASE("rigid body registration retains gravity authoring")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registration = registry->Find(Keire::RigidBodyComponent::StaticType());
    REQUIRE(registration.has_value());
    const auto source = registration->Factory();
    auto& body = dynamic_cast<Keire::RigidBodyComponent&>(*source);
    body.SetUseGravity(false);

    const auto target = registration->Factory();
    registration->Deserialize(*target, registration->Serialize(*source), registration->SchemaVersion);
    CHECK_FALSE(dynamic_cast<const Keire::RigidBodyComponent&>(*target).UseGravity());
}

TEST_CASE("rigid body motion accepts defined modes and rejects invalid values transactionally")
{
    Keire::RigidBodyComponent body;
    for (const auto motion :
         {Keire::PhysicsMotionType::Static, Keire::PhysicsMotionType::Dynamic, Keire::PhysicsMotionType::Kinematic})
    {
        CHECK_NOTHROW(body.SetMotion(motion));
        CHECK(body.Motion() == motion);
    }

    CHECK_THROWS_AS(body.SetMotion(static_cast<Keire::PhysicsMotionType>(255)), std::invalid_argument);
    CHECK(body.Motion() == Keire::PhysicsMotionType::Kinematic);

    const auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Motion"));
    auto entity = scene->CreateEntity("Body");
    REQUIRE(entity.AddComponent<Keire::ColliderComponent>());
    const auto attached = entity.AddComponent<Keire::RigidBodyComponent>();
    REQUIRE(attached);
    attached->SetMotion(Keire::PhysicsMotionType::Dynamic);
    scene->MarkSaved();
    REQUIRE_FALSE(scene->Dirty());

    CHECK_THROWS_AS(attached->SetMotion(static_cast<Keire::PhysicsMotionType>(255)), std::invalid_argument);
    CHECK(attached->Motion() == Keire::PhysicsMotionType::Dynamic);
    CHECK_FALSE(scene->Dirty());
}

TEST_CASE("character local up follows rotated landing walking and jumping at distant origins")
{
    for (const double offset : {0.0, 1.0e12})
    {
        for (const Keire::Vector3 angles :
             {Keire::Vector3{}, Keire::Vector3{0.0F, 0.0F, 90.0F}, Keire::Vector3{0.0F, 0.0F, 180.0F},
              Keire::Vector3{90.0F, 0.0F, 0.0F}, Keire::Vector3{31.0F, 22.0F, 45.0F}})
        {
            CAPTURE(offset);
            CAPTURE(angles.X);
            CAPTURE(angles.Y);
            CAPTURE(angles.Z);
            auto scene =
                Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
            auto frame = scene->CreateEntity("Planet frame");
            frame.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({offset, offset, -offset});
            frame.GetComponent<Keire::TransformComponent>()->SetLocalEulerAngles(angles);
            auto floor = scene->CreateEntity("Rotated floor", frame);
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
            floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.5F, 5.0F});
            auto player = scene->CreateEntity("Rotated player", frame);
            player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.5F, 0.0F});
            player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
            Keire::PhysicsSystemSpecification specification;
            specification.Mode = Keire::PhysicsMode::Enabled;
            const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
            const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
                scene, Keire::Ref<Keire::AssetSystem>{}, Keire::Ref<Keire::AudioSystem>{}, physics);
            session->Play();
            const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
            const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
            const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
            const auto rotation =
                Keire::Math::ComposeTransform({}, Keire::Math::EulerDegreesToQuaternion(angles), {1.0F, 1.0F, 1.0F});
            const auto move = [&](const Keire::Vector3 localMovement)
            {
                REQUIRE(motor->QueueDesiredMovement(Keire::Math::TransformDirection(rotation, localMovement)));
                session->FixedUpdate(1.0F / 60.0F);
                REQUIRE(session->State() == Keire::ScenePlayState::Playing);
            };
            for (int tick = 0; tick < 30; ++tick)
            {
                move({0.0F, -0.04F, 0.0F});
            }
            CHECK(motor->Grounded());
            CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
            const auto up = Keire::Math::TransformDirection(rotation, {0.0F, 1.0F, 0.0F});
            const auto normal = motor->RuntimeState().GroundNormal;
            CHECK(normal.X * up.X + normal.Y * up.Y + normal.Z * up.Z > 0.99F);
            move({0.0F, -0.04F, 0.1F});
            CHECK(transform->LocalPosition().Z == doctest::Approx(0.1F).epsilon(0.005));
            move({0.0F, 0.1F, 0.0F});
            CHECK_FALSE(motor->Grounded());
            CHECK(transform->LocalPosition().Y > 0.99F);
        }
    }
}

TEST_CASE("character changing up rejects stale support and follows continuously rotated frames")
{
    bool rotateTogether = false;
    SUBCASE("turn away from old support") {}
    SUBCASE("continuous frame rotation") { rotateTogether = true; }
    for (const double offset : {0.0, 1.0e12})
    {
        CAPTURE(offset);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        auto frame = scene->CreateEntity("Changing frame");
        frame.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({offset, offset, -offset});
        auto floor = scene->CreateEntity("Floor", frame);
        floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
        floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.5F, 5.0F});
        auto player = scene->CreateEntity("Player", frame);
        player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.9F, 0.0F});
        player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                          Keire::Ref<Keire::AudioSystem>{}, physics);
        session->Play();
        const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
        const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
        const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
        const auto frameTransform =
            session->RuntimeScene()->FindEntity(frame.Id()).GetComponent<Keire::TransformComponent>();
        for (int tick = 0; tick < 6; ++tick)
        {
            REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
            session->FixedUpdate(1.0F / 60.0F);
        }
        REQUIRE(motor->Grounded());
        if (!rotateTogether)
        {
            transform->SetLocalEulerAngles({0.0F, 0.0F, 90.0F});
            session->FixedUpdate(1.0F / 60.0F);
            REQUIRE(session->State() == Keire::ScenePlayState::Playing);
            CHECK_FALSE(motor->Grounded());
            CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
            transform->SetLocalEulerAngles({});
            session->FixedUpdate(1.0F / 60.0F);
            CHECK(motor->Grounded());
        }
        else
        {
            for (int tick = 1; tick <= 180; ++tick)
            {
                const Keire::Vector3 angles{0.0F, 0.0F, static_cast<float>(tick) * 2.0F};
                frameTransform->SetLocalEulerAngles(angles);
                session->FixedUpdate(1.0F / 60.0F);
                REQUIRE(session->State() == Keire::ScenePlayState::Playing);
                CHECK(motor->Grounded());
                CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
                const auto up = Keire::Math::TransformDirection(
                    Keire::Math::ComposeTransform({}, Keire::Math::EulerDegreesToQuaternion(angles),
                                                  {1.0F, 1.0F, 1.0F}),
                    {0.0F, 1.0F, 0.0F});
                const auto normal = motor->RuntimeState().GroundNormal;
                CHECK(normal.X * up.X + normal.Y * up.Y + normal.Z * up.Z > 0.99F);
            }
        }
    }
}

TEST_CASE("character landing preserves fall distance and allows grounded walking")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Landing"));
    auto floor = scene->CreateEntity("Floor");
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
    floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.5F, 5.0F});
    auto wall = scene->CreateEntity("Wall");
    wall.GetComponent<Keire::TransformComponent>()->SetLocalPosition({4.0F, 2.0F, 0.0F});
    wall.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({0.5F, 2.0F, 5.0F});
    auto player = scene->CreateEntity("Player");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.5F, 0.0F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
    for (int frame = 0; frame < 30; ++frame)
    {
        const auto before = transform->LocalPosition().Y;
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.04F, 0.0F}));
        session->FixedUpdate(1.0F / 60.0F);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        CHECK(transform->LocalPosition().Y >= before - 0.041F);
    }
    CHECK(motor->Grounded());
    CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
    REQUIRE(motor->QueueDesiredMovement({0.0F, -0.04F, 0.1F}));
    session->FixedUpdate(1.0F / 60.0F);
    CHECK(transform->LocalPosition().Z == doctest::Approx(0.1F).epsilon(0.005));
    REQUIRE(motor->QueueDesiredMovement({0.0F, 0.1F, 0.0F}));
    session->FixedUpdate(1.0F / 60.0F);
    CHECK_FALSE(motor->Grounded());
    CHECK(transform->LocalPosition().Y > 0.99F);
    // Repeated ballistic landings must not embed the inset query capsule in the floor.
    for (int jump = 0; jump < 3; ++jump)
    {
        float verticalSpeed = 7.5F;
        for (int frame = 0; frame < 90; ++frame)
        {
            verticalSpeed = motor->Grounded() && verticalSpeed < 0.0F ? -2.0F : verticalSpeed;
            verticalSpeed -= 24.0F / 60.0F;
            const float downward = verticalSpeed / 60.0F;
            const auto before = transform->LocalPosition();
            REQUIRE(motor->QueueDesiredMovement({0.01F, downward, 0.0F}));
            session->FixedUpdate(1.0F / 60.0F);
            REQUIRE(session->State() == Keire::ScenePlayState::Playing);
            CHECK(transform->LocalPosition().X == doctest::Approx(before.X + 0.01F).epsilon(0.001));
            if (downward < 0.0F)
                CHECK(transform->LocalPosition().Y >= before.Y + downward - 0.002F);
        }
        CHECK(motor->Grounded());
        CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
    }
    REQUIRE(motor->QueueDesiredMovement({3.0F, -0.04F, 0.0F}));
    session->FixedUpdate(1.0F / 60.0F);
    CHECK(transform->LocalPosition().X == doctest::Approx(3.15F).epsilon(0.005));
    CHECK(transform->LocalPosition().Y == doctest::Approx(0.9F).epsilon(0.005));
    // Sustained perpendicular input must not accumulate lateral drift along a flat wall.
    const auto contact = transform->LocalPosition();
    for (int frame = 0; frame < 300; ++frame)
    {
        REQUIRE(motor->QueueDesiredMovement({0.1F, -0.04F, 0.0F}));
        session->FixedUpdate(1.0F / 60.0F);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        CHECK(std::abs(transform->LocalPosition().Z - contact.Z) < 0.001F);
        CHECK(std::abs(transform->LocalPosition().X - contact.X) < 0.005F);
    }
    // Intentional oblique input still slides; releasing that input must stop the lateral motion.
    for (int frame = 0; frame < 20; ++frame)
    {
        REQUIRE(motor->QueueDesiredMovement({0.1F, -0.04F, 0.05F}));
        session->FixedUpdate(1.0F / 60.0F);
    }
    const auto stopped = transform->LocalPosition();
    CHECK(stopped.Z > contact.Z + 0.9F);
    for (int frame = 0; frame < 30; ++frame)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.04F, 0.0F}));
        session->FixedUpdate(1.0F / 60.0F);
        CHECK(std::abs(transform->LocalPosition().Z - stopped.Z) < 0.001F);
    }
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character shallow spawn overlap recovers without needing a jump")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Spawn overlap"));
    auto floor = scene->CreateEntity("Landing platform");
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.035F, 0.0F});
    floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.035F, 5.0F});
    auto player = scene->CreateEntity("Player");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.95F, 0.0F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    bool recoveryAllowed = true;
    SUBCASE("contact at the inset capsule boundary") {}
    SUBCASE("slightly deeper overlap")
    {
        player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.94F, 0.0F});
    }
    SUBCASE("a low ceiling prevents upward recovery")
    {
        auto ceiling = scene->CreateEntity("Ceiling");
        ceiling.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.91F, 0.0F});
        ceiling.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.05F, 5.0F});
        recoveryAllowed = false;
    }
    SUBCASE("deeply embedded capsules do not teleport out")
    {
        player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.9F, 0.0F});
        recoveryAllowed = false;
    }
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
    for (int frame = 0; frame < 60; ++frame)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0225F}));
        session->FixedUpdate(1.0F / 60.0F);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    }
    if (recoveryAllowed)
    {
        CHECK(transform->LocalPosition().Z == doctest::Approx(1.35F).epsilon(0.005));
        CHECK(transform->LocalPosition().Y == doctest::Approx(0.97F).epsilon(0.002));
    }
    else
    {
        CHECK(transform->LocalPosition().Z == doctest::Approx(0.0F));
        CHECK(transform->LocalPosition().Y == player.GetComponent<Keire::TransformComponent>()->LocalPosition().Y);
    }
    CHECK(motor->Grounded());
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character presentation keeps current body yaw and camera pitch between physics ticks")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto player = scene->CreateEntity("Player");
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
    auto camera = scene->CreateEntity("Camera", player);
    (void)camera.AddComponent<Keire::CameraComponent>();
    camera.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.65F, 0.0F});
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto body = runtimePlayer.GetComponent<Keire::TransformComponent>();
    const auto head = session->RuntimeScene()->FindEntity(camera.Id()).GetComponent<Keire::TransformComponent>();
    session->FixedUpdate(1.0F / 60.0F);
    const auto previous = body->WorldPosition();
    REQUIRE(motor->QueueDesiredMovement({0.0F, 0.0F, 0.2F}));
    session->FixedUpdate(1.0F / 60.0F);
    const auto current = body->WorldPosition();
    REQUIRE(current.Z > previous.Z);
    for (const float alpha : {0.0F, 0.25F, 0.5F, 1.0F})
    {
        session->Update(1.0F / 144.0F, alpha);
        // Look input arrives after interpolation has been installed, without another physics tick.
        body->SetLocalEulerAngles({0.0F, 35.0F + alpha * 20.0F, 0.0F});
        head->SetLocalEulerAngles({-30.0F + alpha * 10.0F, 0.0F, 0.0F});
        const auto actualBody = body->WorldMatrix();
        const auto renderedBody = body->PresentationWorldMatrix();
        const auto actualHead = head->WorldMatrix();
        const auto renderedHead = head->PresentationWorldMatrix();
        const auto cameraSettings =
            session->RuntimeScene()->FindEntity(camera.Id()).GetComponent<Keire::CameraComponent>();
        const auto editorCamera = Keire::Internal::BuildPresentedSceneCamera(*cameraSettings, *head, 16.0F / 9.0F);
        const auto editorCameraWorld = Keire::Math::Inverse(editorCamera.View);
        const auto relativeHead = head->PresentationRelativeMatrix(editorCamera.WorldOrigin, 0.0);
        CHECK(editorCamera.WorldOrigin == head->PresentationWorldPositionPrecise());
        for (int index = 0; index < 16; ++index)
        {
            CHECK(editorCameraWorld.Elements[index] == doctest::Approx(relativeHead.Elements[index]).epsilon(0.0001));
        }
        for (int index = 0; index < 12; ++index)
        {
            CHECK(renderedBody.Elements[index] == doctest::Approx(actualBody.Elements[index]));
            CHECK(renderedHead.Elements[index] == doctest::Approx(actualHead.Elements[index]));
        }
        CHECK(body->PresentationWorldPosition().Z == doctest::Approx(previous.Z + (current.Z - previous.Z) * alpha));
        CHECK(head->PresentationWorldPosition().Y == doctest::Approx(body->PresentationWorldPosition().Y + 0.65F));
        CHECK(body->WorldPosition().Z == doctest::Approx(current.Z));
    }
    body->SetWorldPosition({10.0F, 3.0F, 5.0F});
    body->ResetPresentationInterpolation();
    session->Update(1.0F / 144.0F, 0.0F);
    CHECK(body->PresentationWorldPosition().X == doctest::Approx(10.0F));
    CHECK(head->PresentationWorldPosition().Y == doctest::Approx(3.65F));
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("turning a character preserves its collision body across physics ticks")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto player = scene->CreateEntity("Player");
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto body = runtimePlayer.GetComponent<Keire::TransformComponent>();
    session->FixedUpdate(1.0F / 60.0F);
    const auto generation = motor->RuntimeState().Generation;
    REQUIRE(generation != 0U);
    for (int tick = 1; tick <= 360; ++tick)
    {
        body->SetLocalEulerAngles({0.0F, static_cast<float>(tick) * 0.37F, 0.0F});
        REQUIRE(motor->QueueDesiredMovement({0.0F, 0.0F, 0.002F}));
        session->FixedUpdate(1.0F / 60.0F);
        CAPTURE(tick);
        REQUIRE(motor->RuntimeState().Generation == generation);
        CHECK(body->WorldPosition().Z == doctest::Approx(static_cast<float>(tick) * 0.002F).epsilon(0.001));
    }
    // Sub-tolerance edits must accumulate against the installed shape, not be forgotten each tick.
    for (int tick = 1; tick <= 100; ++tick)
    {
        const float scale = 1.0F + static_cast<float>(tick) * 0.0000001F;
        body->SetLocalScale({scale, scale, scale});
        session->FixedUpdate(1.0F / 60.0F);
    }
    CHECK(motor->RuntimeState().Generation > generation);
    const auto resizedGeneration = motor->RuntimeState().Generation;
    // Actual authored resizes still rebuild the shape.
    body->SetLocalScale({1.1F, 1.1F, 1.1F});
    session->FixedUpdate(1.0F / 60.0F);
    CHECK(motor->RuntimeState().Generation > resizedGeneration);
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character grounding uses current authored moving support geometry")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Moving support"));
    auto platform = scene->CreateEntity("Lift");
    platform.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.2F, 0.0F});
    platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, 0.2F, 2.0F});
    SUBCASE("script moved static collider") {}
    SUBCASE("authored kinematic collider")
    {
        platform.AddComponent<Keire::RigidBodyComponent>()->SetMotion(Keire::PhysicsMotionType::Kinematic);
    }
    auto player = scene->CreateEntity("Player");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.32F, 0.0F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlatform = session->RuntimeScene()->FindEntity(platform.Id());
    const auto support = runtimePlatform.GetComponent<Keire::TransformComponent>();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
    constexpr float step = 1.0F / 60.0F;
    int missedGround = 0;
    float maximumHeightError = 0.0F;
    for (int tick = 0; tick < 840; ++tick)
    {
        const float height = 0.2F + 0.3F * std::sin(static_cast<float>(tick + 1) * step * 0.7F);
        support->SetLocalPosition({0.0F, height, 0.0F});
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(step);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        if (tick >= 120)
        {
            missedGround += !motor->Grounded();
            maximumHeightError = std::max(maximumHeightError, std::abs(transform->LocalPosition().Y - height - 1.1F));
        }
    }
    CHECK(missedGround == 0);
    CHECK(maximumHeightError < 0.002F);
    REQUIRE(motor->QueueDesiredMovement({0.0F, 0.1F, 0.0F}));
    session->FixedUpdate(step);
    CHECK_FALSE(motor->Grounded());
    for (int tick = 0; tick < 20; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(step);
    }
    REQUIRE(motor->Grounded());
    // Leaving the finite platform must not retain support beyond the existing grace interval.
    REQUIRE(motor->QueueDesiredMovement({3.0F, -0.03F, 0.0F}));
    session->FixedUpdate(step);
    for (int tick = 0; tick < 5; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(step);
    }
    CHECK_FALSE(motor->Grounded());
    transform->SetLocalPosition({0.0F, support->LocalPosition().Y + 1.1F, 0.0F});
    REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
    session->FixedUpdate(step);
    REQUIRE(motor->Grounded());
    runtimePlatform.GetComponent<Keire::ColliderComponent>()->SetEnabled(false);
    for (int tick = 0; tick < 5; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(step);
    }
    CHECK_FALSE(motor->Grounded());
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character movement refreshes descendant collider queries in the same physics tick")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Character attachments"));
    auto player = scene->CreateEntity("Player");
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    auto child = scene->CreateEntity("Attached sensor");
    child.SetParent(player, false);
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 3.0F, 0.0F});
    const auto collider = child.AddComponent<Keire::ColliderComponent>();
    collider->SetHalfExtent({0.2F, 0.2F, 0.2F});
    SUBCASE("solid child") {}
    SUBCASE("trigger child") { collider->SetTrigger(true); }
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    REQUIRE(motor->QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
    session->FixedUpdate(1.0F / 60.0F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK(runtimePlayer.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(1.0F));
    CHECK(motor->RuntimeState().Velocity.X == doctest::Approx(60.0F));
    const auto atNewPosition =
        session->RayCast({.Origin = {1.0F, 4.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 2.0F});
    CHECK(std::ranges::any_of(atNewPosition, [&](const auto& hit) { return hit.Entity == child.Id(); }));
    const auto atOldPosition =
        session->RayCast({.Origin = {0.0F, 4.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 2.0F});
    CHECK_FALSE(std::ranges::any_of(atOldPosition, [&](const auto& hit) { return hit.Entity == child.Id(); }));
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character traverses authored step height boundary without exceeding it")
{
    struct FrameConfiguration
    {
        double Offset;
        Keire::Vector3 Angles;
    };
    float stepHeight = 0.30F;
    bool shouldClimb = true;
    bool ceiling = false;
    SUBCASE("exact boundary") {}
    SUBCASE("lower step") { stepHeight = 0.26F; }
    SUBCASE("over configured height")
    {
        stepHeight = 0.31F;
        shouldClimb = false;
    }
    SUBCASE("blocked headroom")
    {
        ceiling = true;
        shouldClimb = false;
    }
    for (const float speed : {0.6F, 1.35F, 3.0F})
    {
        for (const float direction : {-1.0F, 1.0F})
            for (const float angle : {0.0F, 45.0F, 90.0F})
                for (const auto configuration :
                     {FrameConfiguration{0.0, {}}, FrameConfiguration{1.0e12, {0.0F, 0.0F, 90.0F}},
                      FrameConfiguration{1.0e12, {31.0F, 22.0F, 45.0F}}})
                {
                    CAPTURE(configuration.Offset);
                    CAPTURE(configuration.Angles.Z);
                    const float x = std::sin(angle * 3.14159265358979323846F / 180.0F) * direction;
                    const float z = std::cos(angle * 3.14159265358979323846F / 180.0F) * direction;
                    const auto progress = [&](const Keire::Vector3 value) { return value.X * x + value.Z * z; };
                    INFO("step=" << stepHeight << " speed=" << speed << " direction=" << direction
                                 << " angle=" << angle);
                    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                                Keire::SceneAsset::EmptyDefinition("Step boundary"));
                    auto frame = scene->CreateEntity("Step frame");
                    frame.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise(
                        {configuration.Offset, configuration.Offset, -configuration.Offset});
                    frame.GetComponent<Keire::TransformComponent>()->SetLocalEulerAngles(configuration.Angles);
                    const auto frameRotation = Keire::Math::ComposeTransform(
                        {}, Keire::Math::EulerDegreesToQuaternion(configuration.Angles), {1.0F, 1.0F, 1.0F});
                    auto floor = scene->CreateEntity("Floor", frame);
                    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
                    floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.5F, 5.0F});
                    auto plinth = scene->CreateEntity("Plinth", frame);
                    plinth.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, stepHeight * .5F, 0.0F});
                    plinth.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({.75F, stepHeight * .5F, .75F});
                    if (ceiling)
                    {
                        auto roof = scene->CreateEntity("Ceiling", frame);
                        roof.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.95F, 0.0F});
                        roof.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, .05F, 2.0F});
                    }
                    auto player = scene->CreateEntity("Player", frame);
                    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.8F * x, .9F, -1.8F * z});
                    auto authoredMotor = player.AddComponent<Keire::CharacterControllerComponent>();
                    authoredMotor->ConfigureCapsule(.28F, 1.8F, .30F, .02F);
                    authoredMotor->SetMaximumSlopeDegrees(50.0F);
                    Keire::PhysicsSystemSpecification specification;
                    specification.Mode = Keire::PhysicsMode::Enabled;
                    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
                    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
                        scene, Keire::Ref<Keire::AssetSystem>{}, Keire::Ref<Keire::AudioSystem>{}, physics);
                    session->Play();
                    auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
                    auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
                    auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
                    for (int tick = 0; tick < 240; ++tick)
                    {
                        const auto before = transform->LocalPosition();
                        const float forward = tick < 6 ? 0.0F : speed / 60.0F;
                        REQUIRE(motor->QueueDesiredMovement(
                            Keire::Math::TransformDirection(frameRotation, {forward * x, -.03F, forward * z})));
                        session->FixedUpdate(1.0F / 60.0F);
                        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
                        CHECK(std::abs(progress(transform->LocalPosition()) - progress(before)) <=
                              std::abs(forward) + .0001F);
                        CHECK(transform->LocalPosition().Y - before.Y <= .3001F);
                        if (progress(transform->LocalPosition()) > -.2F)
                            break;
                    }
                    const auto position = transform->LocalPosition();
                    INFO("result=" << position.Y << "," << position.Z);
                    if (shouldClimb)
                    {
                        CHECK(progress(position) > -.2F);
                        CHECK(position.Y == doctest::Approx(.9F + stepHeight).epsilon(.003));
                    }
                    else
                        CHECK(progress(position) < -.74F);
                    session->Stop();
                    physics->Close();
                    scene->Close();
                }
    }
}

TEST_CASE("character step following does not create support over a void")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Void"));
    auto floor = scene->CreateEntity("Floor edge");
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -.5F, -1});
    floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2, .5F, 1});
    auto player = scene->CreateEntity("Player");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, .9F, -.5F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(.28F, 1.8F, .3F, .02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
    for (int tick = 0; tick < 90; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0, -.03F, .0225F}));
        session->FixedUpdate(1.0F / 60.0F);
        CHECK(transform->LocalPosition().Y <= .9001F);
        if (transform->LocalPosition().Z > .5F)
            CHECK_FALSE(motor->Grounded());
    }
    CHECK(transform->LocalPosition().Y < .5F);
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character carries an idle rider through rigid authored support motion")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Rigid carry"));
    auto platform = scene->CreateEntity("Support");
    platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, 0.2F, 2.0F});
    SUBCASE("script moved static support") {}
    SUBCASE("script moved kinematic support")
    {
        platform.AddComponent<Keire::RigidBodyComponent>()->SetMotion(Keire::PhysicsMotionType::Kinematic);
    }
    auto player = scene->CreateEntity("Rider");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.95F, 1.12F, 0.7F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto support = session->RuntimeScene()->FindEntity(platform.Id()).GetComponent<Keire::TransformComponent>();
    const auto rider = session->RuntimeScene()->FindEntity(player.Id());
    const auto transform = rider.GetComponent<Keire::TransformComponent>();
    const auto motor = rider.GetComponent<Keire::CharacterControllerComponent>();
    constexpr float dt = 1.0F / 60.0F;
    for (int tick = 0; tick < 6; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(dt);
    }
    REQUIRE(motor->Grounded());
    const auto initial = transform->WorldPosition();
    float maximumError = 0.0F;
    int unsupported = 0;
    for (int tick = 0; tick < 480; ++tick)
    {
        const float phase = static_cast<float>(tick + 1) * dt;
        support->SetLocalPosition(
            {0.3F * std::sin(phase), 0.15F * std::sin(phase * 2.0F), 0.2F * std::sin(phase * 0.7F)});
        support->SetLocalEulerAngles({0.0F, 20.0F * std::sin(phase), 0.0F});
        // No input at all: support carry must not depend on artificial downward movement.
        session->FixedUpdate(dt);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        const auto local =
            Keire::Math::TransformPoint(Keire::Math::Inverse(support->WorldMatrix()), transform->WorldPosition());
        const float dx = local.X - initial.X, dy = local.Y - initial.Y, dz = local.Z - initial.Z;
        maximumError = std::max(maximumError, std::sqrt(dx * dx + dy * dy + dz * dz));
        unsupported += !motor->Grounded();
    }
    CHECK(maximumError < 0.002F);
    CHECK(unsupported == 0);
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character local up preserves idle carry on rotated distant moving platforms")
{
    for (const auto motion : {Keire::PhysicsMotionType::Static, Keire::PhysicsMotionType::Kinematic})
    {
        for (const Keire::Vector3 angles : {Keire::Vector3{0.0F, 0.0F, 90.0F}, Keire::Vector3{0.0F, 0.0F, 180.0F},
                                            Keire::Vector3{31.0F, 22.0F, 45.0F}})
        {
            CAPTURE(static_cast<int>(motion));
            CAPTURE(angles.Z);
            auto scene =
                Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
            auto frame = scene->CreateEntity("Distant support frame");
            frame.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({1.0e12, 1.0e12, -1.0e12});
            frame.GetComponent<Keire::TransformComponent>()->SetLocalEulerAngles(angles);
            auto platform = scene->CreateEntity("Moving support", frame);
            platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, 0.2F, 2.0F});
            if (motion == Keire::PhysicsMotionType::Kinematic)
            {
                platform.AddComponent<Keire::RigidBodyComponent>()->SetMotion(motion);
            }
            auto player = scene->CreateEntity("Rider", frame);
            player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.95F, 1.12F, 0.7F});
            player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
            Keire::PhysicsSystemSpecification specification;
            specification.Mode = Keire::PhysicsMode::Enabled;
            const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
            const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
                scene, Keire::Ref<Keire::AssetSystem>{}, Keire::Ref<Keire::AudioSystem>{}, physics);
            session->Play();
            const auto support =
                session->RuntimeScene()->FindEntity(platform.Id()).GetComponent<Keire::TransformComponent>();
            const auto rider = session->RuntimeScene()->FindEntity(player.Id());
            const auto transform = rider.GetComponent<Keire::TransformComponent>();
            const auto motor = rider.GetComponent<Keire::CharacterControllerComponent>();
            const auto rotation =
                Keire::Math::ComposeTransform({}, Keire::Math::EulerDegreesToQuaternion(angles), {1.0F, 1.0F, 1.0F});
            for (int tick = 0; tick < 6; ++tick)
            {
                REQUIRE(motor->QueueDesiredMovement(Keire::Math::TransformDirection(rotation, {0.0F, -0.03F, 0.0F})));
                session->FixedUpdate(1.0F / 60.0F);
            }
            REQUIRE(motor->Grounded());
            const auto initial = transform->LocalPosition();
            float maximumError = 0.0F;
            for (int tick = 0; tick < 240; ++tick)
            {
                const float phase = static_cast<float>(tick + 1) / 60.0F;
                support->SetLocalPosition(
                    {0.3F * std::sin(phase), 0.15F * std::sin(phase * 2.0F), 0.2F * std::sin(phase * 0.7F)});
                support->SetLocalEulerAngles({0.0F, 20.0F * std::sin(phase), 0.0F});
                session->FixedUpdate(1.0F / 60.0F);
                REQUIRE(session->State() == Keire::ScenePlayState::Playing);
                CHECK(motor->Grounded());
                const auto local = Keire::Math::TransformPoint(Keire::Math::Inverse(support->LocalMatrix()),
                                                               transform->LocalPosition());
                const float dx = local.X - initial.X;
                const float dy = local.Y - initial.Y;
                const float dz = local.Z - initial.Z;
                maximumError = std::max(maximumError, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            CHECK(maximumError < 0.002F);
        }
    }
}

TEST_CASE("character support carry invalidates stale anchors and respects collision")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Carry lifecycle"));
    auto platform = scene->CreateEntity("Support");
    platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({3.0F, 0.2F, 3.0F});
    auto player = scene->CreateEntity("Rider");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.12F, 0.0F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    auto runtimePlatform = session->RuntimeScene()->FindEntity(platform.Id());
    const auto support = runtimePlatform.GetComponent<Keire::TransformComponent>();
    auto rider = session->RuntimeScene()->FindEntity(player.Id());
    const auto transform = rider.GetComponent<Keire::TransformComponent>();
    const auto motor = rider.GetComponent<Keire::CharacterControllerComponent>();
    constexpr float dt = 1.0F / 60.0F;
    for (int tick = 0; tick < 6; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(dt);
    }
    REQUIRE(motor->Grounded());
    float expectedX = 0.0F;
    Keire::Ref<Keire::ColliderComponent> retainedCollider;
    SUBCASE("jump detaches before support carry") { REQUIRE(motor->QueueDesiredMovement({0.0F, 0.1F, 0.0F})); }
    SUBCASE("rider teleport")
    {
        transform->SetWorldPosition({1.0F, 1.1F, 0.0F});
        expectedX = 1.0F;
    }
    SUBCASE("rider reset") { transform->ResetPresentationInterpolation(); }
    SUBCASE("support reset") { support->ResetPresentationInterpolation(); }
    SUBCASE("support disabled") { runtimePlatform.GetComponent<Keire::ColliderComponent>()->SetEnabled(false); }
    SUBCASE("support removed while retained component remains alive")
    {
        retainedCollider = runtimePlatform.GetComponent<Keire::ColliderComponent>();
        REQUIRE(runtimePlatform.RemoveComponent<Keire::ColliderComponent>());
        session->RuntimeScene()->Update(0.0F);
        runtimePlatform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({3.0F, 0.2F, 3.0F});
        CHECK(retainedCollider);
    }
    SUBCASE("controller disable reenables without stale carry")
    {
        motor->SetEnabled(false);
        session->FixedUpdate(dt);
        motor->SetEnabled(true);
    }
    SUBCASE("controller replacement")
    {
        REQUIRE(rider.RemoveComponent<Keire::CharacterControllerComponent>());
        session->RuntimeScene()->Update(0.0F);
        rider.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    }
    SUBCASE("rider reparent") { rider.SetParent(session->RuntimeScene()->CreateEntity("New parent")); }
    SUBCASE("support reparent")
    {
        runtimePlatform.SetParent(session->RuntimeScene()->CreateEntity("New support parent"));
    }
    SUBCASE("support shape changed")
    {
        runtimePlatform.GetComponent<Keire::ColliderComponent>()->SetHalfExtent({2.5F, 0.2F, 3.0F});
    }
    SUBCASE("support becomes trigger") { runtimePlatform.GetComponent<Keire::ColliderComponent>()->SetTrigger(true); }
    SUBCASE("rider inherits support transform without double carry")
    {
        rider.SetParent(runtimePlatform);
        session->FixedUpdate(dt);
        expectedX = 0.2F;
    }
    SUBCASE("carry is swept into a wall")
    {
        auto wall = session->RuntimeScene()->CreateEntity("Wall");
        wall.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.5F, 1.0F, 0.0F});
        wall.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({0.1F, 2.0F, 2.0F});
        support->SetLocalPosition({1.0F, 0.0F, 0.0F});
        session->FixedUpdate(dt);
        CHECK(transform->WorldPosition().X <= 0.121F);
        CHECK(transform->WorldPosition().X >= 0.1F);
        expectedX = transform->WorldPosition().X;
        support->SetLocalPosition({0.2F, 0.0F, 0.0F});
        support->ResetPresentationInterpolation();
    }
    support->SetLocalPosition({0.2F, 0.0F, 0.0F});
    session->FixedUpdate(dt);
    CHECK(transform->WorldPosition().X == doctest::Approx(expectedX).epsilon(0.0001F));
    CHECK(transform->WorldRotation() == Keire::Quaternion{});
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("character carries dynamic support motion without consuming the physics step")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Dynamic carry"));
    auto platform = scene->CreateEntity("Support");
    platform.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({3.0F, 0.2F, 3.0F});
    auto body = platform.AddComponent<Keire::RigidBodyComponent>();
    body->SetUseGravity(false);
    body->SetLinearVelocity({0.3F, 0.0F, 0.0F});
    auto player = scene->CreateEntity("Rider");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.12F, 0.0F});
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.28F, 1.8F, 0.3F, 0.02F);
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    const auto support = session->RuntimeScene()->FindEntity(platform.Id()).GetComponent<Keire::TransformComponent>();
    const auto rider = session->RuntimeScene()->FindEntity(player.Id());
    const auto transform = rider.GetComponent<Keire::TransformComponent>();
    const auto motor = rider.GetComponent<Keire::CharacterControllerComponent>();
    constexpr float dt = 1.0F / 60.0F;
    for (int tick = 0; tick < 6; ++tick)
    {
        REQUIRE(motor->QueueDesiredMovement({0.0F, -0.03F, 0.0F}));
        session->FixedUpdate(dt);
    }
    REQUIRE(motor->Grounded());
    const auto initialOffset = transform->WorldPosition().X - support->WorldPosition().X;
    float maximumError = 0.0F;
    for (int tick = 0; tick < 180; ++tick)
    {
        session->FixedUpdate(dt);
        maximumError =
            std::max(maximumError, std::abs(transform->WorldPosition().X - support->WorldPosition().X - initialOffset));
    }
    CHECK(maximumError < 0.002F);
    CHECK(transform->WorldPosition().X > 0.8F);
    CHECK(motor->Grounded());
    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("precise managed queries preserve filters and triggers at distant origins")
{
    for (const double distance : {0.0, 1.0e7, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        auto context = scene->CreateEntity("Query context");
        context.SetLayer(3);
        auto solid = scene->CreateEntity("Solid target");
        solid.SetLayer(1);
        solid.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({distance, 0.0, distance});
        solid.AddComponent<Keire::ColliderComponent>()->SetMask(1U << 3U);
        auto trigger = scene->CreateEntity("Trigger target");
        trigger.SetLayer(2);
        trigger.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({distance, 2.0, distance});
        const auto triggerCollider = trigger.AddComponent<Keire::ColliderComponent>();
        triggerCollider->SetTrigger(true);
        triggerCollider->SetMask(1U << 3U);
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                          Keire::Ref<Keire::AudioSystem>{}, physics);
        session->Play();
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        auto runtimeContext = session->RuntimeScene()->FindEntity(context.Id());

        Keire::ManagedRaycastQueryPrecise ray;
        ray.World = runtimeContext.World();
        ray.ContextEntity = context.Id().Value();
        ray.Origin = {distance, 4.0, distance};
        ray.Direction = {0.0F, -1.0F, 0.0F};
        ray.MaximumDistance = 6.0F;
        auto hit = Keire::Detail::QueryManagedRayPrecise(session, ray);
        REQUIRE(hit);
        CHECK(hit->Entity == solid.Id().Value());
        ray.IncludeTriggers = true;
        hit = Keire::Detail::QueryManagedRayPrecise(session, ray);
        REQUIRE(hit);
        CHECK(hit->Entity == trigger.Id().Value());
        ray.IgnoredEntity = trigger.Id().Value();
        hit = Keire::Detail::QueryManagedRayPrecise(session, ray);
        REQUIRE(hit);
        CHECK(hit->Entity == solid.Id().Value());
        ray.IgnoredEntity = {};
        ray.Mask = 1U << 1U;
        hit = Keire::Detail::QueryManagedRayPrecise(session, ray);
        REQUIRE(hit);
        CHECK(hit->Entity == solid.Id().Value());
        ray.Mask = 1U << 3U;
        CHECK_FALSE(Keire::Detail::QueryManagedRayPrecise(session, ray));

        Keire::ManagedCapsuleCastQueryPrecise capsule;
        capsule.World = runtimeContext.World();
        capsule.ContextEntity = context.Id().Value();
        capsule.Origin = ray.Origin;
        capsule.Radius = 0.1F;
        capsule.Height = 0.4F;
        capsule.Displacement = {0.0F, -6.0F, 0.0F};
        hit = Keire::Detail::QueryManagedCapsulePrecise(session, capsule);
        REQUIRE(hit);
        CHECK(hit->Entity == solid.Id().Value());
        capsule.IncludeTriggers = true;
        hit = Keire::Detail::QueryManagedCapsulePrecise(session, capsule);
        REQUIRE(hit);
        CHECK(hit->Entity == trigger.Id().Value());
        capsule.IgnoredEntity = trigger.Id().Value();
        hit = Keire::Detail::QueryManagedCapsulePrecise(session, capsule);
        REQUIRE(hit);
        CHECK(hit->Entity == solid.Id().Value());

        Keire::ManagedSphereOverlapQueryPrecise overlap;
        overlap.World = runtimeContext.World();
        overlap.ContextEntity = context.Id().Value();
        overlap.Center = {distance, 1.0, distance};
        overlap.Radius = 3.0F;
        CHECK(Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap).size() == 2);
        overlap.IncludeTriggers = false;
        auto entities = Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap);
        REQUIRE(entities.size() == 1);
        CHECK(entities.front() == solid.Id().Value());
        overlap.IncludeTriggers = true;
        overlap.Mask = 1U << 2U;
        entities = Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap);
        REQUIRE(entities.size() == 1);
        CHECK(entities.front() == trigger.Id().Value());
        overlap.IgnoredEntity = trigger.Id().Value();
        CHECK(Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap).empty());

        runtimeContext.SetLayer(4);
        ray.Mask = ~0U;
        CHECK_FALSE(Keire::Detail::QueryManagedRayPrecise(session, ray));
        CHECK_FALSE(Keire::Detail::QueryManagedCapsulePrecise(session, capsule));
        overlap.IgnoredEntity = {};
        CHECK(Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap).empty());
    }
}

TEST_CASE("precise managed overlaps bound distant results without duplicate entities")
{
    constexpr double distance = 1.0e12;
    constexpr std::size_t bodyCount = 260;
    constexpr std::size_t maximumManagedResults = 256;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto context = scene->CreateEntity("Query context");
    std::vector<Keire::AssetId> expected;
    for (std::size_t index = 0; index < bodyCount; ++index)
    {
        auto target = scene->CreateEntity("Overlap target");
        target.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise(
            {distance + static_cast<double>(index % 20) * 0.25, static_cast<double>(index / 20) * 0.25, distance});
        target.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({0.05F, 0.05F, 0.05F});
        expected.push_back(target.Id().Value());
    }
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    Keire::ManagedSphereOverlapQueryPrecise overlap;
    overlap.World = session->RuntimeScene()->FindEntity(context.Id()).World();
    overlap.ContextEntity = context.Id().Value();
    overlap.Center = {distance, 0.0, distance};
    overlap.Radius = 10.0F;
    const auto native = session->OverlapSpherePrecise({.Center = overlap.Center, .Radius = overlap.Radius});
    REQUIRE(native.size() == bodyCount);
    auto entities = Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap);
    REQUIRE(entities.size() == maximumManagedResults);
    std::ranges::sort(entities);
    CHECK(std::ranges::adjacent_find(entities) == entities.end());
    for (const auto entity : entities)
    {
        CHECK(std::ranges::find(expected, entity) != expected.end());
    }
}

TEST_CASE("distant scenes select a local physics region and preserve falling bodies and precise queries")
{
    for (const double distance : {1.0e7, 1.0e12, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        auto floor = scene->CreateEntity("Distant floor");
        floor.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({distance, -0.5, distance});
        floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({10.0F, 0.5F, 10.0F});
        auto falling = scene->CreateEntity("Distant falling body");
        falling.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise(
            {distance + 0.25, 4.0, distance + 0.125});
        (void)falling.AddComponent<Keire::ColliderComponent>();
        falling.AddComponent<Keire::RigidBodyComponent>()->SetMotion(Keire::PhysicsMotionType::Dynamic);
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                          Keire::Ref<Keire::AudioSystem>{}, physics);
        session->Play();
        INFO(session->Diagnostic().Message);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        REQUIRE(session->Physics());
        CHECK(session->Physics()->WorldOrigin() != Keire::Vector3d{});
        const auto runtimeTransform =
            session->RuntimeScene()->FindEntity(falling.Id()).GetComponent<Keire::TransformComponent>();
        CHECK(runtimeTransform->WorldPositionPrecise() == Keire::Vector3d{distance + 0.25, 4.0, distance + 0.125});
        for (int tick = 0; tick < 240; ++tick)
        {
            session->FixedUpdate(1.0F / 60.0F);
        }
        INFO(session->Diagnostic().Message);
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        const auto position = session->RuntimeScene()
                                  ->FindEntity(falling.Id())
                                  .GetComponent<Keire::TransformComponent>()
                                  ->WorldPositionPrecise();
        CHECK(std::abs(position.X - (distance + 0.25)) < 0.001);
        CHECK(std::abs(position.Z - (distance + 0.125)) < 0.001);
        CHECK(position.Y == doctest::Approx(0.5).epsilon(0.03));
        const auto hits = session->RayCastPrecise(
            {.Origin = {distance + 3.0, 2.0, distance}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 4.0F});
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().Entity == floor.Id());
        CHECK(hits.front().Position.X == distance + 3.0);
        CHECK(hits.front().Position.Y == doctest::Approx(0.0));
        CHECK(hits.front().Distance == doctest::Approx(2.0F));
        const auto context = session->RuntimeScene()->FindEntity(falling.Id());
        Keire::ManagedRaycastQueryPrecise managedRay;
        managedRay.World = context.World();
        managedRay.ContextEntity = falling.Id().Value();
        managedRay.Origin = {distance + 3.0, 2.0, distance};
        managedRay.Direction = {0.0F, -1.0F, 0.0F};
        managedRay.MaximumDistance = 4.0F;
        const auto managedHit = Keire::Detail::QueryManagedRayPrecise(session, managedRay);
        REQUIRE(managedHit);
        CHECK(managedHit->Point == hits.front().Position);
        CHECK(managedHit->Entity == floor.Id().Value());
        managedRay.IgnoredEntity = floor.Id().Value();
        CHECK_FALSE(Keire::Detail::QueryManagedRayPrecise(session, managedRay));
        managedRay.World = 0;
        CHECK_THROWS_AS((void)Keire::Detail::QueryManagedRayPrecise(session, managedRay), std::invalid_argument);

        Keire::ManagedCapsuleCastQueryPrecise capsule;
        capsule.World = context.World();
        capsule.ContextEntity = falling.Id().Value();
        capsule.Origin = {distance + 3.0, 2.0, distance};
        capsule.Displacement = {0.0F, -3.0F, 0.0F};
        const auto capsuleHit = Keire::Detail::QueryManagedCapsulePrecise(session, capsule);
        REQUIRE(capsuleHit);
        CHECK(capsuleHit->Entity == floor.Id().Value());

        Keire::ManagedSphereOverlapQueryPrecise overlap;
        overlap.World = context.World();
        overlap.ContextEntity = falling.Id().Value();
        overlap.Center = {distance + 3.0, 0.0, distance};
        overlap.Radius = 0.25F;
        const auto overlapped = Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap);
        REQUIRE(overlapped.size() == 1);
        CHECK(overlapped.front() == floor.Id().Value());
        overlap.IgnoredEntity = Keire::AssetId::Generate();
        CHECK_THROWS_AS((void)Keire::Detail::QueryManagedSphereOverlapPrecise(session, overlap), std::invalid_argument);
    }
}
