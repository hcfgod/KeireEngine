#include "Keire/Core.h"
#include "KeireInternal/Animation/ProceduralPoseMath.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
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

TEST_CASE("Animator runtime pose generation advances only for committed pose changes")
{
    Keire::AnimatorComponent animator;
    CHECK(animator.PoseGeneration() == 0);

    const std::array<Keire::Matrix4, 1> palette{Keire::Matrix4{}};
    animator.SetRuntimePose("Idle", 0.25F, true, palette);
    const auto firstGeneration = animator.PoseGeneration();
    CHECK(firstGeneration != 0);

    animator.SetRuntimePose("Run", 0.5F, true, palette);
    const auto secondGeneration = animator.PoseGeneration();
    CHECK(secondGeneration > firstGeneration);

    auto invalidPalette = palette;
    invalidPalette.front().Elements[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(animator.SetRuntimePose("Invalid", 0.75F, true, invalidPalette), std::invalid_argument);
    CHECK(animator.PoseGeneration() == secondGeneration);
    CHECK(animator.CurrentState() == "Run");

    animator.ClearRuntimePose();
    const auto clearedGeneration = animator.PoseGeneration();
    CHECK(clearedGeneration > secondGeneration);
    CHECK(animator.SkinPalette().empty());

    animator.ClearRuntimePose();
    CHECK(animator.PoseGeneration() > clearedGeneration);
}

TEST_CASE("Animator runtime pose generation is excluded from serialized authored state")
{
    Keire::AnimatorComponent animator;
    const std::array<Keire::Matrix4, 1> palette{Keire::Matrix4{}};
    animator.SetRuntimePose("Idle", 0.25F, true, palette);
    REQUIRE(animator.PoseGeneration() != 0);

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    const auto values = registration.Serialize(animator);
    CHECK_FALSE(values.contains("poseGeneration"));

    auto restored = registration.Factory();
    registration.Deserialize(*restored, values, registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->PoseGeneration() == 0);
    CHECK(restoredAnimator->SkinPalette().empty());
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
    settings.PlantDistance = 0.1F;
    settings.ReleaseDistance = 0.25F;
    settings.ResponseTime = 0.2F;
    settings.KneeStability = 0.75F;
    settings.LeanCorrectionWeight = 0.8F;
    settings.MaximumLeanCorrectionDegrees = 45.0F;
    settings.CollisionMask = 0x4U;
    animator.SetFootGrounding(settings);
    CHECK(animator.FootGrounding().Enabled);
    CHECK(animator.FootGrounding().LeftFoot == "left-foot");

    auto invalid = settings;
    invalid.Weight = 2.0F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    CHECK(animator.FootGrounding().Weight == doctest::Approx(1.0F));

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    CHECK(registration.SchemaVersion == 7);
    const auto values = registration.Serialize(animator);
    auto restored = registration.Factory();
    registration.Deserialize(*restored, values, registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->FootGrounding().Enabled);
    CHECK(restoredAnimator->FootGrounding().RaycastDistance == doctest::Approx(1.25F));
    CHECK(restoredAnimator->FootGrounding().CollisionMask == 0x4U);
    CHECK_FALSE(restoredAnimator->FootGrounding().AutomaticBoneMapping);
    CHECK(restoredAnimator->FootGrounding().LockPlantedFeet);
    CHECK(restoredAnimator->FootGrounding().PlantDistance == doctest::Approx(0.1F));
    CHECK(restoredAnimator->FootGrounding().ReleaseDistance == doctest::Approx(0.25F));
    CHECK(restoredAnimator->FootGrounding().ResponseTime == doctest::Approx(0.2F));
    CHECK(restoredAnimator->FootGrounding().KneeStability == doctest::Approx(0.75F));
    CHECK(restoredAnimator->FootGrounding().LeanCorrectionWeight == doctest::Approx(0.8F));
    CHECK(restoredAnimator->FootGrounding().MaximumLeanCorrectionDegrees == doctest::Approx(45.0F));

    invalid = settings;
    invalid.ReleaseDistance = 0.05F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    invalid = settings;
    invalid.ResponseTime = -0.1F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    invalid = settings;
    invalid.KneeStability = 1.1F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    invalid = settings;
    invalid.LeanCorrectionWeight = 1.1F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);
    invalid = settings;
    invalid.MaximumLeanCorrectionDegrees = 181.0F;
    CHECK_THROWS_AS(animator.SetFootGrounding(invalid), std::invalid_argument);

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
    CHECK(std::get<bool>(migrated.at("footLockPlanted")));

    const auto migratedV2 = registration.Migrate(values, 2);
    CHECK(std::get<double>(migratedV2.at("footMaximumSlope")) == doctest::Approx(60.0));
    CHECK(std::get<std::string>(migratedV2.at("rightArmIkEnd")) == "RightHand");

    const auto migratedV3 = registration.Migrate(values, 3);
    CHECK_FALSE(std::get<bool>(migratedV3.at("footAutomaticBoneMapping")));
    CHECK(std::get<double>(migratedV3.at("footPlantDistance")) == doctest::Approx(0.08));

    const auto migratedV4 = registration.Migrate(values, 4);
    CHECK_FALSE(std::get<bool>(migratedV4.at("footAutomaticBoneMapping")));
    CHECK(std::get<double>(migratedV4.at("footPlantDistance")) == doctest::Approx(0.1));
    CHECK(std::get<double>(migratedV4.at("footResponseTime")) == doctest::Approx(0.12));
    CHECK(std::get<double>(migratedV4.at("footLeanCorrectionWeight")) == doctest::Approx(1.0));
    CHECK(std::get<double>(migratedV4.at("footMaximumLeanCorrectionDegrees")) == doctest::Approx(35.0));
    CHECK(std::get<double>(migratedV4.at("footKneeStability")) == doctest::Approx(0.9));

    auto schemaFiveValues = values;
    schemaFiveValues.erase("footKneeStability");
    schemaFiveValues["footLockPlanted"] = false;
    const auto migratedV5 = registration.Migrate(schemaFiveValues, 5);
    CHECK_FALSE(std::get<bool>(migratedV5.at("footLockPlanted")));
    CHECK(std::get<double>(migratedV5.at("footResponseTime")) == doctest::Approx(0.2));
    CHECK(std::get<double>(migratedV5.at("footLeanCorrectionWeight")) == doctest::Approx(0.8));
    CHECK(std::get<double>(migratedV5.at("footKneeStability")) == doctest::Approx(0.9));

    auto schemaSixValues = values;
    schemaSixValues.erase("poseSource");
    schemaSixValues.erase("proceduralProfile");
    schemaSixValues.erase("rigDefinition");
    schemaSixValues.erase("proceduralQuality");
    const auto migratedV6 = registration.Migrate(schemaSixValues, 6);
    CHECK(std::get<std::int64_t>(migratedV6.at("poseSource")) == 0);
    CHECK(std::get<Keire::AssetId>(migratedV6.at("proceduralProfile")) == Keire::AssetId{});
    CHECK(std::get<Keire::AssetId>(migratedV6.at("rigDefinition")) == Keire::AssetId{});
}

TEST_CASE("Procedural Motion Profiles round-trip normalized settings and reject invalid data")
{
    auto profile = Keire::ProceduralMotionProfile::GroundedArmored();
    profile.WalkSpeed = 1.65F;
    profile.SprintSpeed = 4.5F;
    profile.StepClearanceRatio = 0.16F;
    profile.ArmRestDropDegrees = 68.0F;
    const auto encoded = Keire::ProceduralMotionProfileAsset::Encode(profile);
    const auto decoded = Keire::ProceduralMotionProfileAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Profile().SchemaVersion == 1);
    CHECK(decoded->Profile().WalkSpeed == doctest::Approx(1.65F));
    CHECK(decoded->Profile().SprintSpeed == doctest::Approx(4.5F));
    CHECK(decoded->Profile().StepClearanceRatio == doctest::Approx(0.16F));
    CHECK(decoded->Profile().ArmRestDropDegrees == doctest::Approx(68.0F));
    CHECK(decoded->Profile().FootLift.Keys().size() == profile.FootLift.Keys().size());

    auto invalid = profile;
    invalid.WalkSpeed = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(Keire::ValidateProceduralMotionProfile(invalid), std::invalid_argument);
    invalid = profile;
    invalid.ReleaseDistanceRatio = invalid.PlantDistanceRatio * 0.5F;
    CHECK_THROWS_AS(Keire::ValidateProceduralMotionProfile(invalid), std::invalid_argument);
    invalid = profile;
    invalid.StrideTravel = Keire::Curve1D{};
    CHECK_THROWS_AS(Keire::ValidateProceduralMotionProfile(invalid), std::invalid_argument);
}

TEST_CASE("Procedural humanoid rest geometry drops arms and applies symmetric stance spacing")
{
    const auto left = Keire::Detail::BuildProceduralArmGeometry({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, -1.0F, 72.0F,
                                                                0.0F, 24.0F, 0.4F, 0.35F);
    const auto right = Keire::Detail::BuildProceduralArmGeometry({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 72.0F,
                                                                 0.0F, 24.0F, 0.4F, 0.35F);
    CHECK(left.TargetDirection.Y < -0.9F);
    CHECK(right.TargetDirection.Y < -0.9F);
    CHECK(left.TargetDirection.X == doctest::Approx(-right.TargetDirection.X));
    CHECK(left.TargetDirection.Z == doctest::Approx(right.TargetDirection.Z));
    CHECK(left.Reach < 0.75F);
    CHECK(left.Reach == doctest::Approx(right.Reach));
    CHECK(Keire::Detail::ProceduralFootLateralCorrection(-0.02F, 1.0F, 0.14F, -1.0F) == doctest::Approx(-0.12F));
    CHECK(Keire::Detail::ProceduralFootLateralCorrection(0.02F, 1.0F, 0.14F, 1.0F) == doctest::Approx(0.12F));
    CHECK_FALSE(Keire::Detail::ShouldResetProceduralFootContacts(true, Keire::ProceduralMotionState::Idle));
    CHECK_FALSE(Keire::Detail::ShouldResetProceduralFootContacts(true, Keire::ProceduralMotionState::Landing));
    CHECK(Keire::Detail::ShouldResetProceduralFootContacts(false, Keire::ProceduralMotionState::Falling));
    CHECK(Keire::Detail::ShouldResetProceduralFootContacts(true, Keire::ProceduralMotionState::Takeoff));
}

TEST_CASE("Procedural gait derives natural strides and gives grounding only to the stance foot")
{
    CHECK(Keire::Detail::ProceduralLocomotionPoseWeight(0.08F, 0.08F, 1.65F) == doctest::Approx(0.0F));
    CHECK(Keire::Detail::ProceduralLocomotionPoseWeight(1.65F, 0.08F, 1.65F) == doctest::Approx(1.0F));

    const auto diagonal =
        Keire::Detail::ProceduralDirectionalStrideRatio({0.70710678F, 0.0F, 0.70710678F}, 0.78F, 0.82F);
    CHECK(diagonal == doctest::Approx(0.896771F));
    CHECK(diagonal < 1.0F);

    const auto walkRate = Keire::Detail::ProceduralGaitPhaseRate(1.65F, 0.0F, 1.65F, 4.5F, 1.15F, 1.75F);
    const auto sprintRate = Keire::Detail::ProceduralGaitPhaseRate(4.5F, 1.0F, 1.65F, 4.5F, 1.15F, 1.75F);
    CHECK(walkRate == doctest::Approx(1.15F));
    CHECK(sprintRate == doctest::Approx(1.75F));
    CHECK(Keire::Detail::ProceduralStrideLength(165.0F, walkRate, 87.7F, 0.82F, 1.0F, 0.0F, 1.65F, 4.5F, 1.15F,
                                                1.75F) == doctest::Approx(71.73913F));
    CHECK(Keire::Detail::ProceduralStrideLength(450.0F, sprintRate, 87.7F, 0.82F, 1.0F, 1.0F, 1.65F, 4.5F, 1.15F,
                                                1.75F) == doctest::Approx(128.57143F));

    CHECK(Keire::Detail::ProceduralFootGroundingWeight(0.25F) == doctest::Approx(0.0F));
    CHECK(Keire::Detail::ProceduralFootGroundingWeight(0.60F) == doctest::Approx(1.0F));
    CHECK(Keire::Detail::ProceduralFootGroundingWeight(0.95F) == doctest::Approx(0.5F));
    CHECK(Keire::Detail::ProceduralFootGroundingWeight(1.25F) == doctest::Approx(0.0F));

    const auto hanging =
        Keire::Detail::ProceduralUnsupportedFootTarget({0.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, 1.0F, 0.1F);
    CHECK(hanging == (Keire::Vector3{0.0F, 0.9F, 0.0F}));
}

TEST_CASE("Procedural response and pre-landing controls are elapsed-time based and bounded")
{
    CHECK(Keire::Detail::ProceduralResponseBlend(1.0F / 60.0F, 0.0F) == doctest::Approx(1.0F));
    CHECK(Keire::Detail::ProceduralResponseBlend(0.0F, 0.1F) == doctest::Approx(0.0F));

    const auto oneStep = Keire::Detail::ProceduralResponseBlend(1.0F / 30.0F, 0.12F);
    const auto halfStep = Keire::Detail::ProceduralResponseBlend(1.0F / 60.0F, 0.12F);
    const auto twoHalfSteps = halfStep + (1.0F - halfStep) * halfStep;
    CHECK(twoHalfSteps == doctest::Approx(oneStep));

    CHECK(Keire::Detail::ProceduralPreLandingAmount(2.0F, 4.0F, 0.25F) == doctest::Approx(0.0F));
    const auto approaching = Keire::Detail::ProceduralPreLandingAmount(0.5F, 4.0F, 0.25F);
    CHECK(approaching > 0.0F);
    CHECK(approaching < 1.0F);
    CHECK(Keire::Detail::ProceduralPreLandingAmount(0.0F, 4.0F, 0.25F) == doctest::Approx(1.0F));
    CHECK(Keire::Detail::ProceduralPreLandingAmount(0.5F, -1.0F, 0.25F) == doctest::Approx(0.0F));
}

TEST_CASE("Procedural Animator assignments serialize and jump intent is consumed exactly once")
{
    Keire::AnimatorComponent animator;
    const auto profile = Keire::AssetId::Generate();
    const auto rig = Keire::AssetId::Generate();
    animator.SetPoseSource(Keire::AnimatorPoseSource::ProceduralHumanoid);
    animator.SetProceduralProfile(profile);
    animator.SetRigDefinition(rig);
    animator.SetProceduralQuality(Keire::ProceduralMotionQuality::Medium);
    animator.SetProceduralLocomotion({{2.0F, 0.0F, 3.0F}, {0.0F, 0.0F, 1.0F}, {}, 0.25F, 0.75F, true});

    const auto first = animator.ConsumeProceduralLocomotionIntent();
    const auto second = animator.ConsumeProceduralLocomotionIntent();
    CHECK(first.JumpRequested);
    CHECK_FALSE(second.JumpRequested);
    CHECK(second.DesiredWorldVelocity == (Keire::Vector3{2.0F, 0.0F, 3.0F}));

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    auto restored = registration.Factory();
    registration.Deserialize(*restored, registration.Serialize(animator), registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->PoseSource() == Keire::AnimatorPoseSource::ProceduralHumanoid);
    CHECK(restoredAnimator->ProceduralProfile() == profile);
    CHECK(restoredAnimator->RigDefinition() == rig);
    CHECK(restoredAnimator->ProceduralQuality() == Keire::ProceduralMotionQuality::Medium);

    CHECK_THROWS_AS(animator.SetProceduralLocomotion({{}, {}, {}, -0.01F, 0.0F, false}), std::invalid_argument);
}

TEST_CASE("Animator runtime foot grounding weight is transient and bounded")
{
    Keire::AnimatorComponent animator;
    Keire::AnimatorFootGroundingSettings settings;
    settings.Enabled = true;
    settings.Weight = 0.75F;
    animator.SetFootGrounding(settings);

    animator.SetRuntimeFootGroundingWeight(0.25F);
    CHECK(animator.RuntimeFootGroundingWeight() == doctest::Approx(0.25F));
    CHECK(animator.FootGrounding().Weight == doctest::Approx(0.75F));
    CHECK_THROWS_AS(animator.SetRuntimeFootGroundingWeight(-0.1F), std::invalid_argument);
    CHECK_THROWS_AS(animator.SetRuntimeFootGroundingWeight(1.1F), std::invalid_argument);

    const auto registration = Keire::CreateAnimatorComponentRegistration();
    const auto values = registration.Serialize(animator);
    auto restored = registration.Factory();
    registration.Deserialize(*restored, values, registration.SchemaVersion);
    const auto restoredAnimator = Keire::DynamicRefCast<Keire::AnimatorComponent>(restored);
    REQUIRE(restoredAnimator);
    CHECK(restoredAnimator->RuntimeFootGroundingWeight() == doctest::Approx(1.0F));

    animator.ClearRuntimePose();
    CHECK(animator.RuntimeFootGroundingWeight() == doctest::Approx(1.0F));
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
