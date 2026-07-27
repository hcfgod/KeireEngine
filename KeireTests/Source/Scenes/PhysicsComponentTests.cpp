#include "Keire/Core.h"

#include <doctest/doctest.h>

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
}
