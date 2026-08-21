#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Vfx/VfxGpuValidationInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] constexpr bool SupportedGpuValueType(const VfxValueType type) noexcept
        {
            return IsVfxGpuExpressionValueType(type);
        }
        [[nodiscard]] constexpr bool FloatGpuValueType(const VfxValueType type) noexcept
        {
            return type == VfxValueType::Scalar || type == VfxValueType::Vector2 || type == VfxValueType::Vector3 ||
                   type == VfxValueType::Vector4 || type == VfxValueType::Color;
        }
        [[nodiscard]] constexpr bool FloatGpuRangeType(const VfxValueType type) noexcept
        {
            return type == VfxValueType::ScalarRange || type == VfxValueType::Vector2Range ||
                   type == VfxValueType::Vector3Range || type == VfxValueType::Vector4Range ||
                   type == VfxValueType::ColorRange;
        }
        [[nodiscard]] constexpr std::uint32_t GpuValueComponentCount(const VfxValueType type) noexcept
        {
            if (type == VfxValueType::Vector2 || type == VfxValueType::Vector2Range)
                return 2;
            if (type == VfxValueType::Vector3 || type == VfxValueType::Vector3Range)
                return 3;
            if (type == VfxValueType::Vector4 || type == VfxValueType::Color || type == VfxValueType::Vector4Range ||
                type == VfxValueType::ColorRange)
            {
                return 4;
            }
            return 1;
        }

        [[nodiscard]] constexpr std::optional<VfxValueType> GpuRangeType(const VfxValueType type) noexcept
        {
            switch (type)
            {
            case VfxValueType::Integer:
                return VfxValueType::IntegerRange;
            case VfxValueType::UnsignedInteger:
                return VfxValueType::UnsignedIntegerRange;
            case VfxValueType::Scalar:
                return VfxValueType::ScalarRange;
            case VfxValueType::Vector2:
                return VfxValueType::Vector2Range;
            case VfxValueType::Vector3:
                return VfxValueType::Vector3Range;
            case VfxValueType::Vector4:
                return VfxValueType::Vector4Range;
            case VfxValueType::Color:
                return VfxValueType::ColorRange;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] bool FiniteGpuValue(const VfxGpuValue& value, const VfxValueType type) noexcept
        {
            if (type == VfxValueType::Boolean)
                return value.Primary[0] <= 1U;
            if (!FloatGpuValueType(type) && !FloatGpuRangeType(type))
                return SupportedGpuValueType(type);
            const auto count = GpuValueComponentCount(type);
            for (std::uint32_t component = 0; component < count; ++component)
            {
                if (!std::isfinite(std::bit_cast<float>(value.Primary[component])))
                    return false;
                if (FloatGpuRangeType(type) && !std::isfinite(std::bit_cast<float>(value.Secondary[component])))
                    return false;
            }
            return true;
        }
        [[nodiscard]] bool ValidGpuInstructionSignature(const VfxValueOpcode opcode, const VfxValueType output,
                                                        const std::uint32_t outputIndex,
                                                        const std::span<const VfxValueType> inputs) noexcept
        {
            if (opcode > VfxValueOpcode::Rotate3D)
                return Internal::ValidVfxExtendedGpuInstructionSignature(opcode, output, outputIndex, inputs);
            const auto allScalar = [&inputs]()
            {
                return std::ranges::all_of(inputs,
                                           [](const VfxValueType type) { return type == VfxValueType::Scalar; });
            };
            switch (opcode)
            {
            case VfxValueOpcode::Constant:
                return inputs.size() == 1 && inputs[0] == output;
            case VfxValueOpcode::Range:
                return inputs.size() == 2 && inputs[0] == inputs[1] && GpuRangeType(inputs[0]) == output;
            case VfxValueOpcode::Random:
                return (output == VfxValueType::Boolean && inputs.empty()) ||
                       (GpuRangeType(output).has_value() && inputs.size() == 2 && inputs[0] == output &&
                        inputs[1] == output);
            case VfxValueOpcode::RandomRange:
                return GpuRangeType(output).has_value() && inputs.size() == 1 && GpuRangeType(output) == inputs[0];
            case VfxValueOpcode::Remap:
                return output == VfxValueType::Scalar && inputs.size() == 3 && inputs[0] == VfxValueType::Scalar &&
                       inputs[1] == VfxValueType::ScalarRange && inputs[2] == VfxValueType::ScalarRange;
            case VfxValueOpcode::Add:
            case VfxValueOpcode::Subtract:
            case VfxValueOpcode::Multiply:
            case VfxValueOpcode::Divide:
            case VfxValueOpcode::Minimum:
            case VfxValueOpcode::Maximum:
            case VfxValueOpcode::Atan2:
            case VfxValueOpcode::Power:
            case VfxValueOpcode::Step:
            case VfxValueOpcode::Modulo:
            case VfxValueOpcode::Discretize:
                return output == VfxValueType::Scalar && inputs.size() == 2 && allScalar();
            case VfxValueOpcode::Clamp:
            case VfxValueOpcode::Lerp:
            case VfxValueOpcode::Smoothstep:
            case VfxValueOpcode::InverseLerp:
                return output == VfxValueType::Scalar && inputs.size() == 3 && allScalar();
            case VfxValueOpcode::Saturate:
            case VfxValueOpcode::Absolute:
            case VfxValueOpcode::Sine:
            case VfxValueOpcode::Cosine:
            case VfxValueOpcode::Tangent:
            case VfxValueOpcode::ArcSine:
            case VfxValueOpcode::ArcCosine:
            case VfxValueOpcode::ArcTangent:
            case VfxValueOpcode::SquareRoot:
            case VfxValueOpcode::Exponential:
            case VfxValueOpcode::Logarithm:
            case VfxValueOpcode::LogarithmBase2:
            case VfxValueOpcode::LogarithmBase10:
            case VfxValueOpcode::Ceiling:
            case VfxValueOpcode::Floor:
            case VfxValueOpcode::Round:
            case VfxValueOpcode::Fractional:
            case VfxValueOpcode::Negate:
            case VfxValueOpcode::Sign:
            case VfxValueOpcode::OneMinus:
            case VfxValueOpcode::Reciprocal:
                return output == VfxValueType::Scalar && inputs.size() == 1 && allScalar();
            case VfxValueOpcode::Compare:
                return output == VfxValueType::Boolean && inputs.size() == 2 && allScalar();
            case VfxValueOpcode::BooleanAnd:
            case VfxValueOpcode::BooleanOr:
            case VfxValueOpcode::BooleanNand:
            case VfxValueOpcode::BooleanNor:
                return output == VfxValueType::Boolean && inputs.size() == 2 && inputs[0] == VfxValueType::Boolean &&
                       inputs[1] == VfxValueType::Boolean;
            case VfxValueOpcode::BooleanNot:
                return output == VfxValueType::Boolean && inputs.size() == 1 && inputs[0] == VfxValueType::Boolean;
            case VfxValueOpcode::Select:
                return inputs.size() == 3 && inputs[0] == VfxValueType::Boolean && inputs[1] == output &&
                       inputs[2] == output;
            case VfxValueOpcode::Combine:
            {
                const auto componentCount = GpuValueComponentCount(output);
                return FloatGpuValueType(output) && output != VfxValueType::Scalar && componentCount >= 2 &&
                       componentCount <= 4 && inputs.size() == componentCount && allScalar();
            }
            case VfxValueOpcode::Split:
                return output == VfxValueType::Scalar && inputs.size() == 1 && FloatGpuValueType(inputs[0]) &&
                       inputs[0] != VfxValueType::Scalar && outputIndex < GpuValueComponentCount(inputs[0]);
            case VfxValueOpcode::Dot:
            case VfxValueOpcode::Distance:
                return output == VfxValueType::Scalar && inputs.size() == 2 && inputs[0] == VfxValueType::Vector3 &&
                       inputs[1] == VfxValueType::Vector3;
            case VfxValueOpcode::Cross:
                return output == VfxValueType::Vector3 && inputs.size() == 2 && inputs[0] == VfxValueType::Vector3 &&
                       inputs[1] == VfxValueType::Vector3;
            case VfxValueOpcode::Normalize:
                return output == VfxValueType::Vector3 && inputs.size() == 1 && inputs[0] == VfxValueType::Vector3;
            case VfxValueOpcode::Length:
            case VfxValueOpcode::SquaredLength:
                return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Vector3;
            case VfxValueOpcode::SquaredDistance:
                return output == VfxValueType::Scalar && inputs.size() == 2 && inputs[0] == VfxValueType::Vector3 &&
                       inputs[1] == VfxValueType::Vector3;
            case VfxValueOpcode::Time:
            case VfxValueOpcode::DeltaTime:
            case VfxValueOpcode::Age:
            case VfxValueOpcode::Lifetime:
            case VfxValueOpcode::AgeOverLifetime:
                return output == VfxValueType::Scalar && inputs.empty();
            case VfxValueOpcode::ParticleId:
            case VfxValueOpcode::SpawnIndex:
            case VfxValueOpcode::FrameIndex:
            case VfxValueOpcode::SystemSeed:
                return output == VfxValueType::UnsignedInteger && inputs.empty();
            case VfxValueOpcode::BitwiseAnd:
            case VfxValueOpcode::BitwiseLeftShift:
            case VfxValueOpcode::BitwiseOr:
            case VfxValueOpcode::BitwiseRightShift:
            case VfxValueOpcode::BitwiseXor:
                return output == VfxValueType::UnsignedInteger && inputs.size() == 2 &&
                       inputs[0] == VfxValueType::UnsignedInteger && inputs[1] == VfxValueType::UnsignedInteger;
            case VfxValueOpcode::BitwiseComplement:
                return output == VfxValueType::UnsignedInteger && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::UnsignedInteger;
            case VfxValueOpcode::ColorLuma:
                return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Color;
            case VfxValueOpcode::HsvToRgb:
                return output == VfxValueType::Vector4 && inputs.size() == 1 && inputs[0] == VfxValueType::Vector3;
            case VfxValueOpcode::RgbToHsv:
                return output == VfxValueType::Vector3 && inputs.size() == 1 && inputs[0] == VfxValueType::Color;
            case VfxValueOpcode::ToFloat:
                return output == VfxValueType::Scalar && inputs.size() == 1 &&
                       (inputs[0] == VfxValueType::Integer || inputs[0] == VfxValueType::UnsignedInteger);
            case VfxValueOpcode::ToInteger:
                return output == VfxValueType::Integer && inputs.size() == 1 && inputs[0] == VfxValueType::Scalar;
            case VfxValueOpcode::ToUnsignedInteger:
                return output == VfxValueType::UnsignedInteger && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::Scalar;
            case VfxValueOpcode::AttributeAlive:
                return output == VfxValueType::Boolean && inputs.empty();
            case VfxValueOpcode::AttributeAlpha:
            case VfxValueOpcode::AttributeSize:
            case VfxValueOpcode::AttributeSpawnTime:
            case VfxValueOpcode::RatioOverStrip:
                return output == VfxValueType::Scalar && inputs.empty();
            case VfxValueOpcode::AttributeAngle:
            case VfxValueOpcode::AttributeAxisX:
            case VfxValueOpcode::AttributeAxisY:
            case VfxValueOpcode::AttributeAxisZ:
            case VfxValueOpcode::AttributeColor:
            case VfxValueOpcode::AttributeOldPosition:
            case VfxValueOpcode::AttributePosition:
            case VfxValueOpcode::AttributeVelocity:
                return output == VfxValueType::Vector3 && inputs.empty();
            case VfxValueOpcode::AttributeParticleCountInStrip:
            case VfxValueOpcode::AttributeParticleIndexInStrip:
            case VfxValueOpcode::AttributeSeed:
            case VfxValueOpcode::AttributeStripIndex:
                return output == VfxValueType::UnsignedInteger && inputs.empty();
            case VfxValueOpcode::Epsilon:
                return output == VfxValueType::Scalar && outputIndex == 0 && inputs.empty();
            case VfxValueOpcode::Pi:
                return output == VfxValueType::Scalar && outputIndex < 4 && inputs.empty();
            case VfxValueOpcode::ValueNoise:
            case VfxValueOpcode::PerlinNoise:
            case VfxValueOpcode::CellularNoise:
                return inputs.size() == 6 && inputs[0] == VfxValueType::Vector3 && inputs[1] == VfxValueType::Scalar &&
                       inputs[2] == VfxValueType::Integer && inputs[3] == VfxValueType::Scalar &&
                       inputs[4] == VfxValueType::Scalar && inputs[5] == VfxValueType::Vector2 &&
                       ((outputIndex == 0 && output == VfxValueType::Scalar) ||
                        (outputIndex == 1 && output == VfxValueType::Vector3));
            case VfxValueOpcode::ValueCurlNoise:
            case VfxValueOpcode::PerlinCurlNoise:
            case VfxValueOpcode::CellularCurlNoise:
                return output == VfxValueType::Vector3 && outputIndex == 0 && inputs.size() == 6 &&
                       inputs[0] == VfxValueType::Vector3 && inputs[1] == VfxValueType::Scalar &&
                       inputs[2] == VfxValueType::Integer && inputs[3] == VfxValueType::Scalar &&
                       inputs[4] == VfxValueType::Scalar && inputs[5] == VfxValueType::Scalar;
            case VfxValueOpcode::PolarToRectangular:
                return output == VfxValueType::Vector2 && inputs.size() == 2 && allScalar();
            case VfxValueOpcode::RectangularToPolar:
                return output == VfxValueType::Scalar && outputIndex < 2 && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::Vector2;
            case VfxValueOpcode::RectangularToSpherical:
                return output == VfxValueType::Scalar && outputIndex < 3 && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::Vector3;
            case VfxValueOpcode::SphericalToRectangular:
                return output == VfxValueType::Vector3 && inputs.size() == 3 && allScalar();
            case VfxValueOpcode::Rotate2D:
                return output == VfxValueType::Vector2 && inputs.size() == 3 && inputs[0] == VfxValueType::Vector2 &&
                       inputs[1] == VfxValueType::Vector2 && inputs[2] == VfxValueType::Scalar;
            case VfxValueOpcode::Rotate3D:
                return output == VfxValueType::Vector3 && inputs.size() == 4 && inputs[0] == VfxValueType::Vector3 &&
                       inputs[1] == VfxValueType::Vector3 && inputs[2] == VfxValueType::Vector3 &&
                       inputs[3] == VfxValueType::Scalar;
            default:
                return false;
            }
        }

        [[nodiscard]] std::string IndexedGpuPayloadError(const std::string_view table, const std::size_t index,
                                                         const std::string_view message)
        {
            return "GPU VFX " + std::string(table) + " " + std::to_string(index) + " " + std::string(message);
        }
    } // namespace

    std::optional<std::string> ValidateGpuVfxExecutionPayload(const VfxGpuExecutionPayload& payload)
    {
        const auto& program = payload.ValueProgram;
        if (program.Instructions.size() > VfxCompiledGpuValueProgram::MaximumInstructions)
            return "GPU VFX expression instruction table exceeds the shader limit.";
        if (program.Sources.size() > VfxCompiledGpuValueProgram::MaximumSources)
            return "GPU VFX expression source table exceeds the shader limit.";
        if (program.Constants.size() > VfxCompiledGpuValueProgram::MaximumConstants)
            return "GPU VFX expression constant table exceeds the shader limit.";
        if (program.RegisterCount > VfxCompiledGpuValueProgram::MaximumRegisters)
            return "GPU VFX expression register count exceeds the shader limit.";
        constexpr auto maximumBufferBytes = std::numeric_limits<std::uint32_t>::max();
        constexpr auto maximumValueRecords = maximumBufferBytes / sizeof(VfxGpuValue);
        if (program.Constants.size() > maximumValueRecords ||
            payload.Parameters.size() > maximumValueRecords - program.Constants.size() ||
            payload.CustomInstructions.size() > maximumBufferBytes / sizeof(VfxGpuCustomInstructionRecord) ||
            payload.ParticleOperations.size() > maximumBufferBytes / sizeof(VfxGpuParticleOperationRecord) ||
            payload.ModuleProperties.size() > maximumBufferBytes / sizeof(VfxGpuModulePropertyRecord) ||
            payload.LifetimeSamples.size() > maximumBufferBytes / sizeof(VfxGpuValue))
        {
            return "GPU VFX execution table exceeds SDL's 32-bit storage-buffer limit.";
        }

        std::array<std::optional<VfxValueType>, VfxCompiledGpuValueProgram::MaximumRegisters> registerTypes{};
        for (std::size_t instructionIndex = 0; instructionIndex < program.Instructions.size(); ++instructionIndex)
        {
            const auto& instruction = program.Instructions[instructionIndex];
            if (instruction.Header[0] > static_cast<std::uint32_t>(VfxValueOpcode::SpawnState))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unsupported opcode.");
            if (instruction.Header[1] > static_cast<std::uint32_t>(VfxValueType::SignedDistanceField))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown value type.");
            const auto opcode = static_cast<VfxValueOpcode>(instruction.Header[0]);
            const auto outputType = static_cast<VfxValueType>(instruction.Header[1]);
            if (!SupportedGpuValueType(outputType))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses a GPU-unsupported value type.");
            if (instruction.Header[2] > static_cast<std::uint32_t>(VfxContextType::Output))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unsupported context.");
            if (instruction.Header[3] > static_cast<std::uint32_t>(VfxEvaluationDomain::PerOutputEvent))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown evaluation domain.");
            if (instruction.Output[0] >= program.RegisterCount ||
                instruction.Output[0] >= VfxCompiledGpuValueProgram::MaximumRegisters)
                return IndexedGpuPayloadError("instruction", instructionIndex, "writes outside the register file.");
            if (registerTypes[instruction.Output[0]])
                return IndexedGpuPayloadError("instruction", instructionIndex, "writes an SSA register twice.");
            const auto firstSource = static_cast<std::size_t>(instruction.Output[2]);
            const auto sourceCount = static_cast<std::size_t>(instruction.Output[3]);
            if (sourceCount > 8 || firstSource > program.Sources.size() ||
                sourceCount > program.Sources.size() - firstSource)
                return IndexedGpuPayloadError("instruction", instructionIndex, "references an invalid source span.");
            if ((instruction.Settings[2] & ~0xfU) != 0U)
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses unknown setting flags.");
            if (instruction.Settings[1] > static_cast<std::uint32_t>(VfxRandomScope::PerParticleStrip))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown Random scope.");
            if (instruction.Settings[3] > static_cast<std::uint32_t>(VfxComparisonCondition::Greater))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown comparison mode.");

            std::array<VfxValueType, 8> inputTypes{};
            for (std::size_t inputIndex = 0; inputIndex < sourceCount; ++inputIndex)
            {
                const auto& source = program.Sources[firstSource + inputIndex];
                if (source.Reserved != 0 || source.Kind > static_cast<std::uint32_t>(VfxGpuValueSourceKind::Register) ||
                    source.Type > static_cast<std::uint32_t>(VfxValueType::SignedDistanceField))
                    return IndexedGpuPayloadError("source", firstSource + inputIndex, "has an invalid header.");
                const auto type = static_cast<VfxValueType>(source.Type);
                if (!SupportedGpuValueType(type))
                    return IndexedGpuPayloadError("source", firstSource + inputIndex,
                                                  "uses a GPU-unsupported value type.");
                inputTypes[inputIndex] = type;
                if (source.Kind == static_cast<std::uint32_t>(VfxGpuValueSourceKind::Literal))
                {
                    if (source.Index >= program.Constants.size() ||
                        !FiniteGpuValue(program.Constants[source.Index], type))
                        return IndexedGpuPayloadError("source", firstSource + inputIndex,
                                                      "references an invalid packed constant.");
                }
                else if (source.Kind == static_cast<std::uint32_t>(VfxGpuValueSourceKind::Parameter))
                {
                    if (source.Index >= payload.Parameters.size() ||
                        !FiniteGpuValue(payload.Parameters[source.Index], type))
                        return IndexedGpuPayloadError("source", firstSource + inputIndex,
                                                      "references an invalid packed parameter.");
                }
                else if (source.Index >= program.RegisterCount || !registerTypes[source.Index] ||
                         *registerTypes[source.Index] != type)
                {
                    return IndexedGpuPayloadError("source", firstSource + inputIndex,
                                                  "references an unwritten or mismatched register.");
                }
            }
            if (!ValidGpuInstructionSignature(opcode, outputType, instruction.Output[1],
                                              std::span(inputTypes).first(sourceCount)))
                return IndexedGpuPayloadError("instruction", instructionIndex, "has an invalid operand signature.");
            registerTypes[instruction.Output[0]] = outputType;
        }

        for (std::size_t customIndex = 0; customIndex < payload.CustomInstructions.size(); ++customIndex)
        {
            const auto& instruction = payload.CustomInstructions[customIndex];
            if (instruction.Context > VfxContextType::Output || instruction.Target > VfxCustomTarget::Size ||
                instruction.Operation > VfxCustomOperation::Multiply || !std::isfinite(instruction.Operand.X) ||
                !std::isfinite(instruction.Operand.Y) || !std::isfinite(instruction.Operand.Z) ||
                !std::isfinite(instruction.Operand.W))
                return IndexedGpuPayloadError("custom instruction", customIndex, "is malformed.");
            if (instruction.ValueRegister == std::numeric_limits<std::uint32_t>::max())
            {
                if (!SupportedGpuValueType(instruction.OperandType))
                    return IndexedGpuPayloadError("custom instruction", customIndex,
                                                  "uses a GPU-unsupported operand type.");
            }
            else
            {
                if (instruction.ValueRegister >= program.RegisterCount || !registerTypes[instruction.ValueRegister])
                    return IndexedGpuPayloadError("custom instruction", customIndex,
                                                  "references an unwritten expression register.");
                if (*registerTypes[instruction.ValueRegister] != instruction.OperandType)
                    return IndexedGpuPayloadError("custom instruction", customIndex,
                                                  "does not match its expression register type.");
            }
            const auto compatible =
                instruction.Target == VfxCustomTarget::Position || instruction.Target == VfxCustomTarget::Velocity
                    ? instruction.OperandType == VfxValueType::Scalar ||
                          instruction.OperandType == VfxValueType::Vector3
                : instruction.Target == VfxCustomTarget::Tint ? instruction.OperandType == VfxValueType::Scalar ||
                                                                    instruction.OperandType == VfxValueType::Color ||
                                                                    instruction.OperandType == VfxValueType::Vector4
                                                              : instruction.OperandType == VfxValueType::Scalar;
            if (!compatible)
                return IndexedGpuPayloadError("custom instruction", customIndex,
                                              "references an incompatible expression value type.");
        }

        for (std::size_t propertyIndex = 0; propertyIndex < payload.ModuleProperties.size(); ++propertyIndex)
        {
            const auto& property = payload.ModuleProperties[propertyIndex];
            if (property.Property == VfxModuleProperty::None ||
                property.Property > VfxModuleProperty::KillShapeInverted)
                return IndexedGpuPayloadError("module property", propertyIndex, "uses an unknown property.");
            if (!SupportedGpuValueType(property.Type))
                return IndexedGpuPayloadError("module property", propertyIndex, "uses a GPU-unsupported value type.");
            if (property.Source > VfxGpuModulePropertySource::Register)
                return IndexedGpuPayloadError("module property", propertyIndex, "uses an unknown source kind.");
            if (property.Source == VfxGpuModulePropertySource::Default ||
                property.Source == VfxGpuModulePropertySource::Literal)
            {
                if (!FiniteGpuValue(property.LiteralValue, property.Type))
                    return IndexedGpuPayloadError("module property", propertyIndex,
                                                  "contains an invalid packed literal.");
            }
            else if (property.Source == VfxGpuModulePropertySource::Parameter)
            {
                if (property.Index >= payload.Parameters.size() ||
                    !FiniteGpuValue(payload.Parameters[property.Index], property.Type))
                    return IndexedGpuPayloadError("module property", propertyIndex,
                                                  "references an invalid packed parameter.");
            }
            else if (property.Index >= program.RegisterCount || !registerTypes[property.Index] ||
                     *registerTypes[property.Index] != property.Type)
            {
                return IndexedGpuPayloadError("module property", propertyIndex,
                                              "references an unwritten or mismatched register.");
            }
        }

        std::vector<bool> referencedShapeResources(payload.ShapeResources.size(), false);
        for (std::size_t resourceIndex = 0; resourceIndex < payload.ShapeResources.size(); ++resourceIndex)
        {
            const auto& resource = payload.ShapeResources[resourceIndex];
            if ((resource.Shape != VfxShape::Mesh && resource.Shape != VfxShape::Volume) || !resource.Asset)
                return IndexedGpuPayloadError("shape resource", resourceIndex, "is malformed.");
        }

        for (std::size_t operationIndex = 0; operationIndex < payload.ParticleOperations.size(); ++operationIndex)
        {
            const auto& operation = payload.ParticleOperations[operationIndex];
            if (operation.Context > VfxContextType::Output || operation.Kind > VfxGpuParticleOperationKind::KillShape)
                return IndexedGpuPayloadError("particle operation", operationIndex, "is malformed.");
            if (operation.FirstProperty > payload.ModuleProperties.size() ||
                operation.PropertyCount > payload.ModuleProperties.size() - operation.FirstProperty ||
                operation.FirstSample > payload.LifetimeSamples.size() ||
                operation.SampleCount > payload.LifetimeSamples.size() - operation.FirstSample)
                return IndexedGpuPayloadError("particle operation", operationIndex,
                                              "references an invalid payload span.");
            if (operation.Kind == VfxGpuParticleOperationKind::CustomHlsl)
            {
                if (operation.Index >= payload.CustomInstructions.size() ||
                    payload.CustomInstructions[operation.Index].Context != operation.Context ||
                    operation.PropertyCount != 0 || operation.SampleCount != 0 || operation.Setting != 0)
                    return IndexedGpuPayloadError("particle operation", operationIndex,
                                                  "references an invalid Custom HLSL instruction.");
                continue;
            }
            if (operation.Kind != VfxGpuParticleOperationKind::Shape && operation.Index != 0)
                return IndexedGpuPayloadError("particle operation", operationIndex,
                                              "has a non-zero built-in operation index.");
            const auto expectedContext =
                operation.Kind == VfxGpuParticleOperationKind::Shape        ? VfxContextType::Initialize
                : operation.Kind == VfxGpuParticleOperationKind::Initialize ? VfxContextType::Initialize
                : operation.Kind == VfxGpuParticleOperationKind::Renderer   ? VfxContextType::Output
                                                                            : VfxContextType::Update;
            if (operation.Context != expectedContext)
                return IndexedGpuPayloadError("particle operation", operationIndex,
                                              "is scheduled in an incompatible context.");
            if (operation.Kind == VfxGpuParticleOperationKind::Shape &&
                operation.Setting > static_cast<std::uint32_t>(VfxShape::Volume))
                return IndexedGpuPayloadError("particle operation", operationIndex, "uses an unknown Shape mode.");
            if (operation.Kind == VfxGpuParticleOperationKind::Shape)
            {
                const auto resourceBacked = operation.Setting == static_cast<std::uint32_t>(VfxShape::Mesh) ||
                                            operation.Setting == static_cast<std::uint32_t>(VfxShape::Volume);
                if (resourceBacked)
                {
                    if (operation.Index == 0 || operation.Index > payload.ShapeResources.size())
                        return IndexedGpuPayloadError("particle operation", operationIndex,
                                                      "references an invalid shape resource.");
                    const auto resourceIndex = static_cast<std::size_t>(operation.Index - 1U);
                    if (static_cast<std::uint32_t>(payload.ShapeResources[resourceIndex].Shape) != operation.Setting)
                        return IndexedGpuPayloadError("particle operation", operationIndex,
                                                      "references a mismatched shape resource.");
                    referencedShapeResources[resourceIndex] = true;
                }
                else if (operation.Index != 0)
                {
                    return IndexedGpuPayloadError("particle operation", operationIndex,
                                                  "has an unexpected shape resource.");
                }
            }
            if (operation.Kind == VfxGpuParticleOperationKind::Collision &&
                operation.Setting > static_cast<std::uint32_t>(VfxCollisionMode::ScenePhysics))
                return IndexedGpuPayloadError("particle operation", operationIndex, "uses an unknown Collision mode.");
            if (operation.Kind == VfxGpuParticleOperationKind::KillShape &&
                operation.Setting != static_cast<std::uint32_t>(VfxShape::Box) &&
                operation.Setting != static_cast<std::uint32_t>(VfxShape::Sphere))
                return IndexedGpuPayloadError("particle operation", operationIndex, "uses an unknown Kill Shape.");
            if (operation.Kind == VfxGpuParticleOperationKind::Renderer &&
                operation.Setting > static_cast<std::uint32_t>(VfxRendererType::Volumetric))
                return IndexedGpuPayloadError("particle operation", operationIndex, "uses an unknown Renderer mode.");
            const auto lifetimeOperation = operation.Kind == VfxGpuParticleOperationKind::Size ||
                                           operation.Kind == VfxGpuParticleOperationKind::Color;
            if (lifetimeOperation && operation.SampleCount != VfxGpuEmitter::LifetimeSampleCount)
                return IndexedGpuPayloadError("particle operation", operationIndex,
                                              "has an invalid lifetime sample count.");
            if (!lifetimeOperation && operation.SampleCount != 0)
                return IndexedGpuPayloadError("particle operation", operationIndex, "has unexpected lifetime samples.");
        }
        if (std::ranges::find(referencedShapeResources, false) != referencedShapeResources.end())
            return std::string("GPU VFX payload contains an unreferenced shape resource.");
        return std::nullopt;
    }

    void RenderSharedState::ReleaseGpuVfxWorld(GpuVfxWorldResources& resources) noexcept
    {
        resources.Emitters.clear();
        if (resources.IndirectArguments)
            SDL_ReleaseGPUBuffer(Device, resources.IndirectArguments);
        if (resources.Counters)
            SDL_ReleaseGPUBuffer(Device, resources.Counters);
        if (resources.AliveIndices)
            SDL_ReleaseGPUBuffer(Device, resources.AliveIndices);
        if (resources.FreeIndices)
            SDL_ReleaseGPUBuffer(Device, resources.FreeIndices);
        if (resources.Particles)
            SDL_ReleaseGPUBuffer(Device, resources.Particles);
        resources = {};
    }

    void RenderSharedState::PrepareGpuVfx(SDL_GPUCommandBuffer* commands, const VfxRenderSnapshot& snapshot,
                                          const RenderSurfaceState& surface)
    {
        const auto requireStripPipelines = std::ranges::any_of(snapshot.GpuEmitters(), [](const auto& emitter)
                                                               { return emitter.Renderer == VfxRendererType::Ribbon; });
        if (snapshot.WorldId() == 0 || snapshot.ParticleCapacity() == 0)
            return;
        const auto existing = GpuVfxWorlds.find(snapshot.WorldId());
        const auto currentCapacity = existing == GpuVfxWorlds.end() ? 0U : existing->second.Capacity;
        const auto selectedCapacity =
            SelectGpuVfxPoolCapacity(snapshot.ParticleCapacity(), currentCapacity, snapshot.GpuEmitters());
        if (selectedCapacity == 0 || !EnsureGpuVfxPipelines(requireStripPipelines))
            return;
        auto& resources = GpuVfxWorlds[snapshot.WorldId()];
        const auto createBuffer = [this](const std::uint64_t size, const SDL_GPUBufferUsageFlags usage)
        {
            if (size == 0 || size > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("GPU VFX buffer size exceeds the backend limit.");
            SDL_GPUBufferCreateInfo information{};
            information.usage = usage;
            information.size = static_cast<std::uint32_t>(size);
            auto* result = SDL_CreateGPUBuffer(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUBuffer(GPU VFX) failed: " + LastSdlError());
            return result;
        };
        if (resources.Capacity != selectedCapacity)
        {
            if (resources.Capacity != 0)
            {
                KEIRE_CORE_WARN("GPU VFX world {} physical pool grew from {} to {} particles; restarting its "
                                "simulation state for the new storage layout.",
                                snapshot.WorldId(), resources.Capacity, selectedCapacity);
            }
            ReleaseGpuVfxWorld(resources);
            constexpr std::uint64_t particleStride = 160;
            const auto capacity = selectedCapacity;
            try
            {
                resources.Particles =
                    createBuffer(particleStride * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.FreeIndices =
                    createBuffer(sizeof(std::uint32_t) * capacity, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.AliveIndices =
                    createBuffer(sizeof(std::uint32_t) * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.Counters =
                    createBuffer(5U * sizeof(std::uint32_t), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.IndirectArguments =
                    createBuffer(sizeof(SDL_GPUIndirectDrawCommand),
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT);
                resources.Capacity = capacity;
            }
            catch (...)
            {
                ReleaseGpuVfxWorld(resources);
                GpuVfxWorlds.erase(snapshot.WorldId());
                throw;
            }
        }
        if (resources.LastPreparedFrame == Statistics.Frame)
            return;
        Statistics.VfxGpuParticleCapacity += resources.Capacity;
        const auto activeParticleBudget =
            GpuVfxActiveParticleBudget(snapshot.ParticleCapacity(), snapshot.GpuEmitters());
        const auto activeParticleGroupCount = (activeParticleBudget + 255U) / 256U;

        static_assert(static_cast<std::uint32_t>(VfxContextType::Spawn) == 0);
        static_assert(static_cast<std::uint32_t>(VfxContextType::Initialize) == 1);
        static_assert(static_cast<std::uint32_t>(VfxContextType::Update) == 2);
        static_assert(static_cast<std::uint32_t>(VfxContextType::Output) == 3);
        static_assert(static_cast<std::uint32_t>(VfxCustomTarget::Position) == 0);
        static_assert(static_cast<std::uint32_t>(VfxCustomTarget::Velocity) == 1);
        static_assert(static_cast<std::uint32_t>(VfxCustomTarget::Rotation) == 2);
        static_assert(static_cast<std::uint32_t>(VfxCustomTarget::Tint) == 3);
        static_assert(static_cast<std::uint32_t>(VfxCustomTarget::Size) == 4);
        static_assert(static_cast<std::uint32_t>(VfxCustomOperation::Assign) == 0);
        static_assert(static_cast<std::uint32_t>(VfxCustomOperation::Add) == 1);
        static_assert(static_cast<std::uint32_t>(VfxCustomOperation::Multiply) == 2);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Shape) == 0);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Initialize) == 1);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Force) == 2);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Size) == 3);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Color) == 4);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Collision) == 5);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::Renderer) == 6);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::CustomHlsl) == 7);
        static_assert(static_cast<std::uint32_t>(VfxGpuEmitter::ParticleOperationKind::KillShape) == 8);
        struct alignas(16) Dispatch final
        {
            std::uint32_t Capacity = 0;
            std::uint32_t SpawnCount = 0;
            float DeltaSeconds = 0.0F;
            std::uint32_t Seed = 0;
            std::array<float, 4> Position{};
            std::array<float, 4> Rotation{};
            std::array<float, 4> ShapeExtentRadius{};
            std::array<float, 4> VelocityMinimumLifetime{};
            std::array<float, 4> VelocityMaximumLifetime{};
            std::array<float, 4> AccelerationShape{};
            std::array<float, 4> ShapeRotationParameters{};
            std::array<float, 4> ColorStart{};
            std::array<float, 4> ColorEnd{};
            std::array<float, 4> Size{};
            std::array<float, 4> PreviousPosition{};
            std::array<float, 4> PreviousRotation{};
            std::array<std::uint32_t, 4> Identity{};
            std::array<std::uint32_t, 4> ValueProgramMetadata{};
            std::array<std::uint32_t, 4> ValueRuntimeMetadata{};
            std::array<std::uint32_t, 4> ValueSystemIdentity{};
            std::array<std::uint32_t, 4> ValueSimulationMetadata{};
            std::array<float, 4> ValueRuntimeTime{};
            std::array<float, 4> RotationMinimum{};
            std::array<float, 4> RotationMaximum{};
            std::array<std::uint32_t, 4> RenderMetadata{};
            Matrix4 CollisionViewProjection;
            Matrix4 CollisionInverseViewProjection;
            std::array<float, 4> CollisionParameters{};
            std::array<std::array<float, 4>, VfxGpuEmitter::LifetimeSampleCount / 4U> SizeCurveSamples{};
            std::array<std::array<float, 4>, VfxGpuEmitter::LifetimeSampleCount> ColorGradientSamples{};
        };
        static_assert(offsetof(Dispatch, Identity) == 208);
        static_assert(offsetof(Dispatch, ValueProgramMetadata) == 224);
        static_assert(offsetof(Dispatch, ValueRuntimeMetadata) == 240);
        static_assert(offsetof(Dispatch, ValueSystemIdentity) == 256);
        static_assert(offsetof(Dispatch, ValueSimulationMetadata) == 272);
        static_assert(offsetof(Dispatch, ValueRuntimeTime) == 288);
        static_assert(offsetof(Dispatch, RotationMinimum) == 304);
        static_assert(offsetof(Dispatch, RotationMaximum) == 320);
        static_assert(offsetof(Dispatch, RenderMetadata) == 336);
        static_assert(offsetof(Dispatch, CollisionViewProjection) == 352);
        static_assert(offsetof(Dispatch, CollisionInverseViewProjection) == 416);
        static_assert(offsetof(Dispatch, CollisionParameters) == 480);
        static_assert(offsetof(Dispatch, SizeCurveSamples) == 496);
        static_assert(offsetof(Dispatch, ColorGradientSamples) == 752);
        static_assert(sizeof(Dispatch) == 1776);
        Dispatch dispatch;
        dispatch.Capacity = resources.Capacity;
        dispatch.CollisionViewProjection = surface.SampledDepthViewProjection;
        dispatch.CollisionInverseViewProjection = surface.SampledDepthInverseViewProjection;
        dispatch.CollisionParameters = {surface.SampledDepthValid ? 1.0F : 0.0F, static_cast<float>(surface.Width),
                                        static_cast<float>(surface.Height), 0.002F};
        const auto simulationDelta = [](const VfxGpuEmitter& emitter) noexcept
        {
            constexpr auto maximumDeltaSeconds = 10.0F * 8.0F;
            return std::isfinite(emitter.SimulationDeltaSeconds)
                       ? std::clamp(emitter.SimulationDeltaSeconds, 0.0F, maximumDeltaSeconds)
                       : 0.0F;
        };

        const auto acquireExecutionBuffers =
            [this, commands, &resources](const std::shared_ptr<const VfxGpuExecutionPayload>& execution)
            -> std::shared_ptr<GpuVfxExecutionBuffers>
        {
            if (!execution)
                throw std::invalid_argument("GPU VFX emitter is missing its immutable execution payload.");
            for (auto cached = resources.ExecutionCache.begin(); cached != resources.ExecutionCache.end();)
            {
                if (cached->second.expired())
                    cached = resources.ExecutionCache.erase(cached);
                else
                    ++cached;
            }
            if (const auto found = resources.ExecutionCache.find(execution.get());
                found != resources.ExecutionCache.end())
            {
                if (auto cached = found->second.lock())
                    return cached;
                resources.ExecutionCache.erase(found);
            }
            if (const auto diagnostic = ValidateGpuVfxExecutionPayload(*execution))
                throw std::invalid_argument(*diagnostic);

            std::vector<VfxGpuCustomInstructionRecord> customInstructions;
            customInstructions.reserve(execution->CustomInstructions.size());
            for (const auto& instruction : execution->CustomInstructions)
            {
                const auto operationFlags = static_cast<std::uint32_t>(instruction.Operation) |
                                            (instruction.ScaleByDeltaTime ? (1U << 8U) : 0U) |
                                            (static_cast<std::uint32_t>(instruction.OperandType) << 16U);
                customInstructions.push_back({
                    {static_cast<std::uint32_t>(instruction.Context), static_cast<std::uint32_t>(instruction.Target),
                     operationFlags, instruction.ValueRegister},
                    {instruction.Operand.X, instruction.Operand.Y, instruction.Operand.Z, instruction.Operand.W},
                });
            }
            std::vector<VfxGpuParticleOperationRecord> particleOperations;
            particleOperations.reserve(execution->ParticleOperations.size());
            for (const auto& operation : execution->ParticleOperations)
            {
                particleOperations.push_back({{
                                                  static_cast<std::uint32_t>(operation.Context),
                                                  static_cast<std::uint32_t>(operation.Kind),
                                                  operation.Index,
                                                  operation.Setting,
                                              },
                                              {
                                                  operation.FirstProperty,
                                                  operation.PropertyCount,
                                                  operation.FirstSample,
                                                  operation.SampleCount,
                                              }});
            }
            std::vector<VfxGpuModulePropertyRecord> moduleProperties;
            moduleProperties.reserve(execution->ModuleProperties.size());
            for (const auto& property : execution->ModuleProperties)
            {
                moduleProperties.push_back(
                    {{{static_cast<std::uint32_t>(property.Property), static_cast<std::uint32_t>(property.Type),
                       static_cast<std::uint32_t>(property.Source), property.Index}},
                     property.LiteralValue});
            }
            std::vector<VfxGpuValue> values;
            values.reserve(execution->ValueProgram.Constants.size() + execution->Parameters.size());
            values.insert(values.end(), execution->ValueProgram.Constants.begin(),
                          execution->ValueProgram.Constants.end());
            values.insert(values.end(), execution->Parameters.begin(), execution->Parameters.end());

            const auto release = [device = Device](GpuVfxExecutionBuffers* buffers) noexcept
            {
                if (buffers->LifetimeSamples)
                    SDL_ReleaseGPUBuffer(device, buffers->LifetimeSamples);
                if (buffers->ModuleProperties)
                    SDL_ReleaseGPUBuffer(device, buffers->ModuleProperties);
                if (buffers->ParticleOperations)
                    SDL_ReleaseGPUBuffer(device, buffers->ParticleOperations);
                if (buffers->CustomInstructions)
                    SDL_ReleaseGPUBuffer(device, buffers->CustomInstructions);
                if (buffers->Values)
                    SDL_ReleaseGPUBuffer(device, buffers->Values);
                if (buffers->Sources)
                    SDL_ReleaseGPUBuffer(device, buffers->Sources);
                if (buffers->Instructions)
                    SDL_ReleaseGPUBuffer(device, buffers->Instructions);
                delete buffers;
            };
            auto result = std::shared_ptr<GpuVfxExecutionBuffers>(new GpuVfxExecutionBuffers, release);
            const auto uploadRecords = [this, commands]<typename T>(const std::span<const T> records)
            {
                const T emptyRecord{};
                const auto uploadSpan = records.empty() ? std::span<const T>(std::addressof(emptyRecord), 1) : records;
                return UploadBuffer(commands, std::as_bytes(uploadSpan), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
            };
            result->Instructions = uploadRecords(std::span(execution->ValueProgram.Instructions));
            result->Sources = uploadRecords(std::span(execution->ValueProgram.Sources));
            result->Values = uploadRecords(std::span<const VfxGpuValue>(values));
            result->CustomInstructions =
                uploadRecords(std::span<const VfxGpuCustomInstructionRecord>(customInstructions));
            result->ParticleOperations =
                uploadRecords(std::span<const VfxGpuParticleOperationRecord>(particleOperations));
            result->ModuleProperties = uploadRecords(std::span<const VfxGpuModulePropertyRecord>(moduleProperties));
            result->LifetimeSamples = uploadRecords(std::span(execution->LifetimeSamples));
            result->ByteSize =
                std::max<std::size_t>(execution->ValueProgram.Instructions.size(), 1) * sizeof(VfxGpuValueInstruction) +
                std::max<std::size_t>(execution->ValueProgram.Sources.size(), 1) * sizeof(VfxGpuValueSource) +
                std::max<std::size_t>(values.size(), 1) * sizeof(VfxGpuValue) +
                std::max<std::size_t>(customInstructions.size(), 1) * sizeof(VfxGpuCustomInstructionRecord) +
                std::max<std::size_t>(particleOperations.size(), 1) * sizeof(VfxGpuParticleOperationRecord) +
                std::max<std::size_t>(moduleProperties.size(), 1) * sizeof(VfxGpuModulePropertyRecord) +
                std::max<std::size_t>(execution->LifetimeSamples.size(), 1) * sizeof(VfxGpuValue);
            resources.ExecutionCache.emplace(execution.get(), result);
            return result;
        };

        const auto acquireShapeBuffers =
            [this, commands](const std::shared_ptr<const VfxGpuExecutionPayload>& execution,
                             const std::shared_ptr<GpuVfxShapeBuffers>& current) -> std::shared_ptr<GpuVfxShapeBuffers>
        {
            if (!execution)
                throw std::invalid_argument("GPU VFX shape resources require an execution payload.");

            std::vector<VfxGpuShapeResourceRecord> resourceRecords;
            std::vector<VfxGpuShapeSampleRecord> sampleRecords;
            resourceRecords.reserve(execution->ShapeResources.size());
            auto revisionHash = std::uint64_t{1469598103934665603ULL};
            const auto hash = [&revisionHash](const auto value) noexcept
            {
                const auto bytes = std::as_bytes(std::span(std::addressof(value), 1));
                for (const auto byte : bytes)
                {
                    revisionHash ^= std::to_integer<std::uint8_t>(byte);
                    revisionHash *= 1099511628211ULL;
                }
            };

            for (const auto& resource : execution->ShapeResources)
            {
                const auto firstSample = sampleRecords.size();
                float totalWeight = 0.0F;
                std::uint64_t revision = 0;
                if (resource.Shape == VfxShape::Mesh)
                {
                    const auto& mesh = ResolveMesh(resource.Asset);
                    revision = mesh.Revision;
                    totalWeight = mesh.ShapeSampleWeight;
                    sampleRecords.reserve(sampleRecords.size() + mesh.ShapeSamples.size());
                    for (const auto& sample : mesh.ShapeSamples)
                        sampleRecords.push_back({sample.A, sample.B, sample.C});
                }
                else if (resource.Shape == VfxShape::Volume)
                {
                    auto [found, inserted] = VfxVolumeCache.try_emplace(resource.Asset);
                    auto& entry = found->second;
                    if (inserted)
                        entry.Handle = Assets ? Assets->Load<VfxVolumeAsset>(resource.Asset, AssetPriority::High)
                                              : AssetHandle<VfxVolumeAsset>{};
                    revision = entry.Handle.Revision();
                    if (revision != 0 && revision > entry.LoadedRevision)
                    {
                        if (const auto loaded = entry.Handle.TryGetLoaded())
                        {
                            entry.LastGood = loaded;
                            entry.LoadedRevision = revision;
                        }
                    }
                    const auto volume = entry.LastGood ? entry.LastGood : entry.Handle.Get();
                    if (volume)
                    {
                        totalWeight = volume->TotalWeight();
                        const auto& cells = volume->Definition().Cells;
                        const auto cumulative = volume->CumulativeWeights();
                        sampleRecords.reserve(sampleRecords.size() + cells.size());
                        for (std::size_t index = 0; index < cells.size(); ++index)
                        {
                            const auto& cell = cells[index];
                            sampleRecords.push_back({{cell.Minimum.X, cell.Minimum.Y, cell.Minimum.Z, 0.0F},
                                                     {cell.Maximum.X, cell.Maximum.Y, cell.Maximum.Z, 0.0F},
                                                     {0.0F, 0.0F, 0.0F, cumulative[index]}});
                        }
                    }
                }
                else
                {
                    throw std::invalid_argument("GPU VFX execution contains an unsupported shape resource.");
                }
                if (sampleRecords.size() == firstSample || !std::isfinite(totalWeight) || totalWeight <= 0.0F ||
                    firstSample > std::numeric_limits<std::uint32_t>::max() ||
                    sampleRecords.size() - firstSample > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::runtime_error("GPU VFX shape asset has no valid weighted samples.");
                }
                resourceRecords.push_back({{
                    static_cast<std::uint32_t>(resource.Shape),
                    static_cast<std::uint32_t>(firstSample),
                    static_cast<std::uint32_t>(sampleRecords.size() - firstSample),
                    std::bit_cast<std::uint32_t>(totalWeight),
                }});
                hash(resource.Asset.High());
                hash(resource.Asset.Low());
                hash(resource.Shape);
                hash(revision);
            }

            if (current && current->RevisionHash == revisionHash)
                return current;
            constexpr auto maximumTableRecords =
                std::numeric_limits<std::uint32_t>::max() / sizeof(VfxGpuShapeSampleRecord);
            if (resourceRecords.size() > maximumTableRecords ||
                sampleRecords.size() > maximumTableRecords - resourceRecords.size())
            {
                throw std::runtime_error("GPU VFX shape table exceeds SDL's 32-bit storage-buffer limit.");
            }
            std::vector<VfxGpuShapeSampleRecord> table;
            table.reserve(resourceRecords.size() + sampleRecords.size());
            for (const auto& resource : resourceRecords)
            {
                table.push_back({
                    {std::bit_cast<float>(resource.Metadata[0]), std::bit_cast<float>(resource.Metadata[1]),
                     std::bit_cast<float>(resource.Metadata[2]), std::bit_cast<float>(resource.Metadata[3])},
                    {},
                    {},
                });
            }
            table.insert(table.end(), sampleRecords.begin(), sampleRecords.end());
            const auto release = [device = Device](GpuVfxShapeBuffers* buffers) noexcept
            {
                if (buffers->Table)
                    SDL_ReleaseGPUBuffer(device, buffers->Table);
                delete buffers;
            };
            auto result = std::shared_ptr<GpuVfxShapeBuffers>(new GpuVfxShapeBuffers, release);
            const auto uploadRecords = [this, commands]<typename T>(const std::span<const T> records)
            {
                const T emptyRecord{};
                const auto uploadSpan = records.empty() ? std::span<const T>(std::addressof(emptyRecord), 1) : records;
                return UploadBuffer(commands, std::as_bytes(uploadSpan), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
            };
            result->Table = uploadRecords(std::span<const VfxGpuShapeSampleRecord>(table));
            result->ResourceCount = static_cast<std::uint32_t>(resourceRecords.size());
            result->FirstSample = result->ResourceCount;
            result->RevisionHash = revisionHash;
            result->ByteSize = std::max<std::size_t>(table.size(), 1U) * sizeof(VfxGpuShapeSampleRecord);
            return result;
        };

        const auto createRenderBuffers = [this, &createBuffer](const std::uint32_t capacity)
        {
            if (capacity == 0)
                throw std::invalid_argument("GPU VFX render compaction capacity must be nonzero.");
            const auto release = [device = Device](GpuVfxRenderBuffers* buffers) noexcept
            {
                if (buffers->Instances)
                    SDL_ReleaseGPUBuffer(device, buffers->Instances);
                if (buffers->IndirectArguments)
                    SDL_ReleaseGPUBuffer(device, buffers->IndirectArguments);
                if (buffers->Indices)
                    SDL_ReleaseGPUBuffer(device, buffers->Indices);
                delete buffers;
            };
            auto result = std::shared_ptr<GpuVfxRenderBuffers>(new GpuVfxRenderBuffers, release);
            result->Indices =
                createBuffer(static_cast<std::uint64_t>(sizeof(std::uint32_t)) * capacity,
                             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
            result->IndirectArguments =
                createBuffer(sizeof(SDL_GPUIndexedIndirectDrawCommand),
                             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT);
            result->Instances =
                createBuffer(static_cast<std::uint64_t>(sizeof(GpuInstanceUniform)) * capacity,
                             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
            result->Capacity = capacity;
            result->ByteSize = static_cast<std::uint64_t>(sizeof(std::uint32_t)) * capacity +
                               sizeof(SDL_GPUIndexedIndirectDrawCommand) +
                               static_cast<std::uint64_t>(sizeof(GpuInstanceUniform)) * capacity;
            return result;
        };

        const auto setExecutionDispatch = [&dispatch](const VfxGpuEmitter& emitter)
        {
            if (!emitter.Execution || !std::isfinite(emitter.EffectTime))
                throw std::invalid_argument("GPU VFX emitter execution timing is invalid.");
            const auto& execution = *emitter.Execution;
            dispatch.ValueProgramMetadata = {static_cast<std::uint32_t>(execution.ValueProgram.Instructions.size()),
                                             static_cast<std::uint32_t>(execution.ValueProgram.Sources.size()),
                                             static_cast<std::uint32_t>(execution.ValueProgram.Constants.size()),
                                             execution.ValueProgram.RegisterCount};
            dispatch.ValueRuntimeMetadata = {static_cast<std::uint32_t>(execution.Parameters.size()),
                                             static_cast<std::uint32_t>(execution.CustomInstructions.size()),
                                             static_cast<std::uint32_t>(execution.ParticleOperations.size()), 0U};
            dispatch.ValueSystemIdentity = execution.ValueProgram.SystemIdentity;
            dispatch.ValueSimulationMetadata = {static_cast<std::uint32_t>(emitter.SimulationStep),
                                                static_cast<std::uint32_t>(emitter.SimulationStep >> 32U), 0U, 0U};
            dispatch.ValueRuntimeTime = {emitter.EffectTime, 0.0F, 0.0F, 0.0F};
        };
        const auto setShapeDispatch = [&dispatch](const GpuVfxShapeBuffers& shapes) noexcept
        {
            dispatch.ValueSimulationMetadata[2] = shapes.ResourceCount;
            dispatch.ValueSimulationMetadata[3] = shapes.FirstSample;
        };
        const auto setLifetimeLookupDispatch = [&dispatch](const VfxGpuEmitter& emitter)
        {
            for (std::size_t sample = 0; sample < VfxGpuEmitter::LifetimeSampleCount; ++sample)
            {
                dispatch.SizeCurveSamples[sample / 4U][sample % 4U] = emitter.SizeCurveSamples[sample];
                const auto& color = emitter.ColorGradientSamples[sample];
                dispatch.ColorGradientSamples[sample] = {color.Red, color.Green, color.Blue, color.Alpha};
            }
        };

        const std::array writeBindings{
            SDL_GPUStorageBufferReadWriteBinding{resources.Particles, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.FreeIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.AliveIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.Counters, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments, false},
        };
        const auto dispatchCompute = [&](SDL_GPUComputePipeline* pipeline, const std::uint32_t groupCount,
                                         const GpuVfxExecutionBuffers* execution = nullptr,
                                         const GpuVfxShapeBuffers* shapes = nullptr)
        {
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                 static_cast<std::uint32_t>(writeBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(VFX) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, pipeline);
            if (execution)
            {
                if (!shapes)
                    throw std::logic_error("GPU VFX execution dispatch is missing shape resource bindings.");
                const std::array readBindings{
                    execution->Instructions,
                    execution->Sources,
                    execution->Values,
                    execution->CustomInstructions,
                    execution->ParticleOperations,
                    execution->ModuleProperties,
                    execution->LifetimeSamples,
                    shapes->Table,
                };
                SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                                 static_cast<std::uint32_t>(readBindings.size()));
                const SDL_GPUTextureSamplerBinding depthBinding{surface.Resources.SampledDepth, ShadowSampler};
                SDL_BindGPUComputeSamplers(pass, 0, &depthBinding, 1);
            }
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
            ++Statistics.VfxComputeDispatches;
            Statistics.VfxComputeThreadGroups += groupCount;
        };
        const auto dispatchRenderCompute = [&](SDL_GPUComputePipeline* pipeline,
                                               const GpuVfxRenderBuffers& renderBuffers, const std::uint32_t groupCount)
        {
            const std::array renderWriteBindings{
                SDL_GPUStorageBufferReadWriteBinding{resources.Particles, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.FreeIndices, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.AliveIndices, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.Counters, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments, false},
                SDL_GPUStorageBufferReadWriteBinding{renderBuffers.Indices, false},
                SDL_GPUStorageBufferReadWriteBinding{renderBuffers.IndirectArguments, false},
                SDL_GPUStorageBufferReadWriteBinding{renderBuffers.Instances, false},
            };
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, renderWriteBindings.data(),
                                                 static_cast<std::uint32_t>(renderWriteBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(VFX render compaction) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, pipeline);
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
            ++Statistics.VfxComputeDispatches;
            Statistics.VfxComputeThreadGroups += groupCount;
        };
        const auto dispatchRenderExecutionCompute =
            [&](SDL_GPUComputePipeline* pipeline, const GpuVfxRenderBuffers& renderBuffers,
                const GpuVfxExecutionBuffers& execution, const GpuVfxShapeBuffers& shapes,
                const std::uint32_t groupCount)
        {
            const std::array renderWriteBindings{
                SDL_GPUStorageBufferReadWriteBinding{resources.Particles, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.FreeIndices, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.AliveIndices, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.Counters, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments, false},
                SDL_GPUStorageBufferReadWriteBinding{renderBuffers.Indices, false},
                SDL_GPUStorageBufferReadWriteBinding{renderBuffers.IndirectArguments, false},
            };
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, renderWriteBindings.data(),
                                                 static_cast<std::uint32_t>(renderWriteBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(VFX bounded spawn) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, pipeline);
            const std::array readBindings{
                execution.Instructions,
                execution.Sources,
                execution.Values,
                execution.CustomInstructions,
                execution.ParticleOperations,
                execution.ModuleProperties,
                execution.LifetimeSamples,
                shapes.Table,
            };
            SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                             static_cast<std::uint32_t>(readBindings.size()));
            const SDL_GPUTextureSamplerBinding depthBinding{surface.Resources.SampledDepth, ShadowSampler};
            SDL_BindGPUComputeSamplers(pass, 0, &depthBinding, 1);
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
            ++Statistics.VfxComputeDispatches;
            Statistics.VfxComputeThreadGroups += groupCount;
        };

        const auto allocatedBufferBytes = [this]() noexcept
        {
            std::uint64_t result = 0;
            for (const auto& [world, liveResources] : GpuVfxWorlds)
            {
                (void)world;
                result += static_cast<std::uint64_t>(liveResources.Capacity) * (160U + sizeof(std::uint32_t) * 2U) +
                          5U * sizeof(std::uint32_t) + sizeof(SDL_GPUIndirectDrawCommand);
                for (const auto& [payload, cached] : liveResources.ExecutionCache)
                {
                    (void)payload;
                    if (const auto buffers = cached.lock())
                        result += buffers->ByteSize;
                }
                for (const auto& [key, emitter] : liveResources.Emitters)
                {
                    (void)key;
                    if (emitter.RenderBuffers)
                        result += emitter.RenderBuffers->ByteSize;
                    if (emitter.ShapeBuffers)
                        result += emitter.ShapeBuffers->ByteSize;
                }
            }
            return result;
        };

        const auto applySnapshot = resources.ShouldApplySnapshot(snapshot.Revision());
        if (!applySnapshot)
        {
            resources.LastPreparedFrame = Statistics.Frame;
            Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());
            Statistics.VfxGpuBufferBytes = allocatedBufferBytes();
            return;
        }
        const auto consumeSimulation = resources.ShouldConsumeSimulationStep(snapshot.SimulationStepRevision());
        const auto resetWorld = applySnapshot && resources.ResetRevision != snapshot.ResetRevision();
        auto nextEmitters = resetWorld ? decltype(resources.Emitters){} : resources.Emitters;
        if (resetWorld)
            dispatchCompute(VfxInitializePipeline, (resources.Capacity + 255U) / 256U);

        if (applySnapshot)
        {
            std::unordered_set<std::uint64_t> activeEmitterKeys;
            activeEmitterKeys.reserve(snapshot.GpuEmitters().size());
            for (const auto& emitter : snapshot.GpuEmitters())
            {
                activeEmitterKeys.emplace((static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) |
                                          emitter.Handle.Generation());
            }
            std::vector<std::uint64_t> retiredEmitterKeys;
            retiredEmitterKeys.reserve(nextEmitters.size());
            for (const auto& [key, state] : nextEmitters)
            {
                (void)state;
                if (!activeEmitterKeys.contains(key))
                    retiredEmitterKeys.push_back(key);
            }
            for (const auto key : retiredEmitterKeys)
            {
                dispatch.Identity = {static_cast<std::uint32_t>(key >> 32U), static_cast<std::uint32_t>(key), 0U, 0U};
                dispatchCompute(VfxKillPipeline, (resources.Capacity + 255U) / 256U);
                nextEmitters.erase(key);
            }
            for (const auto& emitter : snapshot.GpuEmitters())
            {
                const auto key =
                    (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
                const auto previous = nextEmitters.find(key);
                if (previous == nextEmitters.end() || previous->second.SimulationRevision == emitter.SimulationRevision)
                {
                    continue;
                }
                dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
                dispatchCompute(VfxKillPipeline, (resources.Capacity + 255U) / 256U);
                nextEmitters.erase(previous);
            }
            for (const auto& emitter : snapshot.GpuEmitters())
            {
                if (emitter.Space != VfxSimulationSpace::Local)
                    continue;
                const auto key =
                    (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
                const auto previous = nextEmitters.find(key);
                if (previous == nextEmitters.end() || previous->second.Space != VfxSimulationSpace::Local ||
                    (previous->second.Position == emitter.Position && previous->second.Rotation == emitter.Rotation))
                {
                    continue;
                }
                dispatch.Position = {emitter.Position.X, emitter.Position.Y, emitter.Position.Z, 0.0F};
                dispatch.Rotation = {emitter.Rotation.X, emitter.Rotation.Y, emitter.Rotation.Z, emitter.Rotation.W};
                dispatch.PreviousPosition = {previous->second.Position.X, previous->second.Position.Y,
                                             previous->second.Position.Z, 0.0F};
                dispatch.PreviousRotation = {previous->second.Rotation.X, previous->second.Rotation.Y,
                                             previous->second.Rotation.Z, previous->second.Rotation.W};
                dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
                dispatchCompute(VfxTransformPipeline, (resources.Capacity + 255U) / 256U);
            }
        }

        dispatchCompute(VfxResetPipeline, 1);
        for (const auto& emitter : snapshot.GpuEmitters())
        {
            const auto key = (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
            auto& state = nextEmitters[key];
            const auto renderCapacity = std::clamp(emitter.Capacity, 1U, resources.Capacity);
            if (!state.RenderBuffers || state.RenderBuffers->Capacity != renderCapacity)
            {
                state.RenderBuffers = createRenderBuffers(renderCapacity);
                state.HasCompactedParticles = false;
            }
            state.Renderer = emitter.Renderer;
            state.Sprite = emitter.Sprite;
            state.Mesh = emitter.Mesh;
            if (state.Material != emitter.Material)
                state.MaterialDiagnosticReported = false;
            state.Material = emitter.Material;
            if (state.Execution != emitter.Execution || !state.ExecutionBuffers)
            {
                state.ExecutionBuffers = acquireExecutionBuffers(emitter.Execution);
                state.Execution = emitter.Execution;
            }
            state.ShapeBuffers = acquireShapeBuffers(emitter.Execution, state.ShapeBuffers);
            dispatch.DeltaSeconds = consumeSimulation ? simulationDelta(emitter) : 0.0F;
            dispatch.Seed = emitter.Seed;
            dispatch.Position = {emitter.Position.X, emitter.Position.Y, emitter.Position.Z, 0.0F};
            dispatch.Rotation = {emitter.Rotation.X, emitter.Rotation.Y, emitter.Rotation.Z, emitter.Rotation.W};
            dispatch.AccelerationShape = {0.0F, -9.81F, 0.0F, 0.0F};
            dispatch.ShapeRotationParameters = {emitter.ConeAngleDegrees, emitter.ConeLength, 0.0F, 0.0F};
            dispatch.RotationMinimum = {emitter.RotationMinimum.X, emitter.RotationMinimum.Y, emitter.RotationMinimum.Z,
                                        0.0F};
            dispatch.RotationMaximum = {emitter.RotationMaximum.X, emitter.RotationMaximum.Y, emitter.RotationMaximum.Z,
                                        0.0F};
            dispatch.ColorStart = {emitter.ColorStart.Red, emitter.ColorStart.Green, emitter.ColorStart.Blue,
                                   emitter.ColorStart.Alpha};
            dispatch.ColorEnd = {emitter.ColorEnd.Red, emitter.ColorEnd.Green, emitter.ColorEnd.Blue,
                                 emitter.ColorEnd.Alpha};
            dispatch.Size = {emitter.SizeStart, emitter.SizeEnd,
                             std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Space)), 0.0F};
            dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
            dispatch.RenderMetadata = {1U, state.RenderBuffers->Capacity, static_cast<std::uint32_t>(emitter.Renderer),
                                       std::max(emitter.ParticlesPerStrip, 1U)};
            setExecutionDispatch(emitter);
            setShapeDispatch(*state.ShapeBuffers);
            setLifetimeLookupDispatch(emitter);
            if (state.HasCompactedParticles)
            {
                const auto emitterGroupCount = (state.RenderBuffers->Capacity + 255U) / 256U;
                dispatchRenderExecutionCompute(VfxSimulatePipeline, *state.RenderBuffers, *state.ExecutionBuffers,
                                               *state.ShapeBuffers, emitterGroupCount);
                dispatchRenderExecutionCompute(VfxSimulateOutputPipeline, *state.RenderBuffers, *state.ExecutionBuffers,
                                               *state.ShapeBuffers, emitterGroupCount);
            }
        }

        for (const auto& emitter : snapshot.GpuEmitters())
        {
            const auto key = (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
            const auto state = nextEmitters.find(key);
            if (state == nextEmitters.end() || !state->second.RenderBuffers)
                continue;
            const auto primitiveCount =
                emitter.Renderer == VfxRendererType::Mesh ? ResolveMesh(emitter.Mesh).IndexCount : 6U;
            dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
            dispatch.RenderMetadata = {primitiveCount, state->second.RenderBuffers->Capacity,
                                       static_cast<std::uint32_t>(emitter.Renderer),
                                       std::max(emitter.ParticlesPerStrip, 1U)};
            dispatchRenderCompute(VfxResetRenderPipeline, *state->second.RenderBuffers, 1);
            dispatchRenderCompute(VfxFilterRenderPipeline, *state->second.RenderBuffers, activeParticleGroupCount);
            state->second.HasCompactedParticles = true;
        }

        std::unordered_set<std::uint64_t> spawnedEmitterKeys;
        spawnedEmitterKeys.reserve(snapshot.GpuEmitters().size());
        if (applySnapshot)
        {
            for (const auto& emitter : snapshot.GpuEmitters())
            {
                const auto key =
                    (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
                auto& state = nextEmitters[key];
                const auto requested = emitter.SpawnSequence >= state.SpawnSequence
                                           ? emitter.SpawnSequence - state.SpawnSequence
                                           : emitter.SpawnSequence;
                const auto firstSpawnSequence =
                    emitter.SpawnSequence >= requested ? emitter.SpawnSequence - requested : std::uint64_t{0};
                state.SpawnSequence = emitter.SpawnSequence;
                state.SimulationRevision = emitter.SimulationRevision;
                state.Position = emitter.Position;
                state.Rotation = emitter.Rotation;
                state.Space = emitter.Space;
                if (requested == 0)
                    continue;
                spawnedEmitterKeys.emplace(key);
                dispatch.SpawnCount =
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, resources.Capacity));
                dispatch.DeltaSeconds = consumeSimulation ? simulationDelta(emitter) : 0.0F;
                dispatch.Seed = emitter.Seed;
                dispatch.Position = {emitter.Position.X, emitter.Position.Y, emitter.Position.Z, 0.0F};
                dispatch.Rotation = {emitter.Rotation.X, emitter.Rotation.Y, emitter.Rotation.Z, emitter.Rotation.W};
                dispatch.ShapeExtentRadius = {emitter.ShapeExtent.X, emitter.ShapeExtent.Y, emitter.ShapeExtent.Z,
                                              emitter.ShapeRadius};
                dispatch.VelocityMinimumLifetime = {emitter.VelocityMinimum.X, emitter.VelocityMinimum.Y,
                                                    emitter.VelocityMinimum.Z, emitter.LifetimeMinimum};
                dispatch.VelocityMaximumLifetime = {emitter.VelocityMaximum.X, emitter.VelocityMaximum.Y,
                                                    emitter.VelocityMaximum.Z, emitter.LifetimeMaximum};
                dispatch.AccelerationShape = {0.0F, -9.81F, 0.0F, 0.0F};
                dispatch.ShapeRotationParameters = {emitter.ConeAngleDegrees, emitter.ConeLength, 0.0F, 0.0F};
                dispatch.RotationMinimum = {emitter.RotationMinimum.X, emitter.RotationMinimum.Y,
                                            emitter.RotationMinimum.Z, 0.0F};
                dispatch.RotationMaximum = {emitter.RotationMaximum.X, emitter.RotationMaximum.Y,
                                            emitter.RotationMaximum.Z, 0.0F};
                dispatch.ColorStart = {emitter.ColorStart.Red, emitter.ColorStart.Green, emitter.ColorStart.Blue,
                                       emitter.ColorStart.Alpha};
                dispatch.ColorEnd = {emitter.ColorEnd.Red, emitter.ColorEnd.Green, emitter.ColorEnd.Blue,
                                     emitter.ColorEnd.Alpha};
                dispatch.Size = {emitter.SizeStart, emitter.SizeEnd,
                                 std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Space)), 0.0F};
                dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(),
                                     static_cast<std::uint32_t>(firstSpawnSequence),
                                     static_cast<std::uint32_t>(firstSpawnSequence >> 32U)};
                dispatch.RenderMetadata = {1U, state.RenderBuffers->Capacity,
                                           static_cast<std::uint32_t>(emitter.Renderer),
                                           std::max(emitter.ParticlesPerStrip, 1U)};
                setExecutionDispatch(emitter);
                setShapeDispatch(*state.ShapeBuffers);
                setLifetimeLookupDispatch(emitter);
                const auto spawnGroupCount = (dispatch.SpawnCount + 255U) / 256U;
                dispatchRenderExecutionCompute(VfxSpawnPipeline, *state.RenderBuffers, *state.ExecutionBuffers,
                                               *state.ShapeBuffers, spawnGroupCount);
                dispatchRenderExecutionCompute(VfxSpawnInitializePipeline, *state.RenderBuffers,
                                               *state.ExecutionBuffers, *state.ShapeBuffers, spawnGroupCount);
                dispatchRenderExecutionCompute(VfxSpawnOutputPipeline, *state.RenderBuffers, *state.ExecutionBuffers,
                                               *state.ShapeBuffers, spawnGroupCount);
                if (emitter.Renderer == VfxRendererType::Ribbon)
                {
                    dispatchRenderCompute(VfxMapStripsPipeline, *state.RenderBuffers, spawnGroupCount);
                    dispatchRenderCompute(VfxLinkStripsPipeline, *state.RenderBuffers, spawnGroupCount);
                }
            }
        }

        dispatchCompute(VfxFinalizePipeline, 1);
        for (const auto& emitter : snapshot.GpuEmitters())
        {
            const auto key = (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
            const auto state = nextEmitters.find(key);
            if (state == nextEmitters.end() || !state->second.RenderBuffers)
                continue;
            if (!spawnedEmitterKeys.contains(key))
                continue;
            const auto primitiveCount =
                emitter.Renderer == VfxRendererType::Mesh ? ResolveMesh(emitter.Mesh).IndexCount : 6U;
            dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
            dispatch.RenderMetadata = {primitiveCount, state->second.RenderBuffers->Capacity,
                                       static_cast<std::uint32_t>(emitter.Renderer),
                                       std::max(emitter.ParticlesPerStrip, 1U)};
            dispatchRenderCompute(VfxResetRenderPipeline, *state->second.RenderBuffers, 1);
            dispatchRenderCompute(VfxFilterRenderPipeline, *state->second.RenderBuffers, activeParticleGroupCount);
            state->second.HasCompactedParticles = true;
        }
        resources.Emitters = std::move(nextEmitters);
        if (applySnapshot)
        {
            resources.ResetRevision = snapshot.ResetRevision();
            resources.MarkSnapshotApplied(snapshot.Revision());
        }
        if (consumeSimulation)
            resources.MarkSimulationStepConsumed(snapshot.SimulationStepRevision());
        resources.LastPreparedFrame = Statistics.Frame;
        Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());
        Statistics.VfxGpuBufferBytes = allocatedBufferBytes();
    }

} // namespace Keire::RenderBackend
