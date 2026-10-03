#include "Keire/Animation/RiggingSystem.h"
#include "KeireInternal/Scenes/AnimationBoundLimbGoals.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("Animator stable limb goals validate replace clear and invalidate across assigned assets")
{
    Keire::AnimatorComponent animator;
    animator.SetLimbIk({{7}, {1, 2, 3}, {0, 0, 1}});
    animator.SetLimbIk({{7}, {4, 5, 6}, {0, 0, 1}, 0.5F}, Keire::AnimatorIkSpace::Model);
    REQUIRE(animator.LimbIkTargets().size() == 1);
    CHECK(animator.LimbIkTargets()[0].Target.Position == Keire::Vector3{4, 5, 6});
    CHECK_THROWS_AS(animator.SetLimbIk({{7}, {}, {}, 2.0F}), std::invalid_argument);
    CHECK_THROWS_AS(animator.SetLimbIk({{0}, {}, {}}), std::invalid_argument);
    CHECK_THROWS_AS(animator.SetLimbIk({{7}, {}, {}}, static_cast<Keire::AnimatorIkSpace>(255)), std::invalid_argument);
    CHECK(animator.LimbIkTargets()[0].Target.Weight == 0.5F);
    animator.SetRuntimeLimbResults({{{7}, Keire::LimbSolveStatus::Solved}});
    animator.SetRigDefinition(Keire::AssetId::Generate());
    CHECK(animator.LimbIkTargets().empty());
    CHECK(animator.RuntimeLimbResults().empty());
    animator.SetLimbIk({{7}, {}, {}});
    animator.SetSkeleton(Keire::AssetId::Generate());
    CHECK(animator.LimbIkTargets().empty());
    animator.SetLimbIk({{7}, {}, {}});
    CHECK(animator.ClearLimbIk({7}));
    CHECK_FALSE(animator.ClearLimbIk({7}));
    animator.SetLimbIk({{7}, {}, {}});
    animator.ClearRuntimePose();
    CHECK(animator.LimbIkTargets().empty());
}

TEST_CASE("Asset bound limb pass converts simulation and presentation goals before native constrained solve")
{
    using namespace Keire;
    RigDefinition definition;
    definition.SchemaVersion = 2;
    definition.Profile = RigProfileType::Custom;
    definition.Bones = {{RigBoneSemantic::None, "root", -1},
                        {RigBoneSemantic::None, "middle", 0, {{0, -1, 0}}},
                        {RigBoneSemantic::None, "tip", 1, {{0, -1, 0}}}};
    LimbDefinition limb;
    limb.Id = {7};
    limb.Name = "Creature front leg";
    limb.Bones = {"root", "middle", "tip"};
    limb.BendLimits = LimbBendLimits{60, 120};
    definition.Limbs.push_back(limb);
    const auto asset = RigDefinitionAsset::Decode(RigDefinitionAsset::Encode(definition));
    const auto skeleton = CreateRef<SkeletonAsset>(
        std::vector<SkeletonBone>{{"root", -1}, {"middle", 0, {{0, -1, 0}}}, {"tip", 1, {{0, -1, 0}}}});
    BoundLimbRig rig(skeleton, asset->Definition().Limbs);
    const std::array<BoneTransform, 3> rest{{{}, {{0, -1, 0}}, {{0, -1, 0}}}};
    const auto simulationInverse = Math::Inverse(Math::ComposeTransform({10, 0, 0}, {}, {1, 1, 1}));
    const auto presentationInverse = Math::Inverse(Math::ComposeTransform({20, 0, 0}, {}, {1, 1, 1}));
    for (const auto space : {AnimatorIkSpace::Model, AnimatorIkSpace::World, AnimatorIkSpace::PresentationWorld})
    {
        auto pose = rest;
        const auto offset = space == AnimatorIkSpace::Model ? 0.0F : space == AnimatorIkSpace::World ? 10.0F : 20.0F;
        const std::array<AnimatorLimbTarget, 1> goals{{{{{7}, {offset, -1.99F, 0}, {offset, 0, 1}}, space}}};
        std::vector<LimbSolveResult> results;
        CHECK(Detail::ApplyBoundAnimationLimbGoals(rig, goals, pose, simulationInverse, presentationInverse, results)
                  .empty());
        REQUIRE(results.size() == 1);
        CHECK(results[0].Status == LimbSolveStatus::JointLimited);
        CHECK(results[0].EndPosition.X == doctest::Approx(0).epsilon(0.001));
        CHECK(results[0].EndPosition.Y == doctest::Approx(-1.73205F).epsilon(0.001));
        CHECK(results[0].ReachError == 0.0F);
    }
    auto pose = rest;
    const std::array<AnimatorLimbTarget, 1> unknown{{{{{999}, {0, -1, 0}, {0, 0, 1}}, AnimatorIkSpace::Model}}};
    std::vector<LimbSolveResult> results;
    CHECK_FALSE(Detail::ApplyBoundAnimationLimbGoals(rig, unknown, pose, {}, {}, results).empty());
    CHECK(pose == rest);
    const std::array<AnimatorLimbTarget, 1> world{{{{{7}, {0, -1, 0}, {0, 0, 1}}, AnimatorIkSpace::World}}};
    CHECK_FALSE(Detail::ApplyBoundAnimationLimbGoals(rig, world, pose, {}, {}, results).empty());
    CHECK(pose == rest);
    CHECK(results.empty());
}

TEST_CASE("Asset bound limb cache rebinds revisions and drops stale mappings on failed reload")
{
    using namespace Keire;
    const auto skeleton = CreateRef<SkeletonAsset>(
        std::vector<SkeletonBone>{{"root", -1}, {"middle", 0, {{0, -1, 0}}}, {"tip", 1, {{0, -1, 0}}}});
    LimbDefinition definition;
    definition.Id = {7};
    definition.Name = "Front leg";
    definition.Bones = {"root", "middle", "tip"};
    const auto makeRig = [](const LimbDefinition& limb)
    {
        RigDefinition rig;
        rig.SchemaVersion = 2;
        rig.Profile = RigProfileType::Custom;
        rig.Bones = {{RigBoneSemantic::None, "root", -1},
                     {RigBoneSemantic::None, "middle", 0, {{0, -1, 0}}},
                     {RigBoneSemantic::None, "tip", 1, {{0, -1, 0}}}};
        rig.Limbs.push_back(limb);
        return CreateRef<RigDefinitionAsset>(std::move(rig));
    };
    const auto rig = makeRig(definition);
    Detail::AnimationLimbBindingCache cache;
    CHECK(cache.Bind(skeleton, rig, 1, 1).empty());
    REQUIRE(cache.Binding());
    const auto* original = cache.Binding();
    CHECK(cache.Bind(skeleton, rig, 1, 1).empty());
    CHECK(cache.Binding() == original);
    CHECK(cache.BindingAttempts() == 1);
    definition.BendLimits = LimbBendLimits{60, 120};
    const auto constrainedRig = makeRig(definition);
    CHECK(cache.Bind(skeleton, constrainedRig, 2, 1).empty());
    CHECK(cache.Binding()->Limbs()[0].Definition.BendLimits->MinimumDegrees == 60);
    CHECK(cache.BindingAttempts() == 2);
    const auto incompatible = CreateRef<SkeletonAsset>(
        std::vector<SkeletonBone>{{"root", -1}, {"middle", 0, {{0, -1, 0}}}, {"other", 1, {{0, -1, 0}}}});
    const std::string diagnostic(cache.Bind(incompatible, constrainedRig, 2, 2));
    CHECK_FALSE(diagnostic.empty());
    CHECK_FALSE(cache.Binding());
    CHECK(cache.BindingAttempts() == 3);
    for (int frame = 0; frame < 120; ++frame)
        CHECK(cache.Bind(incompatible, constrainedRig, 2, 2) == diagnostic);
    CHECK(cache.BindingAttempts() == 3);
    CHECK_FALSE(cache.Bind(incompatible, constrainedRig, 3, 2).empty());
    CHECK(cache.BindingAttempts() == 4);
    CHECK_FALSE(cache.Bind(incompatible, constrainedRig, 3, 3).empty());
    CHECK(cache.BindingAttempts() == 5);
    const auto replacementRig = makeRig(definition);
    CHECK_FALSE(cache.Bind(incompatible, replacementRig, 3, 3).empty());
    CHECK(cache.BindingAttempts() == 6);
    const auto replacement = CreateRef<SkeletonAsset>(
        std::vector<SkeletonBone>{{"body", -1}, {"root", 0}, {"middle", 1, {{0, -1, 0}}}, {"tip", 2, {{0, -1, 0}}}});
    CHECK(cache.Bind(replacement, replacementRig, 3, 3).empty());
    CHECK(cache.BindingAttempts() == 7);
    CHECK(cache.Binding()->Limbs()[0].Bones == std::vector<std::uint32_t>{1, 2, 3});
    CHECK(&cache.Binding()->Skeleton() == replacement.Get());
    cache.Reset();
    CHECK_FALSE(cache.Binding());
    CHECK(cache.Bind(replacement, replacementRig, 3, 3).empty());
    CHECK(cache.BindingAttempts() == 8);
}
