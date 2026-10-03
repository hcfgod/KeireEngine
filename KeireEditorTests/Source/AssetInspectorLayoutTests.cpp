#include "Keire/Core.h"
#include "KeireClient/Editor/AnimatorInspectorDiagnostic.h"
#include "KeireClient/Editor/AssetInspectorFileActions.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/ImportedModelAnimation.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/RiggingStudioValidation.h"
#include "KeireClientInternal/Editor/AnimatorClipCreation.h"
#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"
#include "KeireClientInternal/Editor/AnimatorControllerPreviewInternal.h"
#include "KeireClientInternal/Editor/AnimatorPreviewSelection.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"
#include "KeireClientInternal/Editor/StandaloneClipPreview.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
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
        preview.PlaybackSpeed = 3.0F;
        preview.StepFrame();
        preview.Synchronize(document, controller, assets);
        CHECK_FALSE(preview.Playing);
        CHECK(preview.NormalizedTime == doctest::Approx(0.5F + 1.0F / 60.0F));
        CHECK(animator->SkinPalette().front().Elements[12] == doctest::Approx(1.0F + 2.0F / 60.0F));
        CHECK(Keire::SceneAsset::Encode(scene->Snapshot()) == before);
        const auto steppedTime = preview.NormalizedTime;
        preview.Synchronize(document, controller, assets);
        CHECK(preview.NormalizedTime == steppedTime);
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
            for (const float width : std::array{150.0F, 220.0F, 230.0F, 360.0F})
            {
                const auto label = "Inspector " + std::to_string(width);
                ui.SetNextWindowSize({width, 700.0F}, false);
                if (auto window = ui.BeginWindow(label); window)
                {
                    std::string name = "Long scene Caf\xc3\xa9.keirescene";
                    const auto bounds = ui.ContentRect();
                    const float addComponentWidth = ui.ContentAvailable().Width;
                    ui.SetNextItemWidth(std::max(ui.ContentAvailable().Width, 1.0F));
                    {
                        const auto addComponent = ui.BeginCombo("##AddComponent", "Add Component...");
                        (void)addComponent;
                    }
                    const auto addComponentBounds = ui.LastItemRect();
                    CHECK(addComponentBounds.Minimum.X >= bounds.Minimum.X);
                    CHECK(addComponentBounds.Maximum.X <= bounds.Maximum.X);
                    CHECK(addComponentBounds.Maximum.X - addComponentBounds.Minimum.X ==
                          doctest::Approx(addComponentWidth));
                    CHECK(addComponentBounds.Maximum.X - addComponentBounds.Minimum.X >=
                          ui.MeasureText("Add Component...").Width);

                    std::string componentSearch;
                    const float componentSearchWidth = ui.ContentAvailable().Width;
                    ui.SetNextItemWidth(std::max(ui.ContentAvailable().Width, 1.0F));
                    (void)ui.InputTextWithHint("##ComponentSearch", "Search scripts and components", componentSearch);
                    const auto componentSearchBounds = ui.LastItemRect();
                    CHECK(componentSearchBounds.Minimum.X >= bounds.Minimum.X);
                    CHECK(componentSearchBounds.Maximum.X <= bounds.Maximum.X);
                    CHECK(componentSearchBounds.Maximum.X - componentSearchBounds.Minimum.X ==
                          doctest::Approx(componentSearchWidth));

                    const auto entityNameLayout =
                        KeireEditor::Detail::ResolveInspectorFieldLayout(ui.ContentAvailable().Width);
                    std::string entityName = "Narrow entity";
                    if (entityNameLayout.Stacked)
                    {
                        ui.TextWrapped("Entity Name");
                        ui.SetNextItemWidth(entityNameLayout.ControlWidth);
                    }
                    (void)ui.InputText(entityNameLayout.Stacked ? "###InspectorEntityName"
                                                                : "Entity Name###InspectorEntityName",
                                       entityName);
                    const auto entityNameBounds = ui.LastItemRect();
                    CHECK(entityNameBounds.Minimum.X >= bounds.Minimum.X);
                    CHECK(entityNameBounds.Maximum.X <= bounds.Maximum.X);
                    if (entityNameLayout.Stacked)
                    {
                        CHECK(entityNameBounds.Maximum.X - entityNameBounds.Minimum.X ==
                              doctest::Approx(entityNameLayout.ControlWidth));
                    }

                    if (width == 230.0F || width == 360.0F)
                    {
                        const auto prepareLightField = [&ui](const std::string_view fieldLabel)
                        {
                            const auto layout = KeireEditor::Detail::ResolveInspectorFieldLayout(
                                ui.ContentAvailable().Width, 0.0F, KeireEditor::Detail::InspectorInlineActionSpacing,
                                KeireEditor::Detail::InspectorDescriptiveFieldWidth);
                            if (layout.Stacked)
                            {
                                ui.TextWrapped(KeireEditor::Detail::InspectorVisibleLabel(fieldLabel));
                                ui.SetNextItemWidth(layout.ControlWidth);
                            }
                            return std::pair{layout,
                                             KeireEditor::Detail::InspectorControlLabel(fieldLabel, layout.Stacked)};
                        };
                        const auto checkLightControlBounds = [&](const Keire::UiItemRect controlBounds,
                                                                 const KeireEditor::Detail::InspectorFieldLayout layout)
                        {
                            CHECK(controlBounds.Minimum.X >= bounds.Minimum.X);
                            CHECK(controlBounds.Maximum.X <= bounds.Maximum.X);
                            if (layout.Stacked)
                            {
                                CHECK(controlBounds.Maximum.X - controlBounds.Minimum.X ==
                                      doctest::Approx(layout.ControlWidth));
                            }
                        };

                        for (const auto fieldLabel :
                             {std::string_view("Temperature (K)"), std::string_view("Shadow Bias"),
                              std::string_view("Indirect Multiplier")})
                        {
                            auto id = ui.PushId(fieldLabel);
                            auto [layout, controlLabel] = prepareLightField(fieldLabel);
                            CHECK(layout.Stacked);
                            CHECK(KeireEditor::Detail::InspectorVisibleLabel(controlLabel) ==
                                  (layout.Stacked ? std::string_view{} : fieldLabel));
                            float value = 0.5F;
                            CHECK_FALSE(ui.SliderFloat(controlLabel, value, 0.0F, 1.0F));
                            checkLightControlBounds(ui.LastItemRect(), layout);
                        }

                        auto id = ui.PushId("ShadowResolutionLayout");
                        auto [layout, controlLabel] = prepareLightField("Shadow Resolution");
                        CHECK(layout.Stacked);
                        CHECK(KeireEditor::Detail::InspectorVisibleLabel(controlLabel) ==
                              (layout.Stacked ? std::string_view{} : std::string_view("Shadow Resolution")));
                        {
                            const auto resolution = ui.BeginCombo(controlLabel, "High");
                            (void)resolution;
                        }
                        checkLightControlBounds(ui.LastItemRect(), layout);

                        for (const auto fieldLabel :
                             {std::string_view("Assembly Name"), std::string_view("Root Namespace")})
                        {
                            auto assemblyId = ui.PushId(fieldLabel);
                            auto [assemblyLayout, assemblyControlLabel] = prepareLightField(fieldLabel);
                            std::string value = "Gameplay.Runtime";
                            CHECK_FALSE(ui.InputText(assemblyControlLabel, value));
                            checkLightControlBounds(ui.LastItemRect(), assemblyLayout);
                        }

                        auto classificationId = ui.PushId("ClassificationLayout");
                        auto [classificationLayout, classificationLabel] = prepareLightField("Classification");
                        {
                            const auto classification = ui.BeginCombo(classificationLabel, "Runtime");
                            (void)classification;
                        }
                        checkLightControlBounds(ui.LastItemRect(), classificationLayout);
                    }

                    {
                        auto id = ui.PushId("UiBuilderInspector");
                        const auto uiBuilderLayout =
                            KeireEditor::Detail::ResolveInspectorFieldLayout(ui.ContentAvailable().Width);
                        std::string slotAssignment = "PrimaryOverlaySlot";
                        if (uiBuilderLayout.Stacked)
                        {
                            ui.TextWrapped("Slot Assignment");
                            ui.SetNextItemWidth(uiBuilderLayout.ControlWidth);
                        }
                        (void)ui.InputText(
                            KeireEditor::Detail::InspectorControlLabel("Slot Assignment", uiBuilderLayout.Stacked),
                            slotAssignment);
                        const auto slotBounds = ui.LastItemRect();
                        CHECK(slotBounds.Minimum.X >= bounds.Minimum.X);
                        CHECK(slotBounds.Maximum.X <= bounds.Maximum.X);

                        const std::string bindingSummary =
                            "Player.Health.Current <- Runtime.Player.Health.Current [OneWay]";
                        const bool stackedAction = KeireEditor::Detail::ShouldStackInspectorAction(
                            ui.ContentAvailable().Width, ui.MeasureText(bindingSummary).Width,
                            ui.MeasureText("Remove").Width + 20.0F);
                        ui.TextWrapped(bindingSummary);
                        if (!stackedAction)
                            ui.SameLine();
                        const auto removeSize = stackedAction
                                                    ? Keire::UiSize{std::max(ui.ContentAvailable().Width, 1.0F), 0.0F}
                                                    : Keire::UiSize{};
                        (void)ui.Button("Remove##UiBuilderBinding", removeSize);
                        const auto removeBounds = ui.LastItemRect();
                        CHECK(removeBounds.Minimum.X >= bounds.Minimum.X);
                        CHECK(removeBounds.Maximum.X <= bounds.Maximum.X);
                        if (stackedAction)
                        {
                            CHECK(removeBounds.Maximum.X - removeBounds.Minimum.X == doctest::Approx(removeSize.Width));
                        }
                    }

                    if (width == 230.0F)
                    {
                        KeireEditor::AssetPicker assetPicker;
                        KeireEditor::InspectorPropertyEditor propertyEditor(ui, {}, {}, {}, assetPicker);
                        Keire::Vector3 directVector{1.0F, 2.0F, 3.0F};
                        Keire::Vector3 propertyVector = directVector;
                        float directVectorHeight = 0.0F;
                        {
                            auto id = ui.PushId("DirectVector");
                            const auto before = ui.CursorPosition();
                            CHECK_FALSE(ui.DragVector3("Center", directVector));
                            directVectorHeight = ui.CursorPosition().Y - before.Y;
                        }
                        {
                            auto id = ui.PushId("PropertyVector");
                            const auto before = ui.CursorPosition();
                            CHECK_FALSE(propertyEditor.EditVector3("Center", propertyVector, 0.1));
                            CHECK(ui.CursorPosition().Y - before.Y == doctest::Approx(directVectorHeight));
                        }

                        bool directBoolean = false;
                        bool propertyBoolean = false;
                        {
                            auto id = ui.PushId("DirectBoolean");
                            CHECK_FALSE(ui.Checkbox("Is Trigger", directBoolean));
                        }
                        const auto directBooleanBounds = ui.LastItemRect();
                        {
                            auto id = ui.PushId("PropertyBoolean");
                            CHECK_FALSE(propertyEditor.EditBoolean("Is Trigger", propertyBoolean));
                        }
                        const auto propertyBooleanBounds = ui.LastItemRect();
                        CHECK(propertyBooleanBounds.Maximum.X - propertyBooleanBounds.Minimum.X ==
                              doctest::Approx(directBooleanBounds.Maximum.X - directBooleanBounds.Minimum.X));
                        CHECK(propertyBooleanBounds.Maximum.Y - propertyBooleanBounds.Minimum.Y ==
                              doctest::Approx(directBooleanBounds.Maximum.Y - directBooleanBounds.Minimum.Y));
                    }

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

TEST_CASE("Rigging Studio reports failed runtime assets and clears the error after regeneration")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog.clear();
    specification.Decoders = {Keire::CreateSkeletonAssetDecoder()};
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const auto id = Keire::AssetId::Generate();
    const auto handle = assets->Load<Keire::SkeletonAsset>(id);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (handle.State() != Keire::AssetState::Failed && std::chrono::steady_clock::now() < deadline)
    {
        (void)assets->PumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_THROWS_AS(handle.Require(), Keire::AssetLoadError);
    REQUIRE(handle.State() == Keire::AssetState::Failed);
    REQUIRE_FALSE(handle.Diagnostic().Message.empty());
    const auto error = KeireEditor::RetargetAssetLoadError(handle, "Target skeleton");
    CHECK(error.find("Target skeleton failed to load.") != std::string::npos);
    CHECK(error.find(handle.Diagnostic().Message) != std::string::npos);
    CHECK(error.find("Regenerate") != std::string::npos);
    REQUIRE(assets->PublishDevelopmentAsset(
        id, Keire::CreateRef<Keire::SkeletonAsset>(std::vector<Keire::SkeletonBone>{{"Root", -1, {}, {}}})));
    CHECK(handle.TryGetLoaded());
    CHECK(KeireEditor::RetargetAssetLoadError(handle, "Target skeleton").empty());
    const Keire::AssetHandle<Keire::SkeletonAsset> cancelled;
    CHECK(KeireEditor::RetargetAssetLoadError(cancelled, "Source skeleton").find("load was cancelled") !=
          std::string::npos);
    assets->Close();
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

TEST_CASE("Rigging Studio suggests valid names for long and unusual imported labels")
{
    for (const auto& model :
         {std::string(" Leading model"), std::string("CON.asset"), std::string(250, 'm'), std::string{}})
    {
        const auto name = KeireEditor::SuggestedRetargetName(model, std::string(250, 'c'));
        CAPTURE(model);
        CAPTURE(name);
        CHECK(KeireEditor::RetargetOutputNameError(name).empty());
        CHECK(name.ends_with(" Retargeted"));
    }
    const auto unicode = KeireEditor::SuggestedRetargetName(std::string(218, 'm') + "\xC3\xA9", "Walk");
    CHECK(KeireEditor::RetargetOutputNameError(unicode).empty());
    CHECK(unicode == std::string(218, 'm') + " Retargeted");
    CHECK(KeireEditor::RetargetOutputNameError(KeireEditor::SuggestedRetargetName("", "")).empty());
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

TEST_CASE("Rigging Studio reveal resolves runtime outputs to their source without losing direct records")
{
    Keire::AssetSourceRecord model;
    model.Id = Keire::AssetId::Generate();
    const auto generated = Keire::AssetId::Generate();
    Keire::AssetSourceRecord editable;
    editable.Id = Keire::AssetId::Generate();
    model.SubAssets = {generated, editable.Id};
    const std::array records{model, editable};
    CHECK(KeireEditor::ResolveRiggingStudioRevealAsset(records, model.Id) == model.Id);
    CHECK(KeireEditor::ResolveRiggingStudioRevealAsset(records, generated) == model.Id);
    CHECK(KeireEditor::ResolveRiggingStudioRevealAsset(records, editable.Id) == editable.Id);
    CHECK_FALSE(KeireEditor::ResolveRiggingStudioRevealAsset(records, {}));
    CHECK_FALSE(KeireEditor::ResolveRiggingStudioRevealAsset(records, Keire::AssetId::Generate()));
    CHECK_FALSE(KeireEditor::ResolveRiggingStudioRevealAsset({}, generated));
}

TEST_CASE("Animator preview reports unmapped and partial retargets and recovers after skeleton reload")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog.clear();
    specification.Decoders = {Keire::CreateSkeletonAssetDecoder(), Keire::CreateAnimationClipAssetDecoder()};
    const auto sourceId = Keire::AssetId::Generate();
    const auto targetId = Keire::AssetId::Generate();
    const auto clipId = Keire::AssetId::Generate();
    struct TemporaryCatalog
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("PreviewRetarget-" + Keire::AssetId::Generate().ToString()));
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
                                                         .UncompressedBytes = 1},
                             Keire::Detail::CatalogEntry{.Id = sourceId,
                                                         .Type = Keire::SkeletonAsset::StaticType(),
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

    const auto makeSkeleton = [](std::string first, std::string second)
    {
        return Keire::CreateRef<Keire::SkeletonAsset>(
            std::vector<Keire::SkeletonBone>{{"SharedRoot", -1, {}, {}},
                                             {std::move(first), 0, {{0, 1, 0}, {}, {1, 1, 1}}, {}},
                                             {std::move(second), 0, {{1, 0, 0}, {}, {1, 1, 1}}, {}}});
    };
    REQUIRE(assets->PublishDevelopmentAsset(sourceId, makeSkeleton("CustomA", "CustomB")));
    REQUIRE(assets->PublishDevelopmentAsset(targetId, makeSkeleton("OtherX", "OtherY")));
    const std::vector<Keire::AnimationTrack> tracks{{1, {{0, {}}, {1, {}}}}, {2, {{0, {}}, {1, {}}}}};
    REQUIRE(assets->PublishDevelopmentAsset(
        clipId, Keire::AnimationClipAsset::Decode(Keire::AnimationClipAsset::Encode(sourceId, 1, tracks, {}, false))));
    // Generated clips have runtime catalog identities but no editable source record of their own.
    CHECK(KeireEditor::RiggingStudioClipPreviewName(clipId, assets).find(clipId.ToString()) != std::string::npos);
    CHECK_THROWS_AS(KeireEditor::RiggingStudioClipPreviewName(sourceId, assets), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::RiggingStudioClipPreviewName(Keire::AssetId::Generate(), assets),
                    std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::RiggingStudioClipPreviewName({}, assets), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::RiggingStudioClipPreviewName(clipId, {}), std::invalid_argument);
    KeireEditor::AnimatorControllerPreviewState preview;
    preview.Skeleton = targetId;
    preview.SkeletonHandle = assets->Load<Keire::SkeletonAsset>(targetId);
    INFO(preview.Diagnostic);
    CHECK_FALSE(preview.ResolveClip(clipId, assets));
    CHECK(preview.Diagnostic.find("incompatible") != std::string::npos);
    CHECK(preview.Diagnostic.find("Rigging Studio") != std::string::npos);
    REQUIRE(assets->PublishDevelopmentAsset(targetId, makeSkeleton("CustomA", "OtherY")));
    REQUIRE(preview.ResolveClip(clipId, assets));
    CHECK(preview.Diagnostic.find("1 of 2") != std::string::npos);
    preview.Diagnostic.clear();
    REQUIRE(preview.ResolveClip(clipId, assets));
    CHECK(preview.Diagnostic.find("1 of 2") != std::string::npos);
    const auto missingClip = Keire::AssetId::Generate();
    const auto makeGraph = [](const Keire::AssetId first, const Keire::AssetId second)
    {
        Keire::AnimationGraphDefinition definition;
        Keire::AnimationLayerDefinition layer;
        layer.Id = "base";
        layer.Name = "Base";
        layer.EntryStateId = "first";
        Keire::AnimationStateDefinition state;
        state.Id = "first";
        state.Name = "First";
        state.Clip = first;
        layer.States.push_back(state);
        state.Id = "second";
        state.Name = "Second";
        state.Clip = second;
        layer.States.push_back(state);
        definition.Layers.push_back(layer);
        return Keire::CreateRef<Keire::AnimationGraphAsset>(definition);
    };
    for (const bool missingFirst : {true, false})
    {
        const auto graph = makeGraph(missingFirst ? missingClip : clipId, missingFirst ? clipId : missingClip);
        CHECK_FALSE(preview.DependenciesReady(*graph, assets));
        CHECK(preview.Diagnostic.find("missing or incompatible") != std::string::npos);
    }
    const auto repairedGraph = makeGraph(clipId, clipId);
    auto maskedDefinition = repairedGraph->Definition();
    maskedDefinition.Layers.front().AvatarMask = Keire::AssetId::Generate();
    const auto maskedGraph = Keire::CreateRef<Keire::AnimationGraphAsset>(maskedDefinition);
    CHECK_FALSE(preview.DependenciesReady(*maskedGraph, assets));
    CHECK(preview.Diagnostic.find("avatar mask") != std::string::npos);
    CHECK(preview.DependenciesReady(*repairedGraph, assets));
    CHECK(preview.Diagnostic.find("1 of 2") != std::string::npos);
    REQUIRE(assets->PublishDevelopmentAsset(targetId, makeSkeleton("CustomA", "CustomB")));
    preview.Diagnostic.clear();
    REQUIRE(preview.ResolveClip(clipId, assets));
    CHECK(preview.Diagnostic.empty());
    CHECK(preview.DependenciesReady(*repairedGraph, assets));
    CHECK(preview.Diagnostic.empty());
    preview.Stop();
    assets->Close();
}
