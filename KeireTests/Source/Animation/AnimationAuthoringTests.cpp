#include "Keire/Animation/AnimationSystem.h"

#include <doctest/doctest.h>

#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view value)
    {
        return {reinterpret_cast<const std::byte*>(value.data()),
                reinterpret_cast<const std::byte*>(value.data() + value.size())};
    }

    [[nodiscard]] std::string Text(const std::span<const std::byte> value)
    {
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    [[nodiscard]] Keire::Ref<Keire::SkeletonAsset> TestSkeleton()
    {
        const std::vector<Keire::SkeletonBone> bones{{"Root", -1, {}, {}},
                                                     {"Hand", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
        return Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(bones));
    }

    [[nodiscard]] Keire::Ref<Keire::AnimationClipAsset> ConstantHandClip(const Keire::AssetId skeleton,
                                                                         const float translation)
    {
        Keire::AnimationTrack track;
        track.Bone = 1;
        const Keire::BoneTransform transform{{translation, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        track.Keys = {{0.0F, transform}, {1.0F, transform}};
        return Keire::AnimationClipAsset::Decode(
            Keire::AnimationClipAsset::Encode(skeleton, 1.0F, std::span(&track, 1), {}, false));
    }

    [[nodiscard]] Keire::Ref<Keire::AnimationClipAsset> RotatingRootClip(const Keire::AssetId skeleton)
    {
        Keire::AnimationTrack track;
        track.Bone = 0;
        const auto quarterTurn = Keire::Math::EulerDegreesToQuaternion({0.0F, 90.0F, 0.0F});
        track.Keys = {{0.0F, {}}, {1.0F, {{}, quarterTurn, {1.0F, 1.0F, 1.0F}}}};
        return Keire::AnimationClipAsset::Decode(
            Keire::AnimationClipAsset::Encode(skeleton, 1.0F, std::span(&track, 1), {}, true));
    }

    [[nodiscard]] Keire::AnimationStateDefinition ClipState(std::string id, std::string name, const Keire::AssetId clip)
    {
        Keire::AnimationStateDefinition result;
        result.Id = std::move(id);
        result.Name = std::move(name);
        result.Clip = clip;
        result.Motion.Clip = clip;
        return result;
    }

    [[nodiscard]] Keire::AnimationGraphDefinition
    GraphWithBaseLayer(std::vector<Keire::AnimationParameterDefinition> parameters,
                       std::vector<Keire::AnimationStateDefinition> states)
    {
        Keire::AnimationGraphDefinition result;
        result.ParameterDefinitions = std::move(parameters);
        Keire::AnimationLayerDefinition base;
        base.Id = "layer-base";
        base.Name = "Base";
        base.EntryStateId = states.front().Id;
        base.States = std::move(states);
        result.Layers.push_back(std::move(base));
        return result;
    }
} // namespace

TEST_CASE("Animation graph schema one migrates in memory and source import remains byte preserving")
{
    const auto source =
        std::string(R"({"schemaVersion":1,"entryState":"Idle","parameters":["Speed"],"states":[)"
                    R"({"name":"Idle","clip":"10000000-0000-4000-8000-000000000001","speed":1.0,"loop":true,)"
                    R"("transitions":[{"destination":"Run","duration":0.2,"hasExitTime":false,"exitTime":1.0,)"
                    R"("conditions":[{"parameter":"Speed","comparison":0,"value":0.1}]}]},)"
                    R"({"name":"Run","clip":"10000000-0000-4000-8000-000000000002","speed":1.0,"loop":true,)"
                    R"("transitions":[]}]})");
    const auto sourceBytes = Bytes(source);
    const auto migrated = Keire::AnimationGraphAsset::Decode(sourceBytes);
    const auto& definition = migrated->Definition();

    CHECK(definition.SchemaVersion == 1);
    REQUIRE(definition.ParameterDefinitions.size() == 1);
    CHECK(definition.ParameterDefinitions.front().Type == Keire::AnimationParameterType::Float);
    CHECK_FALSE(definition.ParameterDefinitions.front().Id.empty());
    REQUIRE(definition.Layers.size() == 1);
    CHECK_FALSE(definition.Layers.front().Id.empty());
    CHECK_FALSE(definition.Layers.front().States.front().Id.empty());
    CHECK_FALSE(definition.Layers.front().States.front().Transitions.front().Id.empty());
    CHECK(definition.Layers.front().States.front().Transitions.front().DestinationId ==
          definition.Layers.front().States.back().Id);

    const auto saved = Keire::AnimationGraphAsset::Encode(definition);
    CHECK(Text(saved).find(R"("schemaVersion": 2)") != std::string::npos);
    const auto reopened = Keire::AnimationGraphAsset::Decode(saved);
    CHECK(reopened->Definition().SchemaVersion == 2);
    CHECK(reopened->Definition().Layers.front().States.front().Id == definition.Layers.front().States.front().Id);

    const auto importer = Keire::CreateAnimationGraphAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport({}, sourceBytes);
    CHECK(imported.Bytes == sourceBytes);
    CHECK(imported.AssetDependencies.size() == 2);
}

TEST_CASE("Animator controller authoring supports empty assets and persists finite graph positions")
{
    Keire::AnimationGraphDefinition empty;
    CHECK_NOTHROW(Keire::ValidateAnimationGraph(empty));
    const auto emptyRoundTrip = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(empty));
    CHECK(emptyRoundTrip->Definition().Layers.empty());
    Keire::AnimatorInstance emptyAnimator(TestSkeleton(), emptyRoundTrip,
                                          [](const Keire::AssetId) -> Keire::Ref<const Keire::AnimationClipAsset>
                                          { return {}; });
    const auto emptySample = emptyAnimator.Update(1.0F / 60.0F);
    CHECK(emptySample.LocalPose.size() == 2);
    CHECK(emptySample.State.empty());
    REQUIRE(emptyAnimator.DebugSnapshot());
    CHECK(emptyAnimator.DebugSnapshot()->Layers.empty());

    const auto clip = Keire::AssetId::Parse("18000000-0000-4000-8000-000000000001");
    auto state = ClipState("state-idle", "Idle", clip);
    state.EditorPosition = {128.0F, 64.0F};
    auto graph = GraphWithBaseLayer({}, {state});
    const auto reopened = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(graph));
    CHECK(reopened->Definition().Layers.front().States.front().EditorPosition.X == doctest::Approx(128.0F));
    CHECK(reopened->Definition().Layers.front().States.front().EditorPosition.Y == doctest::Approx(64.0F));

    graph.Layers.front().States.front().EditorPosition.X = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(Keire::ValidateAnimationGraph(graph), std::invalid_argument);
}

TEST_CASE("Animation graph validates stable IDs typed conditions blend trees and avatar masks")
{
    const auto clipA = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000001");
    const auto clipB = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000002");
    const auto clipC = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000003");
    std::vector<Keire::AnimationParameterDefinition> parameters{
        {"parameter-x", "X", Keire::AnimationParameterType::Float, 0.25F},
        {"parameter-y", "Y", Keire::AnimationParameterType::Float, 0.75F},
        {"parameter-mode", "Mode", Keire::AnimationParameterType::Integer, 0.0F, 2},
        {"parameter-ready", "Ready", Keire::AnimationParameterType::Boolean, 0.0F, 0, true},
        {"parameter-jump", "Jump", Keire::AnimationParameterType::Trigger}};
    auto idle = ClipState("state-idle", "Idle", clipA);
    auto blend = ClipState("state-blend", "Blend", clipA);
    blend.Motion.Type = Keire::AnimationMotionType::BlendTree2D;
    blend.Motion.Clip = {};
    blend.Clip = {};
    blend.Motion.ParameterX = "parameter-x";
    blend.Motion.ParameterY = "parameter-y";
    blend.Motion.Children = {{"blend-a", clipA, 0.0F, {0.0F, 0.0F}},
                             {"blend-b", clipB, 0.0F, {1.0F, 0.0F}},
                             {"blend-c", clipC, 0.0F, {0.0F, 1.0F}}};
    Keire::AnimationTransition transition;
    transition.Id = "transition-idle-blend";
    transition.Destination = "Blend";
    transition.DestinationId = "state-blend";
    transition.Conditions = {{"Mode", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-mode", 2},
                             {"Ready", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-ready", 0, true},
                             {"Jump", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-jump", 0, true}};
    idle.Transitions.push_back(transition);
    auto graph = GraphWithBaseLayer(parameters, {idle, blend});
    CHECK_NOTHROW(Keire::ValidateAnimationGraph(graph));
    const auto encoded = Keire::AnimationGraphAsset::Encode(graph);
    const auto decoded = Keire::AnimationGraphAsset::Decode(encoded);
    CHECK(decoded->Definition().Layers.front().States.back().Motion.Type == Keire::AnimationMotionType::BlendTree2D);
    CHECK(decoded->Definition().Layers.front().States.back().Motion.Children.size() == 3);

    auto invalidBooleanComparison = graph;
    invalidBooleanComparison.Layers.front().States.front().Transitions.front().Conditions[1].Comparison =
        Keire::AnimationConditionComparison::Greater;
    CHECK_THROWS_AS(Keire::ValidateAnimationGraph(invalidBooleanComparison), std::invalid_argument);

    auto duplicateStableId = graph;
    duplicateStableId.Layers.front().States.back().Motion.Children.back().Id = "blend-a";
    CHECK_THROWS_AS(Keire::ValidateAnimationGraph(duplicateStableId), std::invalid_argument);

    auto missingStableId = Text(encoded);
    const auto parameterId = missingStableId.find(R"("id": "parameter-x")");
    REQUIRE(parameterId != std::string::npos);
    missingStableId.replace(parameterId, std::string(R"("id": "parameter-x")").size(), R"("id": "")");
    CHECK_THROWS_AS((void)Keire::AnimationGraphAsset::Decode(Bytes(missingStableId)), std::invalid_argument);

    const auto skeleton = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000010");
    const std::vector<Keire::AvatarMaskBoneWeight> bones{{"Root", 0.0F}, {"Hand", 0.5F}};
    const auto mask = Keire::AvatarMaskAsset::Decode(Keire::AvatarMaskAsset::Encode(skeleton, bones));
    CHECK(mask->Skeleton() == skeleton);
    CHECK(mask->Weight("Root") == doctest::Approx(0.0F));
    CHECK(mask->Weight("Hand") == doctest::Approx(0.5F));
    CHECK(mask->Weight("Missing") == doctest::Approx(0.0F));
    CHECK_THROWS_AS(Keire::ValidateAvatarMask(skeleton, std::vector<Keire::AvatarMaskBoneWeight>{{"Root", 2.0F}}),
                    std::invalid_argument);
}

TEST_CASE("Animator evaluates typed transitions with crossfade bookkeeping and consumes triggers")
{
    const auto skeletonId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001");
    const auto idleId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000002");
    const auto runId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000003");
    const auto skeleton = TestSkeleton();
    const auto idleClip = ConstantHandClip(skeletonId, 0.0F);
    const auto runClip = ConstantHandClip(skeletonId, 4.0F);

    auto idle = ClipState("state-idle", "Idle", idleId);
    auto run = ClipState("state-run", "Run", runId);
    Keire::AnimationTransition transition;
    transition.Id = "transition-run";
    transition.Destination = "Run";
    transition.DestinationId = run.Id;
    transition.Duration = 1.0F;
    transition.Conditions = {
        {"Mode", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-mode", 2},
        {"Grounded", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-grounded", 0, true},
        {"Jump", Keire::AnimationConditionComparison::Equal, 0.0F, "parameter-jump", 0, true}};
    idle.Transitions.push_back(std::move(transition));
    auto definition = GraphWithBaseLayer({{"parameter-mode", "Mode", Keire::AnimationParameterType::Integer},
                                          {"parameter-grounded", "Grounded", Keire::AnimationParameterType::Boolean},
                                          {"parameter-jump", "Jump", Keire::AnimationParameterType::Trigger}},
                                         {idle, run});
    const auto graph = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(definition));
    Keire::AnimatorInstance animator(skeleton, graph,
                                     [&](const Keire::AssetId id)
                                     {
                                         if (id == idleId)
                                             return idleClip;
                                         if (id == runId)
                                             return runClip;
                                         return Keire::Ref<Keire::AnimationClipAsset>{};
                                     });

    animator.SetInteger("Mode", 2);
    animator.SetBool("Grounded", true);
    animator.SetTrigger("Jump");
    const auto halfway = animator.Update(0.25F);
    CHECK(halfway.State == "Run");
    CHECK(halfway.LocalPose[1].Translation.X == doctest::Approx(1.0F));
    CHECK_FALSE(animator.Trigger("Jump"));
    const auto transitionDebug = animator.DebugSnapshot();
    REQUIRE(transitionDebug);
    REQUIRE(transitionDebug->Layers.size() == 1);
    CHECK(transitionDebug->Layers.front().InTransition);
    CHECK(transitionDebug->Layers.front().TransitionProgress == doctest::Approx(0.25F));
    animator.SetLayerWeight("Base", 0.5F);

    auto compatibleDefinition = definition;
    compatibleDefinition.ParameterDefinitions[0].Name = "MovementMode";
    compatibleDefinition.ParameterDefinitions[1].Name = "IsGrounded";
    compatibleDefinition.ParameterDefinitions[2].Name = "JumpRequested";
    compatibleDefinition.Layers.front().Name = "Gameplay";
    compatibleDefinition.Layers.front().States[1].Name = "Sprint";
    const auto compatibleGraph = Keire::CreateRef<Keire::AnimationGraphAsset>(compatibleDefinition);
    CHECK(animator.Reload(compatibleGraph));
    CHECK(animator.Integer("MovementMode") == 2);
    CHECK(animator.Bool("IsGrounded"));
    CHECK_FALSE(animator.Trigger("JumpRequested"));
    CHECK(animator.LayerWeight("Gameplay") == doctest::Approx(0.5F));
    CHECK(animator.DebugSnapshot()->Layers.front().InTransition);
    CHECK(animator.DebugSnapshot()->Layers.front().TransitionProgress == doctest::Approx(0.25F));
    animator.SetLayerWeight("Gameplay", 1.0F);

    const auto completed = animator.Update(0.75F);
    CHECK(completed.State == "Sprint");
    CHECK(completed.LocalPose[1].Translation.X == doctest::Approx(4.0F));
    CHECK_FALSE(animator.DebugSnapshot()->Layers.front().InTransition);
    CHECK_THROWS_AS(animator.SetFloat("MovementMode", 1.0F), std::invalid_argument);

    auto incompatibleDefinition = compatibleDefinition;
    incompatibleDefinition.Layers.front().States[1].Id = "state-sprint-replacement";
    incompatibleDefinition.Layers.front().States[0].Transitions[0].DestinationId = "state-sprint-replacement";
    const auto incompatibleGraph = Keire::CreateRef<Keire::AnimationGraphAsset>(incompatibleDefinition);
    CHECK_FALSE(animator.Reload(incompatibleGraph));
    CHECK(animator.DebugSnapshot()->Layers.front().State == "Idle");
    CHECK(animator.DebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(0.0F));
    CHECK(animator.Integer("MovementMode") == 0);
}

TEST_CASE("Animator samples deterministic blend trees and applies masked layer weights")
{
    const auto skeletonId = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000001");
    const auto clipAId = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000002");
    const auto clipBId = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000003");
    const auto overlayId = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000004");
    const auto maskId = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000005");
    const auto skeleton = TestSkeleton();
    const auto clipA = ConstantHandClip(skeletonId, 0.0F);
    const auto clipB = ConstantHandClip(skeletonId, 10.0F);
    const auto overlayClip = ConstantHandClip(skeletonId, 10.0F);
    const auto mask = Keire::CreateRef<Keire::AvatarMaskAsset>(
        skeletonId, std::vector<Keire::AvatarMaskBoneWeight>{{"Root", 0.0F}, {"Hand", 0.5F}});

    auto blend = ClipState("state-blend", "Blend", clipAId);
    blend.Clip = {};
    blend.Motion.Type = Keire::AnimationMotionType::BlendTree1D;
    blend.Motion.Clip = {};
    blend.Motion.ParameterX = "parameter-speed";
    blend.Motion.Children = {{"child-a", clipAId, 0.0F}, {"child-b", clipBId, 10.0F}};
    auto definition =
        GraphWithBaseLayer({{"parameter-speed", "Speed", Keire::AnimationParameterType::Float, 5.0F}}, {blend});

    Keire::AnimationLayerDefinition overlay;
    overlay.Id = "layer-overlay";
    overlay.Name = "Overlay";
    overlay.Mode = Keire::AnimationLayerMode::Override;
    overlay.DefaultWeight = 0.5F;
    overlay.AvatarMask = maskId;
    overlay.EntryStateId = "state-overlay";
    overlay.States = {ClipState("state-overlay", "Overlay", overlayId)};
    definition.Layers.push_back(std::move(overlay));

    const auto graph = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(definition));
    Keire::AnimatorInstance animator(
        skeleton, graph,
        [&](const Keire::AssetId id)
        {
            if (id == clipAId)
                return clipA;
            if (id == clipBId)
                return clipB;
            if (id == overlayId)
                return overlayClip;
            return Keire::Ref<Keire::AnimationClipAsset>{};
        },
        [&](const Keire::AssetId id) { return id == maskId ? mask : Keire::Ref<Keire::AvatarMaskAsset>{}; });

    const auto sample = animator.Update(0.0F);
    CHECK(sample.LocalPose[1].Translation.X == doctest::Approx(6.25F));
    const auto debug = animator.DebugSnapshot();
    REQUIRE(debug->Layers.size() == 2);
    REQUIRE(debug->Layers.front().BlendWeights.size() == 2);
    CHECK(debug->Layers.front().BlendWeights[0].Weight == doctest::Approx(0.5F));
    CHECK(debug->Layers.front().BlendWeights[1].Weight == doctest::Approx(0.5F));
    CHECK(animator.LayerWeight("Overlay") == doctest::Approx(0.5F));
    animator.SetLayerWeight("layer-overlay", 0.0F);
    CHECK(animator.Update(0.0F).LocalPose[1].Translation.X == doctest::Approx(5.0F));

    auto blend2D = ClipState("state-blend-2d", "Blend2D", clipAId);
    blend2D.Clip = {};
    blend2D.Motion.Type = Keire::AnimationMotionType::BlendTree2D;
    blend2D.Motion.Clip = {};
    blend2D.Motion.ParameterX = "parameter-x";
    blend2D.Motion.ParameterY = "parameter-y";
    blend2D.Motion.Children = {{"child-2d-a", clipAId, 0.0F, {0.0F, 0.0F}},
                               {"child-2d-b", clipBId, 0.0F, {1.0F, 0.0F}},
                               {"child-2d-c", overlayId, 0.0F, {0.0F, 1.0F}}};
    const auto graph2D = Keire::CreateRef<Keire::AnimationGraphAsset>(
        GraphWithBaseLayer({{"parameter-x", "X", Keire::AnimationParameterType::Float, 1.0F},
                            {"parameter-y", "Y", Keire::AnimationParameterType::Float}},
                           {blend2D}));
    Keire::AnimatorInstance animator2D(skeleton, graph2D,
                                       [&](const Keire::AssetId id)
                                       {
                                           if (id == clipAId)
                                               return clipA;
                                           if (id == clipBId)
                                               return clipB;
                                           if (id == overlayId)
                                               return overlayClip;
                                           return Keire::Ref<Keire::AnimationClipAsset>{};
                                       });
    CHECK(animator2D.Update(0.0F).LocalPose[1].Translation.X == doctest::Approx(10.0F));
    REQUIRE(animator2D.DebugSnapshot()->Layers.front().BlendWeights.size() == 1);
    CHECK(animator2D.DebugSnapshot()->Layers.front().BlendWeights.front().ChildId == "child-2d-b");
}

TEST_CASE("Animator publishes rotational root motion without reset or wrap discontinuities")
{
    const auto skeletonId = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000001");
    const auto clipId = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000002");
    const auto skeleton = TestSkeleton();
    const auto clip = RotatingRootClip(skeletonId);
    auto graphDefinition = GraphWithBaseLayer({}, {ClipState("state-turn", "Turn", clipId)});
    const auto graph = Keire::CreateRef<Keire::AnimationGraphAsset>(std::move(graphDefinition));
    Keire::AnimatorInstance animator(skeleton, graph, [clipId, clip](const Keire::AssetId id)
                                     { return id == clipId ? clip : Keire::Ref<Keire::AnimationClipAsset>{}; });

    const auto first = animator.Update(0.0F);
    CHECK(first.RootRotation == Keire::Quaternion{});
    CHECK(first.LocalPose.front().Rotation == Keire::Quaternion{});

    const auto second = animator.Update(0.5F);
    const auto expected = Keire::Math::EulerDegreesToQuaternion({0.0F, 45.0F, 0.0F});
    CHECK(second.RootRotation.X == doctest::Approx(expected.X));
    CHECK(second.RootRotation.Y == doctest::Approx(expected.Y));
    CHECK(second.RootRotation.Z == doctest::Approx(expected.Z));
    CHECK(second.RootRotation.W == doctest::Approx(expected.W));
    CHECK(animator.DebugSnapshot()->RootRotation.Y == doctest::Approx(expected.Y));

    const auto wrapped = animator.Update(0.6F);
    CHECK(wrapped.RootRotation == Keire::Quaternion{});
    CHECK(animator.DebugSnapshot()->RootRotation == Keire::Quaternion{});
}

TEST_CASE("Animator supports explicit play, cross-fade, stop, and restart control")
{
    const auto skeletonId = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000001");
    const auto idleId = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000002");
    const auto runId = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000003");
    const auto skeleton = TestSkeleton();
    const auto idleClip = ConstantHandClip(skeletonId, 0.0F);
    const auto runClip = ConstantHandClip(skeletonId, 4.0F);
    auto definition =
        GraphWithBaseLayer({}, {ClipState("state-idle", "Idle", idleId), ClipState("state-run", "Run", runId)});
    const auto graph = Keire::CreateRef<Keire::AnimationGraphAsset>(std::move(definition));
    Keire::AnimatorInstance animator(skeleton, graph,
                                     [&](const Keire::AssetId id)
                                     {
                                         if (id == idleId)
                                             return idleClip;
                                         if (id == runId)
                                             return runClip;
                                         return Keire::Ref<Keire::AnimationClipAsset>{};
                                     });

    animator.Play("Run", "Base", 0.5F);
    auto sample = animator.Update(0.0F);
    CHECK(animator.Playing());
    CHECK(sample.State == "Run");
    CHECK(sample.NormalizedTime == doctest::Approx(0.5F));
    CHECK(sample.LocalPose[1].Translation.X == doctest::Approx(4.0F));

    animator.CrossFade("Idle", 1.0F, "Base");
    sample = animator.Update(0.5F);
    CHECK(sample.State == "Idle");
    CHECK(sample.LocalPose[1].Translation.X == doctest::Approx(2.0F));
    REQUIRE(animator.DebugSnapshot());
    CHECK(animator.DebugSnapshot()->Layers.front().InTransition);

    animator.Stop();
    sample = animator.Update(1.0F);
    CHECK_FALSE(animator.Playing());
    CHECK(sample.State.empty());
    CHECK(sample.LocalPose[1].Translation.X == doctest::Approx(0.0F));

    animator.Play("Idle");
    CHECK(animator.Playing());
    CHECK(animator.Update(0.0F).State == "Idle");
}
