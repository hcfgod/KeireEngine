#include "KeireClient/Editor/ShaderGraphSaveState.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
    struct Controller
    {
        std::vector<std::byte> Persisted;
        std::string Error;
        KeireEditor::ShaderGraphDocument Document{{.Persist = [this](Keire::AssetId, std::span<const std::byte> bytes)
                                                   { Persisted.assign(bytes.begin(), bytes.end()); }}};
    };

    Keire::ShaderGraphNode OpenGraph(Controller& controller)
    {
        auto graph = Keire::CreateDefaultShaderGraph();
        auto property =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
        property.Name = "Roughness";
        property.Symbol = "Roughness";
        property.Value = 0.4F;
        graph.Nodes.push_back(property);
        controller.Document.Open(Keire::AssetId::Generate(), graph, 1);
        return property;
    }

    void FinishSave(Controller& controller, KeireEditor::ShaderGraphSaveState& panel)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (panel.RequestedAsset.has_value() && std::chrono::steady_clock::now() < deadline)
        {
            controller.Document.AdvanceCompilation(0.1);
            try
            {
                panel.Update(controller.Document, [&] { controller.Document.Save(); });
            }
            catch (const std::exception& error)
            {
                controller.Error = error.what();
            }
            std::this_thread::yield();
        }
        REQUIRE_FALSE(panel.RequestedAsset.has_value());
    }
} // namespace

TEST_CASE("Shader Graph Save persists HDR color metadata and descriptions")
{
    Controller controller;
    auto graph = Keire::CreateDefaultShaderGraph();
    auto property =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    property.Symbol = "Radiance";
    graph.Nodes.push_back(property);
    controller.Document.Open(Keire::AssetId::Generate(), graph, 1);
    KeireEditor::ShaderGraphSaveState panel;
    panel.Stage(controller.Document, property);
    panel.Draft.Description = "Linear radiance";
    panel.Draft.HighDynamicRange = true;
    panel.Request(controller.Document);
    FinishSave(controller, panel);
    REQUIRE(controller.Error.empty());
    const auto saved = Keire::ShaderGraphAsset::DecodeSource(controller.Persisted);
    CHECK(saved.Nodes.back().ParameterMetadata.HighDynamicRange);
    CHECK(saved.Nodes.back().ParameterMetadata.Description == "Linear radiance");
}

TEST_CASE("Shader Graph Save applies staged property metadata and preserves identity")
{
    Controller controller;
    auto property = OpenGraph(controller);
    KeireEditor::ShaderGraphSaveState panel;
    property.Symbol = "RenamedRoughness";
    property.Name = "Surface Roughness";
    panel.Stage(controller.Document, property);
    CHECK(panel.Draft.Dirty);
    CHECK_FALSE(controller.Document.Dirty());
    panel.Request(controller.Document);
    FinishSave(controller, panel);
    REQUIRE(controller.Error.empty());
    REQUIRE_FALSE(controller.Persisted.empty());
    const auto saved = Keire::ShaderGraphAsset::DecodeSource(controller.Persisted);
    const auto found = std::ranges::find(saved.Nodes, property.Id, &Keire::ShaderGraphNode::Id);
    REQUIRE(found != saved.Nodes.end());
    CHECK(found->Symbol == "RenamedRoughness");
    CHECK(found->Name == "Surface Roughness");
    CHECK(found->Value == property.Value);
    CHECK_FALSE(panel.Draft.Dirty);
    CHECK_FALSE(controller.Document.Dirty());
}

TEST_CASE("Shader Graph Save retains invalid property drafts without publishing them")
{
    Controller controller;
    auto property = OpenGraph(controller);
    KeireEditor::ShaderGraphSaveState panel;
    property.Symbol = "invalid symbol";
    panel.Stage(controller.Document, property);
    panel.Request(controller.Document);
    FinishSave(controller, panel);
    CHECK_FALSE(controller.Error.empty());
    CHECK(controller.Persisted.empty());
    CHECK(panel.Draft.Dirty);
    CHECK_FALSE(controller.Document.Dirty());
    CHECK(std::ranges::find(controller.Document.Definition().Nodes, property.Id, &Keire::ShaderGraphNode::Id)->Symbol ==
          "Roughness");
}

TEST_CASE("Shader Graph queued Save cannot publish a different document")
{
    Controller controller;
    auto property = OpenGraph(controller);
    KeireEditor::ShaderGraphSaveState panel;
    property.Symbol = "PendingRename";
    panel.Stage(controller.Document, property);
    panel.Request(controller.Document);
    controller.Document.Open(Keire::AssetId::Generate(), Keire::CreateDefaultShaderGraph(), 1);
    panel.Update(controller.Document, [&] { controller.Document.Save(); });
    CHECK_FALSE(panel.RequestedAsset.has_value());
    CHECK(controller.Persisted.empty());
    CHECK_FALSE(controller.Document.Dirty());
    panel.Reset();
    CHECK_FALSE(panel.Draft.Dirty);
}
