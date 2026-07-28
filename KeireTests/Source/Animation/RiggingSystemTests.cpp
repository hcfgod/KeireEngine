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
