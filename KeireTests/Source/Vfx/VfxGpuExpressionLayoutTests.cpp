#include "Keire/Vfx/VfxSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
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

    [[nodiscard]] Keire::VfxGraphNode& Context(Keire::VfxEffectDefinition& definition,
                                               const Keire::VfxContextType context)
    {
        const auto found =
            std::ranges::find_if(definition.Systems.front().Nodes, [context](const Keire::VfxGraphNode& node)
                                 { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == context; });
        REQUIRE(found != definition.Systems.front().Nodes.end());
        return *found;
    }

    template <typename T>
    [[nodiscard]] Keire::VfxGraphBlock& Block(Keire::VfxEffectDefinition& definition,
                                              const Keire::VfxContextType context)
    {
        const auto module = std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& candidate)
                                                 { return std::holds_alternative<T>(candidate.Payload); });
        REQUIRE(module != definition.Modules.end());
        auto& contextNode = Context(definition, context);
        const auto block = std::ranges::find(contextNode.Blocks, module->Id, &Keire::VfxGraphBlock::Reference);
        REQUIRE(block != contextNode.Blocks.end());
        return *block;
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
        system.Connections.push_back(
            {Keire::AssetId::Generate(), outputNode.Id, output->Id, inputContext.Id, input->Id, {}, inputBlock.Id});
    }

    [[nodiscard]] std::array<std::uint32_t, 4> Identity(const Keire::AssetId id)
    {
        return {static_cast<std::uint32_t>(id.High()), static_cast<std::uint32_t>(id.High() >> 32U),
                static_cast<std::uint32_t>(id.Low()), static_cast<std::uint32_t>(id.Low() >> 32U)};
    }
} // namespace

TEST_CASE("GPU expression compilation preserves typed parameter and literal sources")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& system = definition.Systems.front();
    system.Id = Keire::AssetId(0x1020304050607080ULL, 0x90A0B0C0D0E0F000ULL);

    const Keire::VfxBlackboardParameter parameter{Keire::AssetId::Generate(), "Rate", Keire::VfxValueType::Scalar, 3.0F,
                                                  true};
    definition.Blackboard.push_back(parameter);
    Keire::VfxGraphNode parameterNode;
    parameterNode.Id = Keire::AssetId::Generate();
    parameterNode.Type = parameter.Name;
    parameterNode.Context = Keire::VfxContextType::Spawn;
    parameterNode.Kind = Keire::VfxGraphNodeKind::Parameter;
    parameterNode.Reference = parameter.Id;
    parameterNode.TypeId = {"keire.parameter"};
    parameterNode.Pins.push_back(
        {Keire::AssetId::Generate(), parameter.Name, parameter.Type, false, "value", std::nullopt});

    auto add = Keire::CreateVfxGraphOperatorNode("keire.operator.add");
    add.Id = Keire::AssetId(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    add.Context = Keire::VfxContextType::Spawn;
    Pin(add, "b", true).DefaultValue = 2.25F;
    system.Nodes.push_back(std::move(parameterNode));
    system.Nodes.push_back(std::move(add));
    const auto& storedParameter = system.Nodes[system.Nodes.size() - 2];
    const auto& storedAdd = system.Nodes.back();
    Connect(system, storedParameter, "value", storedAdd, "a");
    auto& spawn = Context(definition, Keire::VfxContextType::Spawn);
    auto& emission = Block<Keire::VfxEmissionRateModule>(definition, Keire::VfxContextType::Spawn);
    Connect(system, storedAdd, "out", spawn, emission, "particlesPerSecond");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    REQUIRE(program.GpuValueProgram.Instructions.size() == 1);
    REQUIRE(program.GpuValueProgram.Sources.size() == 2);
    REQUIRE(program.GpuValueProgram.Constants.size() == 1);
    CHECK(program.GpuValueProgram.RegisterCount == 1);
    CHECK(program.GpuValueProgram.SystemIdentity == Identity(system.Id));

    const auto& instruction = program.GpuValueProgram.Instructions.front();
    CHECK((instruction.Header ==
           std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(Keire::VfxValueOpcode::Add),
                                        static_cast<std::uint32_t>(Keire::VfxValueType::Scalar),
                                        static_cast<std::uint32_t>(Keire::VfxContextType::Spawn),
                                        static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerEffect)}));
    CHECK((instruction.Output == std::array<std::uint32_t, 4>{0, 0, 0, 2}));
    CHECK(instruction.NodeIdentity == Identity(storedAdd.Id));
    CHECK(program.GpuValueProgram.Sources[0].Kind ==
          static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Parameter));
    CHECK(program.GpuValueProgram.Sources[0].Index == 0);
    CHECK(program.GpuValueProgram.Sources[1].Kind == static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Literal));
    CHECK(program.GpuValueProgram.Constants[0].Primary[0] == std::bit_cast<std::uint32_t>(2.25F));
    CHECK((program.GpuValueProgram.Constants[0].Secondary == std::array<std::uint32_t, 4>{}));
}

TEST_CASE("GPU expression compilation packs Range bounds for particle-domain Portable HLSL inputs")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& system = definition.Systems.front();
    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-range");
    random.Context = Keire::VfxContextType::Initialize;
    Pin(random, "range", true).DefaultValue = Keire::VfxScalarRange{1.0F, 20.0F};
    const auto inclusive = std::ranges::find(random.Properties, "Inclusive Maximum", &Keire::VfxGraphProperty::Name);
    REQUIRE(inclusive != random.Properties.end());
    inclusive->Value = true;
    system.Nodes.push_back(std::move(random));
    const auto& storedRandom = system.Nodes.back();

    auto& initialize = Context(definition, Keire::VfxContextType::Initialize);
    auto portable = Keire::CreateVfxGraphPortableHlslBlock("Size = DynamicSize;");
    portable.Pins.push_back(
        {Keire::AssetId::Generate(), "Dynamic Size", Keire::VfxValueType::Scalar, true, "DynamicSize", 1.0F});
    initialize.Blocks.push_back(std::move(portable));
    const auto& storedPortable = initialize.Blocks.back();
    Connect(system, storedRandom, "out", initialize, storedPortable, "DynamicSize");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(program.Valid);
    REQUIRE(program.GpuValueProgram.Instructions.size() == 1);
    REQUIRE(program.GpuValueProgram.Sources.size() == 1);
    REQUIRE(program.GpuValueProgram.Constants.size() == 1);
    const auto& instruction = program.GpuValueProgram.Instructions.front();
    CHECK(instruction.Header[0] == static_cast<std::uint32_t>(Keire::VfxValueOpcode::RandomRange));
    CHECK(instruction.Header[2] == static_cast<std::uint32_t>(Keire::VfxContextType::Initialize));
    CHECK(instruction.Header[3] == static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerSpawn));
    const auto expectedFlags =
        static_cast<std::uint32_t>(Keire::VfxGpuValueInstructionFlag::IndependentRandomChannels) |
        static_cast<std::uint32_t>(Keire::VfxGpuValueInstructionFlag::InclusiveMaximum);
    CHECK(instruction.Settings[2] == expectedFlags);
    CHECK(program.GpuValueProgram.Constants[0].Primary[0] == std::bit_cast<std::uint32_t>(1.0F));
    CHECK(program.GpuValueProgram.Constants[0].Secondary[0] == std::bit_cast<std::uint32_t>(20.0F));
    const auto custom =
        std::ranges::find(program.CustomInstructions, storedPortable.Id, &Keire::VfxCompiledCustomInstruction::Node);
    REQUIRE(custom != program.CustomInstructions.end());
    CHECK(custom->ValueRegister == instruction.Output[0]);
}

TEST_CASE("GPU expression compilation preserves Vector 2 and Vector 4 component operations")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& system = definition.Systems.front();
    const Keire::VfxBlackboardParameter vector{Keire::AssetId::Generate(), "Direction 2D", Keire::VfxValueType::Vector2,
                                               Keire::Vector2{3.0F, 5.0F}, true};
    definition.Blackboard.push_back(vector);

    Keire::VfxGraphNode parameter;
    parameter.Id = Keire::AssetId::Generate();
    parameter.Type = vector.Name;
    parameter.Context = Keire::VfxContextType::Spawn;
    parameter.Kind = Keire::VfxGraphNodeKind::Parameter;
    parameter.Reference = vector.Id;
    parameter.TypeId = {"keire.parameter"};
    parameter.Pins.push_back({Keire::AssetId::Generate(), vector.Name, vector.Type, false, "value", std::nullopt});
    auto splitVector2 = Keire::CreateVfxGraphOperatorNode("keire.operator.split-vector2");
    splitVector2.Context = Keire::VfxContextType::Spawn;
    auto totalTime = Keire::CreateVfxGraphOperatorNode("keire.operator.time");
    totalTime.Context = Keire::VfxContextType::Spawn;
    auto combineVector4 = Keire::CreateVfxGraphOperatorNode("keire.operator.combine-vector4");
    combineVector4.Context = Keire::VfxContextType::Spawn;
    Pin(combineVector4, "z", true).DefaultValue = 7.0F;
    Pin(combineVector4, "w", true).DefaultValue = 11.0F;
    auto splitVector4 = Keire::CreateVfxGraphOperatorNode("keire.operator.split-vector4");
    splitVector4.Context = Keire::VfxContextType::Spawn;

    system.Nodes.push_back(std::move(parameter));
    system.Nodes.push_back(std::move(splitVector2));
    system.Nodes.push_back(std::move(totalTime));
    system.Nodes.push_back(std::move(combineVector4));
    system.Nodes.push_back(std::move(splitVector4));
    const auto& parameterNode = system.Nodes[system.Nodes.size() - 5];
    const auto& splitVector2Node = system.Nodes[system.Nodes.size() - 4];
    const auto& totalTimeNode = system.Nodes[system.Nodes.size() - 3];
    const auto& combineVector4Node = system.Nodes[system.Nodes.size() - 2];
    const auto& splitVector4Node = system.Nodes.back();
    Connect(system, parameterNode, "value", splitVector2Node, "input");
    Connect(system, totalTimeNode, "out", combineVector4Node, "x");
    Connect(system, splitVector2Node, "y", combineVector4Node, "y");
    Connect(system, combineVector4Node, "out", splitVector4Node, "input");
    auto& spawn = Context(definition, Keire::VfxContextType::Spawn);
    auto& emission = Block<Keire::VfxEmissionRateModule>(definition, Keire::VfxContextType::Spawn);
    Connect(system, splitVector4Node, "w", spawn, emission, "particlesPerSecond");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    REQUIRE(program.GpuValueProgram.Instructions.size() == 4);
    CHECK(program.GpuValueProgram.Instructions[0].Header[0] == static_cast<std::uint32_t>(Keire::VfxValueOpcode::Time));
    CHECK(program.GpuValueProgram.Instructions[1].Header[0] ==
          static_cast<std::uint32_t>(Keire::VfxValueOpcode::Split));
    CHECK(program.GpuValueProgram.Instructions[1].Output[1] == 1);
    CHECK(program.GpuValueProgram.Instructions[2].Header[0] ==
          static_cast<std::uint32_t>(Keire::VfxValueOpcode::Combine));
    CHECK(program.GpuValueProgram.Instructions[2].Header[1] ==
          static_cast<std::uint32_t>(Keire::VfxValueType::Vector4));
    CHECK(program.GpuValueProgram.Instructions[2].Output[3] == 4);
    CHECK(program.GpuValueProgram.Instructions[3].Header[0] ==
          static_cast<std::uint32_t>(Keire::VfxValueOpcode::Split));
    CHECK(program.GpuValueProgram.Instructions[3].Output[1] == 3);
}

TEST_CASE("GPU expression layout limits identify the exact overflowing node")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& system = definition.Systems.front();
    system.Nodes.reserve(system.Nodes.size() + Keire::VfxCompiledGpuValueProgram::MaximumRegisters + 1);
    auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age");
    age.Context = Keire::VfxContextType::Update;
    system.Nodes.push_back(std::move(age));
    const Keire::VfxGraphNode* previous = &system.Nodes.back();
    std::vector<Keire::AssetId> addNodes;
    addNodes.reserve(Keire::VfxCompiledGpuValueProgram::MaximumRegisters);
    for (std::size_t index = 0; index < Keire::VfxCompiledGpuValueProgram::MaximumRegisters; ++index)
    {
        auto add = Keire::CreateVfxGraphOperatorNode("keire.operator.add");
        add.Context = Keire::VfxContextType::Update;
        Pin(add, "b", true).DefaultValue = 1.0F;
        system.Nodes.push_back(std::move(add));
        auto& stored = system.Nodes.back();
        addNodes.push_back(stored.Id);
        Connect(system, *previous, "out", stored, "a");
        previous = &stored;
    }

    auto& update = Context(definition, Keire::VfxContextType::Update);
    auto portable = Keire::CreateVfxGraphPortableHlslBlock("Size = DynamicSize;");
    portable.Pins.push_back(
        {Keire::AssetId::Generate(), "Dynamic Size", Keire::VfxValueType::Scalar, true, "DynamicSize", 1.0F});
    update.Blocks.push_back(std::move(portable));
    Connect(system, *previous, "out", update, update.Blocks.back(), "DynamicSize");

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK_FALSE(program.Valid);
    const auto diagnostic =
        std::ranges::find_if(program.Diagnostics,
                             [&addNodes](const Keire::VfxCompileDiagnostic& candidate)
                             {
                                 return candidate.Node == addNodes.back() &&
                                        candidate.Message.find("64-register shader limit") != std::string::npos;
                             });
    CHECK(diagnostic != program.Diagnostics.end());
}
