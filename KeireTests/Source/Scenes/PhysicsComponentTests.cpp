#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterGrounding.h"

#include <doctest/doctest.h>

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
