#include "Keire/Rendering/ShaderGraphRuntime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] Keire::AssetId Pin(const Keire::ShaderGraphNode& node, const std::string_view name,
                                     const Keire::ShaderGraphPinDirection direction)
    {
        const auto found = std::ranges::find_if(node.Pins, [&](const Keire::ShaderGraphPin& pin)
                                                { return pin.Name == name && pin.Direction == direction; });
        if (found == node.Pins.end())
            throw std::logic_error("Test graph pin is unavailable.");
        return found->Id;
    }

    void Connect(Keire::ShaderGraphDefinition& graph, const Keire::ShaderGraphNode& output,
                 const std::string_view outputPin, const Keire::ShaderGraphNode& input, const std::string_view inputPin)
    {
        graph.Connections.push_back({Keire::AssetId::Generate(),
                                     {output.Id, Pin(output, outputPin, Keire::ShaderGraphPinDirection::Output)},
                                     {input.Id, Pin(input, inputPin, Keire::ShaderGraphPinDirection::Input)}});
    }
} // namespace

TEST_CASE("Shader Graph runtime variants prune authored keywords and quality tiers deterministically")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    graph.Keywords = {{"USE_DETAIL", {}, "false", true}, {"LIGHTING", {"Simple", "Full"}, "Full", true}};
    Keire::ShaderGraphVariantPruningOptions options;
    options.AllowedKeywordOptions = {{"USE_DETAIL", {"true"}}, {"LIGHTING", {"Full"}}};
    options.QualityTiers = {Keire::ShaderGraphQualityTier::Low, Keire::ShaderGraphQualityTier::High};

    const auto first = Keire::PruneShaderGraphVariants(graph, options);
    const auto second = Keire::PruneShaderGraphVariants(graph, options);
    REQUIRE(first == second);
    REQUIRE(first.size() == 2);
    CHECK(first[0].Keywords == std::vector<std::string>{"USE_DETAIL", "LIGHTING_Full"});
    CHECK(first[0].StableSuffix != first[1].StableSuffix);

    options.AllowedKeywordOptions["LIGHTING"] = {"Missing"};
    CHECK_THROWS_AS((void)Keire::PruneShaderGraphVariants(graph, options), std::invalid_argument);
    options.AllowedKeywordOptions["LIGHTING"] = {"Full"};
    options.MaximumVariants = 1;
    CHECK_THROWS_AS((void)Keire::PruneShaderGraphVariants(graph, options), std::invalid_argument);
}

TEST_CASE("Shader Graph runtime analysis and per-node previews enforce bounded contracts")
{
    auto graph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto value = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Color);
    const auto output =
        std::ranges::find(value.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction);
    REQUIRE(output != value.Pins.end());
    const auto requestNode = value.Id;
    const auto requestPin = output->Id;
    graph.Nodes.push_back(std::move(value));

    const auto analysis = Keire::AnalyzeShaderGraph(graph);
    CHECK(analysis.Statistics.NodeCount == graph.Nodes.size());
    CHECK(analysis.Statistics.UnusedNodeCount == 1);
    CHECK(analysis.WithinLimits);

    Keire::ShaderGraphNodePreviewRequest request{requestNode, requestPin};
    CHECK_NOTHROW(Keire::ValidateShaderGraphNodePreview(graph, request));
    request.Width = 1025;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphNodePreview(graph, request), std::invalid_argument);
    request.Width = 256;
    request.OutputPin = graph.Nodes.front().Pins.front().Id;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphNodePreview(graph, request), std::invalid_argument);

    Keire::ShaderGraphAnalysisLimits invalid;
    invalid.MaximumDependencyDepth = 0;
    CHECK_THROWS_AS((void)Keire::AnalyzeShaderGraph(graph, invalid), std::invalid_argument);
}

TEST_CASE("Shader Graph runtime analysis measures the longest shared dependency path regardless of edge order")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    const auto value = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant);
    const auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
    const auto second = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
    graph.Nodes.insert(graph.Nodes.end(), {value, first, second});
    Connect(graph, value, "Value", first, "A");
    Connect(graph, first, "Result", second, "A");
    Connect(graph, second, "Result", graph.Nodes.front(), "Roughness");
    Connect(graph, first, "Result", graph.Nodes.front(), "Metallic");

    Keire::ShaderGraphAnalysisLimits limits;
    limits.MaximumDependencyDepth = 3;
    const auto analysis = Keire::AnalyzeShaderGraph(graph, limits);
    CHECK(analysis.MaximumDependencyDepth == 4);
    CHECK(analysis.Statistics.ReachableNodeCount == 4);
    CHECK_FALSE(analysis.WithinLimits);
    std::ranges::reverse(graph.Connections);
    CHECK(Keire::AnalyzeShaderGraph(graph, limits) == analysis);
}

TEST_CASE("Shader Graph node preview budgets follow the selected node's upstream work")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    const auto value = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant);
    const auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
    const auto second = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
    graph.Nodes.insert(graph.Nodes.end(), {value, first, second});
    Connect(graph, value, "Value", first, "A");
    Connect(graph, first, "Result", second, "A");

    Keire::ShaderGraphNodePreviewRequest request{second.Id,
                                                 Pin(second, "Result", Keire::ShaderGraphPinDirection::Output)};
    request.MaximumReachableNodes = 2;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphNodePreview(graph, request), std::invalid_argument);
    request.MaximumReachableNodes = 3;
    CHECK_NOTHROW(Keire::ValidateShaderGraphNodePreview(graph, request));

    Connect(graph, second, "Result", graph.Nodes.front(), "Roughness");
    request.Node = value.Id;
    request.OutputPin = Pin(value, "Value", Keire::ShaderGraphPinDirection::Output);
    request.MaximumReachableNodes = 1;
    CHECK_NOTHROW(Keire::ValidateShaderGraphNodePreview(graph, request));
}

TEST_CASE("Shader Graph runtime analysis counts all triplanar texture samples")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto texture =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Texture2D);
    texture.Symbol = "Albedo";
    const auto triplanar = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TriplanarSample);
    graph.Nodes.insert(graph.Nodes.end(), {texture, triplanar});
    Connect(graph, texture, "Value", triplanar, "Texture");
    Connect(graph, triplanar, "RGBA", graph.Nodes.front(), "BaseColor");
    Keire::ShaderGraphAnalysisLimits limits;
    limits.MaximumTextureSamples = 2;
    const auto analysis = Keire::AnalyzeShaderGraph(graph, limits);
    CHECK(analysis.Statistics.TextureSampleCount == 3);
    CHECK_FALSE(analysis.WithinLimits);
    CHECK(analysis.Statistics.TextureSampleCount == Keire::CompileShaderGraph(graph).Statistics.TextureSampleCount);
}
