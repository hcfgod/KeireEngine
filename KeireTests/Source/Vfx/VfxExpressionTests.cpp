#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] Keire::VfxGraphPin& Pin(Keire::VfxGraphNode& node, const std::string_view semantic, const bool input)
    {
        const auto found = std::ranges::find_if(node.Pins, [semantic, input](const Keire::VfxGraphPin& pin)
                                                { return pin.Semantic == semantic && pin.Input == input; });
        REQUIRE(found != node.Pins.end());
        return *found;
    }

    [[nodiscard]] Keire::VfxGraphPin& Pin(Keire::VfxGraphBlock& block, const std::string_view semantic)
    {
        const auto found = std::ranges::find(block.Pins, semantic, &Keire::VfxGraphPin::Semantic);
        REQUIRE(found != block.Pins.end());
        return *found;
    }

    [[nodiscard]] Keire::VfxGraphProperty& Property(Keire::VfxGraphNode& node, const std::string_view name)
    {
        const auto found = std::ranges::find(node.Properties, name, &Keire::VfxGraphProperty::Name);
        REQUIRE(found != node.Properties.end());
        return *found;
    }

    [[nodiscard]] Keire::VfxGraphNode& ContextNode(Keire::VfxEffectDefinition& definition,
                                                   const Keire::VfxContextType context)
    {
        auto& nodes = definition.Systems.front().Nodes;
        const auto found =
            std::ranges::find_if(nodes, [context](const Keire::VfxGraphNode& node)
                                 { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == context; });
        REQUIRE(found != nodes.end());
        return *found;
    }

    [[nodiscard]] Keire::AssetId ModuleId(const Keire::VfxEffectDefinition& definition, const auto& alternativeTag)
    {
        using T = std::decay_t<decltype(alternativeTag)>;
        const auto found = std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                                { return std::holds_alternative<T>(module.Payload); });
        REQUIRE(found != definition.Modules.end());
        return found->Id;
    }

    void Connect(Keire::VfxGraphSystem& system, const Keire::VfxGraphNode& outputNode,
                 const std::string_view outputSemantic, const Keire::VfxGraphNode& inputNode,
                 const std::string_view inputSemantic)
    {
        const auto output = std::ranges::find_if(outputNode.Pins, [outputSemantic](const Keire::VfxGraphPin& pin)
                                                 { return !pin.Input && pin.Semantic == outputSemantic; });
        const auto input = std::ranges::find_if(inputNode.Pins, [inputSemantic](const Keire::VfxGraphPin& pin)
                                                { return pin.Input && pin.Semantic == inputSemantic; });
        REQUIRE(output != outputNode.Pins.end());
        REQUIRE(input != inputNode.Pins.end());
        system.Connections.push_back({Keire::AssetId::Generate(), outputNode.Id, output->Id, inputNode.Id, input->Id});
    }

    void Connect(Keire::VfxGraphSystem& system, const Keire::VfxGraphNode& outputNode,
                 const std::string_view outputSemantic, const Keire::VfxGraphNode& inputContext,
                 const Keire::VfxGraphBlock& inputBlock, const std::string_view inputSemantic)
    {
        const auto output = std::ranges::find_if(outputNode.Pins, [outputSemantic](const Keire::VfxGraphPin& pin)
                                                 { return !pin.Input && pin.Semantic == outputSemantic; });
        const auto input = std::ranges::find(inputBlock.Pins, inputSemantic, &Keire::VfxGraphPin::Semantic);
        REQUIRE(output != outputNode.Pins.end());
        REQUIRE(input != inputBlock.Pins.end());
        Keire::VfxGraphConnection connection;
        connection.Id = Keire::AssetId::Generate();
        connection.OutputNode = outputNode.Id;
        connection.OutputPin = output->Id;
        connection.InputNode = inputContext.Id;
        connection.InputPin = input->Id;
        connection.InputBlock = inputBlock.Id;
        system.Connections.push_back(connection);
    }

    void ConvertModulesToContextBlocks(Keire::VfxEffectDefinition& definition)
    {
        auto& system = definition.Systems.front();
        std::erase_if(system.Nodes,
                      [](const Keire::VfxGraphNode& node) { return node.Kind == Keire::VfxGraphNodeKind::Module; });
        system.Connections.clear();
        for (auto& node : system.Nodes)
            if (node.Kind == Keire::VfxGraphNodeKind::Context)
                node.Blocks.clear();
        for (const auto& module : definition.Modules)
        {
            const auto context = std::visit(
                [](const auto& payload)
                {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::same_as<T, Keire::VfxEmissionRateModule> ||
                                  std::same_as<T, Keire::VfxBurstModule>)
                        return Keire::VfxContextType::Spawn;
                    else if constexpr (std::same_as<T, Keire::VfxShapeModule> ||
                                       std::same_as<T, Keire::VfxInitializeModule>)
                        return Keire::VfxContextType::Initialize;
                    else if constexpr (std::same_as<T, Keire::VfxRendererModule>)
                        return Keire::VfxContextType::Output;
                    else
                        return Keire::VfxContextType::Update;
                },
                module.Payload);
            ContextNode(definition, context).Blocks.push_back(Keire::CreateVfxGraphBlock(module));
        }

        const std::array contexts{Keire::VfxContextType::Spawn, Keire::VfxContextType::Initialize,
                                  Keire::VfxContextType::Update, Keire::VfxContextType::Output};
        for (std::size_t index = 1; index < contexts.size(); ++index)
        {
            auto& output = ContextNode(definition, contexts[index - 1]);
            auto& input = ContextNode(definition, contexts[index]);
            Connect(system, output, "particles", input, "particles");
        }
    }

    [[nodiscard]] float FoldScalarOperator(const std::string_view typeId,
                                           const std::initializer_list<float> inputValues)
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        const Keire::VfxModuleDefinition force{Keire::AssetId::Generate(), true, Keire::VfxForceModule{}};
        definition.Modules.push_back(force);

        auto operation = Keire::CreateVfxGraphOperatorNode(typeId, {-300.0F, 180.0F});
        operation.Context = Keire::VfxContextType::Update;
        auto value = inputValues.begin();
        for (auto& pin : operation.Pins)
        {
            if (!pin.Input)
                continue;
            REQUIRE(value != inputValues.end());
            pin.DefaultValue = *value++;
        }
        REQUIRE(value == inputValues.end());

        auto& system = definition.Systems.front();
        system.Nodes.push_back(std::move(operation));
        auto& operationNode = system.Nodes.back();
        auto& updateContext = ContextNode(definition, Keire::VfxContextType::Update);
        updateContext.Blocks.push_back(Keire::CreateVfxGraphBlock(force));
        const auto forceBlock = std::ranges::find(updateContext.Blocks, force.Id, &Keire::VfxGraphBlock::Reference);
        REQUIRE(forceBlock != updateContext.Blocks.end());
        Connect(system, operationNode, "out", updateContext, *forceBlock, "gravityMultiplier");

        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        REQUIRE(program.Valid);
        CHECK(program.ValueInstructions.empty());
        const auto binding = std::ranges::find(program.Bindings, Keire::VfxModuleProperty::ForceGravityMultiplier,
                                               &Keire::VfxCompiledBinding::Property);
        REQUIRE(binding != program.Bindings.end());
        REQUIRE(binding->LiteralValue.has_value());
        REQUIRE(std::holds_alternative<float>(*binding->LiteralValue));
        return std::get<float>(*binding->LiteralValue);
    }

    [[nodiscard]] Keire::VfxParameterValue FoldValueOperator(const std::string_view typeId,
                                                             const std::vector<Keire::VfxParameterValue>& inputValues,
                                                             const std::string_view outputSemantic = {})
    {
        Keire::VfxGraphSystem system;
        system.Id = Keire::AssetId::Generate();
        auto operation = Keire::CreateVfxGraphOperatorNode(typeId);
        auto value = inputValues.begin();
        for (auto& pin : operation.Pins)
        {
            if (!pin.Input)
                continue;
            REQUIRE(value != inputValues.end());
            REQUIRE(Keire::VfxValueMatchesType(pin.Type, *value));
            pin.DefaultValue = *value++;
        }
        REQUIRE(value == inputValues.end());
        const auto output =
            std::ranges::find_if(operation.Pins, [outputSemantic](const Keire::VfxGraphPin& pin)
                                 { return !pin.Input && (outputSemantic.empty() || pin.Semantic == outputSemantic); });
        REQUIRE(output != operation.Pins.end());
        const auto outputId = output->Id;
        system.Nodes.push_back(std::move(operation));

        const std::array required{outputId};
        const auto compilation = Keire::Internal::CompileVfxExpressions(system, {}, required);
        const auto source = compilation.SourcesByOutputPin.find(outputId);
        REQUIRE(source != compilation.SourcesByOutputPin.end());
        REQUIRE(source->second.Kind == Keire::VfxCompiledValueSourceKind::Literal);
        return source->second.Literal;
    }

    [[nodiscard]] Keire::VfxParameterValue
    EvaluateBuiltInOperator(const std::string_view typeId,
                            const Keire::Internal::VfxExpressionEvaluationContext& context)
    {
        Keire::VfxGraphSystem system;
        system.Id = context.System;
        auto operation = Keire::CreateVfxGraphOperatorNode(typeId);
        operation.Context = context.Context;
        const auto output =
            std::ranges::find_if(operation.Pins, [](const Keire::VfxGraphPin& pin) { return !pin.Input; });
        REQUIRE(output != operation.Pins.end());
        const auto outputId = output->Id;
        system.Nodes.push_back(std::move(operation));

        const std::array required{outputId};
        auto compilation = Keire::Internal::CompileVfxExpressions(system, {}, required);
        const auto source = compilation.SourcesByOutputPin.find(outputId);
        REQUIRE(source != compilation.SourcesByOutputPin.end());
        REQUIRE(source->second.Kind == Keire::VfxCompiledValueSourceKind::Register);
        Keire::VfxCompiledProgram program;
        program.ValueInstructions = std::move(compilation.Instructions);
        program.ValueRegisterCount = compilation.RegisterCount;
        std::vector<Keire::VfxParameterValue> registers(program.ValueRegisterCount, 0.0F);
        REQUIRE(Keire::Internal::EvaluateVfxExpressions(program, {}, context, registers));
        REQUIRE(source->second.Index < registers.size());
        return registers[source->second.Index];
    }

    [[nodiscard]] Keire::VfxEffectDefinition RandomRangeLifetimeEffect()
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        definition.Loop = false;
        definition.Duration = 0.2F;
        definition.Capacity = 16;
        const auto emissionId = ModuleId(definition, Keire::VfxEmissionRateModule{});
        std::get<Keire::VfxEmissionRateModule>(
            std::ranges::find(definition.Modules, emissionId, &Keire::VfxModuleDefinition::Id)->Payload)
            .ParticlesPerSecond = 10.0F;

        auto range = Keire::CreateVfxGraphOperatorNode("keire.operator.range", {-500.0F, 200.0F});
        range.Context = Keire::VfxContextType::Initialize;
        Pin(range, "minimum", true).DefaultValue = 2.0F;
        Pin(range, "maximum", true).DefaultValue = 2.0F;
        auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-range", {-200.0F, 200.0F});
        random.Context = Keire::VfxContextType::Initialize;

        auto& system = definition.Systems.front();
        system.Nodes.push_back(std::move(range));
        system.Nodes.push_back(std::move(random));
        auto& rangeNode = system.Nodes[system.Nodes.size() - 2];
        auto& randomNode = system.Nodes.back();
        Connect(system, rangeNode, "range", randomNode, "range");

        const auto initializeId = ModuleId(definition, Keire::VfxInitializeModule{});
        auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
        const auto initialize =
            std::ranges::find(initializeContext.Blocks, initializeId, &Keire::VfxGraphBlock::Reference);
        REQUIRE(initialize != initializeContext.Blocks.end());
        Connect(system, randomNode, "out", initializeContext, *initialize, "lifetimeMinimum");
        Connect(system, randomNode, "out", initializeContext, *initialize, "lifetimeMaximum");
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] constexpr Keire::AssetId ResidentId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x5646585245534944ULL, value);
    }

    [[nodiscard]] std::vector<Keire::CurveKey> CurveKeys(const std::size_t count)
    {
        std::vector<Keire::CurveKey> result;
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto time = count <= 1 ? 0.0F : static_cast<float>(index) / static_cast<float>(count - 1);
            result.push_back({time, 1.0F - time});
        }
        return result;
    }

    [[nodiscard]] std::vector<Keire::ColorGradientKey> GradientKeys(const std::size_t count)
    {
        std::vector<Keire::ColorGradientKey> result;
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto time = count <= 1 ? 0.0F : static_cast<float>(index) / static_cast<float>(count - 1);
            result.push_back({time, {time, 1.0F - time, 0.5F, 1.0F}});
        }
        return result;
    }

    [[nodiscard]] Keire::VfxEffectDefinition
    ResidentBytesEffect(const std::size_t curveKeyCount, const std::size_t gradientKeyCount, std::string portableSource)
    {
        auto definition = RandomRangeLifetimeEffect();
        definition.Name = "Schema-4 resident-memory coverage effect with deliberately owned UTF-8 graph text";
        definition.Systems.front().Name = "Resident-memory graph system with Context Blocks and value Operators";
        definition.Blackboard.push_back({ResidentId(1), "Reusable scalar range from one through twenty",
                                         Keire::VfxValueType::ScalarRange, Keire::VfxScalarRange{1.0F, 20.0F}, true});
        definition.Blackboard.push_back({ResidentId(2), "Authored curve default with owned key storage",
                                         Keire::VfxValueType::Curve, Keire::Curve1D(CurveKeys(curveKeyCount)), true});
        definition.Blackboard.push_back({ResidentId(3), "Authored gradient default with owned key storage",
                                         Keire::VfxValueType::Gradient,
                                         Keire::ColorGradient(GradientKeys(gradientKeyCount)), true});

        auto portable = Keire::CreateVfxGraphPortableHlslBlock(std::move(portableSource));
        portable.Pins.push_back({Keire::AssetId::Generate(),
                                 "Portable scalar input with a deliberately non-SSO display name",
                                 Keire::VfxValueType::Scalar, true, "Input", 1.0F});
        ContextNode(definition, Keire::VfxContextType::Update).Blocks.push_back(std::move(portable));
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] std::size_t ExpectedValueOwnedBytes(const Keire::VfxParameterValue& value) noexcept
    {
        if (const auto* curve = std::get_if<Keire::Curve1D>(&value))
            return curve->Keys().size() * sizeof(Keire::CurveKey);
        if (const auto* gradient = std::get_if<Keire::ColorGradient>(&value))
            return gradient->Keys().size() * sizeof(Keire::ColorGradientKey);
        return 0;
    }

    [[nodiscard]] std::size_t ExpectedPropertyOwnedBytes(const Keire::VfxGraphProperty& property) noexcept
    {
        auto result = property.Name.capacity();
        if (const auto* text = std::get_if<std::string>(&property.Value))
            result += text->capacity();
        return result;
    }

    [[nodiscard]] std::size_t ExpectedPinOwnedBytes(const Keire::VfxGraphPin& pin) noexcept
    {
        return pin.Name.capacity() + pin.Semantic.capacity() +
               (pin.DefaultValue ? ExpectedValueOwnedBytes(*pin.DefaultValue) : 0);
    }

    [[nodiscard]] std::size_t ExpectedBlockOwnedBytes(const Keire::VfxGraphBlock& block) noexcept
    {
        auto result = block.TypeId.Value.capacity() + block.Type.capacity() +
                      block.Pins.capacity() * sizeof(Keire::VfxGraphPin) +
                      block.Properties.capacity() * sizeof(Keire::VfxGraphProperty);
        for (const auto& pin : block.Pins)
            result += ExpectedPinOwnedBytes(pin);
        for (const auto& property : block.Properties)
            result += ExpectedPropertyOwnedBytes(property);
        return result;
    }

    [[nodiscard]] std::size_t ExpectedNodeOwnedBytes(const Keire::VfxGraphNode& node) noexcept
    {
        auto result = node.Type.capacity() + node.CustomHlsl.capacity() + node.TypeId.Value.capacity() +
                      node.Pins.capacity() * sizeof(Keire::VfxGraphPin) +
                      node.Properties.capacity() * sizeof(Keire::VfxGraphProperty) +
                      node.ResolvedSignature.capacity() * sizeof(Keire::VfxValueType) +
                      node.DynamicPinOrder.capacity() * sizeof(Keire::AssetId) +
                      node.Blocks.capacity() * sizeof(Keire::VfxGraphBlock);
        for (const auto& pin : node.Pins)
            result += ExpectedPinOwnedBytes(pin);
        for (const auto& property : node.Properties)
            result += ExpectedPropertyOwnedBytes(property);
        for (const auto& block : node.Blocks)
            result += ExpectedBlockOwnedBytes(block);
        return result;
    }

    [[nodiscard]] std::size_t ExpectedResidentBytes(const Keire::VfxEffectAsset& asset) noexcept
    {
        const auto& definition = asset.Definition();
        auto result = sizeof(asset) + definition.Name.capacity() +
                      definition.Modules.capacity() * sizeof(Keire::VfxModuleDefinition) +
                      definition.Systems.capacity() * sizeof(Keire::VfxGraphSystem) +
                      definition.Blackboard.capacity() * sizeof(Keire::VfxBlackboardParameter);
        for (const auto& module : definition.Modules)
        {
            if (const auto* size = std::get_if<Keire::VfxSizeOverLifetimeModule>(&module.Payload))
                result += size->Size.Keys().size() * sizeof(Keire::CurveKey);
            if (const auto* color = std::get_if<Keire::VfxColorOverLifetimeModule>(&module.Payload))
                result += color->Color.Keys().size() * sizeof(Keire::ColorGradientKey);
        }
        for (const auto& system : definition.Systems)
        {
            result += system.Name.capacity() + system.Nodes.capacity() * sizeof(Keire::VfxGraphNode) +
                      system.Connections.capacity() * sizeof(Keire::VfxGraphConnection);
            for (const auto& node : system.Nodes)
                result += ExpectedNodeOwnedBytes(node);
        }
        for (const auto& parameter : definition.Blackboard)
            result += parameter.Name.capacity() + ExpectedValueOwnedBytes(parameter.DefaultValue);
        return result;
    }

    [[nodiscard]] const std::string& PortableSource(const Keire::VfxEffectAsset& asset)
    {
        for (const auto& system : asset.Definition().Systems)
            for (const auto& node : system.Nodes)
                for (const auto& block : node.Blocks)
                    if (block.TypeId.View() == "keire.block.portable-hlsl")
                        return std::get<std::string>(block.Properties.front().Value);
        throw std::logic_error("Test VFX asset has no Portable Custom HLSL Block.");
    }
} // namespace

TEST_CASE("schema-4 VFX catalog exposes canonical executable Range and math Operators")
{
    const auto catalog = Keire::VfxNodeCatalog();
    REQUIRE_FALSE(catalog.empty());
    std::set<std::string> typeIds;
    for (const auto& descriptor : catalog)
    {
        CHECK(typeIds.insert(descriptor.TypeId.Value).second);
        CHECK_FALSE(descriptor.Label.empty());
        CHECK_FALSE(descriptor.Category.empty());
        if (descriptor.SupportTier == Keire::VfxNodeSupportTier::Disabled)
            CHECK_FALSE(descriptor.DisabledReason.empty());
        else if (descriptor.Class == Keire::VfxNodeClass::Operator)
            CHECK(descriptor.Lowering.has_value());
    }

    const auto* randomRange = Keire::FindVfxNodeDescriptor("keire.operator.random-range");
    REQUIRE(randomRange != nullptr);
    CHECK(randomRange->Label == "Random Range");
    CHECK(randomRange->SupportTier == Keire::VfxNodeSupportTier::KeireEquivalent);
    CHECK(randomRange->BackendTier == Keire::VfxNodeBackendTier::CpuAndGpu);

    const auto node = Keire::CreateVfxGraphOperatorNode(randomRange->TypeId.View());
    CHECK(node.Kind == Keire::VfxGraphNodeKind::Operator);
    CHECK(node.TypeId == randomRange->TypeId);
    CHECK(node.Pins.size() == randomRange->Pins.size());
    CHECK(node.Properties.size() == randomRange->Settings.size());
    CHECK_THROWS_AS(static_cast<void>(Keire::CreateVfxGraphOperatorNode("keire.operator.not-real")),
                    std::invalid_argument);

    const auto* eventContext = Keire::FindVfxNodeDescriptor("keire.context.event");
    REQUIRE(eventContext != nullptr);
    CHECK(eventContext->Class == Keire::VfxNodeClass::Context);
    CHECK(eventContext->BackendTier == Keire::VfxNodeBackendTier::CpuAndGpu);
    CHECK_FALSE(eventContext->Lowering.has_value());

    const auto* output = Keire::FindVfxNodeDescriptor("keire.output.renderer");
    REQUIRE(output != nullptr);
    CHECK(output->Class == Keire::VfxNodeClass::Output);
    CHECK(output->Pins.size() == 3);
}

TEST_CASE("Unity core utility Operators fold deterministically with contained edge cases")
{
    const auto scalar = [](const std::string_view typeId, const std::initializer_list<float> values)
    {
        std::vector<Keire::VfxParameterValue> inputs;
        inputs.reserve(values.size());
        for (const auto value : values)
            inputs.emplace_back(value);
        const auto result = FoldValueOperator(typeId, inputs);
        REQUIRE(std::holds_alternative<float>(result));
        return std::get<float>(result);
    };
    const auto boolean = [](const std::string_view typeId, const bool left, const bool right)
    {
        const auto result = FoldValueOperator(typeId, {left, right});
        REQUIRE(std::holds_alternative<bool>(result));
        return std::get<bool>(result);
    };
    const auto unsignedInteger = [](const std::string_view typeId, const std::uint64_t left,
                                    const std::optional<std::uint64_t> right = std::nullopt)
    {
        auto inputs = std::vector<Keire::VfxParameterValue>{left};
        if (right)
            inputs.emplace_back(*right);
        const auto result = FoldValueOperator(typeId, inputs);
        REQUIRE(std::holds_alternative<std::uint64_t>(result));
        return std::get<std::uint64_t>(result);
    };

    CHECK(scalar("keire.operator.modulo", {-3.0F, 2.0F}) == doctest::Approx(1.0F));
    CHECK(scalar("keire.operator.modulo", {3.0F, 0.0F}) == 0.0F);
    CHECK(scalar("keire.operator.one-minus", {0.25F}) == doctest::Approx(0.75F));
    CHECK(scalar("keire.operator.reciprocal", {4.0F}) == doctest::Approx(0.25F));
    CHECK(scalar("keire.operator.reciprocal", {0.0F}) == 0.0F);
    CHECK(scalar("keire.operator.inverse-lerp", {0.0F, 10.0F, 15.0F}) == doctest::Approx(1.5F));
    CHECK(scalar("keire.operator.inverse-lerp", {2.0F, 2.0F, 5.0F}) == 0.0F);
    CHECK(scalar("keire.operator.discretize", {2.9F, 0.5F}) == doctest::Approx(2.5F));
    CHECK(scalar("keire.operator.discretize", {-0.1F, 0.5F}) == doctest::Approx(-0.5F));
    CHECK(scalar("keire.operator.discretize", {2.0F, 0.0F}) == 0.0F);

    CHECK(boolean("keire.operator.nand", true, true) == false);
    CHECK(boolean("keire.operator.nand", true, false) == true);
    CHECK(boolean("keire.operator.nor", false, false) == true);
    CHECK(boolean("keire.operator.nor", false, true) == false);

    CHECK(unsignedInteger("keire.operator.bitwise-and", 0b1010U, 0b1100U) == 0b1000U);
    CHECK(unsignedInteger("keire.operator.bitwise-or", 0b1010U, 0b1100U) == 0b1110U);
    CHECK(unsignedInteger("keire.operator.bitwise-xor", 0b1010U, 0b1100U) == 0b0110U);
    CHECK(unsignedInteger("keire.operator.bitwise-complement", 0U) == std::numeric_limits<std::uint64_t>::max());
    CHECK(unsignedInteger("keire.operator.bitwise-left-shift", 1U, 63U) == (std::uint64_t{1} << 63U));
    CHECK(unsignedInteger("keire.operator.bitwise-left-shift", 1U, 64U) == 0U);
    CHECK(unsignedInteger("keire.operator.bitwise-right-shift", std::uint64_t{1} << 63U, 63U) == 1U);
    CHECK(unsignedInteger("keire.operator.bitwise-right-shift", std::uint64_t{1} << 63U, 64U) == 0U);

    const auto squaredDistance = FoldValueOperator(
        "keire.operator.squared-distance", {Keire::Vector3{1.0F, 2.0F, 3.0F}, Keire::Vector3{4.0F, 6.0F, 3.0F}});
    REQUIRE(std::holds_alternative<float>(squaredDistance));
    CHECK(std::get<float>(squaredDistance) == doctest::Approx(25.0F));
    const auto squaredLength = FoldValueOperator("keire.operator.squared-length", {Keire::Vector3{3.0F, 4.0F, 12.0F}});
    REQUIRE(std::holds_alternative<float>(squaredLength));
    CHECK(std::get<float>(squaredLength) == doctest::Approx(169.0F));

    const auto luma = FoldValueOperator("keire.operator.color-luma", {Keire::Color{1.0F, 0.0F, 0.0F, 0.25F}});
    REQUIRE(std::holds_alternative<float>(luma));
    CHECK(std::get<float>(luma) == doctest::Approx(0.299F));
    const auto rgb = FoldValueOperator("keire.operator.hsv-to-rgb", {Keire::Vector3{0.0F, 1.0F, 1.0F}});
    REQUIRE(std::holds_alternative<Keire::Vector4>(rgb));
    CHECK(std::get<Keire::Vector4>(rgb).X == doctest::Approx(1.0F));
    CHECK(std::get<Keire::Vector4>(rgb).Y == doctest::Approx(0.0F));
    CHECK(std::get<Keire::Vector4>(rgb).Z == doctest::Approx(0.0F));
    CHECK(std::get<Keire::Vector4>(rgb).W == doctest::Approx(1.0F));
    const auto hsv = FoldValueOperator("keire.operator.rgb-to-hsv", {Keire::Color{0.0F, 1.0F, 0.0F, 0.25F}});
    REQUIRE(std::holds_alternative<Keire::Vector3>(hsv));
    CHECK(std::get<Keire::Vector3>(hsv).X == doctest::Approx(1.0F / 3.0F));
    CHECK(std::get<Keire::Vector3>(hsv).Y == doctest::Approx(1.0F));
    CHECK(std::get<Keire::Vector3>(hsv).Z == doctest::Approx(1.0F));
}

TEST_CASE("Unity particle and system utility Operators use stable runtime identity")
{
    Keire::Internal::VfxExpressionEvaluationContext context;
    context.EffectSeed = 0x12345678U;
    context.SeedOffset = 0x00ff00ffU;
    context.System = Keire::AssetId(0x1122334455667788ULL, 0x99aabbccddeeff00ULL);
    context.Context = Keire::VfxContextType::Update;
    context.SimulationStep = 0x123456789abcdef0ULL;
    context.Age = 2.0F;
    context.Lifetime = 8.0F;

    const auto normalizedAge = EvaluateBuiltInOperator("keire.operator.age-over-lifetime", context);
    REQUIRE(std::holds_alternative<float>(normalizedAge));
    CHECK(std::get<float>(normalizedAge) == doctest::Approx(0.25F));
    context.Lifetime = 0.0F;
    const auto zeroLifetime = EvaluateBuiltInOperator("keire.operator.age-over-lifetime", context);
    REQUIRE(std::holds_alternative<float>(zeroLifetime));
    CHECK(std::get<float>(zeroLifetime) == 0.0F);

    const auto frameIndex = EvaluateBuiltInOperator("keire.operator.frame-index", context);
    REQUIRE(std::holds_alternative<std::uint64_t>(frameIndex));
    CHECK(std::get<std::uint64_t>(frameIndex) == context.SimulationStep);
    const auto systemSeed = EvaluateBuiltInOperator("keire.operator.system-seed", context);
    REQUIRE(std::holds_alternative<std::uint64_t>(systemSeed));
    CHECK(std::get<std::uint64_t>(systemSeed) == static_cast<std::uint64_t>(context.EffectSeed ^ context.SeedOffset));
}

TEST_CASE("schema-4 Inline and constant Operators fold typed values without runtime work")
{
    const std::array<std::pair<std::string_view, Keire::VfxParameterValue>, 11> inlineValues{{
        {"keire.operator.inline-color", Keire::Color{0.1F, 0.2F, 0.3F, 0.4F}},
        {"keire.operator.inline-direction", Keire::Vector3{1.0F, 2.0F, 3.0F}},
        {"keire.operator.inline-position", Keire::Vector3{4.0F, 5.0F, 6.0F}},
        {"keire.operator.inline-vector", Keire::Vector3{7.0F, 8.0F, 9.0F}},
        {"keire.operator.inline-vector2", Keire::Vector2{1.0F, 2.0F}},
        {"keire.operator.inline-vector3", Keire::Vector3{1.0F, 2.0F, 3.0F}},
        {"keire.operator.inline-vector4", Keire::Vector4{1.0F, 2.0F, 3.0F, 4.0F}},
        {"keire.operator.inline-bool", true},
        {"keire.operator.inline-float", 12.5F},
        {"keire.operator.inline-int", std::int64_t{-42}},
        {"keire.operator.inline-uint", std::uint64_t{42}},
    }};
    for (const auto& [typeId, value] : inlineValues)
    {
        CAPTURE(typeId);
        CHECK((FoldValueOperator(typeId, {value}) == value));
    }

    const auto epsilon = FoldValueOperator("keire.operator.epsilon", {});
    REQUIRE(std::holds_alternative<float>(epsilon));
    CHECK(std::get<float>(epsilon) == doctest::Approx(0.00001F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.pi", {}, "pi")) ==
          doctest::Approx(3.14159265358979323846F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.pi", {}, "twoPi")) ==
          doctest::Approx(6.28318530717958647692F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.pi", {}, "halfPi")) ==
          doctest::Approx(1.57079632679489661923F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.pi", {}, "thirdPi")) ==
          doctest::Approx(1.04719755119659774615F));
}

TEST_CASE("coordinate conversion and rotation Operators match their documented conventions")
{
    constexpr float HalfPi = 1.57079632679489661923F;
    const auto rectangular = FoldValueOperator("keire.operator.polar-to-rectangular", {90.0F, 2.0F});
    REQUIRE(std::holds_alternative<Keire::Vector2>(rectangular));
    CHECK(std::get<Keire::Vector2>(rectangular).X == doctest::Approx(0.0F).epsilon(0.00001));
    CHECK(std::get<Keire::Vector2>(rectangular).Y == doctest::Approx(2.0F));

    CHECK(std::get<float>(FoldValueOperator("keire.operator.rectangular-to-polar", {Keire::Vector2{0.0F, 2.0F}},
                                            "theta")) == doctest::Approx(HalfPi));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.rectangular-to-polar", {Keire::Vector2{0.0F, 2.0F}},
                                            "distance")) == doctest::Approx(2.0F));

    const auto cartesian = FoldValueOperator("keire.operator.spherical-to-rectangular", {2.0F, HalfPi, 0.0F});
    REQUIRE(std::holds_alternative<Keire::Vector3>(cartesian));
    CHECK(std::get<Keire::Vector3>(cartesian).X == doctest::Approx(0.0F).epsilon(0.00001));
    CHECK(std::get<Keire::Vector3>(cartesian).Y == doctest::Approx(0.0F));
    CHECK(std::get<Keire::Vector3>(cartesian).Z == doctest::Approx(2.0F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.rectangular-to-spherical",
                                            {Keire::Vector3{0.0F, 0.0F, 2.0F}}, "distance")) == doctest::Approx(2.0F));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.rectangular-to-spherical",
                                            {Keire::Vector3{0.0F, 0.0F, 2.0F}}, "theta")) == doctest::Approx(HalfPi));
    CHECK(std::get<float>(FoldValueOperator("keire.operator.rectangular-to-spherical", {Keire::Vector3{}}, "phi")) ==
          0.0F);

    const auto rotated2D =
        FoldValueOperator("keire.operator.rotate-2d", {Keire::Vector2{1.0F, 0.0F}, Keire::Vector2{}, HalfPi});
    REQUIRE(std::holds_alternative<Keire::Vector2>(rotated2D));
    CHECK(std::get<Keire::Vector2>(rotated2D).X == doctest::Approx(0.0F).epsilon(0.00001));
    CHECK(std::get<Keire::Vector2>(rotated2D).Y == doctest::Approx(1.0F));
    const auto rotated3D =
        FoldValueOperator("keire.operator.rotate-3d", {Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{},
                                                       Keire::Vector3{0.0F, 1.0F, 0.0F}, HalfPi});
    REQUIRE(std::holds_alternative<Keire::Vector3>(rotated3D));
    CHECK(std::get<Keire::Vector3>(rotated3D).X == doctest::Approx(0.0F).epsilon(0.00001));
    CHECK(std::get<Keire::Vector3>(rotated3D).Z == doctest::Approx(1.0F));
}

TEST_CASE("procedural noise Operators are deterministic finite and expose derivative and curl outputs")
{
    const std::vector<Keire::VfxParameterValue> noiseInputs{
        Keire::Vector3{0.25F, -1.5F, 2.75F}, 1.5F, std::int64_t{2}, 0.55F, 2.0F, Keire::Vector2{-2.0F, 3.0F}};
    for (const auto typeId :
         {"keire.operator.value-noise", "keire.operator.perlin-noise", "keire.operator.cellular-noise"})
    {
        CAPTURE(typeId);
        const auto first = FoldValueOperator(typeId, noiseInputs, "out");
        const auto second = FoldValueOperator(typeId, noiseInputs, "out");
        REQUIRE(std::holds_alternative<float>(first));
        CHECK(first == second);
        CHECK(std::isfinite(std::get<float>(first)));
        CHECK(std::get<float>(first) >= -2.0F);
        CHECK(std::get<float>(first) <= 3.0F);
        const auto derivatives = FoldValueOperator(typeId, noiseInputs, "derivatives");
        REQUIRE(std::holds_alternative<Keire::Vector3>(derivatives));
        const auto analytic = std::get<Keire::Vector3>(derivatives);
        CHECK(Keire::Math::IsFinite(analytic));
        constexpr float DerivativeStep = 0.001F;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            auto negativeInputs = noiseInputs;
            auto positiveInputs = noiseInputs;
            auto negativeCoordinate = std::get<Keire::Vector3>(negativeInputs.front());
            auto positiveCoordinate = negativeCoordinate;
            auto* negativeComponent = axis == 0   ? &negativeCoordinate.X
                                      : axis == 1 ? &negativeCoordinate.Y
                                                  : &negativeCoordinate.Z;
            auto* positiveComponent = axis == 0   ? &positiveCoordinate.X
                                      : axis == 1 ? &positiveCoordinate.Y
                                                  : &positiveCoordinate.Z;
            *negativeComponent -= DerivativeStep;
            *positiveComponent += DerivativeStep;
            negativeInputs.front() = negativeCoordinate;
            positiveInputs.front() = positiveCoordinate;
            const auto negative = std::get<float>(FoldValueOperator(typeId, negativeInputs, "out"));
            const auto positive = std::get<float>(FoldValueOperator(typeId, positiveInputs, "out"));
            const auto numerical = (positive - negative) / (2.0F * DerivativeStep);
            const auto analyticComponent = axis == 0 ? analytic.X : axis == 1 ? analytic.Y : analytic.Z;
            CHECK(std::abs(analyticComponent - numerical) < 0.02F);
        }
    }

    const std::vector<Keire::VfxParameterValue> curlInputs{
        Keire::Vector3{0.25F, -1.5F, 2.75F}, 1.5F, std::int64_t{2}, 0.55F, 2.0F, 0.75F};
    for (const auto typeId :
         {"keire.operator.value-curl-noise", "keire.operator.perlin-curl-noise", "keire.operator.cellular-curl-noise"})
    {
        CAPTURE(typeId);
        const auto curl = FoldValueOperator(typeId, curlInputs);
        REQUIRE(std::holds_alternative<Keire::Vector3>(curl));
        CHECK(Keire::Math::IsFinite(std::get<Keire::Vector3>(curl)));
    }
}

TEST_CASE("particle Attribute Operators read authoritative CPU simulation state")
{
    Keire::Internal::VfxExpressionEvaluationContext context;
    context.EffectSeed = 0x12345678U;
    context.SeedOffset = 0x01020304U;
    context.System = Keire::AssetId::Generate();
    context.Context = Keire::VfxContextType::Update;
    context.ParticleId = 0x1122334455667788ULL;
    context.StripId = 7;
    context.EffectTime = 12.0F;
    context.Age = 2.5F;
    context.Position = {1.0F, 2.0F, 3.0F};
    context.PreviousPosition = {-1.0F, -2.0F, -3.0F};
    context.Velocity = {4.0F, 5.0F, 6.0F};
    context.Rotation = {};
    context.Tint = {0.2F, 0.4F, 0.6F, 0.8F};
    context.Size = 3.5F;
    context.ParticleIndexInStrip = 2;
    context.ParticlesPerStrip = 5;

    CHECK(std::get<bool>(EvaluateBuiltInOperator("keire.operator.attribute-alive", context)));
    CHECK(std::get<float>(EvaluateBuiltInOperator("keire.operator.attribute-alpha", context)) == doctest::Approx(0.8F));
    CHECK(std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-angle", context)) ==
          Keire::Vector3{});
    CHECK((std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-axis-x", context)) ==
           Keire::Vector3{1.0F, 0.0F, 0.0F}));
    CHECK((std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-axis-y", context)) ==
           Keire::Vector3{0.0F, 1.0F, 0.0F}));
    CHECK((std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-axis-z", context)) ==
           Keire::Vector3{0.0F, 0.0F, 1.0F}));
    CHECK((std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-color", context)) ==
           Keire::Vector3{0.2F, 0.4F, 0.6F}));
    CHECK(std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-old-position", context)) ==
          context.PreviousPosition);
    CHECK(std::get<std::uint64_t>(
              EvaluateBuiltInOperator("keire.operator.attribute-particle-count-in-strip", context)) == 5);
    CHECK(std::get<std::uint64_t>(
              EvaluateBuiltInOperator("keire.operator.attribute-particle-index-in-strip", context)) == 2);
    CHECK(std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-position", context)) ==
          context.Position);
    const auto seed = EvaluateBuiltInOperator("keire.operator.attribute-seed", context);
    CHECK(seed == EvaluateBuiltInOperator("keire.operator.attribute-seed", context));
    CHECK(std::get<float>(EvaluateBuiltInOperator("keire.operator.attribute-size", context)) == doctest::Approx(3.5F));
    CHECK(std::get<float>(EvaluateBuiltInOperator("keire.operator.attribute-spawn-time", context)) ==
          doctest::Approx(9.5F));
    CHECK(std::get<std::uint64_t>(EvaluateBuiltInOperator("keire.operator.attribute-strip-index", context)) == 7);
    CHECK(std::get<Keire::Vector3>(EvaluateBuiltInOperator("keire.operator.attribute-velocity", context)) ==
          context.Velocity);
    CHECK(std::get<float>(EvaluateBuiltInOperator("keire.operator.ratio-over-strip", context)) ==
          doctest::Approx(0.5F));
}

TEST_CASE("executable value Operators publish an accurate CPU and GPU support tier")
{
    static_assert(static_cast<std::uint8_t>(Keire::VfxValueOpcode::Rotate3D) == 108);
    static_assert(static_cast<std::uint8_t>(Keire::VfxValueOpcode::SpawnState) == 131);
    std::size_t executableCount = 0;
    std::size_t gpuInterpretedCount = 0;
    for (const auto& descriptor : Keire::VfxNodeCatalog())
    {
        const auto executable = descriptor.Class == Keire::VfxNodeClass::Operator && descriptor.Lowering &&
                                (descriptor.SupportTier == Keire::VfxNodeSupportTier::Supported ||
                                 descriptor.SupportTier == Keire::VfxNodeSupportTier::KeireEquivalent);
        if (!executable)
            continue;
        ++executableCount;

        const auto opcodeSupported = static_cast<std::uint8_t>(*descriptor.Lowering) <=
                                     static_cast<std::uint8_t>(Keire::VfxValueOpcode::SpawnState);
        const auto typesSupported = std::ranges::all_of(descriptor.Pins, [](const Keire::VfxNodePinDescriptor& pin)
                                                        { return Keire::IsVfxGpuExpressionValueType(pin.Type); });
        CAPTURE(descriptor.TypeId.Value);
        if (descriptor.BackendTier == Keire::VfxNodeBackendTier::CpuAndGpu)
        {
            REQUIRE(opcodeSupported);
            REQUIRE(typesSupported);
            ++gpuInterpretedCount;
        }
        else
        {
            CHECK(descriptor.BackendTier == Keire::VfxNodeBackendTier::CpuOnly);
            const auto fullyGpuInterpreted = opcodeSupported && typesSupported;
            CHECK_FALSE(fullyGpuInterpreted);
        }
    }

    CHECK(executableCount > 0);
    CHECK(gpuInterpretedCount > 0);
    CHECK(gpuInterpretedCount < executableCount);
}

TEST_CASE("schema-4 VFX resident bytes include nested graph and typed-value storage")
{
    const auto baselineDefinition = ResidentBytesEffect(2, 2, "Size = Input;");
    const Keire::VfxEffectAsset baseline(baselineDefinition);
    CHECK(baseline.ResidentBytes() == ExpectedResidentBytes(baseline));

    SUBCASE("curve and gradient key storage has an exact additive cost")
    {
        const auto richerDefinition = ResidentBytesEffect(7, 6, "Size = Input;");
        const Keire::VfxEffectAsset richer(richerDefinition);
        const auto expectedDelta = (7U - 2U) * sizeof(Keire::CurveKey) + (6U - 2U) * sizeof(Keire::ColorGradientKey);

        REQUIRE(richer.ResidentBytes() > baseline.ResidentBytes());
        CHECK(richer.ResidentBytes() - baseline.ResidentBytes() == expectedDelta);
        CHECK(richer.ResidentBytes() == ExpectedResidentBytes(richer));
    }

    SUBCASE("Portable Custom HLSL Block source uses the owned string capacity")
    {
        auto paddedSource = std::string(2048, ' ');
        paddedSource += "Size = Input;";
        const auto richerDefinition = ResidentBytesEffect(2, 2, std::move(paddedSource));
        const Keire::VfxEffectAsset richer(richerDefinition);
        const auto baselineCapacity = PortableSource(baseline).capacity();
        const auto richerCapacity = PortableSource(richer).capacity();

        REQUIRE(richerCapacity > baselineCapacity);
        CHECK(richer.ResidentBytes() - baseline.ResidentBytes() == richerCapacity - baselineCapacity);
        CHECK(richer.ResidentBytes() == ExpectedResidentBytes(richer));
    }

    SUBCASE("first-class ranges remain inline in Blackboard value storage")
    {
        auto scalarDefinition = Keire::VfxEffectAsset::DefaultDefinition();
        scalarDefinition.Blackboard.push_back(
            {ResidentId(10), "Inline value storage", Keire::VfxValueType::Scalar, 1.0F, true});
        auto rangeDefinition = scalarDefinition;
        rangeDefinition.Blackboard.front().Type = Keire::VfxValueType::ScalarRange;
        rangeDefinition.Blackboard.front().DefaultValue = Keire::VfxScalarRange{1.0F, 20.0F};

        const Keire::VfxEffectAsset scalar(scalarDefinition);
        const Keire::VfxEffectAsset range(rangeDefinition);
        CHECK(scalar.ResidentBytes() == range.ResidentBytes());
        CHECK(range.ResidentBytes() == ExpectedResidentBytes(range));
    }
}

TEST_CASE("Range and Random Range lower once for fan-out and execute per spawn on CPU and GPU")
{
    const auto definition = RandomRangeLifetimeEffect();
    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    CHECK(program.ValueInstructions.front().Opcode == Keire::VfxValueOpcode::RandomRange);
    CHECK(program.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerSpawn);
    CHECK(program.ValueInstructions.front().Inputs.size() == 1);
    CHECK(program.ValueInstructions.front().Inputs.front().Kind == Keire::VfxCompiledValueSourceKind::Literal);
    CHECK((std::get<Keire::VfxScalarRange>(program.ValueInstructions.front().Inputs.front().Literal) ==
           Keire::VfxScalarRange{2.0F, 2.0F}));
    REQUIRE(program.Bindings.size() == 2);
    CHECK(program.Bindings[0].ValueRegister == program.Bindings[1].ValueRegister);

    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(gpu.Valid);
    REQUIRE(gpu.ValueInstructions.size() == 1);
    CHECK(gpu.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerSpawn);

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    const auto handle = world->Activate({effect});
    REQUIRE(handle);
    world->Update(0.1F);
    world->Update(0.1F);
    world->Update(0.9F);
    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.EffectCount == 1);
    CHECK(snapshot.Effects[handle.Index()].ActiveParticles == 2);
}

TEST_CASE("GPU operation payloads consume per-frame and particle-domain value programs")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = true;
    definition.Capacity = 16;

    auto time = Keire::CreateVfxGraphOperatorNode("keire.operator.time", {-360.0F, 120.0F});
    time.Context = Keire::VfxContextType::Update;
    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(time));
    auto& timeNode = system.Nodes.back();
    auto& updateContext = ContextNode(definition, Keire::VfxContextType::Update);
    const auto size = std::ranges::find(updateContext.Blocks, ModuleId(definition, Keire::VfxSizeOverLifetimeModule{}),
                                        &Keire::VfxGraphBlock::Reference);
    REQUIRE(size != updateContext.Blocks.end());
    Connect(system, timeNode, "out", updateContext, *size, "size");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    CHECK(program.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerFrame);

    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.25F);
    world->Update(0.25F);
    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    REQUIRE(snapshot.GpuEmitters().front().Execution);
    CHECK(std::ranges::any_of(snapshot.GpuEmitters().front().Execution->ModuleProperties,
                              [](const Keire::VfxGpuModuleProperty& property)
                              {
                                  return property.Property == Keire::VfxModuleProperty::SizeConstant &&
                                         property.Source == Keire::VfxGpuModulePropertySource::Register;
                              }));

    auto particleDefinition = Keire::VfxEffectAsset::DefaultDefinition();
    auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age", {-360.0F, 120.0F});
    age.Context = Keire::VfxContextType::Update;
    auto& particleSystem = particleDefinition.Systems.front();
    particleSystem.Nodes.push_back(std::move(age));
    auto& ageNode = particleSystem.Nodes.back();
    auto& particleUpdate = ContextNode(particleDefinition, Keire::VfxContextType::Update);
    const auto particleSize =
        std::ranges::find(particleUpdate.Blocks, ModuleId(particleDefinition, Keire::VfxSizeOverLifetimeModule{}),
                          &Keire::VfxGraphBlock::Reference);
    REQUIRE(particleSize != particleUpdate.Blocks.end());
    Connect(particleSystem, ageNode, "out", particleUpdate, *particleSize, "size");

    const auto particleProgram = Keire::CompileVfxEffect(particleDefinition, Keire::VfxBackend::Gpu);
    REQUIRE(particleProgram.Valid);
    REQUIRE(particleProgram.ValueInstructions.size() == 1);
    CHECK(particleProgram.ValueInstructions.front().Node == ageNode.Id);
    CHECK(particleProgram.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerParticleUpdate);
}

TEST_CASE("schema-4 Context Blocks, rather than free-flow Module nodes, drive particle execution")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = false;
    definition.Duration = 0.2F;
    definition.Capacity = 16;
    const auto emissionId = ModuleId(definition, Keire::VfxEmissionRateModule{});
    std::get<Keire::VfxEmissionRateModule>(
        std::ranges::find(definition.Modules, emissionId, &Keire::VfxModuleDefinition::Id)->Payload)
        .ParticlesPerSecond = 10.0F;
    ConvertModulesToContextBlocks(definition);
    for (auto& module : definition.Modules)
    {
        if (auto* emission = std::get_if<Keire::VfxEmissionRateModule>(&module.Payload))
            emission->ParticlesPerSecond = 0.0F;
        module.Enabled = false;
    }

    auto range = Keire::CreateVfxGraphOperatorNode("keire.operator.range", {-500.0F, 200.0F});
    range.Context = Keire::VfxContextType::Initialize;
    Pin(range, "minimum", true).DefaultValue = 2.0F;
    Pin(range, "maximum", true).DefaultValue = 2.0F;
    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-range", {-250.0F, 200.0F});
    random.Context = Keire::VfxContextType::Initialize;
    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(range));
    system.Nodes.push_back(std::move(random));
    auto& rangeNode = system.Nodes[system.Nodes.size() - 2];
    auto& randomNode = system.Nodes.back();
    Connect(system, rangeNode, "range", randomNode, "range");

    auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
    const auto initializeId = ModuleId(definition, Keire::VfxInitializeModule{});
    const auto initializeBlock =
        std::ranges::find(initializeContext.Blocks, initializeId, &Keire::VfxGraphBlock::Reference);
    REQUIRE(initializeBlock != initializeContext.Blocks.end());
    Connect(system, randomNode, "out", initializeContext, *initializeBlock, "lifetimeMinimum");
    Connect(system, randomNode, "out", initializeContext, *initializeBlock, "lifetimeMaximum");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    CHECK(program.ValueInstructions.front().Opcode == Keire::VfxValueOpcode::RandomRange);
    REQUIRE(program.Bindings.size() == 3);
    const auto emissionBinding = std::ranges::find(
        program.Bindings, Keire::VfxModuleProperty::EmissionParticlesPerSecond, &Keire::VfxCompiledBinding::Property);
    REQUIRE(emissionBinding != program.Bindings.end());
    REQUIRE(emissionBinding->LiteralValue.has_value());
    CHECK(std::get<float>(*emissionBinding->LiteralValue) == doctest::Approx(10.0F));
    CHECK(std::ranges::count(program.Bindings, initializeBlock->Id, &Keire::VfxCompiledBinding::Node) == 2);
    CHECK(std::ranges::all_of(definition.Modules,
                              [](const Keire::VfxModuleDefinition& module) { return !module.Enabled; }));
    CHECK(std::ranges::none_of(system.Nodes, [](const Keire::VfxGraphNode& node)
                               { return node.Kind == Keire::VfxGraphNodeKind::Module; }));
    const auto enabledBlocks = std::accumulate(
        system.Nodes.begin(), system.Nodes.end(), std::size_t{0},
        [](const std::size_t count, const auto& node)
        {
            return count + static_cast<std::size_t>(std::ranges::count_if(
                               node.Blocks, [](const Keire::VfxGraphBlock& block) { return block.Enabled; }));
        });
    CHECK(program.Modules.size() == enabledBlocks);

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    const auto handle = world->Activate({effect});
    REQUIRE(handle);
    world->Update(0.1F);
    world->Update(0.1F);
    world->Update(0.9F);
    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.EffectCount == 1);
    CHECK(snapshot.Effects[handle.Index()].ActiveParticles == 2);
}

TEST_CASE("pure VFX Operators constant-fold into literal module bindings")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto add = Keire::CreateVfxGraphOperatorNode("keire.operator.add", {-250.0F, -180.0F});
    add.Context = Keire::VfxContextType::Spawn;
    Pin(add, "a", true).DefaultValue = 4.0F;
    Pin(add, "b", true).DefaultValue = 6.0F;

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(add));
    auto& addNode = system.Nodes.back();
    const auto emissionId = ModuleId(definition, Keire::VfxEmissionRateModule{});
    auto& spawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    const auto emission = std::ranges::find(spawnContext.Blocks, emissionId, &Keire::VfxGraphBlock::Reference);
    REQUIRE(emission != spawnContext.Blocks.end());
    Connect(system, addNode, "out", spawnContext, *emission, "particlesPerSecond");

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(cpu.Valid);
    CHECK(cpu.ValueInstructions.empty());
    REQUIRE(cpu.Bindings.size() == 1);
    REQUIRE(cpu.Bindings.front().LiteralValue.has_value());
    CHECK(std::get<float>(*cpu.Bindings.front().LiteralValue) == doctest::Approx(10.0F));

    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(gpu.Valid);
    CHECK(gpu.Hash == cpu.Hash);
}

TEST_CASE("schema-4 catalog exposes the scalar Unity math Operator slice")
{
    struct ExpectedOperator
    {
        std::string_view TypeId;
        std::string_view Label;
        std::string_view Category;
        Keire::VfxValueOpcode Opcode;
        std::size_t InputCount;
    };
    const std::array expected{
        ExpectedOperator{"keire.operator.sine", "Sine", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::Sine, 1},
        ExpectedOperator{"keire.operator.cosine", "Cosine", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::Cosine,
                         1},
        ExpectedOperator{"keire.operator.tangent", "Tangent", "Operator/Math/Trigonometry",
                         Keire::VfxValueOpcode::Tangent, 1},
        ExpectedOperator{"keire.operator.asin", "Asin", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::ArcSine,
                         1},
        ExpectedOperator{"keire.operator.acos", "Acos", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::ArcCosine,
                         1},
        ExpectedOperator{"keire.operator.atan", "Atan", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::ArcTangent,
                         1},
        ExpectedOperator{"keire.operator.atan2", "Atan2", "Operator/Math/Trigonometry", Keire::VfxValueOpcode::Atan2,
                         2},
        ExpectedOperator{"keire.operator.power", "Power", "Operator/Math/Arithmetic", Keire::VfxValueOpcode::Power, 2},
        ExpectedOperator{"keire.operator.square-root", "Square Root", "Operator/Math/Arithmetic",
                         Keire::VfxValueOpcode::SquareRoot, 1},
        ExpectedOperator{"keire.operator.exponential", "Exp", "Operator/Math", Keire::VfxValueOpcode::Exponential, 1},
        ExpectedOperator{"keire.operator.logarithm", "Log", "Operator/Math", Keire::VfxValueOpcode::Logarithm, 1},
        ExpectedOperator{"keire.operator.log2", "Log2", "Operator/Math", Keire::VfxValueOpcode::LogarithmBase2, 1},
        ExpectedOperator{"keire.operator.log10", "Log10", "Operator/Math", Keire::VfxValueOpcode::LogarithmBase10, 1},
        ExpectedOperator{"keire.operator.ceiling", "Ceiling", "Operator/Math/Clamp", Keire::VfxValueOpcode::Ceiling, 1},
        ExpectedOperator{"keire.operator.floor", "Floor", "Operator/Math/Clamp", Keire::VfxValueOpcode::Floor, 1},
        ExpectedOperator{"keire.operator.round", "Round", "Operator/Math/Clamp", Keire::VfxValueOpcode::Round, 1},
        ExpectedOperator{"keire.operator.fractional", "Fractional", "Operator/Math/Arithmetic",
                         Keire::VfxValueOpcode::Fractional, 1},
        ExpectedOperator{"keire.operator.lerp", "Lerp", "Operator/Math/Arithmetic", Keire::VfxValueOpcode::Lerp, 3},
        ExpectedOperator{"keire.operator.smoothstep", "Smoothstep", "Operator/Math/Arithmetic",
                         Keire::VfxValueOpcode::Smoothstep, 3},
        ExpectedOperator{"keire.operator.step", "Step", "Operator/Math/Arithmetic", Keire::VfxValueOpcode::Step, 2},
        ExpectedOperator{"keire.operator.negate", "Negate (-x)", "Operator/Math/Arithmetic",
                         Keire::VfxValueOpcode::Negate, 1},
        ExpectedOperator{"keire.operator.sign", "Sign", "Operator/Math/Arithmetic", Keire::VfxValueOpcode::Sign, 1},
    };

    for (const auto& item : expected)
    {
        const auto* descriptor = Keire::FindVfxNodeDescriptor(item.TypeId);
        REQUIRE(descriptor != nullptr);
        CHECK(descriptor->Label == item.Label);
        CHECK(descriptor->Category == item.Category);
        CHECK(descriptor->Class == Keire::VfxNodeClass::Operator);
        CHECK(descriptor->SupportTier == Keire::VfxNodeSupportTier::Supported);
        REQUIRE(descriptor->Lowering.has_value());
        CHECK(*descriptor->Lowering == item.Opcode);
        CHECK(std::ranges::count(descriptor->Pins, true, &Keire::VfxNodePinDescriptor::Input) == item.InputCount);
        CHECK(std::ranges::count(descriptor->Pins, false, &Keire::VfxNodePinDescriptor::Input) == 1);
        CHECK(descriptor->Pins.back().Type == Keire::VfxValueType::Scalar);
        CHECK(Keire::CreateVfxGraphOperatorNode(item.TypeId).TypeId.View() == item.TypeId);
    }
}

TEST_CASE("scalar trigonometry Operators fold truth values and contain invalid domains")
{
    constexpr float Pi = 3.14159265358979323846F;
    CHECK(FoldScalarOperator("keire.operator.sine", {Pi * 0.5F}) == doctest::Approx(1.0F));
    CHECK(FoldScalarOperator("keire.operator.cosine", {Pi}) == doctest::Approx(-1.0F));
    CHECK(FoldScalarOperator("keire.operator.tangent", {Pi * 0.25F}) == doctest::Approx(1.0F));
    CHECK(FoldScalarOperator("keire.operator.asin", {1.0F}) == doctest::Approx(Pi * 0.5F));
    CHECK(FoldScalarOperator("keire.operator.acos", {0.0F}) == doctest::Approx(Pi * 0.5F));
    CHECK(FoldScalarOperator("keire.operator.atan", {1.0F}) == doctest::Approx(Pi * 0.25F));
    CHECK(FoldScalarOperator("keire.operator.atan2", {1.0F, 0.0F}) == doctest::Approx(Pi * 0.5F));

    CHECK(FoldScalarOperator("keire.operator.asin", {1.01F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.acos", {-1.01F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.atan2", {0.0F, 0.0F}) == 0.0F);
}

TEST_CASE("scalar exponential rounding and sign Operators fold deterministically")
{
    CHECK(FoldScalarOperator("keire.operator.power", {2.0F, 3.0F}) == doctest::Approx(8.0F));
    CHECK(FoldScalarOperator("keire.operator.power", {-2.0F, 0.5F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.power", {0.0F, -1.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.square-root", {9.0F}) == doctest::Approx(3.0F));
    CHECK(FoldScalarOperator("keire.operator.square-root", {-1.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.exponential", {1.0F}) == doctest::Approx(std::exp(1.0F)));
    CHECK(FoldScalarOperator("keire.operator.exponential", {1000.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.logarithm", {std::exp(1.0F)}) == doctest::Approx(1.0F));
    CHECK(FoldScalarOperator("keire.operator.logarithm", {0.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.log2", {8.0F}) == doctest::Approx(3.0F));
    CHECK(FoldScalarOperator("keire.operator.log10", {100.0F}) == doctest::Approx(2.0F));

    CHECK(FoldScalarOperator("keire.operator.ceiling", {-1.25F}) == -1.0F);
    CHECK(FoldScalarOperator("keire.operator.floor", {-1.25F}) == -2.0F);
    CHECK(FoldScalarOperator("keire.operator.round", {2.5F}) == 2.0F);
    CHECK(FoldScalarOperator("keire.operator.round", {3.5F}) == 4.0F);
    CHECK(FoldScalarOperator("keire.operator.round", {-2.5F}) == -2.0F);
    CHECK(FoldScalarOperator("keire.operator.fractional", {-1.25F}) == doctest::Approx(0.75F));
    CHECK(FoldScalarOperator("keire.operator.negate", {5.0F}) == -5.0F);
    CHECK(FoldScalarOperator("keire.operator.sign", {-8.0F}) == -1.0F);
    CHECK(FoldScalarOperator("keire.operator.sign", {0.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.sign", {8.0F}) == 1.0F);
}

TEST_CASE("scalar interpolation Operators fold truth values and contain degenerate ranges")
{
    CHECK(FoldScalarOperator("keire.operator.lerp", {2.0F, 10.0F, 0.25F}) == doctest::Approx(4.0F));
    CHECK(FoldScalarOperator("keire.operator.lerp", {2.0F, 10.0F, 2.0F}) == doctest::Approx(18.0F));
    CHECK(FoldScalarOperator("keire.operator.smoothstep", {0.0F, 1.0F, 0.5F}) == doctest::Approx(0.5F));
    CHECK(FoldScalarOperator("keire.operator.smoothstep", {0.0F, 1.0F, -1.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.smoothstep", {0.0F, 1.0F, 2.0F}) == 1.0F);
    CHECK(FoldScalarOperator("keire.operator.smoothstep", {1.0F, 1.0F, 1.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.step", {0.5F, 0.49F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.step", {0.5F, 0.5F}) == 1.0F);
}

TEST_CASE("schema-4 catalog exposes scalar equivalents for the complete Unity Wave family")
{
    struct ExpectedWave
    {
        std::string_view TypeId;
        std::string_view Label;
    };
    const std::array expected{
        ExpectedWave{"keire.operator.sawtooth-wave", "Sawtooth Wave"},
        ExpectedWave{"keire.operator.sine-wave", "Sine Wave"},
        ExpectedWave{"keire.operator.square-wave", "Square Wave"},
        ExpectedWave{"keire.operator.triangle-wave", "Triangle Wave"},
    };

    for (const auto& item : expected)
    {
        const auto* descriptor = Keire::FindVfxNodeDescriptor(item.TypeId);
        REQUIRE(descriptor != nullptr);
        CHECK(descriptor->Label == item.Label);
        CHECK(descriptor->Category == "Operator/Math/Wave");
        CHECK(descriptor->Class == Keire::VfxNodeClass::Operator);
        CHECK(descriptor->TypeBehavior == Keire::VfxNodeTypeBehavior::Fixed);
        CHECK(descriptor->SupportTier == Keire::VfxNodeSupportTier::KeireEquivalent);
        CHECK(descriptor->BackendTier == Keire::VfxNodeBackendTier::CpuAndGpu);
        REQUIRE(descriptor->Lowering.has_value());
        CHECK(*descriptor->Lowering == Keire::VfxValueOpcode::Lerp);
        REQUIRE(descriptor->Pins.size() == 5);
        CHECK(descriptor->Pins[0].Semantic == "input");
        CHECK(descriptor->Pins[1].Semantic == "frequency");
        CHECK(descriptor->Pins[2].Semantic == "minimum");
        CHECK(descriptor->Pins[3].Semantic == "maximum");
        CHECK(descriptor->Pins[4].Semantic == "out");
        CHECK(std::get<float>(*descriptor->Pins[0].DefaultValue) == 0.5F);
        CHECK(std::get<float>(*descriptor->Pins[1].DefaultValue) == 1.0F);
        CHECK(std::get<float>(*descriptor->Pins[2].DefaultValue) == 0.0F);
        CHECK(std::get<float>(*descriptor->Pins[3].DefaultValue) == 1.0F);
    }
}

TEST_CASE("Wave Operators match Unity scalar formulas and constant-fold finite edges")
{
    CHECK(FoldScalarOperator("keire.operator.sawtooth-wave", {0.25F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.25F));
    CHECK(FoldScalarOperator("keire.operator.sawtooth-wave", {-0.25F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.75F));
    CHECK(FoldScalarOperator("keire.operator.sawtooth-wave", {0.25F, 2.0F, 10.0F, 20.0F}) == doctest::Approx(15.0F));

    CHECK(FoldScalarOperator("keire.operator.sine-wave", {0.0F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.0F));
    CHECK(FoldScalarOperator("keire.operator.sine-wave", {0.25F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.5F));
    CHECK(FoldScalarOperator("keire.operator.sine-wave", {0.5F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(1.0F));

    CHECK(FoldScalarOperator("keire.operator.square-wave", {0.25F, 1.0F, 0.0F, 1.0F}) == 0.0F);
    CHECK(FoldScalarOperator("keire.operator.square-wave", {0.75F, 1.0F, 0.0F, 1.0F}) == 1.0F);

    CHECK(FoldScalarOperator("keire.operator.triangle-wave", {0.25F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.5F));
    CHECK(FoldScalarOperator("keire.operator.triangle-wave", {0.5F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(1.0F));
    CHECK(FoldScalarOperator("keire.operator.triangle-wave", {0.75F, 1.0F, 0.0F, 1.0F}) == doctest::Approx(0.5F));

    CHECK(FoldScalarOperator("keire.operator.sawtooth-wave", {std::numeric_limits<float>::max(), 2.0F, 3.0F, 7.0F}) ==
          3.0F);
    CHECK(FoldScalarOperator("keire.operator.triangle-wave", {0.31F, 0.0F, 4.0F, 4.0F}) == 4.0F);
}

TEST_CASE("Wave recipes lower to reusable host-uniform primitive instructions")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = true;
    const Keire::VfxBlackboardParameter input{Keire::AssetId::Generate(), "Wave input", Keire::VfxValueType::Scalar,
                                              0.5F, true};
    definition.Blackboard.push_back(input);

    Keire::VfxGraphNode parameter;
    parameter.Id = Keire::AssetId::Generate();
    parameter.Type = input.Name;
    parameter.Context = Keire::VfxContextType::Spawn;
    parameter.Kind = Keire::VfxGraphNodeKind::Parameter;
    parameter.Reference = input.Id;
    parameter.TypeId = {"keire.parameter"};
    parameter.Pins.push_back(
        {Keire::AssetId::Generate(), input.Name, Keire::VfxValueType::Scalar, false, "value", std::nullopt});
    auto wave = Keire::CreateVfxGraphOperatorNode("keire.operator.sawtooth-wave", {-240.0F, -100.0F});
    wave.Context = Keire::VfxContextType::Spawn;
    Pin(wave, "minimum", true).DefaultValue = 0.0F;
    Pin(wave, "maximum", true).DefaultValue = 8.0F;

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(parameter));
    system.Nodes.push_back(std::move(wave));
    auto& parameterNode = system.Nodes[system.Nodes.size() - 2];
    auto& waveNode = system.Nodes.back();
    Connect(system, parameterNode, "value", waveNode, "input");
    auto& spawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    const auto emission = std::ranges::find(spawnContext.Blocks, ModuleId(definition, Keire::VfxEmissionRateModule{}),
                                            &Keire::VfxGraphBlock::Reference);
    REQUIRE(emission != spawnContext.Blocks.end());
    Connect(system, waveNode, "out", spawnContext, *emission, "particlesPerSecond");
    auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
    const auto initialize = std::ranges::find(
        initializeContext.Blocks, ModuleId(definition, Keire::VfxInitializeModule{}), &Keire::VfxGraphBlock::Reference);
    REQUIRE(initialize != initializeContext.Blocks.end());
    Pin(*initialize, "lifetimeMinimum").DefaultValue = 5.0F;
    Pin(*initialize, "lifetimeMaximum").DefaultValue = 5.0F;

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(cpu.Valid);
    const std::array expectedOpcodes{Keire::VfxValueOpcode::Multiply, Keire::VfxValueOpcode::Fractional,
                                     Keire::VfxValueOpcode::Absolute, Keire::VfxValueOpcode::Lerp};
    REQUIRE(cpu.ValueInstructions.size() == expectedOpcodes.size());
    for (std::size_t index = 0; index < expectedOpcodes.size(); ++index)
    {
        CHECK(cpu.ValueInstructions[index].Opcode == expectedOpcodes[index]);
        CHECK(cpu.ValueInstructions[index].Domain == Keire::VfxEvaluationDomain::PerEffect);
        CHECK(cpu.ValueInstructions[index].Node == waveNode.Id);
    }
    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(gpu.Valid);
    CHECK(gpu.ValueInstructions.size() == expectedOpcodes.size());

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);
    world->Update(0.5F);
    CHECK(world->Statistics().ActiveParticles == 2);
    world->SetParameter(handle, input.Id, 0.25F);
    world->Update(0.5F);
    CHECK(world->Statistics().ActiveParticles == 3);
}

TEST_CASE("parameter-driven scalar math stays host-uniform on GPU and evaluates on CPU")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = true;
    const Keire::VfxBlackboardParameter input{Keire::AssetId::Generate(), "Square root input",
                                              Keire::VfxValueType::Scalar, -1.0F, true};
    definition.Blackboard.push_back(input);

    Keire::VfxGraphNode parameter;
    parameter.Id = Keire::AssetId::Generate();
    parameter.Type = input.Name;
    parameter.Context = Keire::VfxContextType::Spawn;
    parameter.Kind = Keire::VfxGraphNodeKind::Parameter;
    parameter.Reference = input.Id;
    parameter.TypeId = {"keire.parameter"};
    parameter.Pins.push_back(
        {Keire::AssetId::Generate(), input.Name, Keire::VfxValueType::Scalar, false, "value", std::nullopt});
    auto squareRoot = Keire::CreateVfxGraphOperatorNode("keire.operator.square-root", {-240.0F, -100.0F});
    squareRoot.Context = Keire::VfxContextType::Spawn;

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(parameter));
    system.Nodes.push_back(std::move(squareRoot));
    auto& parameterNode = system.Nodes[system.Nodes.size() - 2];
    auto& squareRootNode = system.Nodes.back();
    Connect(system, parameterNode, "value", squareRootNode, "input");
    auto& spawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    const auto emission = std::ranges::find(spawnContext.Blocks, ModuleId(definition, Keire::VfxEmissionRateModule{}),
                                            &Keire::VfxGraphBlock::Reference);
    REQUIRE(emission != spawnContext.Blocks.end());
    Connect(system, squareRootNode, "out", spawnContext, *emission, "particlesPerSecond");

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(cpu.Valid);
    REQUIRE(cpu.ValueInstructions.size() == 1);
    CHECK(cpu.ValueInstructions.front().Opcode == Keire::VfxValueOpcode::SquareRoot);
    CHECK(cpu.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerEffect);
    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(gpu.Valid);
    REQUIRE(gpu.ValueInstructions.size() == 1);
    CHECK(gpu.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerEffect);

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);
    world->Update(0.25F);
    CHECK(world->Statistics().ActiveParticles == 0);
    world->SetParameter(handle, input.Id, 16.0F);
    world->Update(0.5F);
    CHECK(world->Statistics().ActiveParticles == 2);
}

TEST_CASE("Vector Operators preserve multi-output identity and fold finite edge cases")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto combine = Keire::CreateVfxGraphOperatorNode("keire.operator.combine-vector3", {-520.0F, 140.0F});
    combine.Context = Keire::VfxContextType::Initialize;
    Pin(combine, "x", true).DefaultValue = 2.0F;
    Pin(combine, "y", true).DefaultValue = 3.0F;
    Pin(combine, "z", true).DefaultValue = 4.0F;
    auto split = Keire::CreateVfxGraphOperatorNode("keire.operator.split-vector3", {-300.0F, 140.0F});
    split.Context = Keire::VfxContextType::Initialize;
    auto normalize = Keire::CreateVfxGraphOperatorNode("keire.operator.normalize", {-300.0F, 320.0F});
    normalize.Context = Keire::VfxContextType::Initialize;
    Pin(normalize, "input", true).DefaultValue = Keire::Vector3{};

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(combine));
    system.Nodes.push_back(std::move(split));
    system.Nodes.push_back(std::move(normalize));
    auto& combineNode = system.Nodes[system.Nodes.size() - 3];
    auto& splitNode = system.Nodes[system.Nodes.size() - 2];
    auto& normalizeNode = system.Nodes.back();
    Connect(system, combineNode, "out", splitNode, "input");

    auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
    const auto shape = std::ranges::find(initializeContext.Blocks, ModuleId(definition, Keire::VfxShapeModule{}),
                                         &Keire::VfxGraphBlock::Reference);
    const auto initialize = std::ranges::find(
        initializeContext.Blocks, ModuleId(definition, Keire::VfxInitializeModule{}), &Keire::VfxGraphBlock::Reference);
    REQUIRE(shape != initializeContext.Blocks.end());
    REQUIRE(initialize != initializeContext.Blocks.end());
    Connect(system, splitNode, "x", initializeContext, *shape, "radius");
    Connect(system, splitNode, "y", initializeContext, *shape, "coneLength");
    Connect(system, normalizeNode, "out", initializeContext, *initialize, "velocityMinimum");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    CHECK(program.ValueInstructions.empty());
    REQUIRE(program.Bindings.size() == 3);
    const auto radius = std::ranges::find(program.Bindings, Keire::VfxModuleProperty::ShapeRadius,
                                          &Keire::VfxCompiledBinding::Property);
    const auto coneLength = std::ranges::find(program.Bindings, Keire::VfxModuleProperty::ShapeConeLength,
                                              &Keire::VfxCompiledBinding::Property);
    const auto velocityMinimum = std::ranges::find(
        program.Bindings, Keire::VfxModuleProperty::InitializeVelocityMinimum, &Keire::VfxCompiledBinding::Property);
    REQUIRE(radius != program.Bindings.end());
    REQUIRE(coneLength != program.Bindings.end());
    REQUIRE(velocityMinimum != program.Bindings.end());
    REQUIRE(radius->LiteralValue.has_value());
    REQUIRE(coneLength->LiteralValue.has_value());
    REQUIRE(velocityMinimum->LiteralValue.has_value());
    CHECK(std::get<float>(*radius->LiteralValue) == doctest::Approx(2.0F));
    CHECK(std::get<float>(*coneLength->LiteralValue) == doctest::Approx(3.0F));
    CHECK((std::get<Keire::Vector3>(*velocityMinimum->LiteralValue) == Keire::Vector3{}));
}

TEST_CASE("Combine and Split Operators cover every packed vector and color type")
{
    const auto check = [](const std::string_view combineType, const std::string_view splitType,
                          const std::span<const float> components, const std::string_view outputSemantic,
                          const float expected)
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto combine = Keire::CreateVfxGraphOperatorNode(combineType, {-520.0F, 140.0F});
        auto split = Keire::CreateVfxGraphOperatorNode(splitType, {-300.0F, 140.0F});
        combine.Context = Keire::VfxContextType::Initialize;
        split.Context = Keire::VfxContextType::Initialize;

        std::size_t component = 0;
        for (auto& pin : combine.Pins)
        {
            if (!pin.Input)
                continue;
            REQUIRE(component < components.size());
            pin.DefaultValue = components[component++];
        }
        REQUIRE(component == components.size());

        auto& system = definition.Systems.front();
        system.Nodes.push_back(std::move(combine));
        system.Nodes.push_back(std::move(split));
        auto& combineNode = system.Nodes[system.Nodes.size() - 2];
        auto& splitNode = system.Nodes.back();
        Connect(system, combineNode, "out", splitNode, "input");

        auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
        const auto shape = std::ranges::find(initializeContext.Blocks, ModuleId(definition, Keire::VfxShapeModule{}),
                                             &Keire::VfxGraphBlock::Reference);
        REQUIRE(shape != initializeContext.Blocks.end());
        Connect(system, splitNode, outputSemantic, initializeContext, *shape, "radius");

        const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        REQUIRE(cpu.Valid);
        CHECK(cpu.ValueInstructions.empty());
        const auto binding = std::ranges::find(cpu.Bindings, Keire::VfxModuleProperty::ShapeRadius,
                                               &Keire::VfxCompiledBinding::Property);
        REQUIRE(binding != cpu.Bindings.end());
        REQUIRE(binding->LiteralValue.has_value());
        CHECK(std::get<float>(*binding->LiteralValue) == doctest::Approx(expected));

        const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        REQUIRE(gpu.Valid);
        CHECK(gpu.ValueInstructions.empty());
    };

    SUBCASE("Vector 2")
    {
        constexpr std::array values{2.0F, 7.0F};
        check("keire.operator.combine-vector2", "keire.operator.split-vector2", values, "y", 7.0F);
    }
    SUBCASE("Vector 3")
    {
        constexpr std::array values{2.0F, 7.0F, 11.0F};
        check("keire.operator.combine-vector3", "keire.operator.split-vector3", values, "z", 11.0F);
    }
    SUBCASE("Vector 4")
    {
        constexpr std::array values{2.0F, 7.0F, 11.0F, 13.0F};
        check("keire.operator.combine-vector4", "keire.operator.split-vector4", values, "w", 13.0F);
    }
    SUBCASE("Color")
    {
        constexpr std::array values{0.1F, 0.2F, 0.3F, 0.4F};
        check("keire.operator.combine-color", "keire.operator.split-color", values, "a", 0.4F);
    }
}

TEST_CASE("Boolean casts and particle built-ins lower with explicit domains")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    ConvertModulesToContextBlocks(definition);
    const Keire::VfxModuleDefinition burstModule{Keire::AssetId::Generate(), true, Keire::VfxBurstModule{}};
    const Keire::VfxModuleDefinition collisionModule{Keire::AssetId::Generate(), true, Keire::VfxCollisionModule{}};
    definition.Modules.push_back(burstModule);
    definition.Modules.push_back(collisionModule);
    auto& spawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    spawnContext.Blocks.push_back(Keire::CreateVfxGraphBlock(burstModule));
    auto& updateContext = ContextNode(definition, Keire::VfxContextType::Update);
    updateContext.Blocks.push_back(Keire::CreateVfxGraphBlock(collisionModule));
    auto booleanAnd = Keire::CreateVfxGraphOperatorNode("keire.operator.and", {-460.0F, 100.0F});
    booleanAnd.Context = Keire::VfxContextType::Update;
    Pin(booleanAnd, "a", true).DefaultValue = true;
    Pin(booleanAnd, "b", true).DefaultValue = false;
    auto cast = Keire::CreateVfxGraphOperatorNode("keire.operator.float-to-integer", {-460.0F, -240.0F});
    cast.Context = Keire::VfxContextType::Spawn;
    Pin(cast, "input", true).DefaultValue = 6.9F;
    auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age", {-460.0F, 300.0F});
    age.Context = Keire::VfxContextType::Update;

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(booleanAnd));
    system.Nodes.push_back(std::move(cast));
    system.Nodes.push_back(std::move(age));
    auto& andNode = system.Nodes[system.Nodes.size() - 3];
    auto& castNode = system.Nodes[system.Nodes.size() - 2];
    auto& ageNode = system.Nodes.back();
    auto& connectedSpawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    auto& connectedUpdateContext = ContextNode(definition, Keire::VfxContextType::Update);
    const auto collisionBlock =
        std::ranges::find(connectedUpdateContext.Blocks, collisionModule.Id, &Keire::VfxGraphBlock::Reference);
    const auto burstBlock =
        std::ranges::find(connectedSpawnContext.Blocks, burstModule.Id, &Keire::VfxGraphBlock::Reference);
    const auto sizeId = ModuleId(definition, Keire::VfxSizeOverLifetimeModule{});
    const auto sizeBlock = std::ranges::find(connectedUpdateContext.Blocks, sizeId, &Keire::VfxGraphBlock::Reference);
    REQUIRE(collisionBlock != connectedUpdateContext.Blocks.end());
    REQUIRE(burstBlock != connectedSpawnContext.Blocks.end());
    REQUIRE(sizeBlock != connectedUpdateContext.Blocks.end());
    Connect(system, andNode, "out", connectedUpdateContext, *collisionBlock, "killOnCollision");
    Connect(system, castNode, "out", connectedSpawnContext, *burstBlock, "count");
    Connect(system, ageNode, "out", connectedUpdateContext, *sizeBlock, "size");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    CHECK(program.ValueInstructions.front().Opcode == Keire::VfxValueOpcode::Age);
    CHECK(program.ValueInstructions.front().Domain == Keire::VfxEvaluationDomain::PerParticleUpdate);
    const auto kill = std::ranges::find(program.Bindings, Keire::VfxModuleProperty::CollisionKillOnCollision,
                                        &Keire::VfxCompiledBinding::Property);
    const auto count =
        std::ranges::find(program.Bindings, Keire::VfxModuleProperty::BurstCount, &Keire::VfxCompiledBinding::Property);
    REQUIRE(kill != program.Bindings.end());
    REQUIRE(count != program.Bindings.end());
    REQUIRE(kill->LiteralValue.has_value());
    REQUIRE(count->LiteralValue.has_value());
    CHECK_FALSE(std::get<bool>(*kill->LiteralValue));
    CHECK(std::get<std::int64_t>(*count->LiteralValue) == 6);
}

TEST_CASE("Unity-labelled integer Random keeps its maximum-exclusive contract")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = false;
    definition.Duration = 0.2F;
    std::get<Keire::VfxEmissionRateModule>(std::ranges::find(definition.Modules,
                                                             ModuleId(definition, Keire::VfxEmissionRateModule{}),
                                                             &Keire::VfxModuleDefinition::Id)
                                               ->Payload)
        .ParticlesPerSecond = 0.0F;
    ConvertModulesToContextBlocks(definition);
    const Keire::VfxModuleDefinition burstModule{Keire::AssetId::Generate(), true, Keire::VfxBurstModule{}};
    definition.Modules.push_back(burstModule);
    ContextNode(definition, Keire::VfxContextType::Spawn).Blocks.push_back(Keire::CreateVfxGraphBlock(burstModule));

    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-integer", {-300.0F, -120.0F});
    random.Context = Keire::VfxContextType::Spawn;
    Pin(random, "minimum", true).DefaultValue = std::int64_t{1};
    Pin(random, "maximum", true).DefaultValue = std::int64_t{2};
    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(random));
    auto& randomNode = system.Nodes.back();
    auto& spawnContext = ContextNode(definition, Keire::VfxContextType::Spawn);
    const auto burstBlock = std::ranges::find(spawnContext.Blocks, burstModule.Id, &Keire::VfxGraphBlock::Reference);
    REQUIRE(burstBlock != spawnContext.Blocks.end());
    Connect(system, randomNode, "out", spawnContext, *burstBlock, "count");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    CHECK(program.ValueInstructions.front().Opcode == Keire::VfxValueOpcode::Random);
    CHECK(program.ValueInstructions.front().Type == Keire::VfxValueType::Integer);

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);
    world->Update(0.1F);
    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.EffectCount == 1);
    CHECK(snapshot.Effects[handle.Index()].ActiveParticles == 1);
}

TEST_CASE("VFX range values are first-class Blackboard and scene override values")
{
    const Keire::VfxParameterValue scalarRange = Keire::VfxScalarRange{20.0F, 1.0F};
    CHECK(Keire::VfxValueMatchesType(Keire::VfxValueType::ScalarRange, scalarRange));
    CHECK(Keire::IsFiniteVfxValue(scalarRange));
    CHECK(std::holds_alternative<Keire::VfxVector3Range>(Keire::DefaultVfxValue(Keire::VfxValueType::Vector3Range)));
}

TEST_CASE("schema-4 catalog exposes every core Range and Random value family")
{
    struct RangeFamily
    {
        std::string_view RangeId;
        std::string_view RandomId;
        std::string_view RandomRangeId;
        Keire::VfxValueType ValueType;
        Keire::VfxValueType RangeType;
    };
    const std::array families{
        RangeFamily{"keire.operator.range", "keire.operator.random", "keire.operator.random-range",
                    Keire::VfxValueType::Scalar, Keire::VfxValueType::ScalarRange},
        RangeFamily{"keire.operator.integer-range", "keire.operator.random-integer",
                    "keire.operator.random-integer-range", Keire::VfxValueType::Integer,
                    Keire::VfxValueType::IntegerRange},
        RangeFamily{"keire.operator.unsigned-integer-range", "keire.operator.random-unsigned-integer",
                    "keire.operator.random-unsigned-integer-range", Keire::VfxValueType::UnsignedInteger,
                    Keire::VfxValueType::UnsignedIntegerRange},
        RangeFamily{"keire.operator.vector2-range", "keire.operator.random-vector2",
                    "keire.operator.random-vector2-range", Keire::VfxValueType::Vector2,
                    Keire::VfxValueType::Vector2Range},
        RangeFamily{"keire.operator.vector3-range", "keire.operator.random-vector3",
                    "keire.operator.random-vector3-range", Keire::VfxValueType::Vector3,
                    Keire::VfxValueType::Vector3Range},
        RangeFamily{"keire.operator.vector4-range", "keire.operator.random-vector4",
                    "keire.operator.random-vector4-range", Keire::VfxValueType::Vector4,
                    Keire::VfxValueType::Vector4Range},
        RangeFamily{"keire.operator.color-range", "keire.operator.random-color", "keire.operator.random-color-range",
                    Keire::VfxValueType::Color, Keire::VfxValueType::ColorRange},
    };

    for (const auto& family : families)
    {
        const auto* range = Keire::FindVfxNodeDescriptor(family.RangeId);
        const auto* random = Keire::FindVfxNodeDescriptor(family.RandomId);
        const auto* randomRange = Keire::FindVfxNodeDescriptor(family.RandomRangeId);
        REQUIRE(range != nullptr);
        REQUIRE(random != nullptr);
        REQUIRE(randomRange != nullptr);
        REQUIRE(range->Pins.size() == 3);
        REQUIRE(random->Pins.size() == 3);
        REQUIRE(randomRange->Pins.size() == 2);
        CHECK(range->Pins[0].Type == family.ValueType);
        CHECK(range->Pins[1].Type == family.ValueType);
        CHECK(range->Pins[2].Type == family.RangeType);
        CHECK(random->Pins[0].Type == family.ValueType);
        CHECK(random->Pins[1].Type == family.ValueType);
        CHECK(random->Pins[2].Type == family.ValueType);
        CHECK(randomRange->Pins[0].Type == family.RangeType);
        CHECK(randomRange->Pins[1].Type == family.ValueType);
        CHECK(Keire::CreateVfxGraphOperatorNode(family.RangeId).TypeId.View() == family.RangeId);
        CHECK(Keire::CreateVfxGraphOperatorNode(family.RandomId).TypeId.View() == family.RandomId);
        CHECK(Keire::CreateVfxGraphOperatorNode(family.RandomRangeId).TypeId.View() == family.RandomRangeId);
    }

    const auto* booleanRandom = Keire::FindVfxNodeDescriptor("keire.operator.random-boolean");
    REQUIRE(booleanRandom != nullptr);
    REQUIRE(booleanRandom->Pins.size() == 1);
    CHECK_FALSE(booleanRandom->Pins.front().Input);
    CHECK(booleanRandom->Pins.front().Type == Keire::VfxValueType::Boolean);
}

TEST_CASE("vector and color Random use deterministic optional independent channels")
{
    auto unified = Keire::VfxEffectAsset::DefaultDefinition();
    unified.Loop = true;
    unified.Capacity = 16;
    auto randomVector = Keire::CreateVfxGraphOperatorNode("keire.operator.random-vector3", {-400.0F, 100.0F});
    randomVector.Context = Keire::VfxContextType::Initialize;
    Property(randomVector, "Constant").Value = true;
    Property(randomVector, "Independent Channels").Value = false;
    auto randomColor = Keire::CreateVfxGraphOperatorNode("keire.operator.random-color", {-400.0F, 360.0F});
    randomColor.Context = Keire::VfxContextType::Update;
    Property(randomColor, "Constant").Value = true;
    Property(randomColor, "Independent Channels").Value = false;

    auto& system = unified.Systems.front();
    system.Nodes.push_back(std::move(randomVector));
    system.Nodes.push_back(std::move(randomColor));
    auto& vectorNode = system.Nodes[system.Nodes.size() - 2];
    auto& colorNode = system.Nodes.back();
    auto& initializeContext = ContextNode(unified, Keire::VfxContextType::Initialize);
    const auto initializeBlock = std::ranges::find(
        initializeContext.Blocks, ModuleId(unified, Keire::VfxInitializeModule{}), &Keire::VfxGraphBlock::Reference);
    auto& updateContext = ContextNode(unified, Keire::VfxContextType::Update);
    const auto colorBlock = std::ranges::find(
        updateContext.Blocks, ModuleId(unified, Keire::VfxColorOverLifetimeModule{}), &Keire::VfxGraphBlock::Reference);
    REQUIRE(initializeBlock != initializeContext.Blocks.end());
    REQUIRE(colorBlock != updateContext.Blocks.end());
    Connect(system, vectorNode, "out", initializeContext, *initializeBlock, "velocityMinimum");
    Connect(system, vectorNode, "out", initializeContext, *initializeBlock, "velocityMaximum");
    Connect(system, colorNode, "out", updateContext, *colorBlock, "color");

    auto independent = unified;
    for (auto& node : independent.Systems.front().Nodes)
    {
        if (node.TypeId.View() == "keire.operator.random-vector3" ||
            node.TypeId.View() == "keire.operator.random-color")
        {
            Property(node, "Independent Channels").Value = true;
        }
    }

    const auto unifiedProgram = Keire::CompileVfxEffect(unified, Keire::VfxBackend::Cpu);
    const auto independentProgram = Keire::CompileVfxEffect(independent, Keire::VfxBackend::Cpu);
    REQUIRE(unifiedProgram.Valid);
    REQUIRE(independentProgram.Valid);
    CHECK(unifiedProgram.Hash != independentProgram.Hash);
    REQUIRE(unifiedProgram.ValueInstructions.size() == 2);
    CHECK(std::ranges::all_of(unifiedProgram.ValueInstructions,
                              [](const auto& instruction) { return !instruction.IndependentRandomChannels; }));
    CHECK(std::ranges::all_of(independentProgram.ValueInstructions,
                              [](const auto& instruction) { return instruction.IndependentRandomChannels; }));

    const auto simulate = [](const Keire::VfxEffectDefinition& definition)
    {
        auto world = Keire::CreateRef<Keire::VfxWorld>(
            Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
        const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
        REQUIRE(handle);
        world->Update(0.1F);
        world->Update(0.01F);
        return world->CaptureDebugSnapshot();
    };
    const auto unifiedFirst = simulate(unified);
    const auto unifiedSecond = simulate(unified);
    REQUIRE(unifiedFirst.ParticleCount == 1);
    REQUIRE(unifiedSecond.ParticleCount == 1);
    const auto& unifiedParticle = unifiedFirst.Particles.front();
    CHECK(unifiedParticle.Velocity.X == unifiedParticle.Velocity.Y);
    CHECK(unifiedParticle.Velocity.Y == unifiedParticle.Velocity.Z);
    CHECK(unifiedParticle.Tint.Red == unifiedParticle.Tint.Green);
    CHECK(unifiedParticle.Tint.Green == unifiedParticle.Tint.Blue);
    CHECK(unifiedParticle.Tint.Blue == unifiedParticle.Tint.Alpha);
    CHECK(unifiedParticle.Velocity == unifiedSecond.Particles.front().Velocity);
    CHECK(unifiedParticle.Tint == unifiedSecond.Particles.front().Tint);

    const auto independentSnapshot = simulate(independent);
    REQUIRE(independentSnapshot.ParticleCount == 1);
    const auto& independentParticle = independentSnapshot.Particles.front();
    CHECK((independentParticle.Velocity.X != independentParticle.Velocity.Y ||
           independentParticle.Velocity.Y != independentParticle.Velocity.Z));
    CHECK((independentParticle.Tint.Red != independentParticle.Tint.Green ||
           independentParticle.Tint.Green != independentParticle.Tint.Blue ||
           independentParticle.Tint.Blue != independentParticle.Tint.Alpha));
}

TEST_CASE("typed Range folds before Random Range and reversed vector bounds remain finite")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    definition.Loop = true;
    definition.Capacity = 16;
    auto range = Keire::CreateVfxGraphOperatorNode("keire.operator.vector3-range", {-500.0F, 160.0F});
    range.Context = Keire::VfxContextType::Initialize;
    Pin(range, "minimum", true).DefaultValue = Keire::Vector3{4.0F, 3.0F, 2.0F};
    Pin(range, "maximum", true).DefaultValue = Keire::Vector3{1.0F, 2.0F, 3.0F};
    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-vector3-range", {-250.0F, 160.0F});
    random.Context = Keire::VfxContextType::Initialize;
    Property(random, "Constant").Value = true;

    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(range));
    system.Nodes.push_back(std::move(random));
    auto& rangeNode = system.Nodes[system.Nodes.size() - 2];
    auto& randomNode = system.Nodes.back();
    Connect(system, rangeNode, "range", randomNode, "range");
    auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
    const auto initializeBlock = std::ranges::find(
        initializeContext.Blocks, ModuleId(definition, Keire::VfxInitializeModule{}), &Keire::VfxGraphBlock::Reference);
    REQUIRE(initializeBlock != initializeContext.Blocks.end());
    Connect(system, randomNode, "out", initializeContext, *initializeBlock, "velocityMinimum");
    Connect(system, randomNode, "out", initializeContext, *initializeBlock, "velocityMaximum");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.ValueInstructions.size() == 1);
    const auto& instruction = program.ValueInstructions.front();
    CHECK(instruction.Opcode == Keire::VfxValueOpcode::RandomRange);
    CHECK(instruction.Type == Keire::VfxValueType::Vector3);
    REQUIRE(instruction.Inputs.size() == 1);
    REQUIRE(std::holds_alternative<Keire::VfxVector3Range>(instruction.Inputs.front().Literal));
    CHECK((std::get<Keire::VfxVector3Range>(instruction.Inputs.front().Literal) ==
           Keire::VfxVector3Range{{4.0F, 3.0F, 2.0F}, {1.0F, 2.0F, 3.0F}}));

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.1F);
    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.ParticleCount == 1);
    const auto velocity = snapshot.Particles.front().Velocity;
    CHECK(velocity.X >= 1.0F);
    CHECK(velocity.X <= 4.0F);
    CHECK(velocity.Y >= 2.0F);
    CHECK(velocity.Y <= 3.0F);
    CHECK(velocity.Z >= 2.0F);
    CHECK(velocity.Z <= 3.0F);
}

TEST_CASE("unsigned and Boolean Random execute with typed edge contracts")
{
    auto unsignedDefinition = Keire::VfxEffectAsset::DefaultDefinition();
    unsignedDefinition.Loop = true;
    unsignedDefinition.Capacity = 16;
    auto randomUnsigned =
        Keire::CreateVfxGraphOperatorNode("keire.operator.random-unsigned-integer", {-450.0F, -120.0F});
    randomUnsigned.Context = Keire::VfxContextType::Spawn;
    Pin(randomUnsigned, "minimum", true).DefaultValue = std::uint64_t{4};
    Pin(randomUnsigned, "maximum", true).DefaultValue = std::uint64_t{5};
    auto toFloat = Keire::CreateVfxGraphOperatorNode("keire.operator.unsigned-integer-to-float", {-220.0F, -120.0F});
    toFloat.Context = Keire::VfxContextType::Spawn;
    auto& unsignedSystem = unsignedDefinition.Systems.front();
    unsignedSystem.Nodes.push_back(std::move(randomUnsigned));
    unsignedSystem.Nodes.push_back(std::move(toFloat));
    auto& unsignedNode = unsignedSystem.Nodes[unsignedSystem.Nodes.size() - 2];
    auto& castNode = unsignedSystem.Nodes.back();
    Connect(unsignedSystem, unsignedNode, "out", castNode, "input");
    auto& unsignedSpawn = ContextNode(unsignedDefinition, Keire::VfxContextType::Spawn);
    const auto unsignedEmission =
        std::ranges::find(unsignedSpawn.Blocks, ModuleId(unsignedDefinition, Keire::VfxEmissionRateModule{}),
                          &Keire::VfxGraphBlock::Reference);
    REQUIRE(unsignedEmission != unsignedSpawn.Blocks.end());
    Connect(unsignedSystem, castNode, "out", unsignedSpawn, *unsignedEmission, "particlesPerSecond");

    const auto unsignedProgram = Keire::CompileVfxEffect(unsignedDefinition, Keire::VfxBackend::Cpu);
    REQUIRE(unsignedProgram.Valid);
    REQUIRE(unsignedProgram.ValueInstructions.size() == 2);
    CHECK(unsignedProgram.ValueInstructions.front().Type == Keire::VfxValueType::UnsignedInteger);
    auto unsignedWorld =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    REQUIRE(unsignedWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(unsignedDefinition)}));
    unsignedWorld->Update(0.25F);
    CHECK(unsignedWorld->Statistics().ActiveParticles == 1);

    auto booleanDefinition = Keire::VfxEffectAsset::DefaultDefinition();
    booleanDefinition.Loop = true;
    booleanDefinition.Capacity = 16;
    auto randomBoolean = Keire::CreateVfxGraphOperatorNode("keire.operator.random-boolean", {-450.0F, -120.0F});
    randomBoolean.Context = Keire::VfxContextType::Spawn;
    auto branch = Keire::CreateVfxGraphOperatorNode("keire.operator.branch", {-220.0F, -120.0F});
    branch.Context = Keire::VfxContextType::Spawn;
    Pin(branch, "true", true).DefaultValue = 4.0F;
    Pin(branch, "false", true).DefaultValue = 4.0F;
    auto& booleanSystem = booleanDefinition.Systems.front();
    booleanSystem.Nodes.push_back(std::move(randomBoolean));
    booleanSystem.Nodes.push_back(std::move(branch));
    auto& booleanNode = booleanSystem.Nodes[booleanSystem.Nodes.size() - 2];
    auto& branchNode = booleanSystem.Nodes.back();
    Connect(booleanSystem, booleanNode, "out", branchNode, "predicate");
    auto& booleanSpawn = ContextNode(booleanDefinition, Keire::VfxContextType::Spawn);
    const auto booleanEmission =
        std::ranges::find(booleanSpawn.Blocks, ModuleId(booleanDefinition, Keire::VfxEmissionRateModule{}),
                          &Keire::VfxGraphBlock::Reference);
    REQUIRE(booleanEmission != booleanSpawn.Blocks.end());
    Connect(booleanSystem, branchNode, "out", booleanSpawn, *booleanEmission, "particlesPerSecond");

    const auto booleanProgram = Keire::CompileVfxEffect(booleanDefinition, Keire::VfxBackend::Cpu);
    REQUIRE(booleanProgram.Valid);
    REQUIRE(booleanProgram.ValueInstructions.size() == 2);
    CHECK(booleanProgram.ValueInstructions.front().Type == Keire::VfxValueType::Boolean);
    auto booleanWorld =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
    REQUIRE(booleanWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(booleanDefinition)}));
    booleanWorld->Update(0.5F);
    CHECK(booleanWorld->Statistics().ActiveParticles == 2);
}

TEST_CASE("disabled Context Block consumers eliminate their Random expression tree")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-vector3", {-300.0F, 140.0F});
    random.Context = Keire::VfxContextType::Initialize;
    auto& system = definition.Systems.front();
    system.Nodes.push_back(std::move(random));
    auto& randomNode = system.Nodes.back();
    auto& initializeContext = ContextNode(definition, Keire::VfxContextType::Initialize);
    const auto initializeBlock = std::ranges::find(
        initializeContext.Blocks, ModuleId(definition, Keire::VfxInitializeModule{}), &Keire::VfxGraphBlock::Reference);
    REQUIRE(initializeBlock != initializeContext.Blocks.end());
    Connect(system, randomNode, "out", initializeContext, *initializeBlock, "velocityMinimum");
    initializeBlock->Enabled = false;

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    CHECK(program.ValueInstructions.empty());
    CHECK(std::ranges::none_of(program.Bindings, [initializeBlock](const Keire::VfxCompiledBinding& binding)
                               { return binding.Node == initializeBlock->Id; }));
}

TEST_CASE("schema-4 validates disconnected Operators against their catalog descriptors")
{
    auto valid = Keire::VfxEffectAsset::DefaultDefinition();
    valid.Systems.front().Nodes.push_back(Keire::CreateVfxGraphOperatorNode("keire.operator.add"));
    CHECK_NOTHROW(Keire::ValidateVfxEffect(valid));

    SUBCASE("stale definition version")
    {
        auto malformed = valid;
        ++malformed.Systems.front().Nodes.back().DefinitionVersion;
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(malformed),
                             "VFX Operator node does not match its catalog descriptor.", std::invalid_argument);
    }
    SUBCASE("non-canonical pin signature")
    {
        auto malformed = valid;
        malformed.Systems.front().Nodes.back().Pins.pop_back();
        malformed.Systems.front().Nodes.back().ResolvedSignature.pop_back();
        CHECK_THROWS_AS(Keire::ValidateVfxEffect(malformed), std::invalid_argument);
    }
    SUBCASE("built-in outside its supported Context")
    {
        auto malformed = Keire::VfxEffectAsset::DefaultDefinition();
        auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age");
        age.Context = Keire::VfxContextType::Spawn;
        malformed.Systems.front().Nodes.push_back(std::move(age));
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(malformed),
                             "VFX Operator node does not match its catalog descriptor.", std::invalid_argument);
    }
    SUBCASE("strip Random requires a Particle Strip system")
    {
        auto malformed = Keire::VfxEffectAsset::DefaultDefinition();
        auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random");
        Property(random, "Scope").Value = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerParticleStrip);
        malformed.Systems.front().Nodes.push_back(std::move(random));
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(malformed),
                             "VFX Random Per Particle Strip scope requires a Particle Strip system.",
                             std::invalid_argument);
        malformed.Systems.front().DataType = Keire::VfxParticleDataType::ParticleStrip;
        malformed.Systems.front().ParticlesPerStrip = 8;
        CHECK_NOTHROW(Keire::ValidateVfxEffect(malformed));
    }
}

TEST_CASE("VFX program hashes include the RNG-relevant system stable ID")
{
    const auto original = RandomRangeLifetimeEffect();
    auto changed = original;
    changed.Systems.front().Id = Keire::AssetId::Generate();

    const auto originalProgram = Keire::CompileVfxEffect(original, Keire::VfxBackend::Cpu);
    const auto changedProgram = Keire::CompileVfxEffect(changed, Keire::VfxBackend::Cpu);
    REQUIRE(originalProgram.Valid);
    REQUIRE(changedProgram.Valid);
    CHECK(originalProgram.Hash != changedProgram.Hash);
    CHECK(originalProgram.CanonicalIr != changedProgram.CanonicalIr);
}

TEST_CASE("runtime Burst value bindings reject dangerous count and cycle values")
{
    const auto simulate = [](const std::string_view semantic, const std::int64_t value)
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        definition.Loop = false;
        definition.Duration = 0.25F;
        definition.Capacity = 16;
        const auto emissionId = ModuleId(definition, Keire::VfxEmissionRateModule{});
        std::get<Keire::VfxEmissionRateModule>(
            std::ranges::find(definition.Modules, emissionId, &Keire::VfxModuleDefinition::Id)->Payload)
            .ParticlesPerSecond = 0.0F;
        auto& spawn = ContextNode(definition, Keire::VfxContextType::Spawn);
        const auto emission = std::ranges::find(spawn.Blocks, emissionId, &Keire::VfxGraphBlock::Reference);
        REQUIRE(emission != spawn.Blocks.end());
        Pin(*emission, "particlesPerSecond").DefaultValue = 0.0F;

        const Keire::VfxModuleDefinition burstModule{Keire::AssetId::Generate(), true, Keire::VfxBurstModule{}};
        definition.Modules.push_back(burstModule);
        spawn.Blocks.push_back(Keire::CreateVfxGraphBlock(burstModule));

        auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-integer");
        random.Context = Keire::VfxContextType::Spawn;
        Pin(random, "minimum", true).DefaultValue = value;
        Pin(random, "maximum", true).DefaultValue = value + 1;
        auto& system = definition.Systems.front();
        system.Nodes.push_back(std::move(random));
        auto& connectedSpawn = ContextNode(definition, Keire::VfxContextType::Spawn);
        const auto burst = std::ranges::find(connectedSpawn.Blocks, burstModule.Id, &Keire::VfxGraphBlock::Reference);
        REQUIRE(burst != connectedSpawn.Blocks.end());
        Connect(system, system.Nodes.back(), "out", connectedSpawn, *burst, semantic);

        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        REQUIRE(program.Valid);
        auto world = Keire::CreateRef<Keire::VfxWorld>(
            Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16});
        const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
        REQUIRE(handle);
        world->Update(0.1F);
        return world->CaptureDebugSnapshot();
    };

    const auto zeroCount = simulate("count", 0);
    REQUIRE(zeroCount.EffectCount == 1);
    CHECK(zeroCount.ParticleCount == 0);
    CHECK(Keire::HasVfxDiagnostic(zeroCount.Effects.front().Diagnostics,
                                  Keire::VfxRuntimeDiagnostic::SimulationValueInvalid));

    const auto excessiveCycles = simulate("cycles", 2'000);
    REQUIRE(excessiveCycles.EffectCount == 1);
    CHECK(excessiveCycles.ParticleCount == 0);
    CHECK(Keire::HasVfxDiagnostic(excessiveCycles.Effects.front().Diagnostics,
                                  Keire::VfxRuntimeDiagnostic::SimulationValueInvalid));
}

TEST_CASE("Portable HLSL Block source uses the document limit instead of the setting-name limit")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto source = std::string("Velocity += float3(0.0, 1.0, 0.0) * DeltaTime;");
    source.append(256, ' ');
    auto& update = ContextNode(definition, Keire::VfxContextType::Update);
    update.Blocks.push_back(Keire::CreateVfxGraphPortableHlslBlock(source));

    CHECK_NOTHROW(Keire::ValidateVfxEffect(definition));
    const auto roundTrip = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(definition));
    REQUIRE(roundTrip);
    const auto& decodedUpdate =
        std::ranges::find_if(roundTrip->Definition().Systems.front().Nodes, [](const Keire::VfxGraphNode& node)
                             { return node.Context == Keire::VfxContextType::Update; });
    REQUIRE(decodedUpdate != roundTrip->Definition().Systems.front().Nodes.end());
    REQUIRE_FALSE(decodedUpdate->Blocks.empty());
    const auto portable = std::ranges::find(decodedUpdate->Blocks, Keire::VfxNodeTypeId{"keire.block.portable-hlsl"},
                                            &Keire::VfxGraphBlock::TypeId);
    REQUIRE(portable != decodedUpdate->Blocks.end());
    REQUIRE(portable->Properties.size() == 1);
    CHECK(std::get<std::string>(portable->Properties.front().Value) == source);
}
