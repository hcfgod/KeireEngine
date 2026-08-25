#include "Keire/Rendering/FrameGraphSnapshot.h"
#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace
{
    class RecordingExecutionContext final : public Keire::RenderBackend::FrameGraphExecutionContext
    {
      public:
        void Transition(const Keire::RenderBackend::CompiledFrameGraph::Transition& transition) override
        {
            Transitions.push_back(transition);
        }

        void Execute(const Keire::RenderBackend::FrameGraphPass pass,
                     const Keire::RenderBackend::FrameGraphPassDescription&) override
        {
            Passes.push_back(pass);
        }

        std::vector<Keire::RenderBackend::CompiledFrameGraph::Transition> Transitions;
        std::vector<Keire::RenderBackend::FrameGraphPass> Passes;
    };
} // namespace

TEST_CASE("frame graph compiles deterministic hazards and resource lifetimes")
{
    Keire::RenderBackend::FrameGraph graph;
    const auto uploaded = graph.AddResource({"Uploaded meshes", Keire::RenderBackend::FrameGraphResourceKind::Buffer});
    const auto shadow = graph.AddResource({"Sun shadow", Keire::RenderBackend::FrameGraphResourceKind::Texture});
    const auto scene = graph.AddResource({"HDR scene", Keire::RenderBackend::FrameGraphResourceKind::Texture});
    const auto output =
        graph.AddResource({"Presentation", Keire::RenderBackend::FrameGraphResourceKind::Texture, true});
    (void)graph.AddPass({"Uploads", {}, {uploaded}});
    (void)graph.AddPass({"Shadow", {uploaded}, {shadow}});
    (void)graph.AddPass({"Opaque", {uploaded, shadow}, {scene}});
    (void)graph.AddPass({"Tone map", {scene}, {output}});

    const auto compiled = graph.Compile();
    REQUIRE(compiled.Order.size() == 4);
    CHECK(compiled.Order[0].Value == 0);
    CHECK(compiled.Order[3].Value == 3);
    CHECK(compiled.Lifetimes[uploaded.Value].FirstPass == 0);
    CHECK(compiled.Lifetimes[uploaded.Value].LastPass == 2);
    CHECK(compiled.Lifetimes[scene.Value].FirstPass == 2);
    CHECK(compiled.Lifetimes[scene.Value].LastPass == 3);
    REQUIRE(compiled.Execution.size() == 4);
    CHECK(compiled.Execution.front().Transitions.front().After ==
          Keire::RenderBackend::FrameGraphResourceState::StorageWrite);

    RecordingExecutionContext execution;
    graph.Execute(compiled, execution);
    CHECK(execution.Passes == compiled.Order);
    CHECK_FALSE(execution.Transitions.empty());
}

TEST_CASE("frame graph aliases compatible transient resources with disjoint lifetimes")
{
    using namespace Keire::RenderBackend;
    FrameGraph graph;
    const auto first = graph.AddResource({"First HDR", FrameGraphResourceKind::Texture, false, 16U, 1024U});
    const auto dependency = graph.AddResource({"Dependency", FrameGraphResourceKind::Buffer});
    const auto second = graph.AddResource({"Second HDR", FrameGraphResourceKind::Texture, false, 16U, 2048U});
    (void)graph.AddPass({"First", {}, {first}});
    (void)graph.AddPass({"Consume first", {first}, {dependency}, FrameGraphPassKind::Compute});
    (void)graph.AddPass({"Second", {dependency}, {second}});

    const auto compiled = graph.Compile();
    REQUIRE(compiled.PhysicalResources[first.Value] != std::numeric_limits<std::uint32_t>::max());
    CHECK(compiled.PhysicalResources[first.Value] == compiled.PhysicalResources[second.Value]);
    CHECK(compiled.TransientAllocations[compiled.PhysicalResources[first.Value]].SizeBytes == 2048U);
}

TEST_CASE("frame graph rejects reads before transient production and ambiguous feedback")
{
    Keire::RenderBackend::FrameGraph graph;
    const auto transient = graph.AddResource({"Transient", Keire::RenderBackend::FrameGraphResourceKind::Texture});
    (void)graph.AddPass({"Read too early", {transient}, {}});
    CHECK_THROWS_AS((void)graph.Compile(), std::logic_error);

    Keire::RenderBackend::FrameGraph feedback;
    const auto value = feedback.AddResource({"Feedback", Keire::RenderBackend::FrameGraphResourceKind::Texture});
    CHECK_THROWS_AS((void)feedback.AddPass({"Feedback", {value}, {value}}), std::invalid_argument);
}

TEST_CASE("static scene frame graph declares the complete production pass sequence")
{
    const auto scene = Keire::RenderBackend::BuildStaticSceneFrameGraph();
    REQUIRE(scene.Compiled.Order.size() == 14);
    REQUIRE(scene.Compiled.Diagnostics.size() == 14);
    CHECK(scene.Compiled.Diagnostics.front() == "0: Resource uploads");
    CHECK(scene.Compiled.Diagnostics[1] == "1: Directional shadow maps");
    CHECK(scene.Compiled.Diagnostics[2] == "2: Forward+ light culling");
    CHECK(scene.Compiled.Diagnostics[3] == "3: Occlusion depth");
    CHECK(scene.Compiled.Diagnostics[4] == "4: Occlusion depth pyramid");
    CHECK(scene.Compiled.Diagnostics[5] == "5: GPU occlusion culling");
    CHECK(scene.Compiled.Diagnostics[6] == "6: Opaque and mask");
    CHECK(scene.Compiled.Diagnostics[7] == "7: Sampled scene depth");
    CHECK(scene.Compiled.Diagnostics[9] == "9: Transparency");
    CHECK(scene.Compiled.Diagnostics[10] == "10: ACES tone map");
    CHECK(scene.Compiled.Diagnostics.back() == "13: Presentation");
    REQUIRE(scene.HdrScene);
    REQUIRE(scene.SampledDepth);
    REQUIRE(scene.GpuOcclusionDepth);
    REQUIRE(scene.GpuOcclusionPyramid);
    REQUIRE(scene.GpuOcclusionIndirectArguments);
    REQUIRE(scene.ResolveDepth);
    REQUIRE(scene.Transparency);
    const auto& occlusionPyramidPass = scene.Graph.Passes()[scene.GpuOcclusionPyramidPass.Value];
    CHECK(std::ranges::find(occlusionPyramidPass.Reads, scene.GpuOcclusionDepth) != occlusionPyramidPass.Reads.end());
    CHECK(std::ranges::find(occlusionPyramidPass.Writes, scene.GpuOcclusionPyramid) !=
          occlusionPyramidPass.Writes.end());
    const auto& occlusionCullingPass = scene.Graph.Passes()[scene.GpuOcclusionCullingPass.Value];
    CHECK(std::ranges::find(occlusionCullingPass.Reads, scene.GpuOcclusionPyramid) != occlusionCullingPass.Reads.end());
    CHECK(std::ranges::find(occlusionCullingPass.Writes, scene.GpuOcclusionIndirectArguments) !=
          occlusionCullingPass.Writes.end());
    const auto& opaquePass = scene.Graph.Passes()[scene.Opaque.Value];
    CHECK(std::ranges::find(opaquePass.Reads, scene.GpuOcclusionIndirectArguments) != opaquePass.Reads.end());
    const auto& depthPass = scene.Graph.Passes()[scene.ResolveDepth.Value];
    CHECK(std::ranges::find(depthPass.Writes, scene.SampledDepth) != depthPass.Writes.end());
    const auto& transparencyPass = scene.Graph.Passes()[scene.Transparency.Value];
    CHECK(std::ranges::find(transparencyPass.Reads, scene.SampledDepth) != transparencyPass.Reads.end());
    const auto& overlayPass = scene.Graph.Passes()[scene.Overlays.Value];
    CHECK(std::ranges::find(overlayPass.Reads, scene.GpuOcclusionPyramid) != overlayPass.Reads.end());
    CHECK(std::ranges::find(overlayPass.Reads, scene.GpuOcclusionIndirectArguments) != overlayPass.Reads.end());
    REQUIRE(scene.Compiled.TransientAllocations.size() == 1);
    const auto hdrAllocation = scene.Compiled.PhysicalResources[scene.HdrScene.Value];
    REQUIRE(hdrAllocation < scene.Compiled.TransientAllocations.size());
    CHECK(scene.Compiled.TransientAllocations[hdrAllocation].Kind ==
          Keire::RenderBackend::FrameGraphResourceKind::Texture);
    CHECK(scene.Compiled.TransientAllocations[hdrAllocation].CompatibilityKey == 4);
}

TEST_CASE("frame graph snapshot exports are deterministic and explicit")
{
    Keire::FrameGraphSnapshot snapshot;
    snapshot.Frame = 42;
    snapshot.ActiveTransientBytes = 2048;
    snapshot.TheoreticalUnaliasedBytes = 3072;
    snapshot.SavedAliasingBytes = 1024;
    snapshot.Passes.push_back({0, 0, "Opaque", Keire::FrameGraphSnapshotPassKind::Graphics, {}, {0}, {}});
    snapshot.Resources.push_back(
        {0, "HDR scene", Keire::FrameGraphSnapshotResourceKind::Texture, 0, 0, 0, 16, 2048, false, true});

    const auto directory = std::filesystem::path("Build") / "FrameGraphSnapshotTests";
    const auto json = directory / "graph.json";
    const auto dot = directory / "graph.dot";
    std::filesystem::remove_all(directory);
    CHECK_FALSE(std::filesystem::exists(json));
    Keire::ExportFrameGraphJson(snapshot, json);
    Keire::ExportFrameGraphDot(snapshot, dot);
    std::ifstream jsonStream(json, std::ios::binary);
    std::ifstream dotStream(dot, std::ios::binary);
    const std::string jsonText(std::istreambuf_iterator<char>(jsonStream), {});
    const std::string dotText(std::istreambuf_iterator<char>(dotStream), {});
    CHECK(jsonText.find("\"savedAliasingBytes\": 1024") != std::string::npos);
    CHECK(dotText.find("0: Opaque") != std::string::npos);
    const auto& firstJson = jsonText;
    Keire::ExportFrameGraphJson(snapshot, json);
    std::ifstream secondJsonStream(json, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(secondJsonStream), {}) == firstJson);
    jsonStream.close();
    dotStream.close();
    secondJsonStream.close();
    std::filesystem::remove_all(directory);
}
