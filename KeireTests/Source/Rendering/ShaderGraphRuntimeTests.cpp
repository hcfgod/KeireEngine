#include "Keire/Rendering/ShaderGraphRuntime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <ranges>
#include <string>

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
