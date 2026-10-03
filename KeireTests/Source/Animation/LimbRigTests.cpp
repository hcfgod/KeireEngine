#include "Keire/Animation/LimbRig.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] Keire::Ref<Keire::SkeletonAsset> LimbSkeleton(const std::size_t limbs = 2)
    {
        std::vector<Keire::SkeletonBone> bones{{"body"}};
        for (std::size_t index = 0; index < limbs; ++index)
        {
            const auto prefix = std::to_string(index);
            const auto root = static_cast<std::int32_t>(bones.size());
            bones.push_back({prefix + "root", 0, {{static_cast<float>(index) * 3.0F, 0.0F, 0.0F}}});
            bones.push_back({prefix + "middle", root, {{0.0F, -1.0F, 0.0F}}});
            bones.push_back({prefix + "tip", root + 1, {{0.0F, -1.0F, 0.0F}}});
        }
        return Keire::CreateRef<Keire::SkeletonAsset>(std::move(bones));
    }

    [[nodiscard]] Keire::LimbDefinition Definition(const std::uint32_t index)
    {
        const auto prefix = std::to_string(index);
        Keire::LimbDefinition definition;
        definition.Id = {index + 1};
        definition.Name = "leg" + prefix;
        definition.Bones = {prefix + "root", prefix + "middle", prefix + "tip"};
        return definition;
    }

    [[nodiscard]] std::vector<Keire::BoneTransform> RestPose(const Keire::SkeletonAsset& skeleton)
    {
        std::vector<Keire::BoneTransform> pose;
        for (const auto& bone : skeleton.Bones())
            pose.push_back(bone.BindPose);
        return pose;
    }

    [[nodiscard]] std::vector<Keire::Vector3> Positions(const Keire::SkeletonAsset& skeleton,
                                                        const std::span<const Keire::BoneTransform> pose)
    {
        std::vector<Keire::Matrix4> matrices(pose.size());
        std::vector<Keire::Vector3> result(pose.size());
        for (std::size_t index = 0; index < pose.size(); ++index)
        {
            matrices[index] =
                Keire::Math::ComposeTransform(pose[index].Translation, pose[index].Rotation, pose[index].Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            if (parent >= 0)
                matrices[index] = Keire::Math::Multiply(matrices[static_cast<std::size_t>(parent)], matrices[index]);
            result[index] = Keire::Math::TransformPoint(matrices[index], {});
        }
        return result;
    }

    [[nodiscard]] float MeasuredBend(const std::span<const Keire::Vector3> points)
    {
        const Keire::Vector3 upper{points[2].X - points[1].X, points[2].Y - points[1].Y, points[2].Z - points[1].Z};
        const Keire::Vector3 lower{points[3].X - points[2].X, points[3].Y - points[2].Y, points[3].Z - points[2].Z};
        const auto dot = upper.X * lower.X + upper.Y * lower.Y + upper.Z * lower.Z;
        const auto lengths = std::sqrt((upper.X * upper.X + upper.Y * upper.Y + upper.Z * upper.Z) *
                                       (lower.X * lower.X + lower.Y * lower.Y + lower.Z * lower.Z));
        return std::acos(std::clamp(dot / lengths, -1.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>;
    }
} // namespace

TEST_CASE("Bound limb rig resolves eight independent named legs and owns its skeleton")
{
    auto skeleton = LimbSkeleton(8);
    std::vector<Keire::LimbDefinition> definitions;
    for (std::uint32_t index = 0; index < 8; ++index)
        definitions.push_back(Definition(index));
    Keire::BoundLimbRig rig(skeleton, definitions);
    skeleton = {};
    REQUIRE(rig.Limbs().size() == 8);
    auto pose = RestPose(rig.Skeleton());
    std::vector<Keire::LimbTarget> targets;
    for (std::uint32_t index = 0; index < 8; ++index)
    {
        CHECK(rig.Limbs()[index].MaximumReach == doctest::Approx(2.0F));
        CHECK(rig.Limbs()[index].RestLengths == std::vector<float>{1.0F, 1.0F});
        targets.push_back({{index + 1},
                           {static_cast<float>(index) * 3.0F, -1.5F, 0.5F},
                           {static_cast<float>(index) * 3.0F, 0.0F, 1.0F}});
    }
    std::vector<Keire::LimbSolveResult> results;
    REQUIRE(rig.Solve(pose, targets, results));
    REQUIRE(results.size() == 8);
    for (std::uint32_t index = 0; index < 8; ++index)
    {
        CHECK(results[index].Id == Keire::LimbId{index + 1});
        CHECK(results[index].Status == Keire::LimbSolveStatus::Solved);
        CHECK(results[index].PositionError <= 0.001F);
        CHECK(results[index].ReachError == 0.0F);
    }
}

TEST_CASE("Bound limb rig rejects ambiguous bindings and invalid rest geometry")
{
    const auto skeleton = LimbSkeleton();
    std::array definitions{Definition(0), Definition(1)};
    SUBCASE("Duplicate IDs") { definitions[1].Id = definitions[0].Id; }
    SUBCASE("Duplicate labels") { definitions[1].Name = definitions[0].Name; }
    SUBCASE("Missing bone") { definitions[1].Bones[1] = "missing"; }
    SUBCASE("Reversed chain") { std::swap(definitions[1].Bones[0], definitions[1].Bones[2]); }
    SUBCASE("Repeated bone") { definitions[1].Bones[1] = definitions[1].Bones[0]; }
    SUBCASE("Overlapping chains") { definitions[1].Bones = definitions[0].Bones; }
    SUBCASE("Invalid ID") { definitions[1].Id.Value = 0; }
    SUBCASE("Invalid solver") { definitions[1].Solver = static_cast<Keire::LimbSolver>(255); }
    SUBCASE("Invalid radius") { definitions[1].ContactRadius = -1.0F; }
    SUBCASE("Invalid tolerance") { definitions[1].Tolerance = std::numeric_limits<float>::infinity(); }
    SUBCASE("Invalid iterations") { definitions[1].MaximumIterations = 0; }
    CHECK_THROWS_AS(Keire::BoundLimbRig(skeleton, definitions), std::invalid_argument);
}

TEST_CASE("Bound limb rig rejects null skeleton empty definitions and zero rest length")
{
    const std::array definitions{Definition(0)};
    CHECK_THROWS_AS(Keire::BoundLimbRig({}, definitions), std::invalid_argument);
    CHECK_THROWS_AS(Keire::BoundLimbRig(LimbSkeleton(), {}), std::invalid_argument);
    const auto skeleton = LimbSkeleton();
    std::vector<Keire::SkeletonBone> bones(skeleton->Bones().begin(), skeleton->Bones().end());
    bones[2].BindPose.Translation = {};
    CHECK_THROWS_AS(Keire::BoundLimbRig(Keire::CreateRef<Keire::SkeletonAsset>(std::move(bones)), definitions),
                    std::invalid_argument);
}

TEST_CASE("Bound limb rig distinguishes reach failure from intentional blending and disabled limbs")
{
    const auto skeleton = LimbSkeleton();
    const std::array definitions{Definition(0), Definition(1)};
    Keire::BoundLimbRig rig(skeleton, definitions);
    auto pose = RestPose(*skeleton);
    std::vector<Keire::LimbSolveResult> results;
    Keire::LimbTarget target{{1}, {0.0F, -8.0F, 0.0F}};
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(results[0].Status == Keire::LimbSolveStatus::Unreachable);
    CHECK(results[0].ReachError == doctest::Approx(6.0F));
    CHECK(results[1].Status == Keire::LimbSolveStatus::Disabled);
    target.Position = {0.0F, -1.0F, 1.0F};
    target.Weight = 0.25F;
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(results[0].Status == Keire::LimbSolveStatus::Blended);
    CHECK(results[0].ReachError == 0.0F);
    const auto before = pose;
    target.Weight = 0.0F;
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(pose == before);
    CHECK(results[0].Status == Keire::LimbSolveStatus::Disabled);
    target.Weight = 1.0F;
    target.Enabled = false;
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(pose == before);
}

TEST_CASE("Bound limb rig failure is transactional including a later collapsed limb")
{
    const auto skeleton = LimbSkeleton();
    const std::array definitions{Definition(0), Definition(1)};
    Keire::BoundLimbRig rig(skeleton, definitions);
    auto pose = RestPose(*skeleton);
    std::array<Keire::LimbTarget, 2> targets{{{{1}, {0.0F, -1.0F, 1.0F}}, {{2}, {3.0F, -1.0F, 1.0F}}}};
    SUBCASE("Unknown target") { targets[1].Id = {999}; }
    SUBCASE("Duplicate target") { targets[1].Id = targets[0].Id; }
    SUBCASE("Nonfinite target") { targets[1].Position.X = std::numeric_limits<float>::infinity(); }
    SUBCASE("Out of range weight") { targets[1].Weight = 2.0F; }
    SUBCASE("Invalid disabled target")
    {
        targets[1].Enabled = false;
        targets[1].Pole.X = std::numeric_limits<float>::infinity();
    }
    SUBCASE("Collapsed later limb") { pose[5].Translation = {}; }
    const auto before = pose;
    std::vector<Keire::LimbSolveResult> results;
    CHECK_FALSE(rig.Solve(pose, targets, results));
    CHECK(pose == before);
    REQUIRE(results.size() == 2);
    CHECK(results[0].Status == Keire::LimbSolveStatus::InvalidInput);
    CHECK(results[1].Status == Keire::LimbSolveStatus::InvalidInput);
    pose = RestPose(*skeleton);
    targets = {{{{1}, {0.0F, -1.0F, 1.0F}}, {{2}, {3.0F, -1.0F, 1.0F}}}};
    CHECK(rig.Solve(pose, targets, results));
}

TEST_CASE("Bound limb rig FABRIK uses the bound chain and reports actual endpoint error")
{
    const auto skeleton = LimbSkeleton();
    std::array definitions{Definition(0)};
    definitions[0].Solver = Keire::LimbSolver::Fabrik;
    Keire::BoundLimbRig rig(skeleton, definitions);
    auto pose = RestPose(*skeleton);
    const std::array<Keire::LimbTarget, 1> targets{{{{1}, {0.0F, -1.0F, 1.0F}}}};
    std::vector<Keire::LimbSolveResult> results;
    REQUIRE(rig.Solve(pose, targets, results));
    CHECK(results[0].Status == Keire::LimbSolveStatus::Solved);
    CHECK(results[0].PositionError <= definitions[0].Tolerance);
    CHECK(results[0].EndPosition.Y == doctest::Approx(-1.0F).epsilon(0.001));
    CHECK(results[0].EndPosition.Z == doctest::Approx(1.0F).epsilon(0.001));
}

TEST_CASE("Bound limb rig rejects wrong pose sizes and invalid rotations without mutation")
{
    const auto skeleton = LimbSkeleton();
    const std::array definitions{Definition(0)};
    Keire::BoundLimbRig rig(skeleton, definitions);
    auto pose = RestPose(*skeleton);
    SUBCASE("Wrong size") { pose.pop_back(); }
    SUBCASE("Zero quaternion") { pose[0].Rotation = {0.0F, 0.0F, 0.0F, 0.0F}; }
    SUBCASE("Nonfinite translation") { pose[0].Translation.X = std::numeric_limits<float>::infinity(); }
    const auto before = pose;
    std::vector<Keire::LimbSolveResult> results;
    CHECK_FALSE(rig.Solve(pose, {}, results));
    CHECK(pose == before);
    REQUIRE(results.size() == 1);
    CHECK(results[0].Status == Keire::LimbSolveStatus::InvalidInput);
}

TEST_CASE("Bound limb rig inner reach and FABRIK iteration exhaustion have distinct diagnostics")
{
    const auto source = LimbSkeleton();
    std::vector<Keire::SkeletonBone> bones(source->Bones().begin(), source->Bones().end());
    bones[2].BindPose.Translation.Y = -3.0F;
    const auto skeleton = Keire::CreateRef<Keire::SkeletonAsset>(std::move(bones));
    const std::array definitions{Definition(0)};
    Keire::BoundLimbRig rig(skeleton, definitions);
    auto pose = RestPose(*skeleton);
    const std::array<Keire::LimbTarget, 1> targets{{{{1}, {0.0F, -0.5F, 0.0F}}}};
    std::vector<Keire::LimbSolveResult> results;
    REQUIRE(rig.Solve(pose, targets, results));
    CHECK(results[0].Status == Keire::LimbSolveStatus::Unreachable);
    CHECK(results[0].ReachError == doctest::Approx(1.5F));

    auto fabrikDefinition = Definition(0);
    fabrikDefinition.Solver = Keire::LimbSolver::Fabrik;
    fabrikDefinition.MaximumIterations = 1;
    fabrikDefinition.Tolerance = 0.0000001F;
    Keire::BoundLimbRig fabrik(source, std::span(&fabrikDefinition, 1));
    pose = RestPose(*source);
    const std::array<Keire::LimbTarget, 1> nearTargets{{{{1}, {0.37F, -0.44F, 0.81F}}}};
    REQUIRE(fabrik.Solve(pose, nearTargets, results));
    CHECK(results[0].Status == Keire::LimbSolveStatus::NotConverged);
    CHECK(results[0].ReachError == 0.0F);
    CHECK(results[0].PositionError > fabrikDefinition.Tolerance);
}

TEST_CASE("Bound limb rig enforces actual knee bend limits including partially blended poses")
{
    const auto skeleton = LimbSkeleton();
    auto definition = Definition(0);
    definition.BendLimits = Keire::LimbBendLimits{60.0F, 120.0F};
    Keire::BoundLimbRig rig(skeleton, std::span(&definition, 1));
    auto pose = RestPose(*skeleton);
    CHECK(MeasuredBend(Positions(*skeleton, pose)) == doctest::Approx(0.0F));
    Keire::LimbTarget target{{1}, {0.0F, -1.99F, 0.0F}};
    SUBCASE("Full extension") {}
    SUBCASE("Excessive folding") { target.Position.Y = -0.2F; }
    SUBCASE("Blend from an out-of-bounds pose")
    {
        target.Position.Y = -1.0F;
        target.Weight = 0.1F;
    }
    std::vector<Keire::LimbSolveResult> results;
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(results[0].JointLimited);
    CHECK(results[0].Status == Keire::LimbSolveStatus::JointLimited);
    const auto angle = MeasuredBend(Positions(*skeleton, pose));
    CHECK(angle >= 59.95F);
    CHECK(angle <= 120.05F);
    CHECK(results[0].ReachError == 0.0F);
    CHECK(pose[2].Translation == skeleton->Bones()[2].BindPose.Translation);
    CHECK(pose[3].Translation == skeleton->Bones()[3].BindPose.Translation);
    pose = RestPose(*skeleton);
    target.Enabled = false;
    const auto before = pose;
    REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(pose == before);
    CHECK_FALSE(results[0].JointLimited);
}

TEST_CASE("Bound limb rig preferred direction selects the knee side without humanoid semantics")
{
    const auto skeleton = LimbSkeleton();
    auto definition = Definition(0);
    const Keire::LimbTarget target{{1}, {0.0F, -1.0F, 0.0F}};
    std::vector<Keire::LimbSolveResult> results;
    for (const auto side : {-1.0F, 1.0F})
    {
        definition.PreferredBendDirection = Keire::Vector3{0.0F, 0.0F, side};
        Keire::BoundLimbRig rig(skeleton, std::span(&definition, 1));
        auto pose = RestPose(*skeleton);
        REQUIRE(rig.Solve(pose, std::span(&target, 1), results));
        CHECK(Positions(*skeleton, pose)[2].Z * side > 0.5F);
        CHECK(results[0].Status == Keire::LimbSolveStatus::Solved);
    }
}

TEST_CASE("Bound limb rig rejects unsupported and impossible constraints explicitly")
{
    const auto skeleton = LimbSkeleton();
    auto definition = Definition(0);
    definition.BendLimits = Keire::LimbBendLimits{10.0F, 120.0F};
    SUBCASE("Unimplemented FABRIK limits") { definition.Solver = Keire::LimbSolver::Fabrik; }
    SUBCASE("Reversed interval") { definition.BendLimits = Keire::LimbBendLimits{120.0F, 10.0F}; }
    SUBCASE("Negative angle") { definition.BendLimits->MinimumDegrees = -1.0F; }
    SUBCASE("Nonfinite angle") { definition.BendLimits->MaximumDegrees = std::numeric_limits<float>::infinity(); }
    SUBCASE("Exact singularity") { definition.BendLimits = Keire::LimbBendLimits{0.0F, 0.0F}; }
    SUBCASE("Zero preferred direction") { definition.PreferredBendDirection = Keire::Vector3{}; }
    CHECK_THROWS_AS(Keire::BoundLimbRig(skeleton, std::span(&definition, 1)), std::invalid_argument);
}

TEST_CASE("Bound limb rig rejects incompatible runtime constrained geometry transactionally")
{
    const auto skeleton = LimbSkeleton();
    auto definition = Definition(0);
    definition.BendLimits = Keire::LimbBendLimits{175.0F, 179.0F};
    Keire::BoundLimbRig rig(skeleton, std::span(&definition, 1));
    auto pose = RestPose(*skeleton);
    pose[2].Translation.Y = -3.0F;
    const auto before = pose;
    const Keire::LimbTarget target{{1}, {0.0F, -2.1F, 0.0F}};
    std::vector<Keire::LimbSolveResult> results;
    CHECK_FALSE(rig.Solve(pose, std::span(&target, 1), results));
    CHECK(pose == before);
    CHECK(results[0].Status == Keire::LimbSolveStatus::InvalidInput);
}
