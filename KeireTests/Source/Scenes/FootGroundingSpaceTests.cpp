#include "KeireInternal/Scenes/AnimationIkPasses.h"
#include "KeireInternal/Scenes/FootGroundingSpace.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

namespace
{
    [[nodiscard]] float Distance(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        const auto x = right.X - left.X;
        const auto y = right.Y - left.Y;
        const auto z = right.Z - left.Z;
        return std::sqrt(x * x + y * y + z * z);
    }
} // namespace

TEST_CASE("Foot grounding converts world-space authoring distances for scaled animated models")
{
    const auto modelToWorld = Keire::Math::ComposeTransform(
        {2.0F, 0.5F, -1.0F}, Keire::Math::EulerDegreesToQuaternion({0.0F, 35.0F, 0.0F}), {0.01F, 0.01F, 0.01F});
    const auto worldToModel = Keire::Math::Inverse(modelToWorld);
    const Keire::Vector3 hitPosition{2.25F, 0.6F, -0.75F};
    const Keire::Vector3 hitNormal{0.0F, 1.0F, 0.0F};

    const auto contact = Keire::Detail::ToModelFootGroundContact(worldToModel, hitPosition, hitNormal, 0.0F);
    REQUIRE(contact);
    const auto reconstructedWorldPosition = Keire::Math::TransformPoint(modelToWorld, contact->Position);
    CHECK(Distance(reconstructedWorldPosition, hitPosition) < 0.00001F);

    const auto minimumClearance = Keire::Detail::WorldSurfaceDistanceToModel(worldToModel, hitNormal, 0.02F);
    REQUIRE(minimumClearance);
    const auto minimumTarget = Keire::Detail::FootTargetAboveSurface(*contact, 1.0F, *minimumClearance);
    REQUIRE(minimumTarget);
    CHECK(Distance(Keire::Math::TransformPoint(modelToWorld, *minimumTarget),
                   {hitPosition.X, hitPosition.Y + 0.02F, hitPosition.Z}) < 0.00001F);

    const auto automaticTarget = Keire::Detail::FootTargetAboveSurface(*contact, 3.0F, *minimumClearance);
    REQUIRE(automaticTarget);
    CHECK(Distance(Keire::Math::TransformPoint(modelToWorld, *automaticTarget),
                   {hitPosition.X, hitPosition.Y + 0.03F, hitPosition.Z}) < 0.00001F);

    const auto modelPelvisLimit = Keire::Detail::WorldVerticalDistanceToModel(worldToModel, 0.5F);
    CHECK(modelPelvisLimit == doctest::Approx(50.0F));
    const auto reconstructedWorldAdjustment =
        Keire::Math::TransformDirection(modelToWorld, {0.0F, modelPelvisLimit, 0.0F});
    CHECK(Distance(reconstructedWorldAdjustment, {0.0F, 0.5F, 0.0F}) < 0.00001F);

    CHECK_FALSE(Keire::Detail::ToModelFootGroundContact(worldToModel, hitPosition, {}, 0.02F));
    CHECK_FALSE(Keire::Detail::WorldSurfaceDistanceToModel(worldToModel, {}, 0.02F));
    CHECK_FALSE(Keire::Detail::FootTargetAboveSurface(*contact, -1.0F, 0.0F));
}

TEST_CASE("Planted-foot support anchors follow translated rotated and scaled platforms")
{
    const auto initialSupport = Keire::Math::ComposeTransform(
        {2.0F, 0.5F, -1.0F}, Keire::Math::EulerDegreesToQuaternion({0.0F, 25.0F, 0.0F}), {2.0F, 0.5F, 1.5F});
    const Keire::Vector3 initialPosition{2.25F, 0.75F, -0.5F};
    const Keire::Vector3 initialNormal{0.0F, 1.0F, 0.0F};
    const auto anchor = Keire::Detail::CaptureFootPlantSupportAnchor(initialSupport, initialPosition, initialNormal);
    REQUIRE(anchor);

    const auto movedSupport = Keire::Math::ComposeTransform(
        {-3.0F, 4.0F, 2.0F}, Keire::Math::EulerDegreesToQuaternion({15.0F, -40.0F, 20.0F}), {0.75F, 2.0F, 1.25F});
    const auto resolved = Keire::Detail::ResolveFootPlantSupportAnchor(movedSupport, *anchor);
    REQUIRE(resolved);
    CHECK(Distance(resolved->Position, Keire::Math::TransformPoint(movedSupport, anchor->LocalPosition)) < 0.00001F);
    CHECK(Distance(resolved->Position, initialPosition) > 1.0F);
    CHECK_FALSE(
        Keire::Detail::ShouldReleaseAutomaticFootPlant(initialPosition, initialPosition, initialNormal, 2.0F, 0.18F));

    Keire::Vector3 ignoredPosition;
    Keire::Vector3 ignoredScale;
    Keire::Quaternion movedRotation;
    REQUIRE(Keire::Math::DecomposeTransform(movedSupport, ignoredPosition, movedRotation, ignoredScale));
    const auto expectedNormal = Keire::Math::TransformDirection(
        Keire::Math::ComposeTransform({}, movedRotation, {1.0F, 1.0F, 1.0F}), anchor->LocalNormal);
    CHECK(Distance(resolved->Normal, expectedNormal) < 0.00001F);

    const auto singularSupport = Keire::Math::ComposeTransform({}, Keire::Quaternion{}, {0.0F, 1.0F, 1.0F});
    CHECK_FALSE(Keire::Detail::CaptureFootPlantSupportAnchor(singularSupport, initialPosition, initialNormal));
    CHECK_FALSE(Keire::Detail::CaptureFootPlantSupportAnchor(initialSupport, initialPosition, {}));
    CHECK_FALSE(Keire::Detail::ResolveFootPlantSupportAnchor(initialSupport, {{}, {}}));
}

TEST_CASE("Foot grounding preserves an imported ankle joint's animated sole clearance")
{
    const std::vector<Keire::SkeletonBone> bones{
        {"Hips", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftFoot_$AssimpFbx$_Translation", 0, {{0.0F, 1.25F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftFoot_$AssimpFbx$_PreRotation", 1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftFoot", 2, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftToeBase", 3, {{0.0F, -0.25F, 0.2F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
        {"LeftToeEnd", 4, {{0.0F, 0.0F, 0.2F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const Keire::SkeletonAsset skeleton(bones);
    std::vector<Keire::Matrix4> modelBones(bones.size());
    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        modelBones[index] = Keire::Math::ComposeTransform(bones[index].BindPose.Translation,
                                                          bones[index].BindPose.Rotation, bones[index].BindPose.Scale);
        if (bones[index].Parent >= 0)
        {
            modelBones[index] =
                Keire::Math::Multiply(modelBones[static_cast<std::size_t>(bones[index].Parent)], modelBones[index]);
        }
    }

    const auto clearance = Keire::Detail::FootBoneSurfaceClearance(skeleton, modelBones, 1, {0.0F, 4.0F, 0.0F});
    REQUIRE(clearance);
    CHECK(*clearance == doctest::Approx(0.25F));
    const auto deformFootClearance =
        Keire::Detail::FootBoneSurfaceClearance(skeleton, modelBones, 3, {0.0F, 1.0F, 0.0F});
    REQUIRE(deformFootClearance);
    CHECK(*deformFootClearance == doctest::Approx(0.25F));
    modelBones[3] = Keire::Math::Multiply(
        modelBones[2], Keire::Math::ComposeTransform({}, Keire::Math::EulerDegreesToQuaternion({-90.0F, 0.0F, 0.0F}),
                                                     {1.0F, 1.0F, 1.0F}));
    modelBones[4] = Keire::Math::Multiply(modelBones[3], Keire::Math::ComposeTransform(bones[4].BindPose.Translation,
                                                                                       bones[4].BindPose.Rotation,
                                                                                       bones[4].BindPose.Scale));
    modelBones[5] = Keire::Math::Multiply(modelBones[4], Keire::Math::ComposeTransform(bones[5].BindPose.Translation,
                                                                                       bones[5].BindPose.Rotation,
                                                                                       bones[5].BindPose.Scale));
    const auto animatedClearance = Keire::Detail::FootBoneSurfaceClearance(skeleton, modelBones, 3, {0.0F, 1.0F, 0.0F});
    REQUIRE(animatedClearance);
    CHECK(*animatedClearance == doctest::Approx(0.0F));

    const auto bindClearance = Keire::Detail::FootBoneBindSurfaceClearance(skeleton, 3);
    REQUIRE(bindClearance);
    CHECK(*bindClearance == doctest::Approx(0.25F));
    CHECK_FALSE(Keire::Detail::FootBoneSurfaceClearance(skeleton, modelBones, 99, {0.0F, 1.0F, 0.0F}));
    CHECK_FALSE(Keire::Detail::FootBoneBindSurfaceClearance(skeleton, 99));
}

TEST_CASE("Foot grounding measures visible boot soles from foot-weighted bind mesh vertices")
{
    const std::vector<Keire::SkeletonBone> bones{{"Root", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Foot", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"Toe", 1, {{0.0F, -0.15F, 0.25F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const std::vector<Keire::MeshVertex> vertices{
        {{0.0F, 0.55F, 0.0F}}, {{0.0F, 0.65F, 0.3F}}, {{0.0F, -4.0F, 0.0F}}, {{0.0F, -2.0F, 0.0F}}};
    const Keire::SkeletonAsset skeleton(bones);
    const Keire::MeshAsset mesh(vertices, {0, 1, 2}, {{0.0F, -4.0F, 0.0F}, {0.0F, 0.65F, 0.3F}});
    std::vector<Keire::SkinVertexInfluence8> influences(vertices.size());
    influences[0].Count = 1;
    influences[0].Bones[0] = 1;
    influences[0].Weights[0] = 1.0F;
    influences[1].Count = 1;
    influences[1].Bones[0] = 2;
    influences[1].Weights[0] = 1.0F;
    influences[2].Count = 1;
    influences[2].Bones[0] = 0;
    influences[2].Weights[0] = 1.0F;
    influences[3].Count = 2;
    influences[3].Bones[0] = 0;
    influences[3].Weights[0] = 0.9F;
    influences[3].Bones[1] = 1;
    influences[3].Weights[1] = 0.1F;
    const Keire::SkinnedMeshAsset skin(Keire::AssetId::Generate(), Keire::AssetId::Generate(), influences,
                                       Keire::SkinningMethod::LinearBlend);

    const auto jointClearance = Keire::Detail::FootBoneBindSurfaceClearance(skeleton, 1);
    const auto meshClearance = Keire::Detail::FootMeshBindSurfaceClearance(skeleton, skin, mesh, 1);
    REQUIRE(jointClearance);
    REQUIRE(meshClearance);
    CHECK(*jointClearance == doctest::Approx(0.15F));
    CHECK(*meshClearance == doctest::Approx(0.45F));
    REQUIRE(Keire::Detail::AutomaticFootToeBone(skeleton, &skin, 1));
    CHECK(*Keire::Detail::AutomaticFootToeBone(skeleton, &skin, 1) == 2);
    REQUIRE(Keire::Detail::AutomaticFootToeBone(skeleton, nullptr, 1));
    CHECK(*Keire::Detail::AutomaticFootToeBone(skeleton, nullptr, 1) == 2);
    CHECK_FALSE(Keire::Detail::FootMeshBindSurfaceClearance(skeleton, skin, mesh, 99));
}

TEST_CASE("Foot grounding discovers unnamed toe controls from skin influence and bind topology")
{
    const std::vector<Keire::SkeletonBone> bones{{"joint_a", -1, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"joint_b", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                                 {"joint_c", 1, {{0.0F, -0.1F, 0.3F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    std::vector<Keire::SkinVertexInfluence8> influences(1);
    influences[0].Count = 1;
    influences[0].Bones[0] = 2;
    influences[0].Weights[0] = 1.0F;
    const Keire::SkeletonAsset skeleton(bones);
    const Keire::SkinnedMeshAsset skin(Keire::AssetId::Generate(), Keire::AssetId::Generate(), influences,
                                       Keire::SkinningMethod::LinearBlend);

    const auto toe = Keire::Detail::AutomaticFootToeBone(skeleton, &skin, 1);
    REQUIRE(toe);
    CHECK(*toe == 2);
}
