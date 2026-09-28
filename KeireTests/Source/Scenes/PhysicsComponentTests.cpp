#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterGrounding.h"

#include <doctest/doctest.h>

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
