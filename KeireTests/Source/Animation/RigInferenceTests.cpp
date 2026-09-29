#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] Keire::SkeletonBone Bone(std::string name, const std::int32_t parent)
    {
        Keire::SkeletonBone result;
        result.Name = std::move(name);
        result.Parent = parent;
        return result;
    }

    [[nodiscard]] const Keire::RigBoneDefinition* FindSemantic(const Keire::RigDefinition& rig,
                                                               const Keire::RigBoneSemantic semantic)
    {
        const auto found = std::ranges::find(rig.Bones, semantic, &Keire::RigBoneDefinition::Semantic);
        return found == rig.Bones.end() ? nullptr : &*found;
    }
} // namespace

TEST_CASE("Rig inference recognizes common Mixamo humanoid naming")
{
    const Keire::SkeletonAsset skeleton({Bone("Armature", -1),
                                         Bone("mixamorig:Hips", 0),
                                         Bone("mixamorig:Spine", 1),
                                         Bone("mixamorig:Spine1", 2),
                                         Bone("mixamorig:Neck", 3),
                                         Bone("mixamorig:Head", 4),
                                         Bone("mixamorig:LeftShoulder", 3),
                                         Bone("mixamorig:LeftArm", 6),
                                         Bone("mixamorig:LeftForeArm", 7),
                                         Bone("mixamorig:LeftHand", 8),
                                         Bone("mixamorig:RightShoulder", 3),
                                         Bone("mixamorig:RightArm", 10),
                                         Bone("mixamorig:RightForeArm", 11),
                                         Bone("mixamorig:RightHand", 12),
                                         Bone("mixamorig:LeftUpLeg", 1),
                                         Bone("mixamorig:LeftLeg", 14),
                                         Bone("mixamorig:LeftFoot", 15),
                                         Bone("mixamorig:RightUpLeg", 1),
                                         Bone("mixamorig:RightLeg", 17),
                                         Bone("mixamorig:RightFoot", 18)});

    const auto rig =
        Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Humanoid, Keire::SkinningMethod::DualQuaternion, 8);

    CHECK(rig.Bones.size() == skeleton.Bones().size());
    CHECK(rig.Skinning == Keire::SkinningMethod::DualQuaternion);
    CHECK(rig.MaximumInfluences == 8);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::Pelvis));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::Pelvis)->Name == "mixamorig:Hips");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftLowerArm));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftLowerArm)->Name == "mixamorig:LeftForeArm");
    CHECK(std::ranges::any_of(rig.Chains,
                              [](const Keire::RigChainDefinition& chain) { return chain.Name == "Left Arm"; }));
    CHECK(std::ranges::any_of(rig.Chains,
                              [](const Keire::RigChainDefinition& chain) { return chain.Name == "Right Leg"; }));
}

TEST_CASE("Rig inference recognizes quadruped legs and tail without changing skeleton order")
{
    const Keire::SkeletonAsset skeleton({Bone("Root", -1),
                                         Bone("Pelvis", 0),
                                         Bone("Spine", 1),
                                         Bone("Chest", 2),
                                         Bone("Neck", 3),
                                         Bone("Head", 4),
                                         Bone("FrontLeftUpperLeg", 3),
                                         Bone("FrontLeftLowerLeg", 6),
                                         Bone("FrontLeftPaw", 7),
                                         Bone("FrontRightUpperLeg", 3),
                                         Bone("FrontRightLowerLeg", 9),
                                         Bone("FrontRightPaw", 10),
                                         Bone("HindLeftUpperLeg", 1),
                                         Bone("HindLeftLowerLeg", 12),
                                         Bone("HindLeftPaw", 13),
                                         Bone("HindRightUpperLeg", 1),
                                         Bone("HindRightLowerLeg", 15),
                                         Bone("HindRightPaw", 16),
                                         Bone("TailBase", 1),
                                         Bone("TailTip", 18)});

    const auto rig = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Quadruped);

    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftFrontFoot));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftFrontFoot)->Name == "FrontLeftPaw");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightRearLowerLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightRearLowerLeg)->Name == "HindRightLowerLeg");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::TailTip));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::TailTip)->Name == "TailTip");
    CHECK(std::ranges::any_of(rig.Chains,
                              [](const Keire::RigChainDefinition& chain) { return chain.Name == "Left Front Leg"; }));
    CHECK(std::ranges::any_of(rig.Chains,
                              [](const Keire::RigChainDefinition& chain) { return chain.Name == "Right Rear Leg"; }));
}

TEST_CASE("Quadruped rig inference maps DCC arms and numbered rear legs")
{
    const Keire::SkeletonAsset skeleton(
        {Bone("Root", -1), Bone("b_LeftUpperArm_09", 0), Bone("b_LeftForeArm_010", 1), Bone("b_LeftHand_011", 2),
         Bone("b_RightUpperArm_06", 0), Bone("b_RightForeArm_07", 4), Bone("b_RightHand_08", 5),
         Bone("b_LeftLeg01_015", 0), Bone("b_LeftLeg02_016", 7), Bone("b_LeftFoot01_017", 8),
         Bone("b_LeftFoot02_018", 9), Bone("b_RightLeg01_019", 0), Bone("b_RightLeg02_020", 11),
         Bone("b_RightFoot01_021", 12)});
    const auto rig = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Quadruped);
    REQUIRE(rig.Chains.size() == 4);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftRearUpperLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftRearUpperLeg)->Name == "b_LeftLeg01_015");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftRearFoot));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftRearFoot)->Name == "b_LeftFoot01_017");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightFrontLowerLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightFrontLowerLeg)->Name == "b_RightForeArm_07");
    CHECK(rig.Bones[10].Semantic == Keire::RigBoneSemantic::None);
}

TEST_CASE("Rig inference recognizes non-Mixamo anatomical and DCC side conventions")
{
    const Keire::SkeletonAsset skeleton({Bone("Root", -1), Bone("Pelvis", 0), Bone("Bip01 L Femur", 1),
                                         Bone("Bip01 L Tibia", 2), Bone("Bip01 L Talus", 3), Bone("Bip01 R Femur", 1),
                                         Bone("Bip01 R Tibia", 5), Bone("Bip01 R Talus", 6), Bone("clavicle_l", 1),
                                         Bone("humerus_l", 8), Bone("radius_l", 9), Bone("carpal_l", 10)});

    const auto rig = Keire::InferRigDefinition(skeleton);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperLeg)->Name == "Bip01 L Femur");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightFoot));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightFoot)->Name == "Bip01 R Talus");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftLowerArm));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftLowerArm)->Name == "radius_l");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftHand));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftHand)->Name == "carpal_l");
}

TEST_CASE("Rig inference falls back to bind topology for unnamed biped leg chains")
{
    std::vector<Keire::SkeletonBone> bones{Bone("j0", -1), Bone("j1", 0), Bone("j2", 1), Bone("j3", 2), Bone("j4", 3),
                                           Bone("j5", 4),  Bone("j6", 1), Bone("j7", 6), Bone("j8", 7), Bone("j9", 8)};
    bones[1].BindPose.Translation = {0.0F, 2.0F, 0.0F};
    bones[2].BindPose.Translation = {-0.2F, 0.0F, 0.0F};
    bones[3].BindPose.Translation = {0.0F, -1.0F, 0.0F};
    bones[4].BindPose.Translation = {0.0F, -1.0F, 0.0F};
    bones[5].BindPose.Translation = {0.0F, 0.0F, 0.3F};
    bones[6].BindPose.Translation = {0.2F, 0.0F, 0.0F};
    bones[7].BindPose.Translation = {0.0F, -1.0F, 0.0F};
    bones[8].BindPose.Translation = {0.0F, -1.0F, 0.0F};
    bones[9].BindPose.Translation = {0.0F, 0.0F, 0.3F};
    const Keire::SkeletonAsset skeleton(std::move(bones));

    const auto rig = Keire::InferRigDefinition(skeleton);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::Pelvis));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::Pelvis)->Name == "j1");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperLeg)->Name == "j2");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftFoot));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftFoot)->Name == "j4");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightLowerLeg));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightLowerLeg)->Name == "j7");
}

TEST_CASE("Rig inference rejects unsupported influence counts")
{
    const Keire::SkeletonAsset skeleton({Bone("Root", -1)});
    CHECK_THROWS_AS(static_cast<void>(Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Humanoid,
                                                                Keire::SkinningMethod::LinearBlend, 6)),
                    std::invalid_argument);
}

TEST_CASE("Custom rig inference preserves authored bones without humanoid guesses")
{
    const Keire::SkeletonAsset skeleton(
        {Bone("Root", -1), Bone("Hips", 0), Bone("LeftArm", 1), Bone("LeftForeArm", 2), Bone("LeftHand", 3)});
    const auto rig = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Custom);
    CHECK(rig.Profile == Keire::RigProfileType::Custom);
    CHECK(rig.Chains.empty());
    REQUIRE(rig.Bones.size() == skeleton.Bones().size());
    for (std::size_t index = 0; index < rig.Bones.size(); ++index)
    {
        CHECK(rig.Bones[index].Name == skeleton.Bones()[index].Name);
        CHECK(rig.Bones[index].Parent == skeleton.Bones()[index].Parent);
        CHECK(rig.Bones[index].BindPose == skeleton.Bones()[index].BindPose);
        CHECK(rig.Bones[index].Semantic == Keire::RigBoneSemantic::None);
    }
    CHECK_NOTHROW(Keire::ValidateRigDefinition(rig));
}
