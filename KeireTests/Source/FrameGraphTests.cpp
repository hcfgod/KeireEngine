#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <doctest/doctest.h>

#include <stdexcept>

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
    REQUIRE(scene.Compiled.Order.size() == 10);
    REQUIRE(scene.Compiled.Diagnostics.size() == 10);
    CHECK(scene.Compiled.Diagnostics.front() == "0: Resource uploads");
    CHECK(scene.Compiled.Diagnostics[1] == "1: Directional shadow maps");
    CHECK(scene.Compiled.Diagnostics[2] == "2: Forward+ light culling");
    CHECK(scene.Compiled.Diagnostics[3] == "3: Opaque and mask");
    CHECK(scene.Compiled.Diagnostics[6] == "6: ACES tone map");
    CHECK(scene.Compiled.Diagnostics.back() == "9: Presentation");
}
