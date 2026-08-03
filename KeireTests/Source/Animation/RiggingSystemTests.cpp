#include "Keire/Animation/RiggingSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace
{
    [[nodiscard]] Keire::MeshAsset BoxMesh()
    {
        std::vector<Keire::MeshVertex> vertices{{{-1.0F, 0.0F, -0.5F}}, {{1.0F, 0.0F, -0.5F}}, {{-1.0F, 2.0F, -0.5F}},
                                                {{1.0F, 2.0F, -0.5F}},  {{-1.0F, 0.0F, 0.5F}}, {{1.0F, 0.0F, 0.5F}},
                                                {{-1.0F, 2.0F, 0.5F}},  {{1.0F, 2.0F, 0.5F}}};
        std::vector<std::uint32_t> indices{0, 1, 2, 1, 3, 2, 4, 6, 5, 5, 6, 7};
        return Keire::MeshAsset(std::move(vertices), std::move(indices), {{-1.0F, 0.0F, -0.5F}, {1.0F, 2.0F, 0.5F}});
    }
} // namespace

TEST_CASE("Auto rig generation is deterministic and emits normalized four or eight influence data")
{
    const auto mesh = BoxMesh();
    Keire::AutoRigRequest request;
    request.Profile = Keire::RigProfileType::Humanoid;
    request.MaximumInfluences = 8;
    const auto first = Keire::GenerateRig(mesh, request);
    const auto second = Keire::GenerateRig(mesh, request);

    CHECK(first.Rig.Bones.size() == 20);
    REQUIRE(first.Skeleton.size() == second.Skeleton.size());
    for (std::size_t index = 0; index < first.Skeleton.size(); ++index)
    {
        CHECK(first.Skeleton[index].Name == second.Skeleton[index].Name);
        CHECK(first.Skeleton[index].Parent == second.Skeleton[index].Parent);
        CHECK(first.Skeleton[index].BindPose.Translation == second.Skeleton[index].BindPose.Translation);
        CHECK(first.Skeleton[index].BindPose.Rotation == second.Skeleton[index].BindPose.Rotation);
        CHECK(first.Skeleton[index].BindPose.Scale == second.Skeleton[index].BindPose.Scale);
        CHECK(first.Skeleton[index].InverseBindPose == second.Skeleton[index].InverseBindPose);
    }
    CHECK(first.Influences == second.Influences);
    REQUIRE(first.Influences.size() == mesh.Vertices().size());
    for (const auto& influence : first.Influences)
    {
        CHECK(influence.Count == 8);
        float total = 0.0F;
        for (std::size_t index = 0; index < influence.Count; ++index)
            total += influence.Weights[index];
        CHECK(total == doctest::Approx(1.0F));
    }

    const auto meshId = Keire::AssetId::Generate();
    const auto skeletonId = Keire::AssetId::Generate();
    const auto decoded = Keire::SkinnedMeshAsset::Decode(
        Keire::SkinnedMeshAsset::Encode(meshId, skeletonId, first.Influences, Keire::SkinningMethod::DualQuaternion));
    CHECK(decoded->Mesh() == meshId);
    CHECK(decoded->Skeleton() == skeletonId);
    CHECK(decoded->Method() == Keire::SkinningMethod::DualQuaternion);
    CHECK(decoded->MaximumInfluences() == 8);
    REQUIRE(decoded->Influences8().size() == first.Influences.size());
    for (std::size_t index = 0; index < first.Influences.size(); ++index)
        CHECK(decoded->Influences8()[index] == first.Influences[index]);
}

TEST_CASE("Animator controller authoring permits an empty layer before its first state")
{
    Keire::AnimationGraphDefinition definition;
    definition.SchemaVersion = 2;
    Keire::AnimationLayerDefinition layer;
    layer.Id = "base-layer";
    layer.Name = "Base Layer";
    definition.Layers.push_back(std::move(layer));

    const auto decoded = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(definition));
    REQUIRE(decoded->Definition().Layers.size() == 1);
    CHECK(decoded->Definition().Layers.front().States.empty());
    CHECK(decoded->Definition().Layers.front().EntryStateId.empty());
}

TEST_CASE("Rig definitions round trip semantic chains and reject invalid parent order")
{
    const auto generated = Keire::GenerateRig(BoxMesh(), {});
    const auto decoded = Keire::RigDefinitionAsset::Decode(Keire::RigDefinitionAsset::Encode(generated.Rig));
    CHECK(decoded->Definition().Profile == Keire::RigProfileType::Humanoid);
    CHECK(decoded->Definition().Bones.size() == generated.Rig.Bones.size());
    CHECK(decoded->Definition().Chains.size() == generated.Rig.Chains.size());

    auto invalid = generated.Rig;
    invalid.Bones.front().Parent = 0;
    CHECK_THROWS_AS(Keire::ValidateRigDefinition(invalid), std::invalid_argument);
}

TEST_CASE("Animation retargeting applies source deltas to the target bind pose")
{
    const std::vector<Keire::SkeletonBone> sourceBones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                       {"Hips", 0, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                       {"Pelvis", 0, {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const auto sourceSkeletonId = Keire::AssetId::Generate();
    const auto targetSkeletonId = Keire::AssetId::Generate();
    const Keire::AnimationClipAsset sourceClip(sourceSkeletonId, 1.0F,
                                               {{1, {{0.0F, {{1.0F, 3.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}}}});

    const auto retargeted = Keire::RetargetAnimationClip(sourceSkeleton, sourceRig, sourceClip, targetSkeletonId,
                                                         targetSkeleton, targetRig);

    REQUIRE(retargeted);
    CHECK(retargeted->Skeleton() == targetSkeletonId);
    REQUIRE(retargeted->Tracks().size() == 1);
    CHECK(retargeted->Tracks().front().Bone == 1);
    REQUIRE(retargeted->Tracks().front().Keys.size() == 1);
    CHECK((retargeted->Tracks().front().Keys.front().Value.Translation == Keire::Vector3{3.0F, 1.0F, 0.0F}));
}

TEST_CASE("Animation retargeting rejects pathological source scale ratios")
{
    const std::vector<Keire::SkeletonBone> sourceBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"Hips", 0, {{0.0F, 1.0F, 0.0F}, {}, {0.01F, 0.01F, 0.01F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                       {"Pelvis", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const auto sourceSkeletonId = Keire::AssetId::Generate();
    const auto targetSkeletonId = Keire::AssetId::Generate();
    const Keire::AnimationClipAsset sourceClip(sourceSkeletonId, 1.0F,
                                               {{1, {{0.0F, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}}}});

    const auto result = Keire::RetargetAnimationClipWithDiagnostics(sourceSkeleton, sourceRig, sourceClip,
                                                                    targetSkeletonId, targetSkeleton, targetRig);
    const auto& retargeted = result.Clip;

    REQUIRE(retargeted);
    REQUIRE(retargeted->Tracks().size() == 1);
    CHECK((retargeted->Tracks().front().Keys.front().Value.Scale == Keire::Vector3{1.0F, 1.0F, 1.0F}));
    REQUIRE(result.Diagnostics.Mappings.size() == 1);
    CHECK(result.Diagnostics.Mappings.front().ScaleFallbackKeyCount == 3);
}

TEST_CASE("Animation retarget diagnostics distinguish exact semantic and unmapped tracks")
{
    const std::vector<Keire::SkeletonBone> sourceBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"Hips", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"Accessory", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                       {"Pelvis", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const Keire::AnimationClipAsset sourceClip(Keire::AssetId::Generate(), 1.0F,
                                               {{0, {{0.0F, {}}}}, {1, {{0.0F, {}}}}, {2, {{0.0F, {}}}}}, {}, true);

    const auto diagnostics =
        Keire::DiagnoseAnimationRetargeting(sourceSkeleton, sourceRig, sourceClip, targetSkeleton, targetRig);

    CHECK(diagnostics.Compatible());
    CHECK(diagnostics.SourceTrackCount == 3);
    CHECK(diagnostics.MappedTrackCount == 2);
    CHECK(diagnostics.ExactNameMatchCount == 1);
    CHECK(diagnostics.SemanticMatchCount == 1);
    CHECK(diagnostics.RootMotionMapped);
    REQUIRE(diagnostics.Mappings.size() == 3);
    CHECK(diagnostics.Mappings[0].Match == Keire::AnimationRetargetMatch::ExactName);
    CHECK(diagnostics.Mappings[1].Match == Keire::AnimationRetargetMatch::Semantic);
    CHECK(diagnostics.Mappings[2].Match == Keire::AnimationRetargetMatch::Unmapped);
    CHECK(std::ranges::any_of(diagnostics.Messages,
                              [](const auto& diagnostic) { return diagnostic.Code == "KEIRERETARGET0002"; }));
}

TEST_CASE("Animation retargeting prefers exact bone names without semantic mappings")
{
    const std::vector<Keire::SkeletonBone> sourceBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"SharedBone", 0, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"SharedBone", 0, {{0.0F, 4.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const auto sourceSkeletonId = Keire::AssetId::Generate();
    const auto targetSkeletonId = Keire::AssetId::Generate();
    const Keire::AnimationClipAsset sourceClip(sourceSkeletonId, 1.0F,
                                               {{1, {{0.0F, {{0.0F, 3.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}}}});

    const auto retargeted = Keire::RetargetAnimationClip(sourceSkeleton, sourceRig, sourceClip, targetSkeletonId,
                                                         targetSkeleton, targetRig);

    REQUIRE(retargeted);
    REQUIRE(retargeted->Tracks().size() == 1);
    CHECK(retargeted->Tracks().front().Bone == 1);
    CHECK((retargeted->Tracks().front().Keys.front().Value.Translation == Keire::Vector3{0.0F, 6.0F, 0.0F}));
}

TEST_CASE("Animation retargeting preserves authored Assimp FBX rotation helper tracks")
{
    const Keire::Quaternion sourceBindRotation{0.0F, 0.0F, 0.3826834F, 0.9238795F};
    const Keire::Quaternion authoredRotation{0.0F, 0.0F, 0.1305262F, 0.9914449F};
    const std::vector<Keire::SkeletonBone> sourceBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftArm_$AssimpFbx$_Rotation", 0, {{}, sourceBindRotation, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftArm_$AssimpFbx$_Rotation", 0, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const Keire::AnimationClipAsset sourceClip(Keire::AssetId::Generate(), 1.0F,
                                               {{1, {{0.0F, {{}, authoredRotation, {1.0F, 1.0F, 1.0F}}}}}});

    const auto retargeted = Keire::RetargetAnimationClip(sourceSkeleton, sourceRig, sourceClip,
                                                         Keire::AssetId::Generate(), targetSkeleton, targetRig);

    REQUIRE(retargeted);
    REQUIRE(retargeted->Tracks().size() == 1);
    REQUIRE(retargeted->Tracks().front().Keys.size() == 1);
    const auto rotation = retargeted->Tracks().front().Keys.front().Value.Rotation;
    CHECK(rotation.X == doctest::Approx(authoredRotation.X));
    CHECK(rotation.Y == doctest::Approx(authoredRotation.Y));
    CHECK(rotation.Z == doctest::Approx(authoredRotation.Z));
    CHECK(rotation.W == doctest::Approx(authoredRotation.W));
}

TEST_CASE("Two bone and FABRIK solvers reject malformed chains and move valid chains toward targets")
{
    const std::vector<Keire::SkeletonBone> bones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Middle", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"End", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const auto skeleton = Keire::CreateRef<Keire::SkeletonAsset>(bones);
    std::vector<Keire::BoneTransform> pose;
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    CHECK(Keire::SolveTwoBoneIk(*skeleton, pose, {0, 1, 2, {1.0F, 1.0F, 0.0F}}));
    Keire::FabrikIkRequest fabrik;
    fabrik.Chain = {0, 1, 2};
    fabrik.Target = {-1.0F, 1.0F, 0.0F};
    CHECK(Keire::SolveFabrikIk(*skeleton, pose, fabrik));
    fabrik.Chain = {0, 2};
    CHECK_FALSE(Keire::SolveFabrikIk(*skeleton, pose, fabrik));
}

TEST_CASE("Foot grounding adapts pelvis and legs transactionally to validated contacts")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"UpperLeg", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LowerLeg", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Foot", 2, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::FootGroundingRequest request;
    request.Pelvis = 0;
    request.FootHeight = 0.0F;
    request.MaximumPelvisAdjustment = 0.5F;
    request.Contacts.push_back({1, 2, 3, {0.5F, 2.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 1.0F}});
    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->SolvedFeet == 1);
    CHECK(solved->PelvisAdjustment == doctest::Approx(-0.5F));
    CHECK(pose.front().Translation.Y == doctest::Approx(-0.5F));
    CHECK(pose[1].Rotation != Keire::Quaternion{});

    const auto lastGood = pose;
    request.Contacts.push_back(request.Contacts.front());
    CHECK_FALSE(Keire::SolveFootGrounding(skeleton, pose, request));
    CHECK(pose == lastGood);
}

TEST_CASE("Ragdoll pose transitions blend deterministically and support interruption")
{
    const std::vector<Keire::BoneTransform> animation(2);
    const std::vector<Keire::BoneTransform> ragdoll{
        {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
        {{0.0F, 4.0F, 0.0F}, Keire::Math::EulerDegreesToQuaternion({0.0F, 90.0F, 0.0F}), {1.0F, 1.0F, 1.0F}}};
    Keire::RagdollPoseTransition transition;
    transition.SetRagdoll(true, 1.0F);
    const auto halfway = transition.Update(0.5F, animation, ragdoll);
    CHECK(transition.Mode() == Keire::RagdollPoseMode::TransitionToRagdoll);
    CHECK(transition.Weight() == doctest::Approx(0.5F));
    CHECK(halfway[0].Translation.X == doctest::Approx(1.0F));
    CHECK(halfway[1].Translation.Y == doctest::Approx(2.0F));

    transition.SetRagdoll(false, 0.5F);
    const auto animated = transition.Update(0.5F, animation, ragdoll);
    CHECK(transition.Mode() == Keire::RagdollPoseMode::Animated);
    CHECK(transition.Weight() == doctest::Approx(0.0F));
    CHECK(animated == animation);

    transition.SetRagdoll(true, 0.0F);
    CHECK(transition.Mode() == Keire::RagdollPoseMode::Ragdoll);
    CHECK(transition.Update(0.0F, animation, ragdoll) == ragdoll);
    const auto lastGoodWeight = transition.Weight();
    CHECK_THROWS_AS((void)transition.Update(-1.0F, animation, ragdoll), std::invalid_argument);
    CHECK(transition.Weight() == doctest::Approx(lastGoodWeight));
}
