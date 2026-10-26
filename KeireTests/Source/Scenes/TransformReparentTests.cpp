#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <stdexcept>

TEST_CASE("Rejected preserved reparent keeps hierarchy transform and savepoint unchanged")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto parent = scene->CreateEntity("Large scale parent");
    const auto parentTransform = parent.GetComponent<Keire::TransformComponent>();
    parentTransform->SetLocalScale({2000000.0F, 0.001F, 0.001F});
    auto sibling = scene->CreateEntity("Existing child");
    sibling.SetParent(parent, false);
    auto child = scene->CreateEntity("Child");
    const auto transform = child.GetComponent<Keire::TransformComponent>();
    transform->SetLocalPosition({3.0F, 4.0F, 5.0F});
    const auto position = transform->LocalPosition();
    const auto rotation = transform->LocalRotation();
    const auto scale = transform->LocalScale();
    const auto world = transform->WorldMatrix();
    Keire::Vector3 proposedPosition;
    Keire::Quaternion proposedRotation;
    Keire::Vector3 proposedScale;
    REQUIRE(Keire::Math::DecomposeTransform(
        Keire::Math::Multiply(Keire::Math::Inverse(parentTransform->WorldMatrix()), world), proposedPosition,
        proposedRotation, proposedScale));
    REQUIRE_FALSE(Keire::TransformComponent::IsValidLocalScale(proposedScale));
    const auto resetRevision = transform->PresentationResetRevision();
    scene->MarkSaved();
    const auto before = Keire::SceneAsset::Encode(scene->Snapshot());
    const auto entitiesBefore = scene->Entities();
    const auto childrenBefore = parent.Children();

    SUBCASE("Direct reparent") { CHECK_THROWS_AS(child.SetParent(parent, true), std::invalid_argument); }

    SUBCASE("Hierarchy move before an existing sibling")
    {
        CHECK_THROWS_AS(scene->MoveEntity(child.Id(), parent.Id(), sibling.Id(), true), std::invalid_argument);
    }

    CHECK_FALSE(child.Parent());
    CHECK(parent.Children() == childrenBefore);
    CHECK(scene->Entities() == entitiesBefore);
    CHECK(transform->LocalPosition() == position);
    CHECK(transform->LocalRotation() == rotation);
    CHECK(transform->LocalScale() == scale);
    CHECK(transform->WorldMatrix() == world);
    CHECK(transform->PresentationResetRevision() == resetRevision);
    CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
    CHECK_FALSE(scene->Dirty());

    CHECK_NOTHROW(child.SetParent(parent, false));
    CHECK(child.Parent() == parent);
    CHECK(transform->LocalPosition() == position);
    scene->Close();
}
