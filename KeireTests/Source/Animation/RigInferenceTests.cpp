#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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
    [[nodiscard]] std::vector<Keire::SkeletonBone> NumberedHumanoidFixture()
    {
        // CesiumMan imported hierarchy and local rest transforms, including its axis-conversion parents.
        std::vector<Keire::SkeletonBone> bones;
        bones.push_back(Bone("Z_UP", -1));
        bones.back().BindPose.Translation = {0.0F, 0.0F, -0.0F};
        bones.back().BindPose.Rotation = {0.707106829F, 0.0F, 0.0F, 0.707106829F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Armature", 0));
        bones.back().BindPose.Translation = {0.0F, 0.0F, -0.0F};
        bones.back().BindPose.Rotation = {0.0F, 0.0F, -0.707106829F, 0.707106829F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_torso_joint_1", 1));
        bones.back().BindPose.Translation = {1.57554005e-08F, 0.00499983691F, -0.678999901F};
        bones.back().BindPose.Rotation = {0.0F, -0.0378035344F, 0.0F, 0.999285221F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_torso_joint_2", 2));
        bones.back().BindPose.Translation = {1.33617004e-05F, -1.33738004e-05F, -0.145416901F};
        bones.back().BindPose.Rotation = {0.0F, -0.657396495F, 0.0F, 0.753544927F};
        bones.back().BindPose.Scale = {0.99999994F, 1.0F, 0.99999994F};
        bones.push_back(Bone("torso_joint_3", 3));
        bones.back().BindPose.Translation = {-0.250516891F, 6.07221978e-07F, 7.29081003e-05F};
        bones.back().BindPose.Rotation = {0.0F, 0.622702897F, 0.0F, 0.782458365F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_neck_joint_1", 4));
        bones.back().BindPose.Translation = {-2.36603e-06F, 2.41398993e-06F, -0.0648362115F};
        bones.back().BindPose.Rotation = {0.0F, -0.660634518F, 0.0F, 0.750707746F};
        bones.back().BindPose.Scale = {0.99999994F, 1.0F, 0.99999994F};
        bones.push_back(Bone("Skeleton_neck_joint_2", 5));
        bones.back().BindPose.Translation = {-0.0520401709F, -3.39932988e-08F, 2.66078996e-06F};
        bones.back().BindPose.Rotation = {0.0F, 0.999690473F, -0.0F, -0.0248792283F};
        bones.back().BindPose.Scale = {1.00000024F, 1.0F, 1.00000024F};
        bones.push_back(Bone("Skeleton_arm_joint_L__4_", 4));
        bones.back().BindPose.Translation = {-3.83746992e-05F, 0.0910136029F, 6.14333985e-05F};
        bones.back().BindPose.Rotation = {0.0F, 0.995976865F, -0.0F, -0.0896108225F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_arm_joint_L__3_", 7));
        bones.back().BindPose.Translation = {0.0132216197F, 0.215499505F, -0.1093321F};
        bones.back().BindPose.Rotation = {0.0F, -0.0711694285F, 0.0F, 0.99746424F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_arm_joint_L__2_", 8));
        bones.back().BindPose.Translation = {-0.0933246166F, 0.143000096F, -0.0781479105F};
        bones.back().BindPose.Rotation = {0.0F, -0.0225422289F, 0.0F, 0.999745905F};
        bones.back().BindPose.Scale = {0.99999994F, 1.0F, 0.99999994F};
        bones.push_back(Bone("Skeleton_arm_joint_R", 4));
        bones.back().BindPose.Translation = {-3.83024999e-05F, -0.0909877494F, 6.20323044e-05F};
        bones.back().BindPose.Rotation = {0.0F, 0.990931928F, -0.0F, 0.134364888F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("Skeleton_arm_joint_R__2_", 10));
        bones.back().BindPose.Translation = {-0.03554634F, -0.215498999F, -0.1042329F};
        bones.back().BindPose.Rotation = {0.0F, 0.896147966F, -0.0F, -0.443755388F};
        bones.back().BindPose.Scale = {0.999999881F, 1.0F, 0.999999881F};
        bones.push_back(Bone("Skeleton_arm_joint_R__3_", 11));
        bones.back().BindPose.Translation = {0.0313702188F, -0.143001005F, 0.117611699F};
        bones.back().BindPose.Rotation = {0.0F, 0.379217178F, 0.0F, 0.925307691F};
        bones.back().BindPose.Scale = {1.00000012F, 1.0F, 1.00000012F};
        bones.push_back(Bone("leg_joint_L_1", 2));
        bones.back().BindPose.Translation = {0.0285199992F, 0.0680394471F, 0.0629593581F};
        bones.back().BindPose.Rotation = {0.0F, -0.324633539F, 0.0F, 0.945839882F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("leg_joint_L_2", 13));
        bones.back().BindPose.Translation = {0.209163904F, 0.00905550271F, 0.164269507F};
        bones.back().BindPose.Rotation = {0.0F, -0.529437006F, 0.0F, 0.848349392F};
        bones.back().BindPose.Scale = {1.00000024F, 1.0F, 1.00000024F};
        bones.push_back(Bone("leg_joint_L_3", 14));
        bones.back().BindPose.Translation = {0.275790095F, 0.00139725197F, -0.00412247982F};
        bones.back().BindPose.Rotation = {0.0F, -0.8377648F, 0.0F, 0.546031356F};
        bones.back().BindPose.Scale = {1.00000012F, 0.99999994F, 1.00000012F};
        bones.push_back(Bone("leg_joint_L_5", 15));
        bones.back().BindPose.Translation = {-0.0655838102F, 0.00109065301F, -0.0292914603F};
        bones.back().BindPose.Rotation = {0.0F, 0.313045889F, 0.0F, 0.949738026F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("leg_joint_R_1", 2));
        bones.back().BindPose.Translation = {0.0285571907F, -0.0680391416F, 0.0629586428F};
        bones.back().BindPose.Rotation = {0.0F, -0.689829171F, 0.0F, 0.723972201F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("leg_joint_R_2", 17));
        bones.back().BindPose.Translation = {0.260890812F, -0.00902605057F, -0.0516708903F};
        bones.back().BindPose.Rotation = {0.0F, -0.0941137746F, 0.0F, 0.995561481F};
        bones.back().BindPose.Scale = {1.00000012F, 1.0F, 1.00000012F};
        bones.push_back(Bone("leg_joint_R_3", 18));
        bones.back().BindPose.Translation = {0.275460303F, -0.00143172592F, 0.0141048301F};
        bones.back().BindPose.Rotation = {0.0F, 0.866640747F, -0.0F, -0.49893263F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        bones.push_back(Bone("leg_joint_R_5", 19));
        bones.back().BindPose.Translation = {-0.0668196306F, -0.00107226497F, -0.0263513103F};
        bones.back().BindPose.Rotation = {0.0F, 0.326914757F, 0.0F, 0.945053816F};
        bones.back().BindPose.Scale = {1.0F, 1.0F, 1.0F};
        return bones;
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

TEST_CASE("Numbered humanoid joint families use hierarchy and explicit side rather than world axes")
{
    for (const auto rotation : {Keire::Quaternion{0, 0, 0, 1}, Keire::Quaternion{0, 1, 0, 0},
                                Keire::Quaternion{0.70710678F, 0, 0, 0.70710678F}})
    {
        auto bones = NumberedHumanoidFixture();
        bones[0].BindPose.Rotation = rotation;
        const Keire::SkeletonAsset skeleton(std::move(bones));
        const auto rig = Keire::InferRigDefinition(skeleton);
        const std::array expected{Keire::RigBoneSemantic::None,          Keire::RigBoneSemantic::Root,
                                  Keire::RigBoneSemantic::Pelvis,        Keire::RigBoneSemantic::Spine,
                                  Keire::RigBoneSemantic::Chest,         Keire::RigBoneSemantic::Neck,
                                  Keire::RigBoneSemantic::Head,          Keire::RigBoneSemantic::LeftUpperArm,
                                  Keire::RigBoneSemantic::LeftLowerArm,  Keire::RigBoneSemantic::LeftHand,
                                  Keire::RigBoneSemantic::RightUpperArm, Keire::RigBoneSemantic::RightLowerArm,
                                  Keire::RigBoneSemantic::RightHand,     Keire::RigBoneSemantic::LeftUpperLeg,
                                  Keire::RigBoneSemantic::LeftLowerLeg,  Keire::RigBoneSemantic::LeftFoot,
                                  Keire::RigBoneSemantic::None,          Keire::RigBoneSemantic::RightUpperLeg,
                                  Keire::RigBoneSemantic::RightLowerLeg, Keire::RigBoneSemantic::RightFoot,
                                  Keire::RigBoneSemantic::None};
        REQUIRE(rig.Bones.size() == expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
            CHECK(rig.Bones[index].Semantic == expected[index]);
        CHECK_NOTHROW(Keire::ValidateRigDefinition(rig));
    }
}

TEST_CASE("Numbered joint inference rejects branches and incomplete chains without geometric guessing")
{
    for (int variant = 0; variant < 5; ++variant)
    {
        auto bones = NumberedHumanoidFixture();
        if (variant == 0)
            bones[15].Parent = 13; // Two same-family children of the thigh.
        else if (variant == 1)
            bones[16].Name = "UnknownToe"; // Incomplete named family.
        else if (variant == 2)
            bones[13].Parent = 4; // Leg attached to chest rather than pelvis.
        else if (variant == 3)
            bones[13].Name = "leg_joint_L_R_1"; // Contradictory side tokens.
        else
            bones.push_back(Bone("leg_joint_L_99", 2)); // Competing chain root.
        const auto rig = Keire::InferRigDefinition(Keire::SkeletonAsset(std::move(bones)));
        CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperLeg) == nullptr);
        CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftLowerLeg) == nullptr);
        CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftFoot) == nullptr);
        REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightFoot));
        CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightFoot)->Name == "leg_joint_R_3");
    }
}

TEST_CASE("Numbered joint inference preserves explicit semantics and custom profiles")
{
    auto bones = NumberedHumanoidFixture();
    bones.push_back(Bone("Head", 5));
    const Keire::SkeletonAsset skeleton(std::move(bones));
    const auto rig = Keire::InferRigDefinition(skeleton);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::Head));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::Head)->Name == "Head");
    CHECK(rig.Bones[6].Semantic == Keire::RigBoneSemantic::None);
    const auto custom = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Custom);
    for (const auto& bone : custom.Bones)
        CHECK(bone.Semantic == Keire::RigBoneSemantic::None);
}

TEST_CASE("UAL anatomical names retain semantics with numbered suffix side tokens")
{
    const Keire::SkeletonAsset skeleton(
        {Bone("Armature", -1), Bone("root", 0), Bone("pelvis", 1), Bone("spine_01", 2), Bone("spine_02", 3),
         Bone("spine_03", 4), Bone("neck_01", 5), Bone("Head", 6), Bone("clavicle_l", 5), Bone("upperarm_l", 8),
         Bone("lowerarm_l", 9), Bone("hand_l", 10), Bone("thigh_l", 2), Bone("calf_l", 12), Bone("foot_l", 13),
         Bone("ball_l", 14), Bone("upperarm_R_09", 5), Bone("lowerarm_R_10", 16), Bone("hand_R_11", 17)});
    const auto rig = Keire::InferRigDefinition(skeleton);
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::Head));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::Head)->Name == "Head");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::RightLowerArm));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::RightLowerArm)->Name == "lowerarm_R_10");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftFoot));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftFoot)->Name == "foot_l");
    CHECK(rig.Bones[15].Semantic == Keire::RigBoneSemantic::None);
}

TEST_CASE("Joint family inference does not depend on exporter prefixes or numeric segment order")
{
    auto bones = NumberedHumanoidFixture();
    for (auto& bone : bones)
    {
        if (bone.Name.starts_with("Skeleton_"))
            bone.Name = "Studio:" + bone.Name.substr(9);
    }
    bones[7].Name = "Studio:arm_joint_L_90";
    bones[8].Name = "Studio:arm_joint_L_2";
    bones[9].Name = "Studio:arm_joint_L_40";
    const auto rig = Keire::InferRigDefinition(Keire::SkeletonAsset(std::move(bones)));
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperArm));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftUpperArm)->Name == "Studio:arm_joint_L_90");
    REQUIRE(FindSemantic(rig, Keire::RigBoneSemantic::LeftHand));
    CHECK(FindSemantic(rig, Keire::RigBoneSemantic::LeftHand)->Name == "Studio:arm_joint_L_40");
}

TEST_CASE("Explicit anatomical joint tokens take precedence over numbered family inference")
{
    const Keire::SkeletonAsset skeleton(
        {Bone("Root", -1), Bone("Pelvis", 0), Bone("left_upper_leg_joint_1", 1), Bone("left_lower_leg_joint_2", 2),
         Bone("left_foot_joint_3", 3), Bone("right_upper_arm_joint_1", 1), Bone("right_lower_arm_joint_2", 5),
         Bone("right_hand_joint_3", 6), Bone("Neck", 1), Bone("Head", 8), Bone("upper_arm_L_R_joint_99", 1)});
    const auto rig = Keire::InferRigDefinition(skeleton);
    CHECK(rig.Bones[0].Semantic == Keire::RigBoneSemantic::Root);
    CHECK(rig.Bones[2].Semantic == Keire::RigBoneSemantic::LeftUpperLeg);
    CHECK(rig.Bones[3].Semantic == Keire::RigBoneSemantic::LeftLowerLeg);
    CHECK(rig.Bones[4].Semantic == Keire::RigBoneSemantic::LeftFoot);
    CHECK(rig.Bones[5].Semantic == Keire::RigBoneSemantic::RightUpperArm);
    CHECK(rig.Bones[6].Semantic == Keire::RigBoneSemantic::RightLowerArm);
    CHECK(rig.Bones[7].Semantic == Keire::RigBoneSemantic::RightHand);
    CHECK(rig.Bones[8].Semantic == Keire::RigBoneSemantic::Neck);
    CHECK(rig.Bones[9].Semantic == Keire::RigBoneSemantic::Head);
    CHECK(rig.Bones[10].Semantic == Keire::RigBoneSemantic::None);
}
