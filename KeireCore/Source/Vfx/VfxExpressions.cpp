#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include "Keire/Math/Math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace Keire::Internal
{
    namespace
    {
        constexpr std::uint32_t MaximumValueRegisters = 4096;

        enum class WaveOperatorKind : std::uint8_t
        {
            Sawtooth,
            Sine,
            Square,
            Triangle
        };

        [[nodiscard]] std::optional<WaveOperatorKind> WaveOperator(const std::string_view typeId) noexcept
        {
            if (typeId == "keire.operator.sawtooth-wave")
                return WaveOperatorKind::Sawtooth;
            if (typeId == "keire.operator.sine-wave")
                return WaveOperatorKind::Sine;
            if (typeId == "keire.operator.square-wave")
                return WaveOperatorKind::Square;
            if (typeId == "keire.operator.triangle-wave")
                return WaveOperatorKind::Triangle;
            return std::nullopt;
        }
        [[nodiscard]] const VfxGraphProperty* FindProperty(const VfxGraphNode& node, const std::string_view name)
        {
            const auto found = std::ranges::find(node.Properties, name, &VfxGraphProperty::Name);
            return found == node.Properties.end() ? nullptr : std::addressof(*found);
        }
        template <typename T> [[nodiscard]] T Property(const VfxGraphNode& node, const std::string_view name)
        {
            const auto* property = FindProperty(node, name);
            if (!property || !std::holds_alternative<T>(property->Value))
                throw std::invalid_argument("VFX operator property '" + std::string(name) + "' is missing or invalid.");
            return std::get<T>(property->Value);
        }
        [[nodiscard]] VfxEvaluationDomain DomainForContext(const VfxContextType context) noexcept
        {
            switch (context)
            {
            case VfxContextType::Spawn:
            case VfxContextType::Initialize:
                return VfxEvaluationDomain::PerSpawn;
            case VfxContextType::Update:
                return VfxEvaluationDomain::PerParticleUpdate;
            case VfxContextType::Output:
            case VfxContextType::Event:
                return VfxEvaluationDomain::PerOutputEvent;
            }
            return VfxEvaluationDomain::PerEffect;
        }
        [[nodiscard]] bool IsParticleAttributeOpcode(const VfxValueOpcode opcode) noexcept
        {
            return opcode >= VfxValueOpcode::AttributeAlive && opcode <= VfxValueOpcode::RatioOverStrip;
        }

        [[nodiscard]] VfxEvaluationDomain
        SourceDomain(const VfxCompiledValueSource& source,
                     const std::map<std::uint32_t, VfxEvaluationDomain>& registerDomains) noexcept
        {
            switch (source.Kind)
            {
            case VfxCompiledValueSourceKind::Literal:
                return VfxEvaluationDomain::CompileTimeConstant;
            case VfxCompiledValueSourceKind::Parameter:
                return VfxEvaluationDomain::PerEffect;
            case VfxCompiledValueSourceKind::Register:
            {
                const auto found = registerDomains.find(source.Index);
                return found == registerDomains.end() ? VfxEvaluationDomain::PerParticleUpdate : found->second;
            }
            }
            return VfxEvaluationDomain::PerParticleUpdate;
        }

        [[nodiscard]] VfxComparisonCondition ParseComparison(const VfxGraphNode& node)
        {
            const auto value = Property<std::string>(node, "Condition");
            if (value == "Less")
                return VfxComparisonCondition::Less;
            if (value == "Less Or Equal")
                return VfxComparisonCondition::LessOrEqual;
            if (value == "Equal")
                return VfxComparisonCondition::Equal;
            if (value == "Not Equal")
                return VfxComparisonCondition::NotEqual;
            if (value == "Greater Or Equal")
                return VfxComparisonCondition::GreaterOrEqual;
            if (value == "Greater")
                return VfxComparisonCondition::Greater;
            throw std::invalid_argument("VFX Compare operator condition is unsupported.");
        }
    } // namespace

    VfxExpressionCompilation CompileVfxExpressions(const VfxGraphSystem& system,
                                                   const std::map<AssetId, std::uint32_t>& parameterSlots,
                                                   const std::span<const AssetId> requiredOutputPins)
    {
        std::map<AssetId, const VfxGraphNode*> nodes;
        std::map<AssetId, std::pair<const VfxGraphNode*, const VfxGraphPin*>> pins;
        std::map<AssetId, const VfxGraphConnection*> inputDrivers;
        for (const auto& node : system.Nodes)
        {
            nodes.emplace(node.Id, std::addressof(node));
            for (const auto& pin : node.Pins)
                pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
            for (const auto& block : node.Blocks)
                for (const auto& pin : block.Pins)
                    pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
        }
        for (const auto& connection : system.Connections)
            inputDrivers.emplace(connection.InputPin, std::addressof(connection));

        VfxExpressionCompilation result;
        std::map<std::uint32_t, VfxEvaluationDomain> registerDomains;
        std::set<AssetId> compiling;
        std::function<VfxCompiledValueSource(AssetId)> compileOutput;
        compileOutput = [&](const AssetId outputPin) -> VfxCompiledValueSource
        {
            if (const auto cached = result.SourcesByOutputPin.find(outputPin);
                cached != result.SourcesByOutputPin.end())
            {
                return cached->second;
            }
            const auto located = pins.find(outputPin);
            if (located == pins.end() || located->second.second->Input)
                throw std::invalid_argument("VFX value expression references an invalid output pin.");
            const auto& node = *located->second.first;
            const auto& pin = *located->second.second;
            if (node.Kind == VfxGraphNodeKind::Parameter)
            {
                const auto slot = parameterSlots.find(node.Reference);
                if (slot == parameterSlots.end())
                    throw std::invalid_argument("VFX value expression references an unknown Blackboard parameter.");
                VfxCompiledValueSource source{VfxCompiledValueSourceKind::Parameter, pin.Type, slot->second, 0.0F};
                result.SourcesByOutputPin.emplace(outputPin, source);
                return source;
            }
            if (node.Kind != VfxGraphNodeKind::Operator)
                throw std::invalid_argument("VFX data inputs require a Parameter or executable Operator source.");
            if (!compiling.insert(node.Id).second)
                throw std::invalid_argument("VFX value expression contains a cycle.");

            const auto* descriptor = FindVfxNodeDescriptor(node.TypeId.View());
            if (!descriptor || descriptor->Class != VfxNodeClass::Operator || !descriptor->Lowering)
                throw std::invalid_argument("VFX graph contains an unknown executable Operator type ID.");
            if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
                throw std::invalid_argument("VFX Operator is disabled: " + descriptor->DisabledReason);
            if (node.DefinitionVersion != descriptor->DefinitionVersion || node.Pins.size() != descriptor->Pins.size())
                throw std::invalid_argument("VFX Operator node does not match its catalog definition version.");

            std::vector<VfxCompiledValueSource> inputs;
            const VfxGraphPin* output = nullptr;
            std::uint32_t outputIndex = 0;
            std::uint32_t nextOutputIndex = 0;
            for (std::size_t index = 0; index < descriptor->Pins.size(); ++index)
            {
                const auto& expected = descriptor->Pins[index];
                const auto& actual = node.Pins[index];
                if (actual.Name != expected.Name || actual.Semantic != expected.Semantic ||
                    actual.Type != expected.Type || actual.Input != expected.Input)
                {
                    throw std::invalid_argument("VFX Operator node pin signature is not canonical.");
                }
                if (!actual.Input)
                {
                    if (actual.Id == outputPin)
                    {
                        output = std::addressof(actual);
                        outputIndex = nextOutputIndex;
                    }
                    ++nextOutputIndex;
                    continue;
                }
                const auto driver = inputDrivers.find(actual.Id);
                if (driver == inputDrivers.end())
                {
                    if (!actual.DefaultValue || !VfxValueMatchesType(actual.Type, *actual.DefaultValue) ||
                        !IsFiniteVfxValue(*actual.DefaultValue))
                    {
                        throw std::invalid_argument("VFX Operator input has no cable or compatible inline value.");
                    }
                    inputs.push_back({VfxCompiledValueSourceKind::Literal, actual.Type, 0, *actual.DefaultValue});
                }
                else
                {
                    auto source = compileOutput(driver->second->OutputPin);
                    if (source.Type != actual.Type)
                        throw std::invalid_argument("VFX Operator input source type is incompatible.");
                    inputs.push_back(std::move(source));
                }
            }
            if (!output)
                throw std::invalid_argument("VFX Operator has no output pin.");

            if (const auto wave = WaveOperator(node.TypeId.View()))
            {
                if (inputs.size() != 4 || outputIndex != 0 || output->Type != VfxValueType::Scalar)
                    throw std::invalid_argument("VFX Wave Operator signature is not canonical.");

                const auto literal = [](const float value)
                { return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0, value}; };
                const auto emitPrimitive =
                    [&](const VfxValueOpcode opcode, std::vector<VfxCompiledValueSource> primitiveInputs)
                {
                    VfxCompiledValueInstruction primitive;
                    primitive.Node = node.Id;
                    primitive.Opcode = opcode;
                    primitive.Type = VfxValueType::Scalar;
                    primitive.Context = node.Context;
                    primitive.Inputs = std::move(primitiveInputs);
                    primitive.Domain = VfxEvaluationDomain::CompileTimeConstant;
                    for (const auto& input : primitive.Inputs)
                        primitive.Domain = std::max(primitive.Domain, SourceDomain(input, registerDomains));

                    std::optional<VfxParameterValue> folded;
                    if (primitive.Domain == VfxEvaluationDomain::CompileTimeConstant)
                    {
                        std::array<const VfxParameterValue*, 8> values{};
                        if (primitive.Inputs.size() <= values.size() &&
                            std::ranges::all_of(primitive.Inputs, [](const VfxCompiledValueSource& source)
                                                { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                        {
                            for (std::size_t index = 0; index < primitive.Inputs.size(); ++index)
                                values[index] = std::addressof(primitive.Inputs[index].Literal);
                            folded = EvaluateVfxPureExpression(
                                primitive.Opcode,
                                std::span<const VfxParameterValue* const>(values.data(), primitive.Inputs.size()),
                                VfxValueType::Scalar, false, VfxComparisonCondition::Less);
                        }
                    }
                    if (folded)
                    {
                        return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0,
                                                      std::move(*folded)};
                    }

                    if (result.RegisterCount >= MaximumValueRegisters)
                        throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                    primitive.OutputRegister = result.RegisterCount++;
                    registerDomains.emplace(primitive.OutputRegister, primitive.Domain);
                    const VfxCompiledValueSource source{VfxCompiledValueSourceKind::Register, VfxValueType::Scalar,
                                                        primitive.OutputRegister, 0.0F};
                    result.Instructions.push_back(std::move(primitive));
                    return source;
                };

                const auto phase = emitPrimitive(VfxValueOpcode::Multiply, {inputs[0], inputs[1]});
                VfxCompiledValueSource factor;
                switch (*wave)
                {
                case WaveOperatorKind::Sawtooth:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Absolute, {fractional});
                    break;
                }
                case WaveOperatorKind::Sine:
                {
                    constexpr float Tau = 6.28318530717958647692F;
                    const auto angle = emitPrimitive(VfxValueOpcode::Multiply, {phase, literal(Tau)});
                    const auto cosine = emitPrimitive(VfxValueOpcode::Cosine, {angle});
                    const auto numerator = emitPrimitive(VfxValueOpcode::Subtract, {literal(1.0F), cosine});
                    factor = emitPrimitive(VfxValueOpcode::Divide, {numerator, literal(2.0F)});
                    break;
                }
                case WaveOperatorKind::Square:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    break;
                }
                case WaveOperatorKind::Triangle:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    const auto slope = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    const auto delta = emitPrimitive(VfxValueOpcode::Subtract, {slope, fractional});
                    const auto distance = emitPrimitive(VfxValueOpcode::Absolute, {delta});
                    factor = emitPrimitive(VfxValueOpcode::Multiply, {literal(2.0F), distance});
                    break;
                }
                }

                auto source = emitPrimitive(VfxValueOpcode::Lerp, {inputs[2], inputs[3], factor});
                result.SourcesByOutputPin.emplace(output->Id, source);
                compiling.erase(node.Id);
                return source;
            }

            VfxCompiledValueInstruction instruction;
            instruction.Node = node.Id;
            instruction.Opcode = *descriptor->Lowering;
            instruction.Type = output->Type;
            instruction.Context = node.Context;
            instruction.OutputIndex = outputIndex;
            instruction.Inputs = inputs;
            instruction.ClampRemap = instruction.Opcode == VfxValueOpcode::Remap && Property<bool>(node, "Clamp");
            if (instruction.Opcode == VfxValueOpcode::Compare)
                instruction.Comparison = ParseComparison(node);
            if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                const auto scope = Property<std::uint64_t>(node, "Scope");
                if (scope > static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip))
                    throw std::invalid_argument("VFX Random scope is unsupported.");
                instruction.RandomScope = static_cast<VfxRandomScope>(scope);
                instruction.ConstantRandom = Property<bool>(node, "Constant");
                instruction.IndependentRandomChannels = Property<bool>(node, "Independent Channels");
                if (instruction.Opcode == VfxValueOpcode::RandomRange)
                    instruction.InclusiveMaximum = Property<bool>(node, "Inclusive Maximum");
            }

            instruction.Domain = VfxEvaluationDomain::CompileTimeConstant;
            for (const auto& input : inputs)
                instruction.Domain = std::max(instruction.Domain, SourceDomain(input, registerDomains));
            if (instruction.Opcode == VfxValueOpcode::Time || instruction.Opcode == VfxValueOpcode::DeltaTime ||
                instruction.Opcode == VfxValueOpcode::FrameIndex)
                instruction.Domain = VfxEvaluationDomain::PerFrame;
            else if (instruction.Opcode == VfxValueOpcode::SystemSeed)
                instruction.Domain = VfxEvaluationDomain::PerEffect;
            else if (instruction.Opcode == VfxValueOpcode::Age || instruction.Opcode == VfxValueOpcode::Lifetime ||
                     instruction.Opcode == VfxValueOpcode::AgeOverLifetime ||
                     instruction.Opcode == VfxValueOpcode::ParticleId ||
                     instruction.Opcode == VfxValueOpcode::SpawnIndex || IsParticleAttributeOpcode(instruction.Opcode))
            {
                instruction.Domain = DomainForContext(node.Context);
            }
            else if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                instruction.Domain =
                    instruction.ConstantRandom ? VfxEvaluationDomain::PerEffect : DomainForContext(node.Context);
            }

            std::optional<VfxParameterValue> folded;
            if (instruction.Domain == VfxEvaluationDomain::CompileTimeConstant)
            {
                std::array<const VfxParameterValue*, 8> values{};
                if (inputs.size() <= values.size() &&
                    std::ranges::all_of(inputs, [](const VfxCompiledValueSource& source)
                                        { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                {
                    for (std::size_t index = 0; index < inputs.size(); ++index)
                        values[index] = std::addressof(inputs[index].Literal);
                    folded = EvaluateVfxPureExpression(
                        instruction.Opcode, std::span<const VfxParameterValue* const>(values.data(), inputs.size()),
                        instruction.Type, instruction.ClampRemap, instruction.Comparison, instruction.OutputIndex);
                }
            }

            VfxCompiledValueSource source;
            if (folded)
                source = {VfxCompiledValueSourceKind::Literal, output->Type, 0, std::move(*folded)};
            else
            {
                if (result.RegisterCount >= MaximumValueRegisters)
                    throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                instruction.OutputRegister = result.RegisterCount++;
                registerDomains.emplace(instruction.OutputRegister, instruction.Domain);
                source = {VfxCompiledValueSourceKind::Register, output->Type, instruction.OutputRegister, 0.0F};
                result.Instructions.push_back(std::move(instruction));
            }
            result.SourcesByOutputPin.emplace(output->Id, source);
            compiling.erase(node.Id);
            return source;
        };

        for (const auto outputPin : requiredOutputPins)
            (void)compileOutput(outputPin);
        return result;
    }

    bool PackVfxGpuValue(const VfxValueType type, const VfxParameterValue& value, VfxGpuValue& packed) noexcept
    {
        if (!VfxValueMatchesType(type, value) || !IsFiniteVfxValue(value))
            return false;

        packed = {};
        const auto scalar = [](std::array<std::uint32_t, 4>& lane, const float component)
        { lane[0] = std::bit_cast<std::uint32_t>(component); };
        const auto integer = [](std::array<std::uint32_t, 4>& lane, const std::uint64_t component)
        {
            lane[0] = static_cast<std::uint32_t>(component);
            lane[1] = static_cast<std::uint32_t>(component >> 32U);
        };
        const auto vector2 = [](std::array<std::uint32_t, 4>& lane, const Vector2 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
        };
        const auto vector3 = [](std::array<std::uint32_t, 4>& lane, const Vector3 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
        };
        const auto vector4 = [](std::array<std::uint32_t, 4>& lane, const Vector4 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto quaternion = [](std::array<std::uint32_t, 4>& lane, const Quaternion component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto color = [](std::array<std::uint32_t, 4>& lane, const Color component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.Red);
            lane[1] = std::bit_cast<std::uint32_t>(component.Green);
            lane[2] = std::bit_cast<std::uint32_t>(component.Blue);
            lane[3] = std::bit_cast<std::uint32_t>(component.Alpha);
        };
        const auto asset = [](std::array<std::uint32_t, 4>& lane, const AssetId component)
        {
            lane[0] = static_cast<std::uint32_t>(component.High());
            lane[1] = static_cast<std::uint32_t>(component.High() >> 32U);
            lane[2] = static_cast<std::uint32_t>(component.Low());
            lane[3] = static_cast<std::uint32_t>(component.Low() >> 32U);
        };

        switch (type)
        {
        case VfxValueType::Boolean:
            packed.Primary[0] = std::get<bool>(value) ? 1U : 0U;
            return true;
        case VfxValueType::Integer:
            integer(packed.Primary, std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value)));
            return true;
        case VfxValueType::Scalar:
            scalar(packed.Primary, std::get<float>(value));
            return true;
        case VfxValueType::Vector2:
            vector2(packed.Primary, std::get<Vector2>(value));
            return true;
        case VfxValueType::Vector3:
            vector3(packed.Primary, std::get<Vector3>(value));
            return true;
        case VfxValueType::Color:
            color(packed.Primary, std::get<Color>(value));
            return true;
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            asset(packed.Primary, std::get<AssetId>(value));
            return true;
        case VfxValueType::UnsignedInteger:
            integer(packed.Primary, std::get<std::uint64_t>(value));
            return true;
        case VfxValueType::Vector4:
            vector4(packed.Primary, std::get<Vector4>(value));
            return true;
        case VfxValueType::Quaternion:
            quaternion(packed.Primary, std::get<Quaternion>(value));
            return true;
        case VfxValueType::ScalarRange:
        {
            const auto range = std::get<VfxScalarRange>(value);
            scalar(packed.Primary, range.Minimum);
            scalar(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::IntegerRange:
        {
            const auto range = std::get<VfxIntegerRange>(value);
            integer(packed.Primary, std::bit_cast<std::uint64_t>(range.Minimum));
            integer(packed.Secondary, std::bit_cast<std::uint64_t>(range.Maximum));
            return true;
        }
        case VfxValueType::UnsignedIntegerRange:
        {
            const auto range = std::get<VfxUnsignedIntegerRange>(value);
            integer(packed.Primary, range.Minimum);
            integer(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector2Range:
        {
            const auto range = std::get<VfxVector2Range>(value);
            vector2(packed.Primary, range.Minimum);
            vector2(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector3Range:
        {
            const auto range = std::get<VfxVector3Range>(value);
            vector3(packed.Primary, range.Minimum);
            vector3(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector4Range:
        {
            const auto range = std::get<VfxVector4Range>(value);
            vector4(packed.Primary, range.Minimum);
            vector4(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ColorRange:
        {
            const auto range = std::get<VfxColorRange>(value);
            color(packed.Primary, range.Minimum);
            color(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ParticleStream:
        case VfxValueType::Matrix:
        case VfxValueType::Curve:
        case VfxValueType::Gradient:
            return false;
        }
        return false;
    }

    const VfxParameterValue* ResolveVfxValueSource(const VfxCompiledValueSource& source,
                                                   const std::span<const VfxParameterValue> parameters,
                                                   const std::span<const VfxParameterValue> registers) noexcept
    {
        switch (source.Kind)
        {
        case VfxCompiledValueSourceKind::Literal:
            return std::addressof(source.Literal);
        case VfxCompiledValueSourceKind::Parameter:
            return source.Index < parameters.size() ? std::addressof(parameters[source.Index]) : nullptr;
        case VfxCompiledValueSourceKind::Register:
            return source.Index < registers.size() ? std::addressof(registers[source.Index]) : nullptr;
        }
        return nullptr;
    }

} // namespace Keire::Internal
