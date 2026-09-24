#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    ShaderGraphDefinition CreateFullscreenEffectTemplate(const ShaderGraphTemplate graphTemplate)
    {
        auto graph = CreateTargetShaderGraph(ShaderGraphTarget::Fullscreen);
        const auto add = [&](const ShaderGraphNodeKind kind, const ShaderGraphValueType type)
        {
            auto node = CreateShaderGraphNode(kind, type);
            const auto index = graph.Nodes.size();
            node.EditorPosition = {static_cast<float>((index - 1U) % 6U) * 240.0F,
                                   static_cast<float>((index - 1U) / 6U) * 220.0F};
            graph.Nodes.push_back(std::move(node));
            return index;
        };
        const auto connect = [&](const std::size_t from, const std::size_t to, const std::string_view input)
        {
            const auto& source = graph.Nodes[from];
            const auto& destination = graph.Nodes[to];
            const auto outputPin =
                std::ranges::find(source.Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction);
            const auto inputPin = std::ranges::find(destination.Pins, input, &ShaderGraphPin::Name);
            if (outputPin == source.Pins.end() || inputPin == destination.Pins.end())
                throw std::logic_error("Fullscreen template references a missing node pin.");
            graph.Connections.push_back(
                {AssetId::Generate(), {source.Id, outputPin->Id}, {destination.Id, inputPin->Id}});
        };
        const auto constant = [&](const ShaderGraphValueType type, ShaderGraphValue value)
        {
            const auto index = add(ShaderGraphNodeKind::Constant, type);
            graph.Nodes[index].Value = std::move(value);
            return index;
        };
        const auto parameter = [&](const std::string_view name, const float value)
        {
            const auto index = add(ShaderGraphNodeKind::Parameter, ShaderGraphValueType::Scalar);
            graph.Nodes[index].Name = name;
            graph.Nodes[index].Symbol = name;
            graph.Nodes[index].Value = value;
            return index;
        };
        const auto binary = [&](const ShaderGraphNodeKind kind, const ShaderGraphValueType type, const std::size_t left,
                                const std::size_t right)
        {
            const auto index = add(kind, type);
            connect(left, index, "A");
            connect(right, index, "B");
            return index;
        };
        const auto sample = [&](const std::size_t uv)
        {
            const auto index = add(ShaderGraphNodeKind::SceneColor, ShaderGraphValueType::Color);
            connect(uv, index, "UV");
            return index;
        };
        const auto uv = add(ShaderGraphNodeKind::UV, ShaderGraphValueType::Vector2);
        std::size_t result = 0;
        if (graphTemplate == ShaderGraphTemplate::FullscreenVignette)
        {
            const auto gradient = add(ShaderGraphNodeKind::RadialGradient, ShaderGraphValueType::Scalar);
            connect(uv, gradient, "UV");
            connect(parameter("Radius", 0.75F), gradient, "Radius");
            connect(parameter("Softness", 2.0F), gradient, "Density");
            const auto strength = add(ShaderGraphNodeKind::Lerp, ShaderGraphValueType::Scalar);
            connect(constant(ShaderGraphValueType::Scalar, 1.0F), strength, "A");
            connect(gradient, strength, "B");
            connect(parameter("Strength", 0.6F), strength, "T");
            result = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Color, sample(uv), strength);
        }
        else
        {
            const auto texel = add(ShaderGraphNodeKind::SceneTexelSize, ShaderGraphValueType::Vector2);
            if (graphTemplate == ShaderGraphTemplate::FullscreenBlur)
            {
                const auto radius = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, texel,
                                           parameter("RadiusPixels", 2.0F));
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        const auto offset = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, radius,
                                                   constant(ShaderGraphValueType::Vector2,
                                                            Vector2{static_cast<float>(x), static_cast<float>(y)}));
                        const auto tap =
                            sample(binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Vector2, uv, offset));
                        result = result == 0
                                     ? tap
                                     : binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Color, result, tap);
                    }
                result = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Color, result,
                                constant(ShaderGraphValueType::Color, Color{1.0F / 9, 1.0F / 9, 1.0F / 9, 1.0F / 9}));
            }
            else if (graphTemplate == ShaderGraphTemplate::FullscreenChromaticAberration)
            {
                auto offset = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, texel,
                                     constant(ShaderGraphValueType::Vector2, Vector2{1, 0}));
                offset = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, offset,
                                parameter("OffsetPixels", 3.0F));
                const auto red = sample(binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Vector2, uv, offset));
                const auto green = sample(uv);
                const auto blue =
                    sample(binary(ShaderGraphNodeKind::Subtract, ShaderGraphValueType::Vector2, uv, offset));
                const auto r = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Color, red,
                                      constant(ShaderGraphValueType::Color, Color{1, 0, 0, 0}));
                const auto g = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Color, green,
                                      constant(ShaderGraphValueType::Color, Color{0, 1, 0, 1}));
                const auto b = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Color, blue,
                                      constant(ShaderGraphValueType::Color, Color{0, 0, 1, 0}));
                result = binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Color,
                                binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Color, r, g), b);
            }
            else if (graphTemplate == ShaderGraphTemplate::FullscreenDistortion)
            {
                const auto time = add(ShaderGraphNodeKind::Time, ShaderGraphValueType::Scalar);
                const auto phase =
                    binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Scalar, time, parameter("Speed", 1.0F));
                const auto frequency = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, uv,
                                              parameter("Frequency", 24.0F));
                const auto wave = add(ShaderGraphNodeKind::Sine, ShaderGraphValueType::Vector2);
                connect(binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Vector2, frequency, phase), wave,
                        "Value");
                auto offset = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, wave, texel);
                offset = binary(ShaderGraphNodeKind::Multiply, ShaderGraphValueType::Vector2, offset,
                                parameter("AmplitudePixels", 6.0F));
                result = sample(binary(ShaderGraphNodeKind::Add, ShaderGraphValueType::Vector2, uv, offset));
            }
            else
                throw std::invalid_argument("Unknown fullscreen effect template.");
        }
        connect(result, 0, "Color");
        graph.Nodes.front().EditorPosition = {1440.0F, 220.0F};
        return graph;
    }
} // namespace Keire::Detail
