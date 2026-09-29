#include "Keire/Core.h"
#include "KeireClient/Editor/AnimatorInspectorDiagnostic.h"
#include "KeireClient/Editor/AssetInspectorFileActions.h"
#include "KeireClient/Editor/ImportedModelAnimation.h"
#include "KeireClient/Editor/RiggingStudioValidation.h"
#include "KeireClientInternal/Editor/AnimatorClipCreation.h"
#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"
#include "KeireClientInternal/Editor/AnimatorControllerPreviewInternal.h"
#include "KeireClientInternal/Editor/AnimatorPreviewSelection.h"
#include "KeireClientInternal/Editor/StandaloneClipPreview.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

TEST_CASE("Animator preview keeps authored skeleton settings and clears rejected target poses")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog.clear();
    specification.Decoders = {Keire::CreateSkeletonAssetDecoder(), Keire::CreateSkinnedMeshAssetDecoder(),
                              Keire::CreateAnimationClipAssetDecoder()};
    const auto skeletonId = Keire::AssetId::Generate();
    const auto skinId = Keire::AssetId::Generate();
    const auto graphId = Keire::AssetId::Generate();
    const auto clipId = Keire::AssetId::Generate();
    struct TemporaryCatalog
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("PreviewIsolation-" + Keire::AssetId::Generate().ToString()));
        ~TemporaryCatalog()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } catalog;
    std::filesystem::create_directories(catalog.Root);
    // The catalog supplies type identity; live publication below supplies the payload before any load.
    const std::array entries{Keire::Detail::CatalogEntry{.Id = clipId,
                                                         .Type = Keire::AnimationClipAsset::StaticType(),
                                                         .PackPath = "preview.pack",
                                                         .Offset = 16,
                                                         .CompressedBytes = 1,
                                                         .UncompressedBytes = 1}};
    specification.DevelopmentCatalog = catalog.Root / "catalog.json";
    Keire::Detail::WriteCatalog(specification.DevelopmentCatalog, entries);
    {
        std::ofstream pack(catalog.Root / "preview.pack", std::ios::binary);
        Keire::Detail::WritePackHeader(pack);
        pack.put(0);
        REQUIRE(pack.good());
    }
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);

    Keire::AnimationTrack track;
    track.Bone = 0;
    track.Keys = {{0.0F, {}}, {1.0F, {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
    REQUIRE(assets->PublishDevelopmentAsset(clipId, Keire::AnimationClipAsset::Decode(Keire::AnimationClipAsset::Encode(
                                                        skeletonId, 1.0F, std::span(&track, 1), {}, false))));
    REQUIRE(assets->PublishDevelopmentAsset(
        skeletonId, Keire::CreateRef<Keire::SkeletonAsset>(std::vector<Keire::SkeletonBone>{{"Root", -1, {}, {}}})));
    REQUIRE(assets->PublishDevelopmentAsset(
        skinId, Keire::CreateRef<Keire::SkinnedMeshAsset>(
                    Keire::AssetId::Generate(), skeletonId,
                    std::vector<Keire::SkinVertexInfluence>{{{0, 0, 0, 0}, {1.0F, 0.0F, 0.0F, 0.0F}}})));
    Keire::AnimationGraphDefinition graph;
    Keire::AnimationLayerDefinition layer;
    layer.Id = "base";
    layer.Name = "Base";
    layer.EntryStateId = "idle";
    Keire::AnimationStateDefinition state;
    state.Id = "idle";
    state.Name = "Idle";
    state.Clip = clipId;
    state.Motion.Clip = clipId;
    layer.States.push_back(state);
    graph.Layers.push_back(layer);
    KeireEditor::AnimatorControllerDocument controller;
    controller.Open(graphId, graph, {}, {});
    KeireEditor::SceneDocument document;
    const auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                      Keire::SceneAsset::EmptyDefinition("Preview isolation"));
    document.Open(scene);
    auto entity = scene->CreateEntity("Preview target");
    const auto animator = entity.AddComponent<Keire::AnimatorComponent>();
    animator->SetGraph(graphId);
    animator->SetSkinnedMesh(skinId);
    document.Select(entity.Id().Value());
    KeireEditor::AnimatorControllerPreviewState preview;
    for (const auto authoredSkeleton : {Keire::AssetId{}, Keire::AssetId::Generate(), skeletonId})
    {
        animator->SetSkeleton(authoredSkeleton);
        const auto before = Keire::SceneAsset::Encode(scene->Snapshot());
        preview.Restart();
        preview.Synchronize(document, controller, assets);
        INFO(preview.Diagnostic);
        REQUIRE(preview.Diagnostic.empty());
        CHECK(preview.Skeleton == skeletonId);
        CHECK(animator->SkinPalette().size() == 1);
        CHECK(animator->Skeleton() == authoredSkeleton);
        CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
        preview.Seek(0.5F);
        preview.Synchronize(document, controller, assets);
        CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
        preview.Stop();
        CHECK(animator->SkinPalette().empty());
        CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
    }
    preview.Restart();
    preview.Synchronize(document, controller, assets);
    REQUIRE_FALSE(animator->SkinPalette().empty());
    animator->SetGraph(Keire::AssetId::Generate());
    preview.Synchronize(document, controller, assets);
    CHECK(preview.Diagnostic == "Assign this controller to the selected Animator before previewing it.");
    CHECK(animator->SkinPalette().empty());
    controller.ReplaceDefinition(graph, true);
    KeireEditor::AnimatorControllerDocument clipPreview;
    clipPreview.Open(clipId, graph, {}, {});
    animator->SetSpeed(0.0F);
    const auto clipPreviewSnapshot = Keire::SceneAsset::Encode(scene->Snapshot());
    preview.PlaybackSpeed = 1.0F;
    preview.Restart();
    preview.LastTick -= std::chrono::milliseconds(100);
    preview.Tick(true, document, clipPreview, assets, true);
    CHECK(preview.Diagnostic.empty());
    CHECK_FALSE(animator->SkinPalette().empty());
    const float normalPreviewTime = preview.NormalizedTime;
    CHECK(normalPreviewTime > 0.05F);
    CHECK(animator->Speed() == 0.0F);
    CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == clipPreviewSnapshot);
    preview.PlaybackSpeed = 2.0F;
    preview.Restart();
    preview.LastTick -= std::chrono::milliseconds(100);
    preview.Tick(true, document, clipPreview, assets, true);
    CHECK(preview.NormalizedTime > normalPreviewTime + 0.05F);
    CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == clipPreviewSnapshot);
    preview.Seek(0.5F);
    preview.Tick(true, document, clipPreview, assets, true);
    CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == clipPreviewSnapshot);
    CHECK(controller.Asset() == graphId);
    CHECK(controller.Dirty());
    preview.Tick(false, document, clipPreview, assets, true);
    CHECK_FALSE(preview.Active);
    CHECK(animator->SkinPalette().empty());
    animator->SetGraph(graphId);
    preview.Restart();
    preview.Synchronize(document, controller, assets);
    CHECK_FALSE(animator->SkinPalette().empty());
    preview.Tick(true, document, controller, assets);
    CHECK(preview.Active);
    CHECK(preview.Playing);
    CHECK_FALSE(animator->SkinPalette().empty());
    preview.Tick(false, document, controller, assets);
    CHECK_FALSE(preview.Active);
    CHECK(animator->SkinPalette().empty());
    preview.Tick(true, document, controller, assets);
    CHECK_FALSE(preview.Active);
    preview.Restart();
    preview.Tick(true, document, controller, assets);
    REQUIRE_FALSE(animator->SkinPalette().empty());
    controller.Close();
    preview.Tick(true, document, controller, assets);
    CHECK_FALSE(preview.Active);
    CHECK(animator->SkinPalette().empty());
    controller.Open(graphId, graph, {}, {});
    preview.Restart();
    preview.Tick(true, document, controller, assets);
    REQUIRE_FALSE(animator->SkinPalette().empty());
    document.BeginPlay({}, assets);
    preview.Tick(true, document, controller, assets);
    CHECK_FALSE(preview.Active);
    CHECK(animator->SkinPalette().empty());
    document.EndPlay();
    document.Close();
    CHECK_NOTHROW(preview.Stop());
    CHECK_NOTHROW(preview.Stop());
    assets->Close();
}

TEST_CASE("Adding animation clips creates a base layer and preserves existing entries and subgraphs")
{
    Keire::AnimationGraphDefinition graph;
    std::string layer;
    const auto clip = Keire::AssetId::Generate();
    CHECK_THROWS((void)KeireEditor::AddAnimatorClipState(graph, layer, {}, {}, "Walk"));
    CHECK(graph.Layers.empty());
    const auto first = KeireEditor::AddAnimatorClipState(graph, layer, {}, clip, "Walk");
    REQUIRE(graph.Layers.size() == 1);
    CHECK(layer == graph.Layers.front().Id);
    CHECK(graph.Layers.front().EntryStateId == first);
    const auto second = KeireEditor::AddAnimatorClipState(graph, layer, {}, clip, "Walk");
    CHECK(first != second);
    CHECK(graph.Layers.front().EntryStateId == first);
    CHECK(graph.Layers.front().States.back().Name == "Walk 2");
    CHECK(graph.Layers.front().States.back().Motion.Clip == clip);
    Keire::AnimationStateMachineSubgraphDefinition nested;
    nested.Id = "nested";
    nested.Name = "Nested";
    graph.Layers.front().Subgraphs.push_back(nested);
    const auto nestedState = KeireEditor::AddAnimatorClipState(graph, layer, "nested", clip, "Retargeted Walk");
    CHECK(graph.Layers.front().Subgraphs.front().EntryStateId == nestedState);
    CHECK(graph.Layers.front().EntryStateId == first);
    CHECK(graph.Layers.front().States.back().SubgraphId == "nested");
    CHECK_THROWS((void)KeireEditor::AddAnimatorClipState(graph, layer, "removed", clip, "Invalid"));
    CHECK(graph.Layers.front().States.size() == 3);
    CHECK(graph.Layers.front().Subgraphs.size() == 1);
    CHECK_NOTHROW(Keire::ValidateAnimationGraph(graph));
}

TEST_CASE("Adding animation clips avoids manually positioned states")
{
    Keire::AnimationGraphDefinition graph;
    std::string layer;
    const auto clip = Keire::AssetId::Generate();
    for (std::size_t index = 0; index < 6; ++index)
    {
        (void)KeireEditor::AddAnimatorClipState(graph, layer, {}, clip, "Walk");
        graph.Layers.front().States.back().EditorPosition =
            KeireEditor::AnimatorControllerPanelInternal::StateGridPosition(index, {216.0F, 133.0F});
    }
    const auto before = graph.Layers.front().States;
    const auto entry = graph.Layers.front().EntryStateId;
    (void)KeireEditor::AddAnimatorClipState(graph, layer, {}, clip, "Static pose");
    (void)KeireEditor::AddAnimatorClipState(graph, layer, {}, clip, "Baked walk");
    const auto& states = graph.Layers.front().States;
    CHECK(graph.Layers.front().EntryStateId == entry);
    for (std::size_t index = 0; index < before.size(); ++index)
        CHECK(states[index].EditorPosition == before[index].EditorPosition);
    for (std::size_t index = before.size(); index < states.size(); ++index)
        for (std::size_t other = 0; other < index; ++other)
        {
            const auto position = states[index].EditorPosition;
            const auto previous = states[other].EditorPosition;
            CHECK((std::abs(position.X - previous.X) >= 230.0F || std::abs(position.Y - previous.Y) >= 102.0F));
        }
    CHECK_NOTHROW(Keire::ValidateAnimationGraph(graph));
}

TEST_CASE("Animator drop placement avoids displayed legacy positions and isolates subgraphs")
{
    Keire::AnimationLayerDefinition layer;
    for (int index = 0; index < 3; ++index)
    {
        Keire::AnimationStateDefinition state;
        state.Id = std::to_string(index);
        layer.States.push_back(state);
    }
    CHECK(KeireEditor::FindFreeAnimatorStatePosition(layer, {}, 0) == Keire::Vector2{0.0F, 120.0F});
    CHECK(KeireEditor::FindFreeAnimatorStatePosition(layer, "nested", 0) == Keire::Vector2{});
    const auto drop = KeireEditor::FindFreeAnimatorStatePosition(layer, {}, 0, {240.0F, 10.0F});
    for (std::size_t index = 0; index < layer.States.size(); ++index)
    {
        const auto existing = KeireEditor::AnimatorControllerPanelInternal::DisplayPosition(layer.States[index], index);
        CHECK((std::abs(drop.X - existing.X) >= 230.0F || std::abs(drop.Y - existing.Y) >= 102.0F));
        CHECK(layer.States[index].EditorPosition == Keire::Vector2{});
    }
}

TEST_CASE("Animator placement does not emit a legacy origin marker for appended states")
{
    Keire::AnimationLayerDefinition layer;
    Keire::AnimationStateDefinition existing;
    existing.EditorPosition = {1000.0F, 1000.0F};
    layer.States.push_back(existing);
    for (const auto origin : {Keire::Vector2{}, Keire::Vector2{0.0005F, -0.0005F}})
    {
        Keire::AnimationStateDefinition added;
        added.EditorPosition = KeireEditor::FindFreeAnimatorStatePosition(layer, {}, 0, origin);
        CHECK(KeireEditor::AnimatorControllerPanelInternal::DisplayPosition(added, 1) == added.EditorPosition);
        CHECK(added.EditorPosition.X > 1.0F);
    }
    CHECK(KeireEditor::FindFreeAnimatorStatePosition(layer, "empty", 0) == Keire::Vector2{});
}

TEST_CASE("Selected animator previews preserve authored entry states and scrub the requested layer")
{
    const auto skeletonId = Keire::AssetId::Generate();
    const auto clipId = Keire::AssetId::Generate();
    const std::array bones{Keire::SkeletonBone{"Root", -1, {}, {}}};
    const auto skeleton = Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(bones));
    Keire::AnimationTrack track;
    track.Bone = 0;
    track.Keys = {{0.0F, {}}, {2.0F, {{8.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
    const auto clip = Keire::AnimationClipAsset::Decode(
        Keire::AnimationClipAsset::Encode(skeletonId, 2.0F, std::span(&track, 1), {}, false));
    Keire::AnimationGraphDefinition definition;
    for (const auto layerId : {"base", "upper"})
    {
        Keire::AnimationLayerDefinition layer;
        layer.Id = layerId;
        layer.Name = layerId;
        for (const auto name : {"Idle", "Walk"})
        {
            Keire::AnimationStateDefinition state;
            state.Id = std::string(layerId) + name;
            state.Name = name;
            state.Clip = clipId;
            state.Motion.Clip = clipId;
            layer.States.push_back(state);
        }
        layer.EntryStateId = layer.States.front().Id;
        definition.Layers.push_back(layer);
    }
    const auto graph = Keire::CreateRef<Keire::AnimationGraphAsset>(definition);
    Keire::AnimatorInstance instance(skeleton, graph, [clip](Keire::AssetId) { return clip; });
    KeireEditor::ResetSelectedPreview(instance, definition, "upper", "upperWalk", 0.75F);
    const auto sample = instance.Update(0.0F);
    CHECK(sample.LocalPose.front().Translation.X == doctest::Approx(6.0F));
    const auto snapshot = instance.DebugSnapshot();
    REQUIRE(snapshot->Layers.size() == 2);
    CHECK(snapshot->Layers[0].StateId == "baseIdle");
    CHECK(snapshot->Layers[1].StateId == "upperWalk");
    CHECK(snapshot->Layers[1].NormalizedTime == doctest::Approx(0.75F));
    CHECK(definition.Layers[1].EntryStateId == "upperIdle");
    CHECK(graph->Definition().Layers[1].EntryStateId == "upperIdle");
    CHECK_FALSE(KeireEditor::HasPreviewState(definition, "base", "upperWalk"));
    CHECK_FALSE(KeireEditor::HasPreviewState(definition, "missing", "upperWalk"));
    CHECK_THROWS_WITH(KeireEditor::ResetSelectedPreview(instance, definition, "upper", "removed"),
                      "The previewed state or layer was removed. Select a state and choose Preview Selected, or "
                      "choose Preview Graph to restart from the authored entry state.");
    CHECK(instance.DebugSnapshot()->Layers[1].NormalizedTime == doctest::Approx(0.75F));
    KeireEditor::ResetSelectedPreview(instance, definition, "upper", "upperWalk");
    CHECK(instance.DebugSnapshot()->Layers[1].NormalizedTime == 0.0F);
    instance.Reset();
    CHECK(instance.DebugSnapshot()->Layers[1].StateId == "upperIdle");
}

TEST_CASE("Animator state layout separates every imported clip at the drop location")
{
    using KeireEditor::AnimatorControllerPanelInternal::StateGridPosition;
    using KeireEditor::AnimatorControllerPanelInternal::StateNodeSize;
    const Keire::Vector2 origin{-317.0F, 129.0F};
    CHECK(StateGridPosition(0, origin) == origin);
    for (std::size_t index = 0; index < 15; ++index)
    {
        const auto position = StateGridPosition(index, origin);
        const auto local = StateGridPosition(index);
        CHECK(position.X == local.X + origin.X);
        CHECK(position.Y == local.Y + origin.Y);
        for (std::size_t other = 0; other < index; ++other)
        {
            const auto previous = StateGridPosition(other, origin);
            const bool separated =
                position.X > previous.X + StateNodeSize.X || previous.X > position.X + StateNodeSize.X ||
                position.Y > previous.Y + StateNodeSize.Y || previous.Y > position.Y + StateNodeSize.Y;
            CHECK(separated);
        }
    }
}

TEST_CASE("Imported animation state names preserve authored actions and legacy fallbacks")
{
    using KeireEditor::AnimatorControllerPanelInternal::ImportedClipStateName;
    using KeireEditor::AnimatorControllerPanelInternal::UniqueName;
    CHECK(ImportedClipStateName("Walk", "Fox", 2) == "Walk");
    CHECK(ImportedClipStateName("Run", "Fox", 3) == "Run");
    CHECK(ImportedClipStateName("Spider walking", "WolfSpider", 7) == "Spider walking");
    CHECK(ImportedClipStateName("", "Fox", 1) == "Fox");
    CHECK(ImportedClipStateName("", "Fox", 2) == "Fox 2");
    CHECK(ImportedClipStateName("", "", 1) == "Animation");
    CHECK(ImportedClipStateName("", "", 3) == "Animation 3");
    const std::array states{Keire::AnimationStateDefinition{.Name = "Walk"},
                            Keire::AnimationStateDefinition{.Name = "Walk 2"}};
    CHECK(UniqueName(states, ImportedClipStateName("Walk", "Fox", 2), &Keire::AnimationStateDefinition::Name) ==
          "Walk 3");
}

namespace
{
    class InspectorLayoutLayer final : public Keire::Layer
    {
      public:
        explicit InspectorLayoutLayer(bool& drawn) : Layer("InspectorLayout"), m_Drawn(drawn) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            for (const float width : std::array{150.0F, 220.0F, 360.0F})
            {
                const auto label = "Inspector " + std::to_string(width);
                ui.SetNextWindowSize({width, 700.0F}, false);
                if (auto window = ui.BeginWindow(label); window)
                {
                    std::string name = "Long scene Caf\xc3\xa9.keirescene";
                    const auto bounds = ui.ContentRect();
                    CHECK(KeireEditor::DrawAssetInspectorFileActions(ui, name) ==
                          KeireEditor::AssetInspectorFileAction::None);
                    const auto trash = ui.LastItemRect();
                    CHECK(trash.Minimum.X >= bounds.Minimum.X);
                    CHECK(trash.Maximum.X <= bounds.Maximum.X);
                    CHECK(trash.Maximum.X - trash.Minimum.X >= ui.MeasureText("Move to Trash").Width);
                    CHECK(name == "Long scene Caf\xc3\xa9.keirescene");
                    auto animator = Keire::CreateRef<Keire::AnimatorComponent>();
                    for (const auto source :
                         {Keire::AnimatorPoseSource::AnimationGraph, Keire::AnimatorPoseSource::ProceduralHumanoid})
                    {
                        auto sourceId =
                            ui.PushId(source == Keire::AnimatorPoseSource::AnimationGraph ? "graph" : "procedural");
                        animator->SetPoseSource(source);
                        animator->SetRuntimeDiagnostic("Left arm IK could not resolve a contiguous upper-arm, "
                                                       "lower-arm, and hand chain.");
                        const auto before = ui.CursorPosition();
                        {
                            auto id = ui.PushId("warning");
                            KeireEditor::DrawAnimatorInspectorDiagnostic(ui, *animator, {1.0F, 0.5F, 0.0F, 1.0F});
                        }
                        const float warningHeight = ui.CursorPosition().Y - before.Y;
                        CHECK(warningHeight > 0.0F);
                        CHECK(ui.LastItemRect().Maximum.X <= bounds.Maximum.X);
                        animator->SetRuntimeDiagnostic({});
                        const auto cleared = ui.CursorPosition();
                        {
                            auto id = ui.PushId("clear");
                            KeireEditor::DrawAnimatorInspectorDiagnostic(ui, *animator, {1.0F, 0.5F, 0.0F, 1.0F});
                        }
                        CHECK(ui.CursorPosition().Y - cleared.Y == doctest::Approx(warningHeight));
                    }
                    m_Drawn = true;
                }
                ui.SetNextWindowSize({width, 700.0F}, false);
                if (auto window = ui.BeginWindow("Clip preview " + std::to_string(width)); window)
                {
                    KeireEditor::SceneDocument scene;
                    KeireEditor::AnimatorControllerDocument clip;
                    clip.Open(Keire::AssetId::Generate(), {}, {},
                              std::filesystem::path(u8"Characters/Animations/Café long retargeted run.keireanim"));
                    KeireEditor::AnimatorControllerPreviewState preview;
                    preview.Diagnostic = "Select a scene entity with an Animator component before previewing.";
                    const auto bounds = ui.ContentRect();
                    CHECK_FALSE(KeireEditor::DrawStandaloneClipPreview(ui, {}, scene, clip, preview));
                    CHECK(ui.LastItemRect().Minimum.X >= bounds.Minimum.X);
                    CHECK(ui.LastItemRect().Maximum.X <= bounds.Maximum.X);
                    CHECK_FALSE(preview.Active);
                    CHECK_FALSE(clip.Dirty());
                }
            }
            Owner().RequestExit();
        }

      private:
        bool& m_Drawn;
    };

    class InspectorLayoutApplication final : public Keire::Application
    {
      public:
        explicit InspectorLayoutApplication(bool& drawn) : Application(Specification())
        {
            (void)PushLayer(std::make_unique<InspectorLayoutLayer>(drawn));
        }

      private:
        static Keire::ApplicationSpecification Specification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Title = "Inspector layout regression";
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            return specification;
        }
    };
} // namespace

TEST_CASE("Rigging Studio requires review of partial retargets and committed import settings")
{
    Keire::AnimationRetargetDiagnostics diagnostics;
    diagnostics.SourceTrackCount = 19;
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, false));
    diagnostics.MappedTrackCount = 1;
    CHECK(KeireEditor::HasPartialRetargetMapping(diagnostics));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, false, false));
    CHECK(KeireEditor::CanBakeRetarget(diagnostics, true, false));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, true));
    diagnostics.MappedTrackCount = 19;
    CHECK_FALSE(KeireEditor::HasPartialRetargetMapping(diagnostics));
    CHECK(KeireEditor::CanBakeRetarget(diagnostics, false, false));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, true));
}

TEST_CASE("Rigging Studio prevents baking stale clips after either model import fails")
{
    Keire::AnimationRetargetDiagnostics diagnostics;
    diagnostics.SourceTrackCount = 19;
    diagnostics.MappedTrackCount = 19;
    CHECK(KeireEditor::CanBakeRetarget(diagnostics, false, false, false, false));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, false, true, false));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, false, false, true));
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, false, true, true));
    diagnostics.MappedTrackCount = 1;
    CHECK_FALSE(KeireEditor::CanBakeRetarget(diagnostics, true, false, true, false));
    CHECK(KeireEditor::CanBakeRetarget(diagnostics, true, false, false, false));
}

TEST_CASE("Rigging Studio validates portable output names before baking")
{
    for (const auto name :
         {"", ".", "..", " name", "name ", "name.", "a/b", "a\\b", "a:b", "a*b", "CON", "nul", "COM1", "lpt9.txt"})
        CHECK_FALSE(KeireEditor::RetargetOutputNameError(name).empty());
    for (const auto name : {"Walk", "Fox Walk Retargeted", "COM10", "console", "animation.v2"})
        CHECK(KeireEditor::RetargetOutputNameError(name).empty());
    CHECK_FALSE(KeireEditor::RetargetOutputNameError(std::string(231, 'x')).empty());
    const auto suggested = KeireEditor::SuggestedRetargetName("Fox", "Walk/Run: Take 1");
    CHECK(suggested == "Fox Walk_Run_ Take 1 Retargeted");
    CHECK(KeireEditor::RetargetOutputNameError(suggested).empty());
}

TEST_CASE("Imported model drops configure animation assets and preserve authored Animators")
{
    const auto skeleton = Keire::AssetId::Generate();
    const auto skin = Keire::AssetId::Generate();
    const auto rig = Keire::AssetId::Generate();
    const std::array assets{std::pair{skeleton, Keire::SkeletonAsset::StaticType()},
                            std::pair{skin, Keire::SkinnedMeshAsset::StaticType()},
                            std::pair{rig, Keire::RigDefinitionAsset::StaticType()}};
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Imported model setup"));
    auto entity = scene->CreateEntity("Animated model");
    const auto setup = KeireEditor::ImportedModelAnimation::Resolve(assets);
    setup.Apply(entity);
    const auto animator = entity.GetComponent<Keire::AnimatorComponent>();
    REQUIRE(animator);
    CHECK(animator->Skeleton() == skeleton);
    CHECK(animator->SkinnedMesh() == skin);
    CHECK(animator->RigDefinition() == rig);
    CHECK_FALSE(animator->Graph());
    CHECK(animator->Speed() == 1.0F);
    CHECK_THROWS(setup.Apply(entity));
    CHECK(entity.GetComponent<Keire::AnimatorComponent>() == animator);
    CHECK(animator->SkinnedMesh() == skin);
    auto restored = Keire::CreateRef<Keire::Scene>(scene->Asset(), scene->Snapshot());
    CHECK(restored->FindEntity(entity.Id()).GetComponent<Keire::AnimatorComponent>()->SkinnedMesh() == skin);
    auto staticEntity = scene->CreateEntity("Static model");
    KeireEditor::ImportedModelAnimation::Resolve({}).Apply(staticEntity);
    CHECK_FALSE(staticEntity.GetComponent<Keire::AnimatorComponent>());
    const KeireEditor::ImportedModelAnimation incomplete{.Skin = skin};
    CHECK_THROWS(incomplete.Apply(staticEntity));
    CHECK_FALSE(staticEntity.GetComponent<Keire::AnimatorComponent>());
    const std::array missingSkeleton{std::pair{skin, Keire::SkinnedMeshAsset::StaticType()}};
    CHECK_THROWS(KeireEditor::ImportedModelAnimation::Resolve(missingSkeleton));
    const std::array ambiguous{assets[0], assets[1],
                               std::pair{Keire::AssetId::Generate(), Keire::SkinnedMeshAsset::StaticType()}};
    CHECK_THROWS(KeireEditor::ImportedModelAnimation::Resolve(ambiguous));
    restored->Close();
    scene->Close();
}

TEST_CASE("Asset Inspector file actions remain inside narrow panels")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool drawn = false;
    {
        InspectorLayoutApplication application(drawn);
        CHECK(application.Run() == 0);
    }
    CHECK(drawn);
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    }
    SDL_Quit();
}
TEST_CASE("Retarget bone searches preserve punctuation and match ASCII case without changing names")
{
    CHECK(KeireEditor::RetargetBoneMatchesFilter("Skeleton_arm_joint_L__2_", "ARM_JOINT_l"));
    CHECK_FALSE(KeireEditor::RetargetBoneMatchesFilter("Skeleton_arm_joint_L__2_", "arm_joint_R"));
    CHECK(KeireEditor::RetargetBoneMatchesFilter("Skeleton_arm_joint_L__2_", "__2_"));
    CHECK(KeireEditor::RetargetBoneMatchesFilter("Arm", ""));
    CHECK_FALSE(KeireEditor::RetargetBoneMatchesFilter("", "Arm"));
}

TEST_CASE("Retarget mapping drafts survive asset reloads and reset only for another selection")
{
    KeireEditor::RetargetMappingDraft draft;
    const Keire::AssetId source{1, 2};
    const Keire::AssetId target{3, 4};
    CHECK(draft.Select(source, target));
    draft.Overrides.push_back({"source hand", "target hand"});
    draft.SourceFilter = "hand";
    draft.TargetFilter = "target";
    // Asset refreshes replace loaded objects without changing these stable IDs.
    CHECK_FALSE(draft.Select(Keire::AssetId{1, 2}, Keire::AssetId{3, 4}));
    REQUIRE(draft.Overrides.size() == 1);
    CHECK(draft.Overrides.front().TargetBone == "target hand");
    CHECK(draft.SourceFilter == "hand");
    CHECK(draft.TargetFilter == "target");
    CHECK(draft.Select(Keire::AssetId{5, 6}, target));
    CHECK(draft.Overrides.empty());
    CHECK(draft.SourceFilter.empty());
    CHECK(draft.TargetFilter.empty());
    draft.Overrides.push_back({"source foot", "target foot"});
    CHECK(draft.Select(Keire::AssetId{5, 6}, Keire::AssetId{7, 8}));
    CHECK(draft.Overrides.empty());
}
