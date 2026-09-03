#include "Keire/Animation/RiggingSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
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

    [[nodiscard]] std::vector<Keire::Matrix4> ModelMatrices(const Keire::SkeletonAsset& skeleton,
                                                            const std::span<const Keire::BoneTransform> pose)
    {
        std::vector<Keire::Matrix4> result(pose.size());
        for (std::size_t index = 0; index < pose.size(); ++index)
        {
            result[index] =
                Keire::Math::ComposeTransform(pose[index].Translation, pose[index].Rotation, pose[index].Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            if (parent >= 0)
                result[index] = Keire::Math::Multiply(result[static_cast<std::size_t>(parent)], result[index]);
        }
        return result;
    }

    [[nodiscard]] float Distance(const Keire::Vector3 left, const Keire::Vector3 right)
    {
        const auto x = right.X - left.X;
        const auto y = right.Y - left.Y;
        const auto z = right.Z - left.Z;
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] Keire::Quaternion Multiply(const Keire::Quaternion left, const Keire::Quaternion right) noexcept
    {
        return {left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
                left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
                left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
                left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z};
    }

    [[nodiscard]] float RotationDot(const Keire::Quaternion left, const Keire::Quaternion right) noexcept
    {
        return std::abs(left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W);
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
    CHECK_FALSE(decoded->HasCompleteInfluenceBounds());
    CHECK(decoded->InfluenceBounds().empty());
    REQUIRE(decoded->Influences8().size() == first.Influences.size());
    for (std::size_t index = 0; index < first.Influences.size(); ++index)
        CHECK(decoded->Influences8()[index] == first.Influences[index]);

    const auto v4WithoutBounds =
        Keire::SkinnedMeshAsset::Encode(meshId, skeletonId, first.Influences, Keire::SkinningMethod::LinearBlend);
    auto legacyDocument = nlohmann::json::from_cbor(
        reinterpret_cast<const std::uint8_t*>(v4WithoutBounds.data()),
        reinterpret_cast<const std::uint8_t*>(v4WithoutBounds.data() + v4WithoutBounds.size()));
    legacyDocument["schemaVersion"] = 3;
    legacyDocument.erase("influenceBoundsComplete");
    legacyDocument.erase("influenceBoundsSubmeshCount");
    legacyDocument.erase("influenceBoundsCount");
    legacyDocument.erase("influenceBoundsStride");
    legacyDocument.erase("influenceBounds");
    const auto legacyCbor = nlohmann::json::to_cbor(legacyDocument);
    const std::span legacyBytes{reinterpret_cast<const std::byte*>(legacyCbor.data()), legacyCbor.size()};
    const auto legacy = Keire::SkinnedMeshAsset::Decode(legacyBytes);
    CHECK_FALSE(legacy->HasCompleteInfluenceBounds());
    CHECK(legacy->InfluenceBounds().empty());
    CHECK(std::ranges::equal(legacy->Influences8(), decoded->Influences8()));

    const auto bindBounds = Keire::CalculateBindSpaceSkinInfluenceBounds(mesh.Vertices(), mesh.Indices(),
                                                                         mesh.Submeshes(), first.Influences);
    const auto completeBytes =
        Keire::SkinnedMeshAsset::Encode(meshId, skeletonId, first.Influences, Keire::SkinningMethod::LinearBlend,
                                        static_cast<std::uint32_t>(mesh.Submeshes().size()), bindBounds);
    const auto complete = Keire::SkinnedMeshAsset::Decode(completeBytes);
    CHECK(complete->HasCompleteInfluenceBounds());
    CHECK(complete->InfluenceBoundsSubmeshCount() == mesh.Submeshes().size());
    CHECK(std::ranges::equal(complete->InfluenceBounds(), bindBounds));

    auto malformedDocument =
        nlohmann::json::from_cbor(reinterpret_cast<const std::uint8_t*>(completeBytes.data()),
                                  reinterpret_cast<const std::uint8_t*>(completeBytes.data() + completeBytes.size()));
    malformedDocument["influenceBoundsSubmeshCount"] = bindBounds.size() + 1U;
    const auto malformedCbor = nlohmann::json::to_cbor(malformedDocument);
    const std::span malformedBytes{reinterpret_cast<const std::byte*>(malformedCbor.data()), malformedCbor.size()};
    CHECK_THROWS_AS((void)Keire::SkinnedMeshAsset::Decode(malformedBytes), std::invalid_argument);

    auto incompleteBounds = bindBounds;
    incompleteBounds.clear();
    CHECK_THROWS_AS((void)Keire::SkinnedMeshAsset::Encode(meshId, skeletonId, first.Influences,
                                                          Keire::SkinningMethod::LinearBlend, 1, incompleteBounds),
                    std::invalid_argument);
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

TEST_CASE("Animation retargeting collapses Assimp FBX helpers without a reference-pose jump")
{
    const Keire::Quaternion sourceBindRotation{0.0F, 0.0F, 0.3826834F, 0.9238795F};
    const Keire::Quaternion animatedRotation{0.0F, 0.0F, 0.7071068F, 0.7071068F};
    const std::vector<Keire::SkeletonBone> sourceBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftArm_$AssimpFbx$_Translation",
         0,
         {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
         {}},
        {"mixamorig:LeftArm_$AssimpFbx$_Rotation", 1, {{}, sourceBindRotation, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftArm", 2, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftHand", 3, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::SkeletonBone> targetBones{
        {"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftArm", 0, {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftHand", 1, {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset sourceSkeleton(sourceBones);
    const Keire::SkeletonAsset targetSkeleton(targetBones);
    const auto sourceRig = Keire::InferRigDefinition(sourceSkeleton);
    const auto targetRig = Keire::InferRigDefinition(targetSkeleton);
    const Keire::AnimationClipAsset sourceClip(
        Keire::AssetId::Generate(), 1.0F,
        {{2,
          {{0.0F, {{}, sourceBindRotation, {1.0F, 1.0F, 1.0F}}},
           {1.0F, {{}, animatedRotation, {1.0F, 1.0F, 1.0F}}}}}});

    const auto result = Keire::RetargetAnimationClipWithDiagnostics(
        sourceSkeleton, sourceRig, sourceClip, Keire::AssetId::Generate(), targetSkeleton, targetRig);
    const auto& retargeted = result.Clip;

    REQUIRE(retargeted);
    REQUIRE(retargeted->Tracks().size() == 1);
    CHECK(retargeted->Tracks().front().Bone == 1);
    REQUIRE(retargeted->Tracks().front().Keys.size() == 2);
    const auto& reference = retargeted->Tracks().front().Keys.front().Value;
    CHECK((reference.Translation == Keire::Vector3{2.0F, 0.0F, 0.0F}));
    CHECK(RotationDot(reference.Rotation, {}) > 0.99999F);

    const Keire::Quaternion sourceBindInverse{-sourceBindRotation.X, -sourceBindRotation.Y, -sourceBindRotation.Z,
                                              sourceBindRotation.W};
    const auto expectedDelta = Multiply(sourceBindInverse, animatedRotation);
    const auto& animated = retargeted->Tracks().front().Keys.back().Value;
    CHECK(RotationDot(animated.Rotation, expectedDelta) > 0.9999F);
    CHECK(result.Diagnostics.HierarchyMatchCount == 1);
    REQUIRE(result.Diagnostics.Mappings.size() == 1);
    CHECK(result.Diagnostics.Mappings.front().Match == Keire::AnimationRetargetMatch::Hierarchy);

    std::vector<Keire::BoneTransform> referencePose;
    std::ranges::transform(targetBones, std::back_inserter(referencePose), &Keire::SkeletonBone::BindPose);
    referencePose[1] = reference;
    const auto referenceModels = ModelMatrices(targetSkeleton, referencePose);
    const auto bindModels = ModelMatrices(targetSkeleton, {referencePose.data(), referencePose.size()});
    CHECK(Distance(Keire::Math::TransformPoint(referenceModels[2], {}),
                   Keire::Math::TransformPoint(bindModels[2], {})) < 0.00001F);
}

TEST_CASE("Two bone and FABRIK solvers reject malformed chains and move valid chains toward targets")
{
    const std::vector<Keire::SkeletonBone> bones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Middle", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"End", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const auto skeleton = Keire::CreateRef<Keire::SkeletonAsset>(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
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

TEST_CASE("Two bone IK preserves model-space targets under rotated parents and orients the end effector")
{
    const std::vector<Keire::SkeletonBone> bones{
        {"Parent", -1, {{}, Keire::Math::EulerDegreesToQuaternion({0.0F, 0.0F, 35.0F}), {1.0F, 1.0F, 1.0F}}, {}},
        {"UpperArm", 0, {{0.0F, 0.5F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LowerArm", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"Hand", 2, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::TwoBoneIkRequest request{1, 2, 3, {0.8F, 1.2F, 0.35F}, {0.0F, 0.0F, 2.0F}, 1.0F};
    request.EndRotation = Keire::Math::EulerDegreesToQuaternion({15.0F, 40.0F, -10.0F});
    request.EndRotationWeight = 1.0F;
    REQUIRE(Keire::SolveTwoBoneIk(skeleton, pose, request));

    const auto model = ModelMatrices(skeleton, pose);
    CHECK(Distance(Keire::Math::TransformPoint(model[3], {}), request.Target) < 0.002F);
    Keire::Vector3 ignoredPosition;
    Keire::Vector3 ignoredScale;
    Keire::Quaternion handRotation;
    REQUIRE(Keire::Math::DecomposeTransform(model[3], ignoredPosition, handRotation, ignoredScale));
    const auto targetRotation = Keire::Math::Normalize(*request.EndRotation);
    const auto rotationDot = std::abs(handRotation.X * targetRotation.X + handRotation.Y * targetRotation.Y +
                                      handRotation.Z * targetRotation.Z + handRotation.W * targetRotation.W);
    CHECK(rotationDot > 0.999F);

    std::vector<Keire::BoneTransform> rotationOnlyPose;
    rotationOnlyPose.reserve(bones.size());
    for (const auto& bone : bones)
        rotationOnlyPose.push_back(bone.BindPose);
    const auto initialHandPosition = Keire::Math::TransformPoint(ModelMatrices(skeleton, rotationOnlyPose)[3], {});
    request.Weight = 0.0F;
    request.EndRotation = Keire::Math::EulerDegreesToQuaternion({-20.0F, 15.0F, 25.0F});
    REQUIRE(Keire::SolveTwoBoneIk(skeleton, rotationOnlyPose, request));
    const auto rotationOnlyModel = ModelMatrices(skeleton, rotationOnlyPose);
    CHECK(Distance(Keire::Math::TransformPoint(rotationOnlyModel[3], {}), initialHandPosition) < 0.00001F);
    REQUIRE(Keire::Math::DecomposeTransform(rotationOnlyModel[3], ignoredPosition, handRotation, ignoredScale));
    const auto rotationOnlyTarget = Keire::Math::Normalize(*request.EndRotation);
    const auto rotationOnlyDot =
        std::abs(handRotation.X * rotationOnlyTarget.X + handRotation.Y * rotationOnlyTarget.Y +
                 handRotation.Z * rotationOnlyTarget.Z + handRotation.W * rotationOnlyTarget.W);
    CHECK(rotationOnlyDot > 0.999F);
}

TEST_CASE("Two bone IK stays finite and bent at its folded and extended reach limits")
{
    const std::vector<Keire::SkeletonBone> bones{{"UpperArm", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LowerArm", 0, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Hand", 1, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    const auto solve = [&](const Keire::Vector3 target)
    {
        std::vector<Keire::BoneTransform> pose;
        pose.reserve(bones.size());
        for (const auto& bone : bones)
            pose.push_back(bone.BindPose);
        REQUIRE(Keire::SolveTwoBoneIk(skeleton, pose, {0, 1, 2, target, {0.0F, 0.0F, 1.0F}}));
        return ModelMatrices(skeleton, pose);
    };

    const auto folded = solve({});
    const auto extended = solve({3.0F, 0.0F, 0.0F});
    for (const auto& matrix : {folded[0], folded[1], folded[2], extended[0], extended[1], extended[2]})
    {
        for (const auto element : matrix.Elements)
            CHECK(std::isfinite(element));
    }
    CHECK(std::abs(Keire::Math::TransformPoint(folded[1], {}).Z) > 0.0001F);
    CHECK(std::abs(Keire::Math::TransformPoint(extended[1], {}).Z) > 0.0001F);
}

TEST_CASE("Foot grounding adapts pelvis and legs transactionally to validated contacts")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"UpperLeg", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LowerLeg", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Foot", 2, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
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

TEST_CASE("Foot grounding reach diagnostics use the caller's model-space tolerance")
{
    const std::vector<Keire::SkeletonBone> bones{{"Upper", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Lower", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Foot", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    const auto bindPose = [&]
    {
        std::vector<Keire::BoneTransform> result;
        result.reserve(bones.size());
        for (const auto& bone : bones)
            result.push_back(bone.BindPose);
        return result;
    }();

    Keire::FootGroundingRequest request;
    request.FootHeight = 0.0F;
    request.Contacts.push_back({0, 1, 2, {0.0F, 2.005F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 1.0F}});

    auto tolerantPose = bindPose;
    request.PositionTolerance = 0.01F;
    const auto tolerant = Keire::SolveFootGrounding(skeleton, tolerantPose, request);
    REQUIRE(tolerant);
    CHECK(tolerant->UnreachableFeet == 0);

    auto strictPose = bindPose;
    request.PositionTolerance = 0.001F;
    const auto strict = Keire::SolveFootGrounding(skeleton, strictPose, request);
    REQUIRE(strict);
    CHECK(strict->UnreachableFeet == 1);

    request.PositionTolerance = 0.0F;
    CHECK_FALSE(Keire::SolveFootGrounding(skeleton, strictPose, request));
}

TEST_CASE("Foot grounding lowers the pelvis once and plants both feet on uneven ground")
{
    const std::vector<Keire::SkeletonBone> bones{
        {"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftUpperLeg", 0, {{-0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftLowerLeg", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftFoot", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"RightUpperLeg", 0, {{0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"RightLowerLeg", 4, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"RightFoot", 5, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::FootGroundingRequest request;
    request.Pelvis = 0;
    request.MaximumPelvisAdjustment = 0.75F;
    request.FootHeight = 0.0F;
    request.Contacts.push_back({1, 2, 3, {-0.25F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.25F, 1.0F, 1.0F}});
    request.Contacts.push_back({4, 5, 6, {0.25F, -0.4F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.25F, 1.0F, 1.0F}});

    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->SolvedFeet == 2);
    CHECK(solved->UnreachableFeet == 0);
    CHECK(solved->PelvisAdjustment == doctest::Approx(-0.4F));
    const auto model = ModelMatrices(skeleton, pose);
    CHECK(Distance(Keire::Math::TransformPoint(model[3], {}), request.Contacts[0].Position) < 0.01F);
    CHECK(Distance(Keire::Math::TransformPoint(model[6], {}), request.Contacts[1].Position) < 0.01F);
}

TEST_CASE("Foot grounding solves imported FBX leg chains containing transform helper bones")
{
    const std::vector<Keire::SkeletonBone> bones{
        {"mixamorig:Hips", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftUpLeg_$AssimpFbx$_Translation", 0, {{-0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftUpLeg_$AssimpFbx$_PreRotation", 1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftUpLeg_$AssimpFbx$_Rotation", 2, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftUpLeg", 3, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftLeg_$AssimpFbx$_Translation", 4, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftLeg_$AssimpFbx$_PreRotation", 5, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftLeg", 6, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftFoot_$AssimpFbx$_Translation", 7, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftFoot_$AssimpFbx$_PreRotation", 8, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftFoot_$AssimpFbx$_Rotation", 9, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"mixamorig:LeftFoot", 10, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    const auto rig = Keire::InferRigDefinition(skeleton);
    CHECK(rig.Bones[1].Semantic == Keire::RigBoneSemantic::None);
    CHECK(rig.Bones[5].Semantic == Keire::RigBoneSemantic::None);
    CHECK(rig.Bones[8].Semantic == Keire::RigBoneSemantic::None);
    CHECK(rig.Bones[4].Semantic == Keire::RigBoneSemantic::LeftUpperLeg);
    CHECK(rig.Bones[7].Semantic == Keire::RigBoneSemantic::LeftLowerLeg);
    CHECK(rig.Bones[11].Semantic == Keire::RigBoneSemantic::LeftFoot);

    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::FootGroundingRequest request;
    request.FootHeight = 0.0F;
    request.Contacts.push_back({4, 7, 11, {0.15F, 0.25F, 0.15F}, {0.0F, 1.0F, 0.0F}, {-0.25F, 1.0F, 1.0F}});
    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->SolvedFeet == 1);
    CHECK(solved->UnreachableFeet == 0);
    const auto model = ModelMatrices(skeleton, pose);
    CHECK(Distance(Keire::Math::TransformPoint(model[11], {}), request.Contacts[0].Position) < 0.01F);
    CHECK(pose[4].Rotation != Keire::Quaternion{});
    CHECK(pose[7].Rotation != Keire::Quaternion{});
}

TEST_CASE("Foot grounding uses imported bind axes to flatten animated foot pitch onto the surface")
{
    const auto importedFootRotation = Keire::Math::EulerDegreesToQuaternion({90.0F, 15.0F, 0.0F});
    const std::vector<Keire::SkeletonBone> bones{
        {"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"UpperLeg", 0, {{-0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LowerLeg", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"Foot", 2, {{0.0F, -1.0F, 0.0F}, importedFootRotation, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    auto sampledPose = [&]
    {
        std::vector<Keire::BoneTransform> result;
        result.reserve(bones.size());
        for (const auto& bone : bones)
            result.push_back(bone.BindPose);
        return result;
    }();
    const auto animatedPitch = Keire::Math::EulerDegreesToQuaternion({25.0F, 0.0F, 0.0F});
    sampledPose[3].Rotation = Keire::Math::Normalize(Multiply(animatedPitch, importedFootRotation));
    Keire::Vector3 ignoredPosition;
    Keire::Vector3 ignoredScale;
    Keire::Quaternion sampledRotation;
    REQUIRE(Keire::Math::DecomposeTransform(ModelMatrices(skeleton, sampledPose)[3], ignoredPosition, sampledRotation,
                                            ignoredScale));

    auto flatPose = sampledPose;
    Keire::FootGroundingRequest request;
    request.FootHeight = 0.0F;
    request.Contacts.push_back({1, 2, 3, {0.15F, 0.25F, 0.15F}, {0.0F, 1.0F, 0.0F}, {-0.25F, 1.0F, 1.0F}});
    REQUIRE(Keire::SolveFootGrounding(skeleton, flatPose, request));
    Keire::Quaternion flatRotation;
    REQUIRE(Keire::Math::DecomposeTransform(ModelMatrices(skeleton, flatPose)[3], ignoredPosition, flatRotation,
                                            ignoredScale));
    CHECK(RotationDot(flatRotation, importedFootRotation) > 0.9999F);
    CHECK(RotationDot(flatRotation, sampledRotation) < 0.999F);

    auto slopePose = sampledPose;
    const auto slopeRotation = Keire::Math::EulerDegreesToQuaternion({0.0F, 0.0F, 30.0F});
    request.Contacts.front().Normal = Keire::Math::TransformDirection(
        Keire::Math::ComposeTransform({}, slopeRotation, {1.0F, 1.0F, 1.0F}), {0.0F, 1.0F, 0.0F});
    REQUIRE(Keire::SolveFootGrounding(skeleton, slopePose, request));
    Keire::Quaternion groundedSlopeRotation;
    REQUIRE(Keire::Math::DecomposeTransform(ModelMatrices(skeleton, slopePose)[3], ignoredPosition,
                                            groundedSlopeRotation, ignoredScale));
    const auto groundedFromBind = Keire::Math::Multiply(
        Keire::Math::ComposeTransform({}, groundedSlopeRotation, {1.0F, 1.0F, 1.0F}),
        Keire::Math::Inverse(Keire::Math::ComposeTransform({}, importedFootRotation, {1.0F, 1.0F, 1.0F})));
    const auto groundedSoleNormal = Keire::Math::TransformDirection(groundedFromBind, {0.0F, 1.0F, 0.0F});
    CHECK(Distance(groundedSoleNormal, request.Contacts.front().Normal) < 0.0001F);
}

TEST_CASE("Foot grounding restores a discovered toe control to its planted bind rotation")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Upper", 0, {{-0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Lower", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Ankle", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"ToeControl", 3, {{0.0F, -0.1F, 0.3F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);
    pose[4].Rotation = Keire::Math::EulerDegreesToQuaternion({-35.0F, 0.0F, 0.0F});

    Keire::FootGroundingRequest request;
    request.FootHeight = 0.0F;
    Keire::FootGroundContact contact{1, 2, 3, {-0.2F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.2F, 1.0F, 1.0F}};
    contact.Toe = 4;
    request.Contacts.push_back(contact);
    REQUIRE(Keire::SolveFootGrounding(skeleton, pose, request));
    CHECK(RotationDot(pose[4].Rotation, bones[4].BindPose.Rotation) > 0.9999F);
}

TEST_CASE("Foot grounding restores the rig's bind-neutral pelvis offset over two planted feet")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{0.0F, 2.0F, 0.5F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftUpper", 0, {{-0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftLower", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftFoot", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightUpper", 0, {{0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightLower", 4, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightFoot", 5, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::FootGroundingRequest request;
    request.Pelvis = 0;
    request.FootHeight = 0.0F;
    request.MaximumPelvisAdjustment = 0.5F;
    request.MaximumHorizontalPelvisAdjustment = 0.2F;
    request.PelvisSupportRadius = 0.1F;
    request.Contacts.push_back({1, 2, 3, {-0.2F, 0.1F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.2F, 1.0F, 1.0F}});
    request.Contacts.push_back({4, 5, 6, {0.2F, 0.1F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.2F, 1.0F, 1.0F}});

    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->PelvisAdjustment == doctest::Approx(0.0F));
    CHECK(solved->HorizontalPelvisAdjustment.Z == doctest::Approx(-0.2F));
    CHECK(pose[0].Translation.Y == doctest::Approx(2.0F));
    CHECK(pose[0].Translation.Z == doctest::Approx(0.3F));
}

TEST_CASE("Foot grounding shifts the pelvis toward a single ledge support")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftUpper", 0, {{-0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftLower", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftFoot", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);

    Keire::FootGroundingRequest request;
    request.Pelvis = 0;
    request.FootHeight = 0.0F;
    request.MaximumPelvisAdjustment = 0.5F;
    request.MaximumHorizontalPelvisAdjustment = 0.2F;
    request.PelvisSupportRadius = 0.05F;
    request.Contacts.push_back({1, 2, 3, {-0.45F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.45F, 1.0F, 1.0F}});

    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->SolvedFeet == 1);
    CHECK(solved->HorizontalPelvisAdjustment.X == doctest::Approx(-0.2F));
    CHECK(pose[0].Translation.X == doctest::Approx(-0.2F));
}

TEST_CASE("Foot grounding removes bounded pelvis pitch using the rig's own torso axis")
{
    const std::vector<Keire::SkeletonBone> bones{{"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Spine", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftUpper", 0, {{-0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftLower", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"LeftFoot", 3, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightUpper", 0, {{0.2F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightLower", 5, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"RightFoot", 6, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::BoneTransform> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.BindPose);
    pose[0].Rotation = Keire::Math::EulerDegreesToQuaternion({25.0F, 0.0F, 0.0F});
    const auto before = ModelMatrices(skeleton, pose);
    const auto beforePelvis = Keire::Math::TransformPoint(before[0], {});
    const auto beforeTorso = Keire::Math::TransformPoint(before[1], {});

    Keire::FootGroundingRequest request;
    request.Pelvis = 0;
    request.Torso = 1;
    request.FootHeight = 0.0F;
    request.MaximumPelvisAdjustment = 0.0F;
    request.PelvisRotationWeight = 1.0F;
    request.MaximumPelvisRotationDegrees = 10.0F;
    request.Contacts.push_back({2, 3, 4, {-0.2F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.2F, 1.0F, 1.0F}});
    request.Contacts.push_back({5, 6, 7, {0.2F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.2F, 1.0F, 1.0F}});

    const auto solved = Keire::SolveFootGrounding(skeleton, pose, request);
    REQUIRE(solved);
    CHECK(solved->PelvisRotationAdjustmentDegrees == doctest::Approx(10.0F).epsilon(0.01));
    const auto after = ModelMatrices(skeleton, pose);
    const auto afterPelvis = Keire::Math::TransformPoint(after[0], {});
    const auto afterTorso = Keire::Math::TransformPoint(after[1], {});
    CHECK(std::abs(afterTorso.Z - afterPelvis.Z) < std::abs(beforeTorso.Z - beforePelvis.Z));
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
