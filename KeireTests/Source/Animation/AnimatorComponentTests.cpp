#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <stdexcept>

TEST_CASE("Animator edit preview state can be cleared without changing authored assignments")
{
    Keire::AnimatorComponent animator;
    const auto graph = Keire::AssetId::Generate();
    const auto skeleton = Keire::AssetId::Generate();
    const auto skin = Keire::AssetId::Generate();
    animator.SetGraph(graph);
    animator.SetSkeleton(skeleton);
    animator.SetSkinnedMesh(skin);

    const std::array<Keire::Matrix4, 1> palette{};
    animator.SetRuntimePose("Idle", 0.25F, true, palette);
    animator.SetRuntimeDebugSnapshot(std::make_shared<Keire::AnimatorDebugSnapshot>());
    animator.SetRuntimeDiagnostic("preview");
    animator.ClearRuntimePose();

    CHECK(animator.Graph() == graph);
    CHECK(animator.Skeleton() == skeleton);
    CHECK(animator.SkinnedMesh() == skin);
    CHECK(animator.CurrentState().empty());
    CHECK(animator.SkinPalette().empty());
    CHECK_FALSE(animator.RuntimeDebugSnapshot());
    CHECK(animator.RuntimeDiagnostic().empty());
}

TEST_CASE("Animator component queues ordered managed playback controls and preserves speed while paused")
{
    Keire::AnimatorComponent animator;
    animator.SetSpeed(1.75F);
    animator.SetPaused(true);
    animator.Play("Run", "Base", 0.25F);
    animator.CrossFade("Jump", 0.4F, "Upper Body", 0.1F);
    animator.Stop();

    CHECK(animator.Speed() == doctest::Approx(1.75F));
    CHECK(animator.Paused());
    const auto commands = animator.ConsumeRuntimeCommands();
    REQUIRE(commands.size() == 3);
    CHECK(commands[0].Type == Keire::AnimatorCommandType::Play);
    CHECK(commands[0].Name == "Run");
    CHECK(commands[0].Layer == "Base");
    CHECK(commands[0].FloatValue == doctest::Approx(0.25F));
    CHECK(commands[1].Type == Keire::AnimatorCommandType::CrossFade);
    CHECK(commands[1].Name == "Jump");
    CHECK(commands[1].Layer == "Upper Body");
    CHECK(commands[1].FloatValue == doctest::Approx(0.1F));
    CHECK(commands[1].SecondaryFloatValue == doctest::Approx(0.4F));
    CHECK(commands[2].Type == Keire::AnimatorCommandType::Stop);
    CHECK(commands[0].Sequence < commands[1].Sequence);
    CHECK(commands[1].Sequence < commands[2].Sequence);

    CHECK_THROWS_AS(animator.Play("Run", {}, -0.1F), std::invalid_argument);
    CHECK_THROWS_AS(animator.CrossFade("Run", -0.1F), std::invalid_argument);
}

TEST_CASE("Animator foot grounding settings validate and migrate as authored component state")
{
    Keire::AnimatorComponent animator;
    Keire::AnimatorFootGroundingSettings settings;
    settings.Enabled = true;
    settings.AutomaticBoneMapping = false;
    settings.AutomaticRaycastDistance = false;
    settings.Pelvis = "pelvis";
    settings.LeftUpperLeg = "left-upper";
    settings.LeftLowerLeg = "left-lower";
    settings.LeftFoot = "left-foot";
    settings.RightUpperLeg = "right-upper";
    settings.RightLowerLeg = "right-lower";
    settings.RightFoot = "right-foot";
    settings.RaycastDistance = 1.25F;
    settings.CollisionMask = 0x4U;
    animator.SetFootGrounding(settings);
    CHECK(animator.FootGrounding().Enabled);
    CHECK(animator.FootGrounding().LeftFoot == "left-foot");

    auto invalid = settings;
    invalid.Weight = 2.0F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    CHECK(animator.FootGrounding().Weight == doctest::Approx(1.0F));

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    CHECK(registration.SchemaVersion == 3);
    const auto values = registration.Serialize(animator);
    auto restored = registration.Factory();
    registration.Deserialize(*restored, values, registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->FootGrounding().Enabled);
    CHECK(restoredAnimator->FootGrounding().RaycastDistance == doctest::Approx(1.25F));
    CHECK(restoredAnimator->FootGrounding().CollisionMask == 0x4U);
    CHECK_FALSE(restoredAnimator->FootGrounding().AutomaticBoneMapping);

    auto invalidMask = values;
    invalidMask["footCollisionMask"] = std::int64_t{-1};
    CHECK_THROWS_AS(registration.Deserialize(*registration.Factory(), invalidMask, registration.SchemaVersion),
                    std::invalid_argument);

    REQUIRE(registration.Migrate);
    const auto migrated = registration.Migrate({}, 1);
    CHECK(std::get<bool>(migrated.at("footGrounding")) == false);
    CHECK(std::get<std::string>(migrated.at("leftFoot")) == "LeftFoot");
    CHECK_FALSE(std::get<bool>(migrated.at("footAutomaticBoneMapping")));
    CHECK(std::get<bool>(migrated.at("leftArmIkAutomaticBoneMapping")));

    const auto migratedV2 = registration.Migrate(values, 2);
    CHECK(std::get<double>(migratedV2.at("footMaximumSlope")) == doctest::Approx(60.0));
    CHECK(std::get<std::string>(migratedV2.at("rightArmIkEnd")) == "RightHand");
}

TEST_CASE("Animator authored arm IK is automatic, serializable, and validates safe weights")
{
    Keire::AnimatorComponent animator;
    Keire::AnimatorLimbIkSettings left;
    left.Enabled = true;
    left.Target = Keire::EntityId(Keire::AssetId::Generate());
    left.Pole = Keire::EntityId(Keire::AssetId::Generate());
    left.TargetOffset = {0.1F, -0.2F, 0.3F};
    left.PositionWeight = 0.8F;
    left.RotationWeight = 0.6F;
    animator.SetLeftArmIk(left);

    CHECK(animator.LeftArmIk().AutomaticBoneMapping);
    CHECK(animator.LeftArmIk().Target == left.Target);
    CHECK(animator.LeftArmIk().TargetOffset == left.TargetOffset);

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    const auto values = registration.Serialize(animator);
    auto restored = registration.Factory();
    registration.Deserialize(*restored, values, registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->LeftArmIk().Enabled);
    CHECK(restoredAnimator->LeftArmIk().Target == left.Target);
    CHECK(restoredAnimator->LeftArmIk().Pole == left.Pole);
    CHECK(restoredAnimator->LeftArmIk().PositionWeight == doctest::Approx(0.8F));
    CHECK(restoredAnimator->LeftArmIk().RotationWeight == doctest::Approx(0.6F));

    auto invalid = left;
    invalid.PositionWeight = -0.1F;
    CHECK_THROWS_AS(animator.SetLeftArmIk(invalid), std::invalid_argument);
    CHECK(animator.LeftArmIk().PositionWeight == doctest::Approx(0.8F));

    invalid = left;
    invalid.AutomaticBoneMapping = false;
    invalid.Root.clear();
    CHECK_THROWS_AS(animator.SetLeftArmIk(invalid), std::invalid_argument);
}
