#include "KeireClient/Editor/LimbRigAssignment.h"
#include "KeireClient/Editor/LimbRigAuthoring.h"
#include "KeireClient/Editor/RigChainInspection.h"
#include "KeireClientInternal/Editor/AnimatorControllerPreviewInternal.h"

#include <doctest/doctest.h>

#include <limits>
#include <utility>
#include <vector>

namespace
{
    std::vector<Keire::SkeletonBone> SpiderLimb()
    {
        std::vector<Keire::SkeletonBone> bones(5);
        bones[0].Name = "body";
        bones[0].BindPose.Scale = {2.0F, 2.0F, 2.0F};
        bones[1].Name = "front_a";
        bones[1].Parent = 0;
        bones[2].Name = "front_b";
        bones[2].Parent = 1;
        bones[2].BindPose.Translation = {1.0F, 0.0F, 0.0F};
        bones[3].Name = "front_tip";
        bones[3].Parent = 2;
        bones[3].BindPose.Translation = {0.0F, 0.0F, 2.0F};
        bones[4].Name = "other_limb";
        bones[4].Parent = 0;
        return bones;
    }
} // namespace

TEST_CASE("Rig chain inspection measures custom limbs including ancestor scale")
{
    const Keire::SkeletonAsset skeleton(SpiderLimb());
    const auto report = KeireEditor::InspectRigChain(skeleton, 1, 3);
    REQUIRE(report.Valid());
    CHECK(report.Bones == std::vector<std::size_t>{1, 2, 3});
    CHECK(report.SegmentLengths == std::vector<float>{2.0F, 4.0F});
    CHECK(report.MaximumReach == doctest::Approx(6.0F));
    CHECK(skeleton.Bones()[2].BindPose.Translation.X == 1.0F);
}

TEST_CASE("Rig chain inspection explains invalid endpoints without proposing an unrelated chain")
{
    const Keire::SkeletonAsset skeleton(SpiderLimb());
    for (const auto pair : {std::pair<std::size_t, std::size_t>{1, 4}, {3, 1}})
    {
        const auto report = KeireEditor::InspectRigChain(skeleton, pair.first, pair.second);
        CHECK_FALSE(report.Valid());
        CHECK(report.Bones.empty());
        CHECK(report.Error.find("not below") != std::string::npos);
    }
    CHECK_FALSE(KeireEditor::InspectRigChain(skeleton, 1, 1).Valid());
    CHECK_FALSE(KeireEditor::InspectRigChain(skeleton, 99, 3).Valid());
    CHECK_FALSE(KeireEditor::InspectRigChain(skeleton, 1, 99).Valid());
    CHECK_FALSE(KeireEditor::InspectRigChain(Keire::SkeletonAsset{}, 0, 0).Valid());
}

TEST_CASE("Rig chain inspection identifies collapsed segments and overflowing transforms")
{
    auto bones = SpiderLimb();
    bones[2].BindPose.Translation = {};
    const auto collapsed = KeireEditor::InspectRigChain(Keire::SkeletonAsset(bones), 1, 3);
    CHECK_FALSE(collapsed.Valid());
    CHECK(collapsed.Error.find("front_b") != std::string::npos);
    bones = SpiderLimb();
    bones[0].BindPose.Scale = {std::numeric_limits<float>::max(), 1.0F, 1.0F};
    bones[2].BindPose.Translation = {2.0F, 0.0F, 0.0F};
    const auto overflow = KeireEditor::InspectRigChain(Keire::SkeletonAsset(bones), 1, 3);
    CHECK_FALSE(overflow.Valid());
    CHECK(overflow.Error.find("invalid bind segment") != std::string::npos);
}

TEST_CASE("Rig chain overlay reconstructs published joints rather than skin deformation translations")
{
    std::vector<Keire::SkeletonBone> bones(2);
    bones[0].Name = "root";
    bones[1].Name = "tip";
    bones[1].Parent = 0;
    bones[1].BindPose.Translation = {2.0F, 0.0F, 0.0F};
    bones[1].InverseBindPose = Keire::Math::ComposeTransform({-2.0F, 0.0F, 0.0F}, {}, {1, 1, 1});
    const Keire::SkeletonAsset skeleton(bones);
    const std::vector<Keire::Matrix4> palette{Keire::Math::ComposeTransform({0.0F, 3.0F, 0.0F}, {}, {1, 1, 1}),
                                              Keire::Math::ComposeTransform({0.0F, 4.0F, 0.0F}, {}, {1, 1, 1})};
    const std::vector<std::size_t> chain{0, 1};
    const auto world = Keire::Math::ComposeTransform({10.0F, 0.0F, 0.0F}, {}, {1, 1, 1});
    const auto points = KeireEditor::PublishedRigChainPoints(skeleton, palette, chain, world);
    REQUIRE(points.size() == 2);
    CHECK(points[0] == Keire::Vector3{10.0F, 3.0F, 0.0F});
    CHECK(points[1] == Keire::Vector3{12.0F, 4.0F, 0.0F});
    CHECK_THROWS_AS((void)KeireEditor::PublishedRigChainPoints(skeleton, {}, chain, world), std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::PublishedRigChainPoints(skeleton, palette, std::vector<std::size_t>{99}, world),
                    std::invalid_argument);
}

TEST_CASE("Animator preview frame stepping pauses and clears pending steps on seek restart or stop")
{
    KeireEditor::AnimatorControllerPreviewState preview;
    preview.PlaybackSpeed = 3.0F;
    preview.StepFrame();
    CHECK(preview.Active);
    CHECK_FALSE(preview.Playing);
    CHECK(preview.StepSecondsRequested == doctest::Approx(1.0F / 60.0F));
    preview.StepFrame();
    CHECK(preview.StepSecondsRequested == doctest::Approx(2.0F / 60.0F));
    preview.Seek(0.5F);
    CHECK(preview.StepSecondsRequested == 0.0F);
    preview.StepFrame();
    CHECK(preview.SeekRequested == 0.5F);
    preview.Restart();
    CHECK(preview.StepSecondsRequested == 0.0F);
    CHECK_FALSE(preview.SeekRequested);
    preview.StepFrame();
    preview.Stop();
    CHECK_FALSE(preview.Active);
    CHECK(preview.StepSecondsRequested == 0.0F);
}

TEST_CASE("Limb authoring mirrors only explicit existing names and updates drafts transactionally")
{
    const auto skeleton =
        Keire::CreateRef<Keire::SkeletonAsset>(std::vector<Keire::SkeletonBone>{{"LeftRoot", -1, {}, {}},
                                                                                {"LeftKnee", 0, {{1, 0, 0}}, {}},
                                                                                {"LeftTip", 1, {{1, 0, 0}}, {}},
                                                                                {"RightRoot", -1, {}, {}},
                                                                                {"RightKnee", 3, {{1, 0, 0}}, {}},
                                                                                {"RightTip", 4, {{1, 0, 0}}, {}}});
    std::vector<Keire::LimbDefinition> limbs;
    Keire::LimbDefinition left{{1}, "left", {"LeftRoot", "LeftKnee", "LeftTip"}};
    left.PreferredBendDirection = Keire::Vector3{0, 0, 1};
    KeireEditor::UpsertAuthoredLimb(skeleton, limbs, left);
    const auto right = KeireEditor::MirrorAuthoredLimbNames(left, "Left", "Right", {2}, "right");
    CHECK_FALSE(right.PreferredBendDirection);
    KeireEditor::UpsertAuthoredLimb(skeleton, limbs, right);
    REQUIRE(limbs.size() == 2);
    CHECK(limbs.back().Bones.back() == "RightTip");
    auto updated = left;
    updated.ContactRadius = 0.05F;
    updated.Tolerance = 0.003F;
    updated.MaximumIterations = 23;
    updated.BendLimits = Keire::LimbBendLimits{10.0F, 150.0F};
    KeireEditor::UpsertAuthoredLimb(skeleton, limbs, updated);
    CHECK(limbs.size() == 2);
    CHECK(limbs.front().ContactRadius == 0.05F);
    CHECK(limbs.front().Tolerance == 0.003F);
    CHECK(limbs.front().MaximumIterations == 23);
    REQUIRE(limbs.front().BendLimits);
    CHECK(limbs.front().BendLimits->MinimumDegrees == 10.0F);
    auto rig = Keire::InferRigDefinition(*skeleton, Keire::RigProfileType::Custom);
    rig.SchemaVersion = 2;
    rig.Limbs = limbs;
    const auto before = Keire::RigDefinitionAsset::Encode(rig);
    auto invalid = KeireEditor::MirrorAuthoredLimbNames(left, "Left", "Missing", {3}, "missing");
    CHECK_THROWS_AS((void)KeireEditor::UpsertAuthoredLimb(skeleton, limbs, invalid), std::invalid_argument);
    rig.Limbs = limbs;
    CHECK(Keire::RigDefinitionAsset::Encode(rig) == before);
    invalid = updated;
    invalid.BendLimits = Keire::LimbBendLimits{160.0F, 10.0F};
    CHECK_THROWS_AS((void)KeireEditor::UpsertAuthoredLimb(skeleton, limbs, invalid), std::invalid_argument);
    invalid = updated;
    invalid.Solver = Keire::LimbSolver::Fabrik;
    CHECK_THROWS_AS((void)KeireEditor::UpsertAuthoredLimb(skeleton, limbs, invalid), std::invalid_argument);
    rig.Limbs = limbs;
    CHECK(Keire::RigDefinitionAsset::Encode(rig) == before);
    CHECK_THROWS_AS((void)KeireEditor::MirrorAuthoredLimbNames(left, "", "Right", {4}, "bad"), std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::MirrorAuthoredLimbNames(left, "left", "right", {4}, "bad"),
                    std::invalid_argument);
    const auto loaded = Keire::RigDefinitionAsset::Decode(before);
    const Keire::BoundLimbRig rebound(skeleton, loaded->Definition().Limbs);
    CHECK(rebound.Limbs().size() == 2);
}

TEST_CASE("Pending limb rig loading retains drafts until compatible completion and cancels stale loads")
{
    const auto skeleton = Keire::CreateRef<Keire::SkeletonAsset>(SpiderLimb());
    auto definition = Keire::InferRigDefinition(*skeleton, Keire::RigProfileType::Custom);
    definition.SchemaVersion = 2;
    definition.Limbs.push_back({{9}, "loaded", {"front_a", "front_b", "front_tip"}});
    const auto asset = Keire::CreateRef<Keire::RigDefinitionAsset>(definition);
    std::vector<Keire::LimbDefinition> draft{{{1}, "original", {"front_a", "front_b", "front_tip"}}};
    KeireEditor::LimbRigDraftLoad request;
    request.Begin(Keire::AssetId::Generate(), skeleton);
    CHECK(request.Complete(skeleton, {}, draft) == KeireEditor::LimbRigLoadProgress::Loading);
    CHECK(draft.front().Id.Value == 1);
    CHECK(request.Complete(skeleton, asset, draft) == KeireEditor::LimbRigLoadProgress::Loaded);
    CHECK(draft.front().Id.Value == 9);
    CHECK_FALSE(request.Asset());
    request.Begin(Keire::AssetId::Generate(), skeleton);
    request.Cancel();
    CHECK(request.Complete(skeleton, asset, draft) == KeireEditor::LimbRigLoadProgress::Idle);
    CHECK(draft.front().Id.Value == 9);
    request.Begin(Keire::AssetId::Generate(), skeleton);
    const auto reloaded = Keire::CreateRef<Keire::SkeletonAsset>(SpiderLimb());
    CHECK(request.Complete(reloaded, asset, draft) == KeireEditor::LimbRigLoadProgress::Cancelled);
    CHECK_FALSE(request.Asset());
    CHECK(draft.front().Id.Value == 9);
    request.Begin(Keire::AssetId::Generate(), skeleton);
    definition.Limbs.front().Bones.back() = "missing";
    const auto invalid = Keire::CreateRef<Keire::RigDefinitionAsset>(definition);
    CHECK_THROWS_AS((void)request.Complete(skeleton, invalid, draft), std::invalid_argument);
    CHECK_FALSE(request.Asset());
    CHECK(draft.front().Id.Value == 9);
}

TEST_CASE("Pending limb rig assignment waits without scene mutations and cancels changed targets")
{
    const auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Assignment"));
    auto entity = scene->CreateEntity("Actor");
    const auto animator = entity.AddComponent<Keire::AnimatorComponent>();
    const auto skin = Keire::AssetId::Generate();
    const auto requested = Keire::AssetId::Generate();
    animator->SetSkinnedMesh(skin);
    const KeireEditor::LimbRigAssignmentGuard guard{scene, entity.Id(), animator, requested, {}, skin, {}};
    const auto before = Keire::SceneAsset::Encode(scene->Snapshot());
    unsigned undoCount = 0;
    const auto recordUndo = [&] { ++undoCount; };
    const auto complete = [&](const auto& rig, const auto& skeleton)
    { return KeireEditor::CompleteLimbRigAssignment(guard, scene, entity.Id(), true, rig, skeleton, recordUndo); };
    const auto skeleton = Keire::CreateRef<Keire::SkeletonAsset>(SpiderLimb());
    auto definition = Keire::InferRigDefinition(*skeleton, Keire::RigProfileType::Custom);
    definition.SchemaVersion = 2;
    definition.Limbs.push_back({{9}, "front", {"front_a", "front_b", "front_tip"}});
    const auto rig = Keire::CreateRef<Keire::RigDefinitionAsset>(definition);
    CHECK(complete(Keire::Ref<const Keire::RigDefinitionAsset>{}, skeleton) ==
          KeireEditor::LimbRigAssignmentProgress::Loading);
    CHECK(complete(rig, Keire::Ref<const Keire::SkeletonAsset>{}) == KeireEditor::LimbRigAssignmentProgress::Loading);
    CHECK(undoCount == 0);
    CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
    CHECK_FALSE(guard.Matches(scene, {}, true));
    CHECK_FALSE(guard.Matches(scene, entity.Id(), false));
    animator->SetSkeleton(Keire::AssetId::Generate());
    CHECK(complete(rig, skeleton) == KeireEditor::LimbRigAssignmentProgress::Cancelled);
    animator->SetSkeleton({});
    animator->SetRigDefinition(Keire::AssetId::Generate());
    CHECK(complete(rig, skeleton) == KeireEditor::LimbRigAssignmentProgress::Cancelled);
    animator->SetRigDefinition({});
    animator->SetSkinnedMesh(Keire::AssetId::Generate());
    CHECK(complete(rig, skeleton) == KeireEditor::LimbRigAssignmentProgress::Cancelled);
    animator->SetSkinnedMesh(skin);
    CHECK(undoCount == 0);
    auto incompatible = definition;
    incompatible.Limbs.front().Bones.back() = "missing";
    CHECK_THROWS_AS((void)complete(Keire::CreateRef<Keire::RigDefinitionAsset>(incompatible), skeleton),
                    std::invalid_argument);
    CHECK_FALSE(animator->RigDefinition());
    CHECK(undoCount == 0);
    CHECK(complete(rig, skeleton) == KeireEditor::LimbRigAssignmentProgress::Complete);
    CHECK(animator->RigDefinition() == requested);
    CHECK(undoCount == 1);
    CHECK(complete(rig, skeleton) == KeireEditor::LimbRigAssignmentProgress::Cancelled);
    CHECK(undoCount == 1);
    scene->Close();
    CHECK_FALSE(guard.Matches(scene, entity.Id(), true));
}
