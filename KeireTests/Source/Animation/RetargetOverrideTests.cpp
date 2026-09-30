#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Math/Math.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>

TEST_CASE("Retargeting equivalent limbs with different bone axes preserves the model-space bend")
{
    const auto quarterTurn = Keire::Math::EulerDegreesToQuaternion({0.0F, 0.0F, 90.0F});
    const auto bend = Keire::Math::EulerDegreesToQuaternion({30.0F, 0.0F, 0.0F});
    const Keire::SkeletonAsset source({{"Root", -1, {}}, {"Arm", 0, {}}, {"Hand", 1, {{0.0F, 1.0F, 0.0F}}}});
    const Keire::SkeletonAsset target(
        {{"Root", -1, {}}, {"Arm", 0, {{}, quarterTurn}}, {"Hand", 1, {{1.0F, 0.0F, 0.0F}}}});
    const auto sourceRig = Keire::InferRigDefinition(source, Keire::RigProfileType::Custom);
    const auto targetRig = Keire::InferRigDefinition(target, Keire::RigProfileType::Custom);
    const Keire::AnimationClipAsset clip(Keire::AssetId::Generate(), 1.0F, {{1, {{0.0F, {{}, bend}}}}});
    const auto result =
        Keire::RetargetAnimationClip(source, sourceRig, clip, Keire::AssetId::Generate(), target, targetRig);
    REQUIRE(result->Tracks().size() == 1);
    const auto& pose = result->Tracks()[0].Keys[0].Value;
    const auto actual = Keire::Math::TransformPoint(
        Keire::Math::ComposeTransform(pose.Translation, pose.Rotation, pose.Scale), {1.0F, 0.0F, 0.0F});
    const auto expected =
        Keire::Math::TransformPoint(Keire::Math::ComposeTransform({}, bend, {1.0F, 1.0F, 1.0F}), {0.0F, 1.0F, 0.0F});
    CHECK(actual.X == doctest::Approx(expected.X).epsilon(0.0001F));
    CHECK(actual.Y == doctest::Approx(expected.Y).epsilon(0.0001F));
    CHECK(actual.Z == doctest::Approx(expected.Z).epsilon(0.0001F));
}

TEST_CASE("Retargeting preserves translated limb endpoints under animated parents with different reference axes")
{
    const auto matrix = [](const Keire::BoneTransform& pose)
    { return Keire::Math::ComposeTransform(pose.Translation, pose.Rotation, pose.Scale); };
    const auto quarterTurn = Keire::Math::ComposeTransform(
        {}, Keire::Math::EulerDegreesToQuaternion({0.0F, 0.0F, 90.0F}), {1.0F, 1.0F, 1.0F});
    for (const auto rootAngles : std::array<Keire::Vector3, 3>{{{}, {20.0F, 35.0F, -15.0F}, {-70.0F, 10.0F, 40.0F}}})
    {
        CAPTURE(rootAngles.X);
        CAPTURE(rootAngles.Y);
        CAPTURE(rootAngles.Z);
        const Keire::BoneTransform sourceRoot{{0.3F, -0.2F, 0.1F}, Keire::Math::EulerDegreesToQuaternion(rootAngles)};
        Keire::BoneTransform targetRoot;
        REQUIRE(Keire::Math::DecomposeTransform(Keire::Math::Multiply(matrix(sourceRoot), quarterTurn),
                                                targetRoot.Translation, targetRoot.Rotation, targetRoot.Scale));
        const Keire::SkeletonAsset source(
            {{"Root", -1, sourceRoot}, {"Arm", 0, {{0.0F, 1.0F, 0.0F}}}, {"Hand", 1, {{0.0F, 1.0F, 0.0F}}}});
        const Keire::SkeletonAsset target(
            {{"Root", -1, targetRoot}, {"Arm", 0, {{1.0F, 0.0F, 0.0F}}}, {"Hand", 1, {{1.0F, 0.0F, 0.0F}}}});
        const Keire::BoneTransform animatedRoot{{0.5F, 0.1F, -0.2F},
                                                Keire::Math::EulerDegreesToQuaternion({40.0F, -25.0F, 60.0F})};
        const Keire::BoneTransform animatedArm{{0.2F, 1.1F, -0.15F},
                                               Keire::Math::EulerDegreesToQuaternion({30.0F, 15.0F, -20.0F})};
        const Keire::AnimationClipAsset clip(
            Keire::AssetId::Generate(), 1.0F,
            {{0, {{0.0F, sourceRoot}, {0.5F, animatedRoot}, {1.0F, sourceRoot}}},
             {1, {{0.0F, source.Bones()[1].BindPose}, {0.5F, animatedArm}, {1.0F, source.Bones()[1].BindPose}}}});
        const auto result = Keire::RetargetAnimationClip(
            source, Keire::InferRigDefinition(source, Keire::RigProfileType::Custom), clip, Keire::AssetId::Generate(),
            target, Keire::InferRigDefinition(target, Keire::RigProfileType::Custom));
        REQUIRE(result->Tracks().size() == 2);
        REQUIRE(result->Tracks()[0].Bone == 0);
        REQUIRE(result->Tracks()[1].Bone == 1);
        REQUIRE(result->Tracks()[0].Keys.size() == 3);
        REQUIRE(result->Tracks()[1].Keys.size() == 3);
        for (std::size_t key = 0; key < 3; ++key)
        {
            CAPTURE(key);
            const auto expectedModel = Keire::Math::Multiply(matrix(clip.Tracks()[0].Keys[key].Value),
                                                             matrix(clip.Tracks()[1].Keys[key].Value));
            const auto actualModel = Keire::Math::Multiply(matrix(result->Tracks()[0].Keys[key].Value),
                                                           matrix(result->Tracks()[1].Keys[key].Value));
            // Compare both joint origins and the child endpoint; a matching endpoint alone can hide a bad translation.
            for (const float distance : {0.0F, 1.0F})
            {
                const auto expected = Keire::Math::TransformPoint(expectedModel, {0.0F, distance, 0.0F});
                const auto actual = Keire::Math::TransformPoint(actualModel, {distance, 0.0F, 0.0F});
                CHECK(actual.X == doctest::Approx(expected.X).epsilon(0.0001F));
                CHECK(actual.Y == doctest::Approx(expected.Y).epsilon(0.0001F));
                CHECK(actual.Z == doctest::Approx(expected.Z).epsilon(0.0001F));
            }
        }
    }
}

TEST_CASE("Retargeted bent limbs retain target proportions and reference orientation after serialization")
{
    const Keire::SkeletonAsset source({{"Root", -1, {}},
                                       {"Upper", 0, {{0.0F, 1.0F, 0.0F}}},
                                       {"Lower", 1, {{0.0F, 1.0F, 0.0F}}},
                                       {"Foot", 2, {{0.0F, 1.0F, 0.0F}}}});
    const auto sourceRig = Keire::InferRigDefinition(source, Keire::RigProfileType::Custom);
    const auto rotation = [](float degrees) { return Keire::Math::EulerDegreesToQuaternion({0.0F, 0.0F, degrees}); };
    const Keire::AnimationClipAsset clip(
        Keire::AssetId::Generate(), 1.0F,
        {{1,
          {{0.0F, {{0.0F, 1.0F, 0.0F}}}, {0.5F, {{0.0F, 1.0F, 0.0F}, rotation(30.0F)}}, {1.0F, {{0.0F, 1.0F, 0.0F}}}}},
         {2,
          {{0.0F, {{0.0F, 1.0F, 0.0F}}}, {0.5F, {{0.0F, 1.0F, 0.0F}, rotation(-50.0F)}}, {1.0F, {{0.0F, 1.0F, 0.0F}}}}},
         {3, {{0.0F, {{0.0F, 1.0F, 0.0F}}}, {1.0F, {{0.0F, 1.0F, 0.0F}}}}}},
        {{0.5F, "Footstep", "left"}});
    for (const auto lengths :
         std::array<Keire::Vector3, 3>{{{2.0F, 0.5F, 1.5F}, {0.5F, 2.0F, 0.75F}, {0.02F, 0.03F, 0.01F}}})
    {
        CAPTURE(lengths.X);
        CAPTURE(lengths.Y);
        const Keire::SkeletonAsset target({{"Root", -1, {}},
                                           {"Upper", 0, {{0.0F, lengths.X, 0.0F}, rotation(10.0F)}},
                                           {"Lower", 1, {{0.0F, lengths.Y, 0.0F}, rotation(-15.0F)}},
                                           {"Foot", 2, {{0.0F, lengths.Z, 0.0F}}}});
        const auto targetRig = Keire::InferRigDefinition(target, Keire::RigProfileType::Custom);
        const auto targetId = Keire::AssetId::Generate();
        const auto result =
            Keire::RetargetAnimationClipWithDiagnostics(source, sourceRig, clip, targetId, target, targetRig);
        REQUIRE(result.Diagnostics.MappedTrackCount == 3);
        const auto restored = Keire::AnimationClipAsset::Decode(
            Keire::AnimationClipAsset::Encode(result.Clip->Skeleton(), result.Clip->Duration(), result.Clip->Tracks(),
                                              result.Clip->Events(), result.Clip->RootMotion()));
        CHECK(restored->Skeleton() == targetId);
        CHECK(restored->Duration() == 1.0F);
        REQUIRE(restored->Events().size() == 1);
        CHECK(restored->Events()[0].Time == 0.5F);
        CHECK(restored->Events()[0].Name == "Footstep");
        CHECK(restored->Events()[0].Payload == "left");
        REQUIRE(restored->Tracks().size() == 3);
        for (const auto& track : restored->Tracks())
        {
            REQUIRE(track.Bone > 0);
            REQUIRE(track.Bone < 4);
            REQUIRE(track.Keys.size() == 3);
            for (const auto& key : track.Keys)
            {
                CAPTURE(track.Bone);
                CAPTURE(key.Time);
                auto expected = target.Bones()[track.Bone].BindPose;
                if (key.Time == 0.5F && track.Bone != 3)
                    expected.Rotation = rotation(track.Bone == 1 ? 40.0F : -65.0F);
                const auto expectedMatrix =
                    Keire::Math::ComposeTransform(expected.Translation, expected.Rotation, expected.Scale);
                const auto actual =
                    Keire::Math::ComposeTransform(key.Value.Translation, key.Value.Rotation, key.Value.Scale);
                for (std::size_t index = 0; index < actual.Elements.size(); ++index)
                    CHECK(actual.Elements[index] == doctest::Approx(expectedMatrix.Elements[index]).epsilon(0.0001F));
            }
        }
    }
}

TEST_CASE("Manual retarget mappings repair unmatched tracks and reject ambiguous bindings")
{
    const Keire::SkeletonAsset source(
        {{"Root", -1, {}}, {"AuthoredArm", 0, {{1.0F, 0.0F, 0.0F}}}, {"AutomaticArm", 0, {{1.0F, 0.0F, 0.0F}}}});
    const Keire::SkeletonAsset target({{"Root", -1, {}}, {"AutomaticArm", 0, {{2.0F, 0.0F, 0.0F}}}});
    const auto sourceRig = Keire::InferRigDefinition(source, Keire::RigProfileType::Custom);
    const auto targetRig = Keire::InferRigDefinition(target, Keire::RigProfileType::Custom);
    const Keire::AnimationClipAsset clip(Keire::AssetId::Generate(), 1.0F,
                                         {{1, {{0.0F, {{1.0F, 1.0F, 0.0F}}}}}, {2, {{0.0F, {{1.0F, 0.0F, 0.0F}}}}}});
    const std::array bindings{Keire::AnimationRetargetOverride{"AuthoredArm", "AutomaticArm"}};
    const auto automatic = Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig);
    REQUIRE(automatic.Mappings.size() == 2);
    CHECK_FALSE(automatic.Mappings[0].TargetBone);
    CHECK(automatic.Mappings[1].TargetBone == 1);
    const auto result = Keire::RetargetAnimationClipWithDiagnostics(source, sourceRig, clip, Keire::AssetId::Generate(),
                                                                    target, targetRig, bindings);
    CHECK(result.Diagnostics.ManualMatchCount == 1);
    CHECK(result.Diagnostics.MappedTrackCount == 1);
    CHECK(result.Diagnostics.Mappings[0].Match == Keire::AnimationRetargetMatch::Manual);
    CHECK(result.Diagnostics.Mappings[1].Match == Keire::AnimationRetargetMatch::TargetConflict);
    REQUIRE(result.Clip->Tracks().size() == 1);
    CHECK(result.Clip->Tracks()[0].Bone == 1);
    CHECK(result.Clip->Tracks()[0].Keys[0].Value.Translation.Y == doctest::Approx(2.0F));

    for (const auto& invalid : {Keire::AnimationRetargetOverride{"Missing", "AutomaticArm"},
                                Keire::AnimationRetargetOverride{"AuthoredArm", "Missing"}})
    {
        const std::array invalidBindings{invalid};
        CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target,
                                                                              targetRig, invalidBindings)),
                        std::invalid_argument);
    }
    const std::array duplicateSource{bindings[0], bindings[0]};
    CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig,
                                                                          duplicateSource)),
                    std::invalid_argument);
    const std::array duplicateTarget{bindings[0], Keire::AnimationRetargetOverride{"AutomaticArm", "AutomaticArm"}};
    CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig,
                                                                          duplicateTarget)),
                    std::invalid_argument);
    CHECK(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig).ManualMatchCount == 0);
}
