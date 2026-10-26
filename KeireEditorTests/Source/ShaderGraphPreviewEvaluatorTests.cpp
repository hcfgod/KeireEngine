#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using KeireEditor::ShaderGraphPreviewInternal::ShaderGraphPreviewEvaluator;

    Keire::ShaderGraphPin& Pin(Keire::ShaderGraphNode& node, const std::string_view name)
    {
        INFO("Requested pin: " << name << ", node: " << node.Name);
        const auto found = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        REQUIRE(found != node.Pins.end());
        return *found;
    }

    void Connect(Keire::ShaderGraphDefinition& graph, Keire::ShaderGraphNode& source, const std::string_view output,
                 Keire::ShaderGraphNode& destination, const std::string_view input)
    {
        graph.Connections.push_back({Keire::AssetId::Generate(),
                                     {source.Id, Pin(source, output).Id},
                                     {destination.Id, Pin(destination, input).Id}});
    }

    Keire::ShaderGraphDefinition ProbeGraph(const Keire::ShaderGraphNode& node, const Keire::ShaderGraphPin& output)
    {
        auto graph = Keire::CreateDefaultShaderGraph();
        auto probe = output;
        probe.Id = Keire::AssetId::Generate();
        probe.Name = "Probe";
        probe.Direction = Keire::ShaderGraphPinDirection::Input;
        graph.Nodes.front().Pins.push_back(probe);
        graph.Connections.push_back(
            {Keire::AssetId::Generate(), {node.Id, output.Id}, {graph.Nodes.front().Id, probe.Id}});
        graph.Nodes.push_back(node);
        return graph;
    }

    Keire::Vector4 EvaluateDefault(const Keire::ShaderGraphNode& node, const Keire::ShaderGraphPin& output)
    {
        const auto graph = ProbeGraph(node, output);
        ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
        state.SetContext({0.23F, 0.71F}, {0.2F, 0.3F, 0.9F}, {1.0F, 2.0F, 3.0F});
        const auto value = state.MasterInput("Probe", output.Type);
        REQUIRE(value);
        return *value;
    }

    void CheckVector(const Keire::Vector4 actual, const Keire::Vector4 expected)
    {
        CHECK(actual.X == doctest::Approx(expected.X));
        CHECK(actual.Y == doctest::Approx(expected.Y));
        CHECK(actual.Z == doctest::Approx(expected.Z));
        CHECK(actual.W == doctest::Approx(expected.W));
    }
} // namespace

TEST_CASE("Preview evaluator dispatches every canonical expression output without an Editor workspace")
{
    std::size_t kinds = 0;
    std::size_t outputs = 0;
    for (const auto& descriptor : Keire::ShaderGraphNodeCatalog())
    {
        if (descriptor.Kind == Keire::ShaderGraphNodeKind::Master ||
            descriptor.Kind == Keire::ShaderGraphNodeKind::FunctionCall)
        {
            continue;
        }
        INFO("Node: " << descriptor.TypeId);
        const auto node = Keire::CreateShaderGraphNode(descriptor.Kind, descriptor.DefaultValueType);
        bool hasOutput = false;
        for (const auto& pin : node.Pins)
        {
            if (pin.Direction != Keire::ShaderGraphPinDirection::Output)
            {
                continue;
            }
            std::optional<Keire::Vector4> result;
            CHECK_NOTHROW(result = EvaluateDefault(node, pin));
            if (result)
            {
                CHECK(std::isfinite(result->X));
                CHECK(std::isfinite(result->Y));
                CHECK(std::isfinite(result->Z));
                CHECK(std::isfinite(result->W));
            }
            ++outputs;
            hasOutput = true;
        }
        CHECK(hasOutput);
        ++kinds;
    }
    CHECK(kinds >= 120U);
    CHECK(outputs > kinds);
}

TEST_CASE("Preview evaluator preserves arithmetic geometry and parameter values across sample resets")
{
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
    Pin(node, "A").DefaultValue = 2.25F;
    Pin(node, "B").DefaultValue = -0.5F;
    CheckVector(EvaluateDefault(node, Pin(node, "Result")), {1.75F, 0.0F, 0.0F, 0.0F});

    node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldPosition);
    auto graph = ProbeGraph(node, Pin(node, "Vector"));
    ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
    state.SetContext({}, {0.0F, 0.0F, 1.0F}, {1.0F, 2.0F, 3.0F});
    CheckVector(*state.MasterInput("Probe", Keire::ShaderGraphValueType::Vector3), {1.0F, 2.0F, 3.0F, 0.0F});
    state.SetContext({}, {0.0F, 0.0F, 1.0F}, {-2.0F, 4.0F, 8.0F});
    CheckVector(*state.MasterInput("Probe", Keire::ShaderGraphValueType::Vector3), {-2.0F, 4.0F, 8.0F, 0.0F});
}

TEST_CASE("Preview evaluator keeps unselected cyclic branches lazy and resets traversal after errors")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto selector = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::StaticSwitch);
    auto cycle = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute);
    Pin(selector, "False").DefaultValue = 0.25F;
    Connect(graph, cycle, "Output", cycle, "Input");
    Connect(graph, cycle, "Output", selector, "True");
    Connect(graph, selector, "Result", graph.Nodes.front(), "Metallic");
    graph.Nodes.push_back(selector);
    graph.Nodes.push_back(cycle);
    ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
    state.SetContext({}, {}, {});
    CHECK(state.MasterInput("Metallic", Keire::ShaderGraphValueType::Scalar)->X == doctest::Approx(0.25F));
    Pin(graph.Nodes[1], "Condition").DefaultValue = 1.0F;
    state.SetContext({}, {}, {});
    CHECK_THROWS_AS((void)state.MasterInput("Metallic", Keire::ShaderGraphValueType::Scalar), std::invalid_argument);
    // The graph's storage and topology stay fixed; changing an existing default tests retry on the same state owner.
    Pin(graph.Nodes[1], "Condition").DefaultValue = 0.0F;
    CHECK(state.MasterInput("Metallic", Keire::ShaderGraphValueType::Scalar)->X == doctest::Approx(0.25F));
}

TEST_CASE("Preview evaluator preserves deep-chain iteration and typed coercion failures")
{
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant);
    node.Value = 0.125F;
    auto graph = ProbeGraph(node, node.Pins.front());
    auto previous = graph.Connections.front().Output;
    graph.Connections.clear();
    constexpr std::size_t depth = 512;
    for (std::size_t index = 0; index < depth; ++index)
    {
        auto add = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add);
        Pin(add, "B").DefaultValue = 0.125F;
        graph.Connections.push_back({Keire::AssetId::Generate(), previous, {add.Id, Pin(add, "A").Id}});
        previous = {add.Id, Pin(add, "Result").Id};
        graph.Nodes.push_back(std::move(add));
    }
    graph.Connections.push_back(
        {Keire::AssetId::Generate(), previous, {graph.Nodes.front().Id, Pin(graph.Nodes.front(), "Probe").Id}});
    ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
    state.SetContext({}, {}, {});
    const auto value = state.MasterInput("Probe", Keire::ShaderGraphValueType::Scalar);
    REQUIRE(value);
    CHECK(value->X == doctest::Approx((depth + 1U) * 0.125F));
    CHECK_THROWS_AS((void)state.MasterInput("Probe", Keire::ShaderGraphValueType::Texture2D), std::invalid_argument);
    CHECK(state.MasterInput("Probe", Keire::ShaderGraphValueType::Scalar) == value);
}

TEST_CASE("Preview evaluator carries OpenPBR values through BSDF conversion without frontend clamping")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto bsdf = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf);
    Pin(bsdf, "BaseColor").DefaultValue = Keire::Color{2.0F, 0.3F, 0.4F, 1.0F};
    Pin(bsdf, "Metallic").DefaultValue = 0.73F;
    Pin(bsdf, "FuzzWeight").DefaultValue = 0.5F;
    Pin(bsdf, "FuzzColor").DefaultValue = Keire::Color{0.2F, 0.4F, 0.6F, 1.0F};
    auto convert = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes);
    Connect(graph, bsdf, "Slab", convert, "BSDF");
    Connect(graph, convert, "Attributes", graph.Nodes.front(), "MaterialAttributes");
    graph.Nodes.push_back(bsdf);
    graph.Nodes.push_back(convert);
    ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
    state.SetContext({}, {}, {});
    const auto surface = state.MasterAttributes();
    REQUIRE(surface);
    CheckVector(surface->BaseColor, {2.0F, 0.3F, 0.4F, 1.0F});
    CHECK(surface->Metallic == doctest::Approx(0.73F));
    CheckVector(surface->SheenColor, {0.1F, 0.2F, 0.3F, 1.0F});
}

TEST_CASE("Preview math honors canonical Input pins for every typed extended unary operation")
{
    using Kind = Keire::ShaderGraphNodeKind;
    using Type = Keire::ShaderGraphValueType;
    struct Operation
    {
        Kind Node;
        float (*Expected)(float);
    };
    const std::array operations{
        Operation{Kind::ArcTangent, [](const float value) { return std::atan(value); }},
        Operation{Kind::HyperbolicSine, [](const float value) { return std::sinh(value); }},
        Operation{Kind::HyperbolicCosine, [](const float value) { return std::cosh(value); }},
        Operation{Kind::HyperbolicTangent, [](const float value) { return std::tanh(value); }},
        Operation{Kind::DegreesToRadians, [](const float value) { return value * 0.017453292519943295F; }},
        Operation{Kind::RadiansToDegrees, [](const float value) { return value * 57.29577951308232F; }},
        Operation{Kind::Negate, [](const float value) { return -value; }},
        Operation{Kind::Exponential, [](const float value) { return std::exp(value); }},
        Operation{Kind::Logarithm, [](const float value) { return std::log(value); }},
        Operation{Kind::ScaleAndBias, [](const float value) { return value * 2.0F + 0.125F; }},
    };
    for (const auto& operation : operations)
    {
        for (const auto type : {Type::Scalar, Type::Vector2, Type::Vector3, Type::Vector4, Type::Color})
        {
            INFO("Kind: " << static_cast<int>(operation.Node) << ", type: " << static_cast<int>(type));
            auto node = Keire::CreateShaderGraphNode(operation.Node, type);
            auto& input = Pin(node, "Input");
            switch (type)
            {
            case Type::Scalar:
                input.DefaultValue = 0.5F;
                break;
            case Type::Vector2:
                input.DefaultValue = Keire::Vector2{0.5F, 0.25F};
                break;
            case Type::Vector3:
                input.DefaultValue = Keire::Vector3{0.5F, 0.25F, 0.75F};
                break;
            case Type::Vector4:
                input.DefaultValue = Keire::Vector4{0.5F, 0.25F, 0.75F, 1.0F};
                break;
            case Type::Color:
                input.DefaultValue = Keire::Color{0.5F, 0.25F, 0.75F, 1.0F};
                break;
            default:
                FAIL("The unary fixture requires a numeric type.");
            }
            if (operation.Node == Kind::ScaleAndBias)
            {
                Pin(node, "Scale").DefaultValue = 2.0F;
                Pin(node, "Bias").DefaultValue = 0.125F;
            }
            const Keire::Vector4 expected{
                operation.Expected(0.5F), type != Type::Scalar ? operation.Expected(0.25F) : 0.0F,
                type != Type::Scalar && type != Type::Vector2 ? operation.Expected(0.75F) : 0.0F,
                type == Type::Vector4 || type == Type::Color ? operation.Expected(1.0F) : 0.0F};
            CheckVector(EvaluateDefault(node, Pin(node, "Result")), expected);
        }
    }
    auto legacy = Keire::CreateShaderGraphNode(Kind::Sine);
    Pin(legacy, "Value").DefaultValue = 0.5F;
    CheckVector(EvaluateDefault(legacy, Pin(legacy, "Result")), {std::sin(0.5F), 0.0F, 0.0F, 0.0F});
}

TEST_CASE("Preview evaluator retains expression diagnostics and recovers after a missing output")
{
    for (const auto kind : {Keire::ShaderGraphNodeKind::FunctionCall, Keire::ShaderGraphNodeKind::Master})
    {
        auto expression = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom);
        expression.Kind = kind;
        const auto graph = ProbeGraph(expression, Pin(expression, "Result"));
        ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
        CHECK_THROWS_AS((void)state.MasterInput("Probe", Keire::ShaderGraphValueType::Scalar), std::invalid_argument);
    }
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant);
    auto graph = ProbeGraph(node, node.Pins.front());
    graph.Connections.front().Output.Pin = Keire::AssetId::Generate();
    ShaderGraphPreviewEvaluator::Impl state(graph, {}, {});
    CHECK_THROWS_AS((void)state.MasterInput("Probe", Keire::ShaderGraphValueType::Scalar), std::invalid_argument);
    CHECK_FALSE(state.MasterInput("Not a master input", Keire::ShaderGraphValueType::Scalar));
}
