#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterGrounding.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

TEST_CASE("character vertical grounding stops instead of creating downhill motion")
{
    constexpr Keire::Vector3 downward{0.0F, -0.5F, 0.0F};
    const Keire::Vector3 slopeNormal{0.0F, 0.8F, 0.6F};

    const auto sliding = Keire::Detail::ResolveCharacterCollisionRemainder(downward, slopeNormal, true);
    CHECK(sliding.Z == doctest::Approx(0.24F));
    CHECK(sliding.Y == doctest::Approx(-0.18F));
    CHECK(Keire::Detail::ResolveCharacterCollisionRemainder(downward, slopeNormal, false) == Keire::Vector3{});
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
        for (int index = 0; index < 16; ++index)
            CHECK(editorCameraWorld.Elements[index] == doctest::Approx(renderedHead.Elements[index]).epsilon(0.0001));
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
            {
                const float x = std::sin(angle * 3.14159265358979323846F / 180.0F) * direction;
                const float z = std::cos(angle * 3.14159265358979323846F / 180.0F) * direction;
                const auto progress = [&](const Keire::Vector3 value) { return value.X * x + value.Z * z; };
                INFO("step=" << stepHeight << " speed=" << speed << " direction=" << direction << " angle=" << angle);
                auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                            Keire::SceneAsset::EmptyDefinition("Step boundary"));
                auto floor = scene->CreateEntity("Floor");
                floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
                floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5.0F, 0.5F, 5.0F});
                auto plinth = scene->CreateEntity("Plinth");
                plinth.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, stepHeight * .5F, 0.0F});
                plinth.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({.75F, stepHeight * .5F, .75F});
                if (ceiling)
                {
                    auto roof = scene->CreateEntity("Ceiling");
                    roof.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.95F, 0.0F});
                    roof.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({2.0F, .05F, 2.0F});
                }
                auto player = scene->CreateEntity("Player");
                player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.8F * x, .9F, -1.8F * z});
                auto authoredMotor = player.AddComponent<Keire::CharacterControllerComponent>();
                authoredMotor->ConfigureCapsule(.28F, 1.8F, .30F, .02F);
                authoredMotor->SetMaximumSlopeDegrees(50.0F);
                Keire::PhysicsSystemSpecification specification;
                specification.Mode = Keire::PhysicsMode::Enabled;
                auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
                auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                            Keire::Ref<Keire::AudioSystem>{}, physics);
                session->Play();
                auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
                auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
                auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
                for (int tick = 0; tick < 240; ++tick)
                {
                    const auto before = transform->LocalPosition();
                    const float forward = tick < 6 ? 0.0F : speed / 60.0F;
                    REQUIRE(motor->QueueDesiredMovement({forward * x, -.03F, forward * z}));
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
