#include "KeireClient/Editor/GraphClipboard.h"
#include "KeireClient/Editor/GraphDuplication.h"
#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
    struct TextureTransformFixture
    {
        Keire::ShaderGraphDefinition Graph = Keire::CreateDefaultShaderGraph();
        Keire::ShaderGraphNode Texture =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Texture2D);
        Keire::ShaderGraphNode Transform =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Vector4);

        TextureTransformFixture()
        {
            Texture.Symbol = "Texture";
            Transform.Symbol = "TextureST";
            Transform.Value = Keire::Vector4{1.0F, 1.0F, 0.0F, 0.0F};
            Texture.ParameterMetadata.TextureTransformProperty = Transform.Id;
            Graph.Nodes.push_back(Texture);
            Graph.Nodes.push_back(Transform);
        }
    };
} // namespace

TEST_CASE("Shader Graph preview applies texture transforms to ordinary explicit LOD and triplanar sampling")
{
    for (const auto kind : {Keire::ShaderGraphNodeKind::TextureSample, Keire::ShaderGraphNodeKind::TextureSampleLevel,
                            Keire::ShaderGraphNodeKind::TriplanarSample})
    {
        CAPTURE(static_cast<int>(kind));
        TextureTransformFixture fixture;
        auto sample = Keire::CreateShaderGraphNode(kind);
        const auto texturePin = std::ranges::find(sample.Pins, "Texture", &Keire::ShaderGraphPin::Name);
        const auto output =
            std::ranges::find(sample.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction);
        const auto color =
            std::ranges::find(fixture.Graph.Nodes.front().Pins, "BaseColor", &Keire::ShaderGraphPin::Name);
        REQUIRE(texturePin != sample.Pins.end());
        REQUIRE(output != sample.Pins.end());
        REQUIRE(color != fixture.Graph.Nodes.front().Pins.end());
        fixture.Graph.Connections.push_back({Keire::AssetId::Generate(),
                                             {fixture.Texture.Id, fixture.Texture.Pins.front().Id},
                                             {sample.Id, texturePin->Id}});
        fixture.Graph.Connections.push_back(
            {Keire::AssetId::Generate(), {sample.Id, output->Id}, {fixture.Graph.Nodes.front().Id, color->Id}});
        fixture.Graph.Nodes.push_back(sample);
        KeireEditor::ShaderGraphPreviewRequest request{.Definition = &fixture.Graph};
        const auto evaluate = [&]()
        {
            KeireEditor::ShaderGraphPreviewInternal::ShaderGraphPreviewEvaluator evaluator(request);
            return evaluator.Resolve({0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}).BaseColor;
        };
        const auto identity = evaluate();
        fixture.Graph.Nodes[2].Value = Keire::Vector4{1.0F, 1.0F, 0.15F, 0.0F};
        CHECK(evaluate() != identity);
        std::vector<Keire::ShaderPropertyDefinition> properties(1);
        properties.front().Name = fixture.Transform.Symbol;
        properties.front().DefaultValue = {1.0F, 1.0F, 0.0F, 0.0F};
        request.Properties = properties;
        CHECK(evaluate() == identity);
        fixture.Graph.Nodes[1].ParameterMetadata.TextureTransformProperty = {};
        request.Properties = {};
        CHECK(evaluate() == identity);
    }
}

TEST_CASE("Shader Graph duplicate and paste remap paired texture transform identities")
{
    TextureTransformFixture fixture;
    const std::array selected{fixture.Texture.Id, fixture.Transform.Id};
    const auto fragment = KeireEditor::CopyShaderGraphFragment(fixture.Graph, selected);
    const auto duplicated = KeireEditor::DuplicateShaderGraphSelection(fixture.Graph, selected);
    REQUIRE(duplicated.size() == 2);
    const auto texture = std::ranges::find(fixture.Graph.Nodes, duplicated.front(), &Keire::ShaderGraphNode::Id);
    REQUIRE(texture != fixture.Graph.Nodes.end());
    CHECK(texture->ParameterMetadata.TextureTransformProperty == duplicated.back());
    CHECK(fixture.Graph.Nodes[1].ParameterMetadata.TextureTransformProperty == fixture.Transform.Id);
    auto destination = Keire::CreateDefaultShaderGraph();
    const auto pasted = KeireEditor::PasteShaderGraphFragment(destination, fragment);
    REQUIRE(pasted.size() == 2);
    const auto pastedTexture = std::ranges::find(destination.Nodes, pasted.front(), &Keire::ShaderGraphNode::Id);
    REQUIRE(pastedTexture != destination.Nodes.end());
    CHECK(pastedTexture->ParameterMetadata.TextureTransformProperty == pasted.back());
    const std::array textureOnly{fixture.Texture.Id};
    const auto single = KeireEditor::DuplicateShaderGraphSelection(fixture.Graph, textureOnly);
    REQUIRE(single.size() == 1);
    CHECK(fixture.Graph.Nodes.back().ParameterMetadata.TextureTransformProperty == fixture.Transform.Id);
}
