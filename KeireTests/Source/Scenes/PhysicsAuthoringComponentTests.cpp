#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/JointComponents.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/Scenes/Scene.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

TEST_CASE("joint registrations expose stable versioned authoring contracts")
{
    const auto fixed = Keire::CreateFixedJointComponentRegistration();
    const auto hinge = Keire::CreateHingeJointComponentRegistration();
    const auto distance = Keire::CreateDistanceJointComponentRegistration();
    const auto spring = Keire::CreateSpringJointComponentRegistration();

    CHECK(fixed.SchemaVersion == 2);
    CHECK(hinge.SchemaVersion == 2);
    CHECK(distance.SchemaVersion == 2);
    CHECK(spring.SchemaVersion == 2);
    CHECK(fixed.AllowMultiple);
    CHECK(hinge.AllowMultiple);
    CHECK(distance.AllowMultiple);
    CHECK(spring.AllowMultiple);
    const auto runtimeId = std::ranges::find(fixed.Properties, "runtimeId", &Keire::ComponentProperty::Key);
    REQUIRE(runtimeId != fixed.Properties.end());
    CHECK(runtimeId->Kind == Keire::ComponentPropertyKind::Asset);
    CHECK(runtimeId->ReadOnly);
    REQUIRE(fixed.RequiredComponents.size() == 1);
    CHECK(fixed.RequiredComponents.front() == Keire::RigidBodyComponent::StaticType());
    CHECK(Keire::FixedJointComponent::StaticType() != Keire::HingeJointComponent::StaticType());
    CHECK(Keire::HingeJointComponent::StaticType() != Keire::DistanceJointComponent::StaticType());
    CHECK(Keire::DistanceJointComponent::StaticType() != Keire::SpringJointComponent::StaticType());
}

TEST_CASE("hinge joint registration round trips stable IDs limits motors and breakage")
{
    const auto registration = Keire::CreateHingeJointComponentRegistration();
    const auto source = registration.Factory();
    auto& joint = dynamic_cast<Keire::HingeJointComponent&>(*source);
    const auto runtimeId = Keire::AssetId::Parse("1b0b627c-b0e6-43d1-8de4-3703f530c696");
    const auto connected = Keire::EntityId::Parse("893c7a71-2641-47b7-90fc-5d9fd046f2c4");
    joint.SetRuntimeId(runtimeId);
    joint.SetConnectedEntity(connected);
    joint.SetLocalAnchor({1.0F, 2.0F, 3.0F});
    joint.SetConnectedAnchor({-1.0F, 0.5F, 4.0F});
    joint.SetBreakForce(125.0F);
    joint.SetBreakTorque(75.0F);
    joint.SetEnableCollision(true);
    joint.SetAxis({0.0F, 2.0F, 0.0F});
    joint.SetLimitsEnabled(true);
    joint.SetLimits(-30.0F, 80.0F);
    joint.SetMotorEnabled(true);
    joint.SetMotor(-120.0F, 450.0F);

    const auto target = registration.Factory();
    registration.Deserialize(*target, registration.Serialize(*source), registration.SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::HingeJointComponent&>(*target);
    CHECK(restored.RuntimeId() == runtimeId);
    CHECK(restored.ConnectedEntity() == connected);
    CHECK(restored.LocalAnchor() == Keire::Vector3{1.0F, 2.0F, 3.0F});
    CHECK(restored.ConnectedAnchor() == Keire::Vector3{-1.0F, 0.5F, 4.0F});
    CHECK(restored.BreakForce() == doctest::Approx(125.0F));
    CHECK(restored.BreakTorque() == doctest::Approx(75.0F));
    CHECK(restored.EnableCollision());
    CHECK(restored.Axis() == Keire::Vector3{0.0F, 1.0F, 0.0F});
    CHECK(restored.LimitsEnabled());
    CHECK(restored.LowerLimitDegrees() == doctest::Approx(-30.0F));
    CHECK(restored.UpperLimitDegrees() == doctest::Approx(80.0F));
    CHECK(restored.MotorEnabled());
    CHECK(restored.MotorSpeedDegrees() == doctest::Approx(-120.0F));
    CHECK(restored.MaximumMotorTorque() == doctest::Approx(450.0F));

    auto malformed = registration.Serialize(*source);
    malformed.insert_or_assign("axis", Keire::Vector3{});
    CHECK_THROWS_AS(registration.Deserialize(*registration.Factory(), malformed, registration.SchemaVersion),
                    std::invalid_argument);
    malformed = registration.Serialize(*source);
    malformed.insert_or_assign("runtimeId", Keire::AssetId{});
    CHECK_THROWS_AS(registration.Deserialize(*registration.Factory(), malformed, registration.SchemaVersion),
                    std::invalid_argument);
}

TEST_CASE("joint schema one migration supplies stable runtime and break settings")
{
    const auto registration = Keire::CreateFixedJointComponentRegistration();
    REQUIRE(registration.Migrate);
    const Keire::ComponentPropertyBag legacy{{"connectedEntity", Keire::EntityId{}},
                                             {"localAnchor", Keire::Vector3{}},
                                             {"connectedAnchor", Keire::Vector3{}}};
    const auto migrated = registration.Migrate(legacy, 1);
    CHECK(static_cast<bool>(std::get<Keire::AssetId>(migrated.at("runtimeId"))));
    CHECK(std::get<double>(migrated.at("breakForce")) == doctest::Approx(0.0));
    CHECK(std::get<double>(migrated.at("breakTorque")) == doctest::Approx(0.0));
    CHECK_FALSE(std::get<bool>(migrated.at("enableCollision")));

    const auto restored = registration.Factory();
    CHECK_NOTHROW(registration.Deserialize(*restored, migrated, registration.SchemaVersion));
    CHECK_THROWS_AS(registration.Migrate(legacy, 0), std::invalid_argument);
}

TEST_CASE("joint scene clones preserve runtime IDs across current and migrated schemas")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Joint clone"));
    auto body = scene->CreateEntity("Body");
    REQUIRE(body.AddComponent<Keire::ColliderComponent>());
    REQUIRE(body.AddComponent<Keire::RigidBodyComponent>());
    const auto joint = body.AddComponent<Keire::HingeJointComponent>();
    REQUIRE(joint);
    const auto runtimeId = Keire::AssetId::Parse("1e259016-17b0-4d92-a066-69756f0ed19f");
    joint->SetRuntimeId(runtimeId);

    const auto currentDefinition = scene->Snapshot();
    const auto currentClone = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), currentDefinition);
    const auto currentJoint = currentClone->FindEntity(body.Id()).GetComponent<Keire::HingeJointComponent>();
    REQUIRE(currentJoint);
    CHECK(currentJoint->RuntimeId() == runtimeId);

    auto legacyDefinition = currentDefinition;
    REQUIRE(legacyDefinition.Objects.size() == 1);
    auto& components = legacyDefinition.Objects.front().Components;
    const auto legacyJoint =
        std::ranges::find(components, Keire::HingeJointComponent::StaticType(), &Keire::SceneComponentDefinition::Type);
    REQUIRE(legacyJoint != components.end());
    legacyJoint->SchemaVersion = 1;
    legacyJoint->Data = R"({"connectedEntity":null,"localAnchor":[0,0,0],"connectedAnchor":[0,0,0],"axis":[0,1,0]})";

    const auto migratedScene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), std::move(legacyDefinition));
    const auto migratedJoint = migratedScene->FindEntity(body.Id()).GetComponent<Keire::HingeJointComponent>();
    REQUIRE(migratedJoint);
    const auto migratedRuntimeId = migratedJoint->RuntimeId();
    REQUIRE(migratedRuntimeId);

    const auto migratedDefinition = migratedScene->Snapshot();
    const auto migratedClone = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), migratedDefinition);
    const auto restoredJoint = migratedClone->FindEntity(body.Id()).GetComponent<Keire::HingeJointComponent>();
    REQUIRE(restoredJoint);
    CHECK(restoredJoint->RuntimeId() == migratedRuntimeId);
}

TEST_CASE("distance and spring joint registrations validate authored constraints")
{
    const auto distanceRegistration = Keire::CreateDistanceJointComponentRegistration();
    const auto distance = distanceRegistration.Factory();
    auto& authoredDistance = dynamic_cast<Keire::DistanceJointComponent&>(*distance);
    authoredDistance.SetDistanceLimits(0.25F, 4.0F);
    const auto restoredDistance = distanceRegistration.Factory();
    distanceRegistration.Deserialize(*restoredDistance, distanceRegistration.Serialize(*distance),
                                     distanceRegistration.SchemaVersion);
    CHECK(dynamic_cast<const Keire::DistanceJointComponent&>(*restoredDistance).MinimumDistance() ==
          doctest::Approx(0.25F));
    CHECK(dynamic_cast<const Keire::DistanceJointComponent&>(*restoredDistance).MaximumDistance() ==
          doctest::Approx(4.0F));
    CHECK_THROWS_AS(authoredDistance.SetDistanceLimits(2.0F, 1.0F), std::invalid_argument);

    const auto springRegistration = Keire::CreateSpringJointComponentRegistration();
    const auto spring = springRegistration.Factory();
    auto& authoredSpring = dynamic_cast<Keire::SpringJointComponent&>(*spring);
    authoredSpring.SetRestLength(2.0F);
    authoredSpring.SetStiffness(350.0F);
    authoredSpring.SetDamping(25.0F);
    const auto restoredSpring = springRegistration.Factory();
    springRegistration.Deserialize(*restoredSpring, springRegistration.Serialize(*spring),
                                   springRegistration.SchemaVersion);
    const auto& value = dynamic_cast<const Keire::SpringJointComponent&>(*restoredSpring);
    CHECK(value.RestLength() == doctest::Approx(2.0F));
    CHECK(value.Stiffness() == doctest::Approx(350.0F));
    CHECK(value.Damping() == doctest::Approx(25.0F));
    CHECK_THROWS_AS(authoredSpring.SetStiffness(-1.0F), std::invalid_argument);
}

TEST_CASE("character controller registration persists authoring but not runtime state")
{
    const auto registration = Keire::CreateCharacterControllerComponentRegistration();
    CHECK(registration.SchemaVersion == 2);
    const auto source = registration.Factory();
    auto& controller = dynamic_cast<Keire::CharacterControllerComponent&>(*source);
    const auto runtimeId = Keire::AssetId::Parse("94f9ca9c-727f-417b-9ed1-414069e565e9");
    controller.SetRuntimeId(runtimeId);
    controller.ConfigureCapsule(0.35F, 1.4F, 0.2F, 0.025F);
    controller.SetMaximumSlopeDegrees(52.0F);
    controller.SetLayer(8);
    controller.SetMask(0x00FF00FFU);
    REQUIRE(controller.QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
    controller.ApplyRuntimeState(7, true, {0.0F, 2.0F, 0.0F}, {2.0F, 0.0F, 1.0F});

    const auto target = registration.Factory();
    registration.Deserialize(*target, registration.Serialize(*source), registration.SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::CharacterControllerComponent&>(*target);
    CHECK(restored.RuntimeId() == runtimeId);
    CHECK(restored.Radius() == doctest::Approx(0.35F));
    CHECK(restored.Height() == doctest::Approx(1.4F));
    CHECK(restored.StepHeight() == doctest::Approx(0.2F));
    CHECK(restored.SkinWidth() == doctest::Approx(0.025F));
    CHECK(restored.MaximumSlopeDegrees() == doctest::Approx(52.0F));
    CHECK(restored.Layer() == 8);
    CHECK(restored.Mask() == 0x00FF00FFU);
    CHECK(restored.QueuedMovementCount() == 0);
    CHECK(restored.RuntimeState().Generation == 0);
    CHECK_FALSE(restored.Grounded());

    auto malformed = registration.Serialize(*source);
    malformed.insert_or_assign("layer", std::int64_t{3});
    CHECK_THROWS_AS(registration.Deserialize(*registration.Factory(), malformed, registration.SchemaVersion),
                    std::invalid_argument);
}

TEST_CASE("character controller has a bounded deterministic movement queue")
{
    Keire::CharacterControllerComponent controller;
    for (std::size_t index = 0; index < Keire::CharacterControllerComponent::MaximumQueuedMovementCommands; ++index)
        REQUIRE(controller.QueueDesiredMovement({1.0F, 2.0F, -0.5F}));
    CHECK_FALSE(controller.QueueDesiredMovement({5.0F, 0.0F, 0.0F}));
    CHECK(controller.RuntimeState().DroppedMovementCommands == 1);
    CHECK(controller.QueuedMovementCount() == Keire::CharacterControllerComponent::MaximumQueuedMovementCommands);

    const auto movement = controller.ConsumeDesiredMovement();
    CHECK(movement.X == doctest::Approx(64.0F));
    CHECK(movement.Y == doctest::Approx(128.0F));
    CHECK(movement.Z == doctest::Approx(-32.0F));
    CHECK(controller.QueuedMovementCount() == 0);

    controller.ApplyRuntimeState(11, true, {0.0F, 4.0F, 0.0F}, {2.0F, 3.0F, 4.0F});
    CHECK(controller.Grounded());
    CHECK(controller.RuntimeState().GroundNormal == Keire::Vector3{0.0F, 1.0F, 0.0F});
    CHECK_THROWS_AS(controller.ApplyRuntimeState(12, true, {}, {}), std::invalid_argument);
    CHECK_THROWS_AS((void)controller.QueueDesiredMovement({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}),
                    std::invalid_argument);
}

TEST_CASE("character controller resize validation is transactional and schema one migrates")
{
    Keire::CharacterControllerComponent controller;
    controller.ConfigureCapsule(0.4F, 1.6F, 0.25F, 0.03F);
    CHECK_THROWS_AS(controller.Resize(0.2F, 0.3F), std::invalid_argument);
    CHECK(controller.Radius() == doctest::Approx(0.4F));
    CHECK(controller.Height() == doctest::Approx(1.6F));

    const auto registration = Keire::CreateCharacterControllerComponentRegistration();
    REQUIRE(registration.Migrate);
    const Keire::ComponentPropertyBag legacy{
        {"radius", 0.4}, {"height", 1.6}, {"maximumSlope", 45.0}, {"stepHeight", 0.25}};
    const auto migrated = registration.Migrate(legacy, 1);
    CHECK(static_cast<bool>(std::get<Keire::AssetId>(migrated.at("runtimeId"))));
    CHECK(std::get<std::int64_t>(migrated.at("layer")) == 1);
    CHECK(std::get<std::int64_t>(migrated.at("mask")) == static_cast<std::int64_t>(~0U));
    const auto restored = registration.Factory();
    CHECK_NOTHROW(registration.Deserialize(*restored, migrated, registration.SchemaVersion));
    CHECK_THROWS_AS(registration.Migrate(legacy, 3), std::invalid_argument);
}
