#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/ForwardPlusInternal.h"
#include "KeireInternal/Rendering/InstanceBatchInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"

#include "Keire/BuiltinSkinningShaders.h"
#include "Keire/BuiltinVfxShaders.h"
#include "Keire/Log.h"

#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace
{
    enum class BuiltinVfxShaderStage : std::uint8_t
    {
        Initialize,
        Reset,
        Kill,
        Transform,
        Simulate,
        Spawn,
        Finalize,
        Vertex,
        Fragment
    };

    struct EmbeddedShader final
    {
        const unsigned char* Code = nullptr;
        std::size_t Size = 0;
    };

    [[nodiscard]] EmbeddedShader SelectVfxShader(const BuiltinVfxShaderStage stage,
                                                 const SDL_GPUShaderFormat format) noexcept
    {
#define KEIRE_SELECT_VFX_SHADER(StageName)                                                                             \
    if (format == SDL_GPU_SHADERFORMAT_DXIL)                                                                           \
        return {Keire::Detail::BuiltinVfx##StageName##Dxil, sizeof(Keire::Detail::BuiltinVfx##StageName##Dxil)};       \
    if (format == SDL_GPU_SHADERFORMAT_SPIRV)                                                                          \
        return {Keire::Detail::BuiltinVfx##StageName##Spirv, sizeof(Keire::Detail::BuiltinVfx##StageName##Spirv)};     \
    if (format == SDL_GPU_SHADERFORMAT_MSL)                                                                            \
        return { Keire::Detail::BuiltinVfx##StageName##Msl, sizeof(Keire::Detail::BuiltinVfx##StageName##Msl) }
        switch (stage)
        {
        case BuiltinVfxShaderStage::Initialize:
            KEIRE_SELECT_VFX_SHADER(Initialize);
        case BuiltinVfxShaderStage::Reset:
            KEIRE_SELECT_VFX_SHADER(Reset);
        case BuiltinVfxShaderStage::Kill:
            KEIRE_SELECT_VFX_SHADER(Kill);
        case BuiltinVfxShaderStage::Transform:
            KEIRE_SELECT_VFX_SHADER(Transform);
        case BuiltinVfxShaderStage::Simulate:
            KEIRE_SELECT_VFX_SHADER(Simulate);
        case BuiltinVfxShaderStage::Spawn:
            KEIRE_SELECT_VFX_SHADER(Spawn);
        case BuiltinVfxShaderStage::Finalize:
            KEIRE_SELECT_VFX_SHADER(Finalize);
        case BuiltinVfxShaderStage::Vertex:
            KEIRE_SELECT_VFX_SHADER(Vertex);
        case BuiltinVfxShaderStage::Fragment:
            KEIRE_SELECT_VFX_SHADER(Fragment);
        }
#undef KEIRE_SELECT_VFX_SHADER
        return {};
    }

    class CallbackFrameGraphExecutionContext final : public Keire::RenderBackend::FrameGraphExecutionContext
    {
      public:
        using TransitionCallback = std::function<void(const Keire::RenderBackend::CompiledFrameGraph::Transition&)>;
        using PassCallback = std::function<void(Keire::RenderBackend::FrameGraphPass)>;

        CallbackFrameGraphExecutionContext(TransitionCallback transition, PassCallback pass)
            : m_Transition(std::move(transition)), m_Pass(std::move(pass))
        {
        }

        void Transition(const Keire::RenderBackend::CompiledFrameGraph::Transition& transition) override
        {
            m_Transition(transition);
        }

        void Execute(const Keire::RenderBackend::FrameGraphPass pass,
                     const Keire::RenderBackend::FrameGraphPassDescription&) override
        {
            m_Pass(pass);
        }

      private:
        TransitionCallback m_Transition;
        PassCallback m_Pass;
    };

    struct ClipPoint final
    {
        float X;
        float Y;
        float Z;
        float W;
    };

    [[nodiscard]] ClipPoint TransformClip(const Keire::Matrix4& matrix, const Keire::Vector3 point) noexcept
    {
        const auto& value = matrix.Elements;
        return {value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12],
                value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13],
                value[2] * point.X + value[6] * point.Y + value[10] * point.Z + value[14],
                value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15]};
    }

    [[nodiscard]] Keire::Vector3 Add(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    [[nodiscard]] Keire::Vector3 Subtract(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] Keire::Vector3 Scale(const Keire::Vector3 value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale, value.Z * scale};
    }

    [[nodiscard]] float Length(const Keire::Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] bool IntersectsFrustum(const Keire::Matrix4& clipFromLocal, const Keire::MeshBounds bounds) noexcept
    {
        const std::array corners{Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
        std::array<ClipPoint, corners.size()> clip{};
        std::ranges::transform(corners, clip.begin(),
                               [&](const auto corner) { return TransformClip(clipFromLocal, corner); });
        const auto all = [&](const auto predicate) { return std::ranges::all_of(clip, predicate); };
        return !all([](const auto point) { return point.X < -point.W; }) &&
               !all([](const auto point) { return point.X > point.W; }) &&
               !all([](const auto point) { return point.Y < -point.W; }) &&
               !all([](const auto point) { return point.Y > point.W; }) &&
               !all([](const auto point) { return point.Z < 0.0F; }) &&
               !all([](const auto point) { return point.Z > point.W; });
    }

    [[nodiscard]] float ProjectedHeight(const Keire::Matrix4& viewFromLocal, const Keire::Matrix4& projection,
                                        const Keire::MeshBounds bounds) noexcept
    {
        const Keire::Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                    (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                                    (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
        const Keire::Vector3 extent{(bounds.Maximum.X - bounds.Minimum.X) * 0.5F,
                                    (bounds.Maximum.Y - bounds.Minimum.Y) * 0.5F,
                                    (bounds.Maximum.Z - bounds.Minimum.Z) * 0.5F};
        const auto viewCenter = Keire::Math::TransformPoint(viewFromLocal, center);
        const float localRadius = std::sqrt(extent.X * extent.X + extent.Y * extent.Y + extent.Z * extent.Z);
        const auto& matrix = viewFromLocal.Elements;
        const float scaleX = std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
        const float scaleY = std::sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
        const float scaleZ = std::sqrt(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10]);
        const float radius = localRadius * std::max({scaleX, scaleY, scaleZ});
        return viewCenter.Z > 0.0001F ? 2.0F * radius * std::abs(projection.Elements[5]) / viewCenter.Z : 1.0F;
    }
} // namespace

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
                return output == VfxValueType::Scalar && inputs.size() == 2 && allScalar();
            case VfxValueOpcode::Clamp:
            case VfxValueOpcode::Lerp:
            case VfxValueOpcode::Smoothstep:
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
                return output == VfxValueType::Scalar && inputs.size() == 1 && allScalar();
            case VfxValueOpcode::Compare:
                return output == VfxValueType::Boolean && inputs.size() == 2 && allScalar();
            case VfxValueOpcode::BooleanAnd:
            case VfxValueOpcode::BooleanOr:
                return output == VfxValueType::Boolean && inputs.size() == 2 && inputs[0] == VfxValueType::Boolean &&
                       inputs[1] == VfxValueType::Boolean;
            case VfxValueOpcode::BooleanNot:
                return output == VfxValueType::Boolean && inputs.size() == 1 && inputs[0] == VfxValueType::Boolean;
            case VfxValueOpcode::Select:
                return inputs.size() == 3 && inputs[0] == VfxValueType::Boolean && inputs[1] == output &&
                       inputs[2] == output;
            case VfxValueOpcode::Combine:
                return output == VfxValueType::Vector3 && inputs.size() == 3 && allScalar();
            case VfxValueOpcode::Split:
                return output == VfxValueType::Scalar && outputIndex < 3 && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::Vector3;
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
                return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Vector3;
            case VfxValueOpcode::Time:
            case VfxValueOpcode::DeltaTime:
            case VfxValueOpcode::Age:
            case VfxValueOpcode::Lifetime:
                return output == VfxValueType::Scalar && inputs.empty();
            case VfxValueOpcode::ParticleId:
            case VfxValueOpcode::SpawnIndex:
                return output == VfxValueType::UnsignedInteger && inputs.empty();
            case VfxValueOpcode::ToFloat:
                return output == VfxValueType::Scalar && inputs.size() == 1 &&
                       (inputs[0] == VfxValueType::Integer || inputs[0] == VfxValueType::UnsignedInteger);
            case VfxValueOpcode::ToInteger:
                return output == VfxValueType::Integer && inputs.size() == 1 && inputs[0] == VfxValueType::Scalar;
            case VfxValueOpcode::ToUnsignedInteger:
                return output == VfxValueType::UnsignedInteger && inputs.size() == 1 &&
                       inputs[0] == VfxValueType::Scalar;
            }
            return false;
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
        if (payload.Parameters.size() > maximumBufferBytes / sizeof(VfxGpuValue) ||
            payload.CustomInstructions.size() > maximumBufferBytes / sizeof(VfxGpuCustomInstructionRecord) ||
            payload.ParticleOperations.size() > maximumBufferBytes / sizeof(VfxGpuParticleOperationRecord))
        {
            return "GPU VFX execution table exceeds SDL's 32-bit storage-buffer limit.";
        }

        std::array<std::optional<VfxValueType>, VfxCompiledGpuValueProgram::MaximumRegisters> registerTypes{};
        for (std::size_t instructionIndex = 0; instructionIndex < program.Instructions.size(); ++instructionIndex)
        {
            const auto& instruction = program.Instructions[instructionIndex];
            if (instruction.Header[0] > static_cast<std::uint32_t>(VfxValueOpcode::Sign))
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
            if (sourceCount > 4 || firstSource > program.Sources.size() ||
                sourceCount > program.Sources.size() - firstSource)
                return IndexedGpuPayloadError("instruction", instructionIndex, "references an invalid source span.");
            if ((instruction.Settings[2] & ~0xfU) != 0U)
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses unknown setting flags.");
            if (instruction.Settings[1] > static_cast<std::uint32_t>(VfxRandomScope::PerParticleStrip))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown Random scope.");
            if ((opcode == VfxValueOpcode::Random || opcode == VfxValueOpcode::RandomRange) &&
                instruction.Settings[1] == static_cast<std::uint32_t>(VfxRandomScope::PerParticleStrip))
                return IndexedGpuPayloadError("instruction", instructionIndex,
                                              "requires particle-strip identity, which this renderer does not expose.");
            if (instruction.Settings[3] > static_cast<std::uint32_t>(VfxComparisonCondition::Greater))
                return IndexedGpuPayloadError("instruction", instructionIndex, "uses an unknown comparison mode.");

            std::array<VfxValueType, 4> inputTypes{};
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

        for (std::size_t operationIndex = 0; operationIndex < payload.ParticleOperations.size(); ++operationIndex)
        {
            const auto& operation = payload.ParticleOperations[operationIndex];
            if (operation.Context > VfxContextType::Output || operation.Kind > VfxGpuParticleOperationKind::CustomHlsl)
                return IndexedGpuPayloadError("particle operation", operationIndex, "is malformed.");
            if (operation.Kind == VfxGpuParticleOperationKind::CustomHlsl)
            {
                if (operation.Index >= payload.CustomInstructions.size() ||
                    payload.CustomInstructions[operation.Index].Context != operation.Context)
                    return IndexedGpuPayloadError("particle operation", operationIndex,
                                                  "references an invalid Custom HLSL instruction.");
                continue;
            }
            if (operation.Index != 0)
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
        }
        return std::nullopt;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateGpuVfxPipeline(const SDL_GPUSampleCount samples)
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("The active GPU backend cannot compile built-in VFX shaders.");
        const auto createShader = [this, format](const BuiltinVfxShaderStage stage, const bool vertex)
        {
            const auto embedded = SelectVfxShader(stage, format);
            SDL_GPUShaderCreateInfo information{};
            information.code = embedded.Code;
            information.code_size = embedded.Size;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.format = format;
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_storage_buffers = vertex ? 2U : 0U;
            information.num_uniform_buffers = vertex ? 1U : 0U;
            auto* result = SDL_CreateGPUShader(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUShader(GPU VFX) failed: " + LastSdlError());
            return result;
        };

        auto* vertex = createShader(BuiltinVfxShaderStage::Vertex, true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = createShader(BuiltinVfxShaderStage::Fragment, false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = true;

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = samples;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = false;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU VFX) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    bool RenderSharedState::EnsureGpuVfxPipelines()
    {
        if (VfxPipelinesAttempted)
            return VfxInitializePipeline && VfxResetPipeline && VfxKillPipeline && VfxTransformPipeline &&
                   VfxSimulatePipeline && VfxSpawnPipeline && VfxFinalizePipeline;
        VfxPipelinesAttempted = true;

        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            return false;

        const auto create = [this, format](const BuiltinVfxShaderStage stage, const char* entrypoint,
                                           const std::uint32_t threads, const bool usesExecutionTables = false)
        {
            const auto shader = SelectVfxShader(stage, format);
            SDL_GPUComputePipelineCreateInfo information{};
            information.code = shader.Code;
            information.code_size = shader.Size;
            information.entrypoint = entrypoint;
            information.format = format;
            information.num_readonly_storage_buffers = usesExecutionTables ? 6U : 0U;
            information.num_readwrite_storage_buffers = 5;
            information.num_uniform_buffers = 1;
            information.threadcount_x = threads;
            information.threadcount_y = 1;
            information.threadcount_z = 1;
            return SDL_CreateGPUComputePipeline(Device, &information);
        };
        VfxInitializePipeline = create(BuiltinVfxShaderStage::Initialize, "CSInitialize", 256);
        VfxResetPipeline = create(BuiltinVfxShaderStage::Reset, "CSReset", 1);
        VfxKillPipeline = create(BuiltinVfxShaderStage::Kill, "CSKill", 256);
        VfxTransformPipeline = create(BuiltinVfxShaderStage::Transform, "CSTransform", 256);
        VfxSimulatePipeline = create(BuiltinVfxShaderStage::Simulate, "CSSimulate", 256, true);
        VfxSpawnPipeline = create(BuiltinVfxShaderStage::Spawn, "CSSpawn", 256, true);
        VfxFinalizePipeline = create(BuiltinVfxShaderStage::Finalize, "CSFinalize", 1);
        return VfxInitializePipeline && VfxResetPipeline && VfxKillPipeline && VfxTransformPipeline &&
               VfxSimulatePipeline && VfxSpawnPipeline && VfxFinalizePipeline;
    }

    void RenderSharedState::ReleaseGpuVfxWorld(GpuVfxWorldResources& resources) noexcept
    {
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

    void RenderSharedState::PrepareGpuVfx(SDL_GPUCommandBuffer* commands, const VfxRenderSnapshot& snapshot)
    {
        if (snapshot.WorldId() == 0 || snapshot.ParticleCapacity() == 0 || !EnsureGpuVfxPipelines())
            return;
        auto& resources = GpuVfxWorlds[snapshot.WorldId()];
        if (resources.Capacity != snapshot.ParticleCapacity())
        {
            ReleaseGpuVfxWorld(resources);
            const auto create = [this](const std::uint64_t size, const SDL_GPUBufferUsageFlags usage)
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
            constexpr std::uint64_t particleStride = 144;
            const auto capacity = snapshot.ParticleCapacity();
            try
            {
                resources.Particles = create(particleStride * capacity, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                                                            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.FreeIndices =
                    create(sizeof(std::uint32_t) * capacity, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.AliveIndices =
                    create(sizeof(std::uint32_t) * capacity,
                           SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.Counters = create(5U * sizeof(std::uint32_t), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.IndirectArguments =
                    create(sizeof(SDL_GPUIndirectDrawCommand),
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
        };
        static_assert(offsetof(Dispatch, Identity) == 208);
        static_assert(offsetof(Dispatch, ValueProgramMetadata) == 224);
        static_assert(offsetof(Dispatch, ValueRuntimeMetadata) == 240);
        static_assert(offsetof(Dispatch, ValueSystemIdentity) == 256);
        static_assert(offsetof(Dispatch, ValueSimulationMetadata) == 272);
        static_assert(offsetof(Dispatch, ValueRuntimeTime) == 288);
        static_assert(sizeof(Dispatch) == 304);
        Dispatch dispatch;
        dispatch.Capacity = resources.Capacity;
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
                particleOperations.push_back({{{
                    static_cast<std::uint32_t>(operation.Context),
                    static_cast<std::uint32_t>(operation.Kind),
                    operation.Index,
                    0U,
                }}});
            }

            const auto release = [device = Device](GpuVfxExecutionBuffers* buffers) noexcept
            {
                if (buffers->ParticleOperations)
                    SDL_ReleaseGPUBuffer(device, buffers->ParticleOperations);
                if (buffers->CustomInstructions)
                    SDL_ReleaseGPUBuffer(device, buffers->CustomInstructions);
                if (buffers->Parameters)
                    SDL_ReleaseGPUBuffer(device, buffers->Parameters);
                if (buffers->Constants)
                    SDL_ReleaseGPUBuffer(device, buffers->Constants);
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
            result->Constants = uploadRecords(std::span(execution->ValueProgram.Constants));
            result->Parameters = uploadRecords(std::span(execution->Parameters));
            result->CustomInstructions =
                uploadRecords(std::span<const VfxGpuCustomInstructionRecord>(customInstructions));
            result->ParticleOperations =
                uploadRecords(std::span<const VfxGpuParticleOperationRecord>(particleOperations));
            result->ByteSize =
                std::max<std::size_t>(execution->ValueProgram.Instructions.size(), 1) * sizeof(VfxGpuValueInstruction) +
                std::max<std::size_t>(execution->ValueProgram.Sources.size(), 1) * sizeof(VfxGpuValueSource) +
                std::max<std::size_t>(execution->ValueProgram.Constants.size(), 1) * sizeof(VfxGpuValue) +
                std::max<std::size_t>(execution->Parameters.size(), 1) * sizeof(VfxGpuValue) +
                std::max<std::size_t>(customInstructions.size(), 1) * sizeof(VfxGpuCustomInstructionRecord) +
                std::max<std::size_t>(particleOperations.size(), 1) * sizeof(VfxGpuParticleOperationRecord);
            resources.ExecutionCache.emplace(execution.get(), result);
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

        const std::array writeBindings{
            SDL_GPUStorageBufferReadWriteBinding{resources.Particles, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.FreeIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.AliveIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.Counters, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments, false},
        };
        const auto dispatchCompute = [&](SDL_GPUComputePipeline* pipeline, const std::uint32_t groupCount,
                                         const GpuVfxExecutionBuffers* execution = nullptr)
        {
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                 static_cast<std::uint32_t>(writeBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(VFX) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, pipeline);
            if (execution)
            {
                const std::array readBindings{execution->Instructions,       execution->Sources,
                                              execution->Constants,          execution->Parameters,
                                              execution->CustomInstructions, execution->ParticleOperations};
                SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                                 static_cast<std::uint32_t>(readBindings.size()));
            }
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
            ++Statistics.VfxComputeDispatches;
        };

        const auto allocatedBufferBytes = [this]() noexcept
        {
            std::uint64_t result = 0;
            for (const auto& [world, liveResources] : GpuVfxWorlds)
            {
                (void)world;
                result += static_cast<std::uint64_t>(liveResources.Capacity) * (144U + sizeof(std::uint32_t) * 2U) +
                          5U * sizeof(std::uint32_t) + sizeof(SDL_GPUIndirectDrawCommand);
                for (const auto& [payload, cached] : liveResources.ExecutionCache)
                {
                    (void)payload;
                    if (const auto buffers = cached.lock())
                        result += buffers->ByteSize;
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
            if (state.Execution != emitter.Execution || !state.ExecutionBuffers)
            {
                state.ExecutionBuffers = acquireExecutionBuffers(emitter.Execution);
                state.Execution = emitter.Execution;
            }
            dispatch.DeltaSeconds = consumeSimulation ? simulationDelta(emitter) : 0.0F;
            dispatch.Seed = emitter.Seed;
            dispatch.Position = {emitter.Position.X, emitter.Position.Y, emitter.Position.Z, 0.0F};
            dispatch.Rotation = {emitter.Rotation.X, emitter.Rotation.Y, emitter.Rotation.Z, emitter.Rotation.W};
            dispatch.AccelerationShape = {emitter.Acceleration.X, emitter.Acceleration.Y, emitter.Acceleration.Z, 0.0F};
            dispatch.ShapeRotationParameters = {emitter.ConeAngleDegrees, emitter.ConeLength, emitter.RotationMinimum.Z,
                                                emitter.RotationMaximum.Z};
            dispatch.ColorStart = {emitter.ColorStart.Red, emitter.ColorStart.Green, emitter.ColorStart.Blue,
                                   emitter.ColorStart.Alpha};
            dispatch.ColorEnd = {emitter.ColorEnd.Red, emitter.ColorEnd.Green, emitter.ColorEnd.Blue,
                                 emitter.ColorEnd.Alpha};
            dispatch.Size = {emitter.SizeStart, emitter.SizeEnd,
                             std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Space)), 0.0F};
            dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(), 0U, 0U};
            setExecutionDispatch(emitter);
            dispatchCompute(VfxSimulatePipeline, (resources.Capacity + 255U) / 256U, state.ExecutionBuffers.get());
        }

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
                dispatch.AccelerationShape = {emitter.Acceleration.X, emitter.Acceleration.Y, emitter.Acceleration.Z,
                                              std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Shape))};
                dispatch.ShapeRotationParameters = {emitter.ConeAngleDegrees, emitter.ConeLength,
                                                    emitter.RotationMinimum.Z, emitter.RotationMaximum.Z};
                dispatch.ColorStart = {emitter.ColorStart.Red, emitter.ColorStart.Green, emitter.ColorStart.Blue,
                                       emitter.ColorStart.Alpha};
                dispatch.ColorEnd = {emitter.ColorEnd.Red, emitter.ColorEnd.Green, emitter.ColorEnd.Blue,
                                     emitter.ColorEnd.Alpha};
                dispatch.Size = {emitter.SizeStart, emitter.SizeEnd,
                                 std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Space)), 0.0F};
                dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(),
                                     static_cast<std::uint32_t>(firstSpawnSequence),
                                     static_cast<std::uint32_t>(firstSpawnSequence >> 32U)};
                setExecutionDispatch(emitter);
                dispatchCompute(VfxSpawnPipeline, (dispatch.SpawnCount + 255U) / 256U, state.ExecutionBuffers.get());
            }
        }

        dispatchCompute(VfxFinalizePipeline, 1);
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

    bool RenderSharedState::EnsureSkinningPipeline()
    {
        if (SkinningPipelineAttempted)
            return SkinningPipeline != nullptr;
        SkinningPipelineAttempted = true;

        const auto formats = SDL_GetGPUShaderFormats(Device);
        const unsigned char* code = nullptr;
        std::size_t codeSize = 0;
        SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
        if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeDxil;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeDxil);
            format = SDL_GPU_SHADERFORMAT_DXIL;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeSpirv;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeSpirv);
            format = SDL_GPU_SHADERFORMAT_SPIRV;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeMsl;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeMsl);
            format = SDL_GPU_SHADERFORMAT_MSL;
        }
        if (!code)
            return false;

        SDL_GPUComputePipelineCreateInfo createInfo{};
        createInfo.code = code;
        createInfo.code_size = codeSize;
        createInfo.entrypoint = "CSMain";
        createInfo.format = format;
        createInfo.num_readonly_storage_buffers = 3;
        createInfo.num_readwrite_storage_buffers = 2;
        createInfo.num_uniform_buffers = 1;
        createInfo.threadcount_x = 64;
        createInfo.threadcount_y = 1;
        createInfo.threadcount_z = 1;
        SkinningPipeline = SDL_CreateGPUComputePipeline(Device, &createInfo);
        return SkinningPipeline != nullptr;
    }

    void RenderSharedState::PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet)
    {
        struct alignas(16) GpuSkinInfluence
        {
            std::array<std::uint32_t, 4> Bones0{};
            std::array<std::uint32_t, 4> Bones1{};
            std::array<float, 4> Weights0{};
            std::array<float, 4> Weights1{};
        };
        struct alignas(16) GpuSkinMatrix
        {
            std::array<float, 4> Column0{};
            std::array<float, 4> Column1{};
            std::array<float, 4> Column2{};
            std::array<float, 4> Column3{};
        };
        struct SkinDispatch
        {
            std::uint32_t VertexCount = 0;
            std::uint32_t InfluenceCount = 4;
            std::uint32_t SkinningMode = 0;
            std::uint32_t Padding = 0;
        };
        static_assert(sizeof(GpuSkinInfluence) == 64);
        static_assert(alignof(GpuSkinInfluence) == 16);
        static_assert(sizeof(GpuSkinMatrix) == 64);
        static_assert(alignof(GpuSkinMatrix) == 16);
        static_assert(sizeof(SkinDispatch) == 16);
        static_assert(sizeof(GpuMeshVertex) == 80);
        static_assert(sizeof(GpuRenderVertex) == 48);

        const auto createOutput = [this](const std::uint32_t vertexCount)
        {
            const auto assetBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuMeshVertex);
            const auto builtinBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuRenderVertex);
            if (assetBytes > std::numeric_limits<std::uint32_t>::max() ||
                builtinBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Skinned mesh output exceeds SDL's 32-bit buffer limit.");
            }

            GpuSkinOutputResources result;
            const auto createBuffer = [this](const std::uint32_t bytes)
            {
                SDL_GPUBufferCreateInfo createInfo{};
                createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
                createInfo.size = bytes;
                auto* buffer = SDL_CreateGPUBuffer(Device, &createInfo);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer(skin cache) failed: " + LastSdlError());
                return buffer;
            };
            try
            {
                result.AssetVertices = createBuffer(static_cast<std::uint32_t>(assetBytes));
                result.BuiltinVertices = createBuffer(static_cast<std::uint32_t>(builtinBytes));
                return result;
            }
            catch (...)
            {
                if (result.BuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.BuiltinVertices);
                if (result.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                throw;
            }
        };

        for (auto& item : packet.DrawItems)
        {
            item.SkinnedAssetVertices = nullptr;
            item.SkinnedBuiltinVertices = nullptr;
            if (!Assets || !item.Skin || item.SkinPalette.empty())
                continue;

            auto [cacheIterator, inserted] = SkinCache.try_emplace(item.Skin);
            (void)inserted;
            auto& cache = cacheIterator->second;
            cache.LastRequestedFrame = Statistics.Frame;

            const auto skinHandle = Assets->Load<SkinnedMeshAsset>(item.Skin, AssetPriority::High);
            const auto skin = skinHandle.TryGetLoaded();
            if (!skin)
                continue;
            const auto meshHandle = Assets->Load<MeshAsset>(skin->Mesh(), AssetPriority::High);
            const auto meshAsset = meshHandle.TryGetLoaded();
            if (!meshAsset || skinHandle.Revision() == 0 || meshHandle.Revision() == 0)
                continue;

            auto dependencyStamp = std::uint64_t{1469598103934665603ULL};
            dependencyStamp = HashDependencyStamp(dependencyStamp, item.Skin);
            dependencyStamp = HashDependencyStamp(dependencyStamp, skinHandle.Revision());
            dependencyStamp = HashDependencyStamp(dependencyStamp, skin->Mesh());
            dependencyStamp = HashDependencyStamp(dependencyStamp, meshHandle.Revision());
            if (dependencyStamp != cache.LastAttemptedDependencyStamp)
            {
                cache.LastAttemptedDependencyStamp = dependencyStamp;
                bool valid = skin->Influences8().size() == meshAsset->Vertices().size() &&
                             !meshAsset->Vertices().empty() &&
                             meshAsset->Vertices().size() <= std::numeric_limits<std::uint32_t>::max();
                std::uint32_t maximumBoneIndex = 0;
                if (valid)
                {
                    for (const auto& influence : skin->Influences8())
                    {
                        if (influence.Count == 0 || influence.Count > influence.Bones.size())
                        {
                            valid = false;
                            break;
                        }
                        for (std::size_t index = 0; index < influence.Count; ++index)
                        {
                            if (!std::isfinite(influence.Weights[index]) || influence.Weights[index] < 0.0F)
                            {
                                valid = false;
                                break;
                            }
                            maximumBoneIndex =
                                std::max(maximumBoneIndex, static_cast<std::uint32_t>(influence.Bones[index]));
                        }
                        if (!valid)
                            break;
                    }
                }

                try
                {
                    GpuSkinResources replacement;
                    replacement.Valid = valid;
                    if (valid)
                    {
                        replacement.VertexCount = static_cast<std::uint32_t>(meshAsset->Vertices().size());
                        replacement.MaximumBoneIndex = maximumBoneIndex;
                        replacement.MaximumInfluences = skin->MaximumInfluences();
                        const auto* driver = SDL_GetGPUDeviceDriver(Device);
                        if (driver && SupportsComputeSkinning(driver, skin->Method()) && EnsureSkinningPipeline())
                        {
                            std::vector<GpuSkinInfluence> influences(skin->Influences8().size());
                            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
                            {
                                const auto& source = skin->Influences8()[vertex];
                                for (std::size_t influence = 0; influence < source.Count; ++influence)
                                {
                                    if (influence < 4)
                                    {
                                        influences[vertex].Bones0[influence] = source.Bones[influence];
                                        influences[vertex].Weights0[influence] = source.Weights[influence];
                                    }
                                    else
                                    {
                                        influences[vertex].Bones1[influence - 4] = source.Bones[influence];
                                        influences[vertex].Weights1[influence - 4] = source.Weights[influence];
                                    }
                                }
                            }
                            replacement.Influences = UploadBuffer(commands, std::as_bytes(std::span(influences)),
                                                                  SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
                        }
                    }
                    Retire(std::exchange(cache.Resources, std::move(replacement)));
                    cache.Skin = skin;
                    cache.Mesh = meshAsset;
                    cache.LoadedDependencyStamp = dependencyStamp;
                    ++SkinningStaticBuilds;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR("Skin GPU cache rebuild failed for id={} dependency={}: {}", item.Skin.ToString(),
                                     dependencyStamp, error.what());
                }
            }

            if (cache.LoadedDependencyStamp != dependencyStamp || !cache.Skin || !cache.Mesh ||
                !cache.Resources.Valid || cache.Skin->Mesh() != item.Mesh ||
                cache.Skin->Skeleton() != item.SkinSkeleton ||
                cache.Resources.VertexCount != cache.Mesh->Vertices().size() ||
                cache.Resources.MaximumBoneIndex >= item.SkinPalette.size() ||
                !std::ranges::all_of(item.SkinPalette, [](const Matrix4& matrix) { return Math::IsFinite(matrix); }))
            {
                continue;
            }

            item.Skinning = cache.Skin->Method();
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty() || !mesh.AssetVertices)
                continue;

            const auto useCompute = cache.Resources.Influences && SkinningPipeline;
            if (!useCompute)
            {
                std::vector<MeshVertex> deformed(cache.Mesh->Vertices().size());
                SkinMeshCpu(cache.Mesh->Vertices(), cache.Skin->Influences8(), item.SkinPalette, item.Skinning,
                            deformed);
                const auto& sourceBounds = cache.Mesh->Bounds();
                const auto sourceMagnitude =
                    std::max({1.0F, std::abs(sourceBounds.Minimum.X), std::abs(sourceBounds.Minimum.Y),
                              std::abs(sourceBounds.Minimum.Z), std::abs(sourceBounds.Maximum.X),
                              std::abs(sourceBounds.Maximum.Y), std::abs(sourceBounds.Maximum.Z)});
                const auto maximumCoordinate = sourceMagnitude * 8.0F;
                const auto validDeformation =
                    std::ranges::all_of(deformed,
                                        [maximumCoordinate](const MeshVertex& vertex)
                                        {
                                            return Math::IsFinite(vertex.Position) && Math::IsFinite(vertex.Normal) &&
                                                   Math::IsFinite(vertex.Tangent) &&
                                                   std::abs(vertex.Position.X) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Y) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Z) <= maximumCoordinate;
                                        });
                if (!validDeformation)
                    continue;
                std::vector<RenderVertex> builtinVertices;
                builtinVertices.reserve(deformed.size());
                for (const auto& vertex : deformed)
                {
                    builtinVertices.push_back(
                        {vertex.Position,
                         {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                         vertex.Normal});
                }
                item.SkinnedAssetVertices = UploadMeshVertexBuffer(commands, deformed);
                item.SkinnedBuiltinVertices = UploadVertexBuffer(commands, builtinVertices);
                FrameTransientBuffers.push_back(item.SkinnedAssetVertices);
                FrameTransientBuffers.push_back(item.SkinnedBuiltinVertices);
                continue;
            }

            std::vector<GpuSkinMatrix> palette;
            palette.reserve(item.SkinPalette.size());
            for (const auto& matrix : item.SkinPalette)
            {
                const auto& elements = matrix.Elements;
                palette.push_back({
                    {elements[0], elements[1], elements[2], elements[3]},
                    {elements[4], elements[5], elements[6], elements[7]},
                    {elements[8], elements[9], elements[10], elements[11]},
                    {elements[12], elements[13], elements[14], elements[15]},
                });
            }
            auto* paletteBuffer =
                UploadBuffer(commands, std::as_bytes(std::span(palette)), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
            FrameTransientBuffers.push_back(paletteBuffer);

            const GpuSkinInstanceKey instanceKey{packet.Scene, item.Entity};
            auto [instanceIterator, instanceInserted] = cache.Resources.Instances.try_emplace(instanceKey);
            auto& instance = instanceIterator->second;
            if (instanceInserted)
                instance.Outputs.resize(Specification.MaximumFramesInFlight);
            instance.LastPreparedFrame = Statistics.Frame;
            auto& output =
                instance.Outputs[SkinningOutputSlot(ActiveGpuSubmissionSerial, Specification.MaximumFramesInFlight)];
            if (output.Empty())
            {
                output = createOutput(cache.Resources.VertexCount);
                ++SkinningOutputBuilds;
            }
            item.SkinnedAssetVertices = output.AssetVertices;
            item.SkinnedBuiltinVertices = output.BuiltinVertices;

            const std::array writeBindings{SDL_GPUStorageBufferReadWriteBinding{item.SkinnedAssetVertices, false},
                                           SDL_GPUStorageBufferReadWriteBinding{item.SkinnedBuiltinVertices, false}};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                 static_cast<std::uint32_t>(writeBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(skin cache) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, SkinningPipeline);
            const std::array readBindings{mesh.AssetVertices, cache.Resources.Influences, paletteBuffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                             static_cast<std::uint32_t>(readBindings.size()));
            const SkinDispatch dispatch{cache.Resources.VertexCount, cache.Resources.MaximumInfluences};
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (dispatch.VertexCount + 63U) / 64U, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    PreparedSceneDrawLists RenderSharedState::PrepareSceneDrawLists(SDL_GPUCommandBuffer* commands,
                                                                    RenderSurfaceState& surface,
                                                                    const SceneRenderPacket& packet)
    {
        PreparedSceneDrawLists result;
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        const auto& camera = packet.Camera;
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Submeshes.empty())
                continue;
            const auto viewFromLocal = Math::Multiply(camera.View, item.World);
            const auto clipFromLocal = Math::Multiply(camera.Projection, viewFromLocal);
            std::uint32_t firstSubmesh = 0;
            std::uint32_t submeshCount = static_cast<std::uint32_t>(mesh.Submeshes.size());
            if (!mesh.Lods.empty())
            {
                const auto height = ProjectedHeight(viewFromLocal, camera.Projection, mesh.Lods.front().Bounds);
                const auto selected =
                    std::ranges::find_if(mesh.Lods, [&](const auto& lod) { return height >= lod.MinimumScreenHeight; });
                const auto& lod = selected != mesh.Lods.end() ? *selected : mesh.Lods.back();
                firstSubmesh = lod.FirstSubmesh;
                submeshCount = lod.SubmeshCount;
            }
            for (std::uint32_t offset = 0; offset < submeshCount; ++offset)
            {
                const auto submeshIndex = firstSubmesh + offset;
                const auto& submesh = mesh.Submeshes[submeshIndex];
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                MaterialSurfaceState surfaceState;
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr)
                    surfaceState = material->Surface;
                if (!IntersectsFrustum(clipFromLocal, submesh.Bounds))
                {
                    ++Statistics.CulledSubmeshes;
                    continue;
                }
                const Vector3 center{(submesh.Bounds.Minimum.X + submesh.Bounds.Maximum.X) * 0.5F,
                                     (submesh.Bounds.Minimum.Y + submesh.Bounds.Maximum.Y) * 0.5F,
                                     (submesh.Bounds.Minimum.Z + submesh.Bounds.Maximum.Z) * 0.5F};
                auto& destination =
                    surfaceState.AlphaMode == MaterialAlphaMode::Blend ? result.Transparent : result.Opaque;
                destination.Draws.push_back({&item, submesh, materialId, surfaceState,
                                             Math::TransformPoint(viewFromLocal, center).Z, submeshIndex});
                ++Statistics.VisibleSubmeshes;
            }
        }

        const auto sortDraws = [](std::vector<PreparedSceneDraw>& draws)
        {
            std::ranges::stable_sort(
                draws,
                [](const PreparedSceneDraw& left, const PreparedSceneDraw& right)
                {
                    const bool blended = left.Surface.AlphaMode == MaterialAlphaMode::Blend;
                    if (blended && left.Depth != right.Depth)
                        return Detail::TransparentBackToFront(left.Depth, right.Depth);
                    if (!blended)
                    {
                        const auto leftKey =
                            std::tie(left.Surface.AlphaMode, left.Material, left.Item->Mesh, left.SubmeshIndex,
                                     left.Item->ReceiveShadows, left.Item->CastShadows, left.Depth);
                        const auto rightKey =
                            std::tie(right.Surface.AlphaMode, right.Material, right.Item->Mesh, right.SubmeshIndex,
                                     right.Item->ReceiveShadows, right.Item->CastShadows, right.Depth);
                        if (leftKey != rightKey)
                            return leftKey < rightKey;
                    }
                    if (left.Item->Entity != right.Item->Entity)
                        return left.Item->Entity < right.Item->Entity;
                    return left.SubmeshIndex < right.SubmeshIndex;
                });
        };
        sortDraws(result.Opaque.Draws);
        sortDraws(result.Transparent.Draws);

        const auto prepareBatches = [&](PreparedSceneDrawList& list)
        {
            std::vector<InstanceBatchKey> instanceKeys;
            instanceKeys.reserve(list.Draws.size());
            for (const auto& draw : list.Draws)
            {
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                instanceKeys.push_back({draw.Item->Mesh, draw.Material, draw.SubmeshIndex, draw.Surface.AlphaMode,
                                        draw.Item->ReceiveShadows, draw.Item->CastShadows,
                                        material && material->UsesInstancing && !draw.Item->SkinnedAssetVertices});
            }
            const auto batches = BuildInstanceBatches(instanceKeys);
            list.Batches.reserve(batches.size());
            for (const auto batch : batches)
            {
                const auto drawIndex = static_cast<std::size_t>(batch.First);
                const auto& draw = list.Draws[drawIndex];
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                SDL_GPUBuffer* instanceBuffer = nullptr;
                if (material && material->UsesInstancing)
                {
                    std::vector<GpuInstanceUniform> instances;
                    instances.reserve(batch.Count);
                    for (std::uint32_t instance = 0; instance < batch.Count; ++instance)
                    {
                        const auto& instanceDraw = list.Draws[drawIndex + instance];
                        instances.push_back({instanceDraw.Item->World,
                                             Transpose(Math::Inverse(instanceDraw.Item->World)),
                                             instanceDraw.Item->Tint});
                    }
                    instanceBuffer = UploadBuffer(commands, std::as_bytes(std::span(instances)),
                                                  SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                    FrameTransientBuffers.push_back(instanceBuffer);
                }
                list.Batches.push_back({batch.First, batch.Count, batch.GpuFirstInstance(), instanceBuffer});
            }
        };
        prepareBatches(result.Opaque);
        prepareBatches(result.Transparent);

        if (packet.Environment.Environment)
            (void)ResolveTexture(packet.Environment.Environment);
        for (const auto& particle : packet.Vfx.Particles())
            if (particle.Renderer != VfxRendererType::Sprite && particle.Mesh)
                (void)ResolveMesh(particle.Mesh);
        return result;
    }

    void RenderSharedState::DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                      const ShadowFrameData& shadows, const SceneDrawPhase phase,
                                      const PreparedSceneDrawList& prepared)
    {
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;
        if (surface.ForwardPlus.Empty())
            throw std::logic_error("Forward+ GPU resources were not prepared before scene recording.");
        std::array<SDL_GPUBuffer*, 3> forwardPlusBuffers{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                         surface.ForwardPlus.LightIndices};

        if (phase == SceneDrawPhase::Opaque && packet.Environment.SkyVisible && pipelines.Sky)
        {
            const auto& environment =
                packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
            if (!environment.Empty())
            {
                const SkyUniforms sky{
                    Math::Inverse(camera.Projection),
                    Math::Inverse(camera.View),
                    {packet.Environment.EnvironmentRotationDegrees, packet.Environment.EnvironmentSpecularIntensity,
                     packet.Environment.Exposure,
                     static_cast<float>(environment.EnvironmentLayout) + (environment.HdrEncoded ? 16.0F : 0.0F)}};
                const SDL_GPUTextureSamplerBinding binding{environment.Texture, environment.Sampler};
                SDL_PushGPUFragmentUniformData(commands, 0, &sky, sizeof(sky));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Sky);
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                ++Statistics.DrawCalls;
            }
        }

        if (phase == SceneDrawPhase::Opaque && packet.DrawGrid && GridBuffer && GridVertexCount > 0)
        {
            const ObjectUniforms object =
                MakeObjectUniforms(Math::Multiply(camera.Projection, camera.View), {}, {}, {1.0F, 1.0F, 1.0F, 1.0F},
                                   lighting, packet.Environment, false);
            const AssetShadowUniforms noShadows{};
            const AssetLocalLightUniforms noLocalLights{};
            const std::array shadowBindings{SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler},
                                            SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler}};
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            SDL_PushGPUFragmentUniformData(commands, 0, &noShadows, sizeof(noShadows));
            SDL_PushGPUFragmentUniformData(commands, 1, &noLocalLights, sizeof(noLocalLights));
            const SDL_GPUBufferBinding binding{GridBuffer, 0};
            SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
            SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                        static_cast<std::uint32_t>(shadowBindings.size()));
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, GridVertexCount, 1, 0, 0);
            ++Statistics.DrawCalls;
        }

        AssetLocalLightUniforms localLights{};
        const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
        localLights.Counts.X = static_cast<float>(packet.LocalLights.size());
        localLights.Counts.Y = static_cast<float>(surface.ForwardPlus.Columns);
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            auto& uniform = localLights.Lights[lightIndex];
            uniform.PositionRange = {light.Position.X, light.Position.Y, light.Position.Z, light.Range};
            uniform.DirectionOuter = {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine};
            uniform.ColorIntensity = {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green,
                                      light.ColorAndIntensity.Blue, light.ColorAndIntensity.Alpha};
            uniform.Parameters = {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F,
                                  0.0F};
        }
        enum class FragmentSlot2Binding : std::uint8_t
        {
            None,
            LocalLights,
            Shadows
        };
        auto fragmentSlot2Binding = FragmentSlot2Binding::None;
        AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
        AssetShadowUniforms disabledShadowUniforms{};
        for (auto& parameters : disabledShadowUniforms.Local.Parameters)
            parameters.X = -1.0F;
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex], light.ShadowStrength,
                                                           light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                           std::max(light.ShadowBias * 0.01F, 0.0001F)};
        }

        for (const auto& batch : prepared.Batches)
        {
            const auto drawIndex = static_cast<std::size_t>(batch.First);
            const auto& draw = prepared.Draws[drawIndex];
            const auto& item = *draw.Item;
            const auto& mesh = ResolveMesh(item.Mesh);
            const auto viewModel = Math::Multiply(camera.View, item.World);
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            const auto instanceCount = batch.Count;
            if (material)
            {
                SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
                if (material->UsesForwardPlus)
                    SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                                      static_cast<std::uint32_t>(forwardPlusBuffers.size()));
                const AssetObjectUniforms object{item.World, camera.View, camera.Projection,
                                                 Transpose(Math::Inverse(item.World))};
                AssetSceneUniforms scene{};
                scene.AmbientColorIntensity = {
                    packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                    packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity};
                scene.DirectionalColorIntensity = {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green,
                                                   lighting.ColorAndIntensity.Blue, lighting.ColorAndIntensity.Alpha};
                scene.DirectionalDirectionExposure = {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z,
                                                      packet.Environment.Exposure};
                scene.SurfaceParameters = {material->Surface.AlphaCutoff,
                                           static_cast<float>(material->Surface.AlphaMode),
                                           item.ReceiveShadows ? 1.0F : 0.0F, item.CastShadows ? 1.0F : 0.0F};
                scene.LocalLightCounts = localLights.Counts;
                scene.LocalLights = localLights.Lights;
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                std::array<Vector4, 64> numericProperties;
                std::ranges::copy(material->NumericProperties, numericProperties.begin());
                if (material->TintSlot && !material->UsesInstancing)
                {
                    auto& tint = numericProperties[*material->TintSlot];
                    tint.X *= item.Tint.Red;
                    tint.Y *= item.Tint.Green;
                    tint.Z *= item.Tint.Blue;
                    tint.W *= item.Tint.Alpha;
                }
                SDL_PushGPUFragmentUniformData(
                    commands, 1, numericProperties.data(),
                    static_cast<std::uint32_t>(material->NumericProperties.size() * sizeof(Vector4)));
                if (material->ReceivesShadows)
                {
                    if (fragmentSlot2Binding != FragmentSlot2Binding::Shadows)
                    {
                        SDL_PushGPUFragmentUniformData(commands, 2, &shadowUniforms, sizeof(shadowUniforms));
                        fragmentSlot2Binding = FragmentSlot2Binding::Shadows;
                    }
                }
                else if (fragmentSlot2Binding != FragmentSlot2Binding::LocalLights)
                {
                    SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
                    fragmentSlot2Binding = FragmentSlot2Binding::LocalLights;
                }
                if (!material->Textures.empty() || material->ReceivesShadows)
                {
                    std::array<SDL_GPUTextureSamplerBinding, 18> bindings{};
                    std::ranges::copy(material->Textures, bindings.begin());
                    auto bindingCount = material->Textures.size();
                    if (material->ReceivesShadows)
                    {
                        bindings[bindingCount++] = {surface.Resources.DirectionalShadow
                                                        ? surface.Resources.DirectionalShadow
                                                        : EmptyShadowTexture,
                                                    ShadowSampler};
                        bindings[bindingCount++] = {surface.Resources.LocalShadow ? surface.Resources.LocalShadow
                                                                                  : EmptyShadowTexture,
                                                    ShadowSampler};
                    }
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
                }
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                if (material->UsesInstancing)
                    SDL_BindGPUVertexStorageBuffers(pass, 0, &batch.InstanceBuffer, 1);
            }
            else
            {
                const Color tint = draw.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                const ObjectUniforms object =
                    MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), item.World, camera.View, tint,
                                       lighting, packet.Environment, item.ReceiveShadows);
                const auto& builtInShadows = item.ReceiveShadows ? shadowUniforms : disabledShadowUniforms;
                const std::array shadowBindings{
                    SDL_GPUTextureSamplerBinding{
                        surface.Resources.DirectionalShadow ? surface.Resources.DirectionalShadow : EmptyShadowTexture,
                        ShadowSampler},
                    SDL_GPUTextureSamplerBinding{surface.Resources.LocalShadow ? surface.Resources.LocalShadow
                                                                               : EmptyShadowTexture,
                                                 ShadowSampler}};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &builtInShadows, sizeof(builtInShadows));
                SDL_PushGPUFragmentUniformData(commands, 1, &localLights, sizeof(localLights));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                            static_cast<std::uint32_t>(shadowBindings.size()));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedBuiltinVertices ? item.SkinnedBuiltinVertices : mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, instanceCount, draw.Submesh.FirstIndex, 0,
                                         batch.GpuFirstInstance);
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3 * instanceCount;
            Statistics.InstanceBatches += instanceCount > 1 ? 1U : 0U;
        }
    }

    void RenderSharedState::DrawVfx(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                    RenderSurfaceState& surface, const SceneRenderPacket& packet)
    {
        auto& pipelines = PipelinesFor(ToSdlSampleCount(surface.ActualSamples));
        if (packet.Vfx.WorldId() != 0 && pipelines.GpuVfx)
        {
            const auto world = GpuVfxWorlds.find(packet.Vfx.WorldId());
            if (world != GpuVfxWorlds.end() && !world->second.Empty())
            {
                struct alignas(16) CameraUniforms final
                {
                    Matrix4 ViewProjection;
                    std::array<float, 4> Right{};
                    std::array<float, 4> Up{};
                };
                const auto cameraWorld = Math::Inverse(packet.Camera.View);
                const auto right = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
                const auto up = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
                const CameraUniforms camera{Math::Multiply(packet.Camera.Projection, packet.Camera.View),
                                            {right.X, right.Y, right.Z, 0.0F},
                                            {up.X, up.Y, up.Z, 0.0F}};
                SDL_BindGPUGraphicsPipeline(pass, pipelines.GpuVfx);
                SDL_PushGPUVertexUniformData(commands, 0, &camera, sizeof(camera));
                const std::array storage{world->second.Particles, world->second.AliveIndices};
                SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<std::uint32_t>(storage.size()));
                SDL_DrawGPUPrimitivesIndirect(pass, world->second.IndirectArguments, 0, 1);
                ++Statistics.DrawCalls;
                ++Statistics.VfxIndirectDraws;
            }
        }

        const auto particles = packet.Vfx.Particles();
        if (particles.empty())
            return;
        if (!pipelines.Vfx)
            return;

        struct PreparedParticle final
        {
            const VfxRenderParticle* Particle = nullptr;
            float Depth = 0.0F;
            std::uint32_t SpriteFirstVertex = 0;
        };
        std::vector<PreparedParticle> prepared;
        prepared.reserve(particles.size());
        for (const auto& particle : particles)
        {
            if (particle.Renderer == VfxRendererType::Sprite)
            {
                prepared.push_back(
                    {std::addressof(particle), Math::TransformPoint(packet.Camera.View, particle.Position).Z});
            }
        }
        if (prepared.empty())
            return;
        std::ranges::stable_sort(prepared, [](const auto& left, const auto& right)
                                 { return Detail::TransparentBackToFront(left.Depth, right.Depth); });

        const auto cameraWorld = Math::Inverse(packet.Camera.View);
        const auto cameraRight = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
        const auto cameraUp = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
        const auto cameraForward = Math::TransformDirection(cameraWorld, {0.0F, 0.0F, -1.0F});
        std::vector<RenderVertex> spriteVertices;
        spriteVertices.reserve(std::ranges::count_if(prepared, [](const PreparedParticle& value)
                                                     { return value.Particle->Renderer == VfxRendererType::Sprite; }) *
                               6U);
        constexpr float degreesToRadians = 0.01745329251994329577F;
        for (auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            if (particle.Renderer != VfxRendererType::Sprite)
                continue;
            value.SpriteFirstVertex = static_cast<std::uint32_t>(spriteVertices.size());
            const auto angle = particle.Rotation.Z * degreesToRadians;
            const auto cosine = std::cos(angle);
            const auto sine = std::sin(angle);
            const auto right = Scale(Add(Scale(cameraRight, cosine), Scale(cameraUp, sine)), particle.Size * 0.5F);
            const auto up = Scale(Add(Scale(cameraUp, cosine), Scale(cameraRight, -sine)), particle.Size * 0.5F);
            const auto lowerLeft = Subtract(Subtract(particle.Position, right), up);
            const auto lowerRight = Add(Subtract(particle.Position, up), right);
            const auto upperRight = Add(Add(particle.Position, right), up);
            const auto upperLeft = Add(Subtract(particle.Position, right), up);
            constexpr Vector3 white{1.0F, 1.0F, 1.0F};
            spriteVertices.push_back({lowerLeft, white, cameraForward});
            spriteVertices.push_back({lowerRight, white, cameraForward});
            spriteVertices.push_back({upperRight, white, cameraForward});
            spriteVertices.push_back({lowerLeft, white, cameraForward});
            spriteVertices.push_back({upperRight, white, cameraForward});
            spriteVertices.push_back({upperLeft, white, cameraForward});
        }

        SDL_GPUBuffer* spriteBuffer = nullptr;
        if (!spriteVertices.empty())
        {
            spriteBuffer = UploadVertexBuffer(spriteVertices);
            FrameTransientBuffers.push_back(spriteBuffer);
        }

        const AssetShadowUniforms noShadows{};
        const AssetLocalLightUniforms noLocalLights{};
        const std::array shadowBindings{SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler},
                                        SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler}};
        SDL_BindGPUGraphicsPipeline(pass, pipelines.Vfx);
        SDL_PushGPUFragmentUniformData(commands, 0, &noShadows, sizeof(noShadows));
        SDL_PushGPUFragmentUniformData(commands, 1, &noLocalLights, sizeof(noLocalLights));
        SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(), static_cast<std::uint32_t>(shadowBindings.size()));

        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        for (const auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            if (particle.Size <= 0.0F)
                continue;
            if (particle.Renderer == VfxRendererType::Sprite)
            {
                const ObjectUniforms object = MakeObjectUniforms(viewProjection, {}, packet.Camera.View, particle.Tint,
                                                                 packet.Lighting, packet.Environment, false);
                const SDL_GPUBufferBinding vertexBinding{spriteBuffer, 0};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, value.SpriteFirstVertex, 0);
                ++Statistics.DrawCalls;
                Statistics.Triangles += 2;
            }
        }
    }

    void RenderSharedState::RecordSampledDepth(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                               const SceneRenderPacket& packet)
    {
        if (!surface.Resources.SampledDepth || !SceneDepthPipeline)
            return;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = surface.Resources.SampledDepth;
        depth.clear_depth = 1.0F;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(sampled depth) failed: " + LastSdlError());
        SDL_BindGPUGraphicsPipeline(pass, SceneDepthPipeline);
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty())
                continue;
            const auto object = Math::Multiply(viewProjection, item.World);
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            const SDL_GPUBufferBinding vertexBinding{
                item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            for (const auto& submesh : mesh.Submeshes)
            {
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                    material && material->Surface.AlphaMode == MaterialAlphaMode::Blend)
                {
                    continue;
                }
                SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.SampledResolvedDepthAvailable = true;
    }

    ShadowFrameData RenderSharedState::RecordShadows(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                     const SceneRenderPacket& packet)
    {
        ShadowFrameData result;
        result.LocalLayers.fill(-1.0F);
        if (!ShadowPipeline || !ShadowSampler)
            return result;

        const auto ensureTexture = [&](SDL_GPUTexture*& texture, std::uint32_t& currentResolution,
                                       std::uint32_t& currentLayers, const std::uint32_t resolution,
                                       const std::uint32_t layers)
        {
            if (texture && currentResolution == resolution && currentLayers == layers)
                return;
            if (texture)
            {
                GpuTextureResources retired;
                retired.Texture = texture;
                Retire(std::move(retired));
                texture = nullptr;
            }
            SDL_GPUTextureCreateInfo information{};
            information.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            information.format = ShadowDepthFormat;
            information.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            information.width = resolution;
            information.height = resolution;
            information.layer_count_or_depth = layers;
            information.num_levels = 1;
            information.sample_count = SDL_GPU_SAMPLECOUNT_1;
            texture = SDL_CreateGPUTexture(Device, &information);
            if (!texture)
                throw std::runtime_error("SDL_CreateGPUTexture(shadow array) failed: " + LastSdlError());
            currentResolution = resolution;
            currentLayers = layers;
        };

        const auto drawLayer = [&](SDL_GPUTexture* texture, const std::uint32_t layer, const Matrix4& lightMatrix)
        {
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = texture;
            depth.clear_depth = 1.0F;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.layer = static_cast<std::uint8_t>(layer);
            auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPURenderPass(shadow) failed: " + LastSdlError());
            SDL_BindGPUGraphicsPipeline(pass, ShadowPipeline);
            for (const auto& item : packet.DrawItems)
            {
                if (!item.CastShadows)
                    continue;
                const auto& mesh = ResolveMesh(item.Mesh);
                if (mesh.Empty())
                    continue;
                const auto object = Math::Multiply(lightMatrix, item.World);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                for (const auto& submesh : mesh.Submeshes)
                    SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
            SDL_EndGPURenderPass(pass);
            ++Statistics.Passes;
        };

        if (packet.Lighting.Enabled && packet.Lighting.Shadows != ShadowQuality::Disabled)
        {
            const auto cascadeCount = std::clamp(packet.Environment.DirectionalShadowCascadeCount, 1U, 4U);
            const auto resolution = packet.Environment.DirectionalShadowResolution;
            ensureTexture(surface.Resources.DirectionalShadow, surface.Resources.DirectionalShadowResolution,
                          surface.Resources.DirectionalShadowLayers, resolution, cascadeCount);
            const float nearPlane = std::max(packet.Camera.NearPlane, 0.0001F);
            const float shadowDistance = std::min(
                std::max(packet.Environment.DirectionalShadowDistance, nearPlane + 0.0001F), packet.Camera.FarPlane);
            const auto splits = BuildPracticalCascadeSplits(nearPlane, shadowDistance, cascadeCount,
                                                            packet.Environment.DirectionalShadowSplitLambda);
            const auto inverseViewProjection =
                Math::Inverse(Math::Multiply(packet.Camera.Projection, packet.Camera.View));
            std::array<Vector3, 4> nearCorners{};
            std::array<Vector3, 4> farCorners{};
            constexpr std::array<Vector2, 4> coordinates{Vector2{-1.0F, -1.0F}, Vector2{1.0F, -1.0F},
                                                         Vector2{1.0F, 1.0F}, Vector2{-1.0F, 1.0F}};
            for (std::size_t index = 0; index < coordinates.size(); ++index)
            {
                const auto nearClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 0.0F});
                const auto farClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 1.0F});
                nearCorners[index] = {nearClip.X / nearClip.W, nearClip.Y / nearClip.W, nearClip.Z / nearClip.W};
                farCorners[index] = {farClip.X / farClip.W, farClip.Y / farClip.W, farClip.Z / farClip.W};
            }
            const auto direction = Normalize(
                Vector3{packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z});
            float previousSplit = nearPlane;
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            {
                const float nearRatio = (previousSplit - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                const float farRatio = (splits[cascade] - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                std::array<Vector3, 8> corners{};
                for (std::size_t corner = 0; corner < 4; ++corner)
                {
                    const auto ray = Subtract(farCorners[corner], nearCorners[corner]);
                    corners[corner] = Add(nearCorners[corner], Scale(ray, nearRatio));
                    corners[corner + 4] = Add(nearCorners[corner], Scale(ray, farRatio));
                }
                Vector3 center{};
                for (const auto corner : corners)
                    center = Add(center, corner);
                center = Scale(center, 1.0F / static_cast<float>(corners.size()));
                float radius = 0.0F;
                for (const auto corner : corners)
                    radius = std::max(radius, Length(Subtract(corner, center)));
                radius = std::max(std::ceil(radius * 16.0F) / 16.0F, 0.25F);
                const auto up = std::abs(direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                const auto eye = Subtract(center, Scale(direction, radius * 2.0F));
                const auto view = Math::LookAt(eye, center, up);
                const auto projection = Math::Orthographic(radius * 2.0F, 1.0F, 0.01F, radius * 4.0F);
                result.Directional.DirectionalMatrices[cascade] = Math::Multiply(projection, view);
                drawLayer(surface.Resources.DirectionalShadow, cascade,
                          result.Directional.DirectionalMatrices[cascade]);
                switch (cascade)
                {
                case 0:
                    result.Directional.DirectionalCascadeSplits.X = splits[cascade];
                    break;
                case 1:
                    result.Directional.DirectionalCascadeSplits.Y = splits[cascade];
                    break;
                case 2:
                    result.Directional.DirectionalCascadeSplits.Z = splits[cascade];
                    break;
                default:
                    result.Directional.DirectionalCascadeSplits.W = splits[cascade];
                    break;
                }
                previousSplit = splits[cascade];
            }
            const float encodedCascadeCount = packet.Lighting.Shadows == ShadowQuality::Hard
                                                  ? -static_cast<float>(cascadeCount)
                                                  : static_cast<float>(cascadeCount);
            result.Directional.DirectionalParameters = {encodedCascadeCount, packet.Lighting.ShadowStrength,
                                                        std::max(packet.Lighting.ShadowBias * 0.01F, 0.0001F),
                                                        1.0F / static_cast<float>(resolution)};
            Statistics.DirectionalShadowCascades += cascadeCount;
        }

        const bool hasLocalShadows = std::ranges::any_of(packet.LocalLights, [](const SceneLocalLight& light)
                                                         { return light.Shadows != ShadowQuality::Disabled; });
        if (hasLocalShadows)
        {
            ensureTexture(surface.Resources.LocalShadow, surface.Resources.LocalShadowResolution,
                          surface.Resources.LocalShadowLayers, LocalShadowResolution, LocalShadowLayerCount);
            std::size_t spotCount = 0;
            std::size_t pointCount = 0;
            constexpr float radiansToDegrees = 57.295779513082320876F;
            constexpr std::array<Vector3, 6> pointDirections{Vector3{1.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F},
                                                             Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, -1.0F, 0.0F},
                                                             Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 0.0F, -1.0F}};
            constexpr std::array<Vector3, 6> pointUps{Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F},
                                                      Vector3{0.0F, 0.0F, -1.0F}, Vector3{0.0F, 0.0F, 1.0F},
                                                      Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F}};
            const auto lightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                if (light.Type == SceneLocalLightType::Spot && spotCount < MaximumShadowedSpotLights)
                {
                    const float outerAngle = std::acos(std::clamp(light.OuterConeCosine, -1.0F, 1.0F));
                    const auto up =
                        std::abs(light.Direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                    const auto view = Math::LookAt(light.Position, Add(light.Position, light.Direction), up);
                    const auto projection = Math::Perspective(
                        std::clamp(outerAngle * 2.0F * radiansToDegrees, 1.01F, 178.0F), 1.0F, 0.05F, light.Range);
                    result.Local.Matrices[spotCount] = Math::Multiply(projection, view);
                    result.LocalLayers[lightIndex] = static_cast<float>(spotCount);
                    drawLayer(surface.Resources.LocalShadow, static_cast<std::uint32_t>(spotCount),
                              result.Local.Matrices[spotCount]);
                    ++spotCount;
                }
                else if (light.Type == SceneLocalLightType::Point && pointCount < MaximumShadowedPointLights)
                {
                    const auto baseLayer = static_cast<std::uint32_t>(MaximumShadowedSpotLights + pointCount * 6U);
                    result.LocalLayers[lightIndex] = static_cast<float>(baseLayer);
                    const auto projection = Math::Perspective(90.0F, 1.0F, 0.05F, light.Range);
                    for (std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto view =
                            Math::LookAt(light.Position, Add(light.Position, pointDirections[face]), pointUps[face]);
                        result.Local.Matrices[baseLayer + face] = Math::Multiply(projection, view);
                        drawLayer(surface.Resources.LocalShadow, baseLayer + face,
                                  result.Local.Matrices[baseLayer + face]);
                    }
                    ++pointCount;
                }
            }
        }
        return result;
    }

    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
    {
        if (!surface.Resources.SampledColor || !surface.Resources.HdrColor)
            return;

        const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
        PreparedSceneDrawLists preparedDraws;
        if (request != Requests.end())
        {
            auto started = std::chrono::steady_clock::now();
            PrepareSkinning(commands, request->Packet);
            Statistics.SkinningPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            PrepareGpuVfx(commands, request->Packet.Vfx);
            Statistics.VfxPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            preparedDraws = PrepareSceneDrawLists(commands, surface, request->Packet);
            Statistics.DrawPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
        ShadowFrameData shadows;
        shadows.LocalLayers.fill(-1.0F);
        CallbackFrameGraphExecutionContext execution(
            [&](const CompiledFrameGraph::Transition&) { ++Statistics.FrameGraphTransitions; },
            [&](const FrameGraphPass frameGraphPass)
            {
                ++Statistics.ExecutedFrameGraphPasses;
                if (frameGraphPass == SceneFrameGraph.DirectionalShadows)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != Requests.end())
                        shadows = RecordShadows(commands, surface, request->Packet);
                    Statistics.ShadowRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ForwardPlusCulling)
                {
                    if (request == Requests.end())
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    auto contentHash = std::uint64_t{1469598103934665603ULL};
                    const auto hashValue = [&](const auto value)
                    {
                        const auto bytes = std::as_bytes(std::span(std::addressof(value), 1));
                        for (const auto byte : bytes)
                        {
                            contentHash ^= std::to_integer<std::uint8_t>(byte);
                            contentHash *= 1099511628211ULL;
                        }
                    };
                    hashValue(surface.Width);
                    hashValue(surface.Height);
                    for (const auto value : request->Packet.Camera.View.Elements)
                        hashValue(value);
                    for (const auto value : request->Packet.Camera.Projection.Elements)
                        hashValue(value);
                    for (const auto& light : request->Packet.LocalLights)
                    {
                        hashValue(light.Position.X);
                        hashValue(light.Position.Y);
                        hashValue(light.Position.Z);
                        hashValue(light.Range);
                        hashValue(light.Direction.X);
                        hashValue(light.Direction.Y);
                        hashValue(light.Direction.Z);
                        hashValue(light.OuterConeCosine);
                        hashValue(light.ColorAndIntensity.Red);
                        hashValue(light.ColorAndIntensity.Green);
                        hashValue(light.ColorAndIntensity.Blue);
                        hashValue(light.ColorAndIntensity.Alpha);
                        hashValue(light.InnerConeCosine);
                        hashValue(light.Type);
                    }
                    Statistics.VisibleLocalLights += static_cast<std::uint32_t>(request->Packet.LocalLights.size());
                    if (surface.ForwardPlusContentValid && surface.ForwardPlusContentHash == contentHash &&
                        !surface.ForwardPlus.Empty())
                    {
                        ++Statistics.ForwardPlusCacheHits;
                        Statistics.ForwardPlusCullingMilliseconds +=
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
                                .count();
                        return;
                    }
                    std::vector<ForwardPlusLightBounds> localLightBounds;
                    localLightBounds.reserve(request->Packet.LocalLights.size());
                    for (const auto& light : request->Packet.LocalLights)
                        localLightBounds.push_back(
                            {Math::TransformPoint(request->Packet.Camera.View, light.Position), light.Range});
                    const auto tiles = BuildForwardPlusCpuTiles(surface.Width, surface.Height,
                                                                request->Packet.Camera.Projection, localLightBounds);
                    Statistics.OverflowedLightTiles += tiles.OverflowedTiles;
                    std::vector<AssetLocalLightUniform> gpuLights(
                        std::max<std::size_t>(1, request->Packet.LocalLights.size()));
                    for (std::size_t lightIndex = 0; lightIndex < request->Packet.LocalLights.size(); ++lightIndex)
                    {
                        const auto& light = request->Packet.LocalLights[lightIndex];
                        gpuLights[lightIndex] = {
                            {light.Position.X, light.Position.Y, light.Position.Z, light.Range},
                            {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine},
                            {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green, light.ColorAndIntensity.Blue,
                             light.ColorAndIntensity.Alpha},
                            {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F, 0.0F}};
                    }
                    std::vector<ForwardPlusTileUniform> gpuTiles(tiles.Offsets.size());
                    for (std::size_t tileIndex = 0; tileIndex < gpuTiles.size(); ++tileIndex)
                        gpuTiles[tileIndex] = {tiles.Offsets[tileIndex], tiles.Counts[tileIndex]};
                    std::vector<ForwardPlusIndexGroup> gpuIndices(
                        std::max<std::size_t>(1, (tiles.LightIndices.size() + 3U) / 4U));
                    for (std::size_t index = 0; index < tiles.LightIndices.size(); ++index)
                        gpuIndices[index / 4U].Indices[index % 4U] = tiles.LightIndices[index];

                    const std::array payloads{std::as_bytes(std::span(gpuLights)), std::as_bytes(std::span(gpuTiles)),
                                              std::as_bytes(std::span(gpuIndices))};
                    const auto capacityFor = [](const std::size_t required)
                    {
                        if (required == 0 || required > std::numeric_limits<std::uint32_t>::max())
                            throw std::invalid_argument("Forward+ buffer payload exceeds SDL's 32-bit limit.");
                        auto capacity = std::uint32_t{256};
                        while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U)
                            capacity *= 2U;
                        if (capacity < required)
                            capacity = static_cast<std::uint32_t>(required);
                        return capacity;
                    };
                    const std::array requiredCapacities{capacityFor(payloads[0].size()),
                                                        capacityFor(payloads[1].size()),
                                                        capacityFor(payloads[2].size())};
                    const bool requiresReplacement =
                        surface.ForwardPlus.Empty() || surface.ForwardPlus.LightCapacityBytes < requiredCapacities[0] ||
                        surface.ForwardPlus.TileCapacityBytes < requiredCapacities[1] ||
                        surface.ForwardPlus.LightIndexCapacityBytes < requiredCapacities[2];
                    if (requiresReplacement)
                    {
                        ForwardPlusGpuResources replacement;
                        const auto createBuffer = [&](const std::uint32_t byteSize)
                        {
                            SDL_GPUBufferCreateInfo information{};
                            information.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                            information.size = byteSize;
                            auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                            if (!buffer)
                                throw std::runtime_error("SDL_CreateGPUBuffer(Forward+) failed: " + LastSdlError());
                            return buffer;
                        };
                        try
                        {
                            replacement.Lights = createBuffer(requiredCapacities[0]);
                            replacement.Tiles = createBuffer(requiredCapacities[1]);
                            replacement.LightIndices = createBuffer(requiredCapacities[2]);
                            replacement.LightCapacityBytes = requiredCapacities[0];
                            replacement.TileCapacityBytes = requiredCapacities[1];
                            replacement.LightIndexCapacityBytes = requiredCapacities[2];
                        }
                        catch (...)
                        {
                            ReleaseForwardPlusResources(replacement);
                            throw;
                        }
                        Retire(std::exchange(surface.ForwardPlus, replacement));
                        ++Statistics.ForwardPlusBufferReallocations;
                    }

                    std::size_t totalBytes = 0;
                    for (const auto payload : payloads)
                    {
                        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                            throw std::invalid_argument("Combined Forward+ upload exceeds SDL's 32-bit limit.");
                        totalBytes += payload.size();
                    }
                    SDL_GPUTransferBuffer* transfer = nullptr;
                    try
                    {
                        SDL_GPUTransferBufferCreateInfo information{};
                        information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                        information.size = static_cast<std::uint32_t>(totalBytes);
                        transfer = SDL_CreateGPUTransferBuffer(Device, &information);
                        if (!transfer)
                            throw std::runtime_error("SDL_CreateGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
                        if (!mapped)
                            throw std::runtime_error("SDL_MapGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        std::size_t offset = 0;
                        for (const auto payload : payloads)
                        {
                            std::memcpy(mapped + offset, payload.data(), payload.size());
                            offset += payload.size();
                        }
                        SDL_UnmapGPUTransferBuffer(Device, transfer);

                        auto* copy = SDL_BeginGPUCopyPass(commands);
                        if (!copy)
                            throw std::runtime_error("SDL_BeginGPUCopyPass(Forward+) failed: " + LastSdlError());
                        const std::array destinations{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                      surface.ForwardPlus.LightIndices};
                        offset = 0;
                        for (std::size_t index = 0; index < payloads.size(); ++index)
                        {
                            SDL_GPUTransferBufferLocation source{transfer, static_cast<std::uint32_t>(offset)};
                            SDL_GPUBufferRegion destination{destinations[index], 0,
                                                            static_cast<std::uint32_t>(payloads[index].size())};
                            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
                            offset += payloads[index].size();
                        }
                        SDL_EndGPUCopyPass(copy);
                        FrameUploadTransfers.push_back(transfer);
                        transfer = nullptr;
                    }
                    catch (...)
                    {
                        if (transfer)
                            SDL_ReleaseGPUTransferBuffer(Device, transfer);
                        throw;
                    }
                    surface.ForwardPlus.Columns = tiles.Columns;
                    surface.ForwardPlus.Rows = tiles.Rows;
                    surface.ForwardPlusContentHash = contentHash;
                    surface.ForwardPlusContentValid = true;
                    Statistics.ForwardPlusUploadBytes += totalBytes;
                    Statistics.ForwardPlusCullingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Opaque)
                {
                    const auto started = std::chrono::steady_clock::now();
                    SDL_GPUColorTargetInfo color{};
                    color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                          : surface.Resources.HdrColor;
                    color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                         surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                    color.load_op = SDL_GPU_LOADOP_CLEAR;
                    color.store_op = surface.Resources.MultisampleHdrColor ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                                                           : SDL_GPU_STOREOP_STORE;
                    color.resolve_texture =
                        surface.Resources.MultisampleHdrColor ? surface.Resources.HdrColor : nullptr;
                    SDL_GPUDepthStencilTargetInfo depth{};
                    SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                    if (surface.Resources.Depth)
                    {
                        depth.texture = surface.Resources.Depth;
                        depth.clear_depth = 1.0F;
                        depth.load_op = SDL_GPU_LOADOP_CLEAR;
                        depth.store_op = SDL_GPU_STOREOP_STORE;
                        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depthPointer = &depth;
                    }
                    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                    if (!pass)
                        throw std::runtime_error("SDL_BeginGPURenderPass(HDR scene) failed: " + LastSdlError());
                    if (request != Requests.end())
                        DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Opaque,
                                  preparedDraws.Opaque);
                    SDL_EndGPURenderPass(pass);
                    ++Statistics.Passes;
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ResolveDepth)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != Requests.end())
                        RecordSampledDepth(commands, surface, request->Packet);
                    Statistics.DepthPassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Transparency)
                {
                    const auto started = std::chrono::steady_clock::now();
                    SDL_GPUColorTargetInfo color{};
                    color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                          : surface.Resources.HdrColor;
                    color.load_op = SDL_GPU_LOADOP_LOAD;
                    color.store_op =
                        surface.Resources.MultisampleHdrColor ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
                    color.resolve_texture =
                        surface.Resources.MultisampleHdrColor ? surface.Resources.HdrColor : nullptr;
                    SDL_GPUDepthStencilTargetInfo depth{};
                    SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                    if (surface.Resources.Depth)
                    {
                        depth.texture = surface.Resources.Depth;
                        depth.load_op = SDL_GPU_LOADOP_LOAD;
                        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depthPointer = &depth;
                    }
                    auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                    if (!pass)
                        throw std::runtime_error("SDL_BeginGPURenderPass(transparency) failed: " + LastSdlError());
                    if (request != Requests.end())
                    {
                        DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Transparent,
                                  preparedDraws.Transparent);
                        DrawVfx(commands, pass, surface, request->Packet);
                    }
                    SDL_EndGPURenderPass(pass);
                    ++Statistics.Passes;
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ToneMap)
                {
                    const auto started = std::chrono::steady_clock::now();
                    RecordToneMap(commands, surface);
                    Statistics.ToneMapMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                }
            });
        SceneFrameGraph.Graph.Execute(SceneFrameGraph.Compiled, execution);
        ++Statistics.Surfaces;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface)
    {
        if (!ToneMapPipeline || !ToneMapSampler || !surface.Resources.HdrColor || !surface.Resources.SampledColor)
            throw std::logic_error("Tone-map resources are unavailable for an active render surface.");
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.HasOutput ? surface.Resources.ExchangeColor : surface.Resources.SampledColor;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
        const SDL_GPUTextureSamplerBinding binding{surface.Resources.HdrColor, ToneMapSampler};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::EndFrame(ImDrawData* drawData)
    {
        RequireOwner("EndFrame");
        if (!FrameActive)
            throw std::logic_error("No render frame is active.");
        FrameActive = false;

        if (Specification.Mode == RenderMode::Headless)
        {
            Statistics.Surfaces = static_cast<std::uint32_t>(LiveSurfaces().size());
            Statistics.Passes = Statistics.Surfaces;
            return;
        }

        const auto started = std::chrono::steady_clock::now();
        DispatchRender([this, drawData] { ExecuteFrame(drawData); });
        Statistics.RendererLatencyMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::ExecuteFrame(ImDrawData* drawData)
    {
        Statistics.CommandRecordingMilliseconds = 0.0F;
        Statistics.SkinningPreparationMilliseconds = 0.0F;
        Statistics.VfxPreparationMilliseconds = 0.0F;
        Statistics.DrawPreparationMilliseconds = 0.0F;
        Statistics.ShadowRecordingMilliseconds = 0.0F;
        Statistics.ForwardPlusCullingMilliseconds = 0.0F;
        Statistics.ScenePassMilliseconds = 0.0F;
        Statistics.DepthPassMilliseconds = 0.0F;
        Statistics.ToneMapMilliseconds = 0.0F;
        Statistics.CommandRecordingUnattributedMilliseconds = 0.0F;
        Statistics.FrameUploadMilliseconds = 0.0F;
        Statistics.SwapchainWaitMilliseconds = 0.0F;
        Statistics.UiRecordingMilliseconds = 0.0F;
        Statistics.GpuSubmissionMilliseconds = 0.0F;
        Statistics.GpuFrameMilliseconds = 0.0F;
        Statistics.ForwardPlusCacheHits = 0;
        Statistics.FrameUploadSubmissions = 0;
        if (InjectDeviceLossAtNextFrame.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("Injected GPU device loss.");
        if (FrameUploadCommands || FrameUploadPass || !FrameUploadTransfers.empty())
            throw std::logic_error("A previous frame left the GPU upload context active.");

        if (GpuSubmissionSerial == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("GPU submission serial exhausted.");
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
        if (!commands)
            throw std::runtime_error("SDL_AcquireGPUCommandBuffer failed: " + LastSdlError());
        ActiveGpuSubmissionSerial = GpuSubmissionSerial + 1U;
        FrameExecutionActive = true;
        bool frameUploadsSubmitted = false;

        try
        {
            const auto recordingStarted = std::chrono::steady_clock::now();
            for (const auto& request : Requests)
                RecordSurface(commands, *request.Surface);
            Statistics.CommandRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordingStarted).count();
            const auto attributedRecordingMilliseconds =
                Statistics.SkinningPreparationMilliseconds + Statistics.VfxPreparationMilliseconds +
                Statistics.DrawPreparationMilliseconds + Statistics.ShadowRecordingMilliseconds +
                Statistics.ForwardPlusCullingMilliseconds + Statistics.ScenePassMilliseconds +
                Statistics.DepthPassMilliseconds + Statistics.ToneMapMilliseconds;
            Statistics.CommandRecordingUnattributedMilliseconds =
                std::max(0.0F, Statistics.CommandRecordingMilliseconds - attributedRecordingMilliseconds);

            const auto uploadStarted = std::chrono::steady_clock::now();
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                auto* uploadCommands = std::exchange(FrameUploadCommands, nullptr);
                if (FrameUploadTransfers.empty())
                {
                    (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                }
                else if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                {
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
                }
                else
                {
                    frameUploadsSubmitted = true;
                    ++Statistics.FrameUploadSubmissions;
                }
            }
            Statistics.FrameUploadMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStarted).count();

            SDL_GPUTexture* swapchain = nullptr;
            const auto swapchainStarted = std::chrono::steady_clock::now();
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, nullptr, nullptr))
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                commands = nullptr;
                throw std::runtime_error("SDL_WaitAndAcquireGPUSwapchainTexture failed: " + LastSdlError());
            }
            Statistics.SwapchainWaitMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - swapchainStarted).count();

            const auto uiRecordingStarted = std::chrono::steady_clock::now();
            if (swapchain)
            {
                const bool renderUi = drawData && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F;
                if (renderUi)
                    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

                SDL_GPUColorTargetInfo target{};
                target.texture = swapchain;
                target.clear_color = {Specification.SwapchainClearColor.Red, Specification.SwapchainClearColor.Green,
                                      Specification.SwapchainClearColor.Blue, Specification.SwapchainClearColor.Alpha};
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPURenderPass(swapchain) failed: " + LastSdlError());
                if (renderUi)
                    ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
            }
            Statistics.UiRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uiRecordingStarted).count();

            const auto submissionStarted = std::chrono::steady_clock::now();
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            commands = nullptr;
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
            Statistics.GpuSubmissionMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - submissionStarted).count();
            InFlight.push_back({fence, std::move(PendingRetired), std::move(PendingRetiredMeshes),
                                std::move(PendingRetiredSkins), std::move(PendingRetiredTextures),
                                std::move(PendingRetiredPipelines), std::move(PendingRetiredForwardPlus),
                                std::move(FrameTransientBuffers), std::move(FrameUploadTransfers)});
            PendingRetired.clear();
            PendingRetiredMeshes.clear();
            PendingRetiredSkins.clear();
            PendingRetiredTextures.clear();
            PendingRetiredPipelines.clear();
            PendingRetiredForwardPlus.clear();
            FrameTransientBuffers.clear();
            FrameUploadTransfers.clear();
            GpuSubmissionSerial = ActiveGpuSubmissionSerial;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            for (const auto& request : Requests)
            {
                auto* surface = request.Surface;
                if (surface->HasOutput)
                    std::swap(surface->Resources.SampledColor, surface->Resources.ExchangeColor);
                else
                    surface->HasOutput = true;
            }
        }
        catch (...)
        {
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
                FrameUploadCommands = nullptr;
            }
            if (frameUploadsSubmitted && Device)
                (void)SDL_WaitForGPUIdle(Device);
            for (auto* transfer : FrameUploadTransfers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            FrameUploadTransfers.clear();
            if (commands)
                (void)SDL_CancelGPUCommandBuffer(commands);
            for (auto* buffer : FrameTransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            FrameTransientBuffers.clear();
            // A canceled command buffer invalidates the emitter sequencing recorded for every world it touched.
            for (auto& [worldId, resources] : GpuVfxWorlds)
            {
                (void)worldId;
                if (resources.LastPreparedFrame != Statistics.Frame)
                    continue;
                resources.InvalidateSequencing();
            }
            FrameActive = false;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            throw;
        }
    }

    void RenderSharedState::Close() noexcept
    {
        if (!Open)
            return;
        Open = false;
        FrameActive = false;
        StopRenderThread();
        FrameExecutionActive = false;
        ActiveGpuSubmissionSerial = 0;

        if (Device)
            (void)SDL_WaitForGPUIdle(Device);
        for (const auto& surface : LiveSurfaces())
        {
            ReleaseResources(surface->Resources);
            ReleaseForwardPlusResources(surface->ForwardPlus);
            surface->Owner.reset();
            surface->Width = 0;
            surface->Height = 0;
        }
        for (auto& resources : PendingRetired)
            ReleaseResources(resources);
        PendingRetired.clear();
        for (auto& resources : PendingRetiredMeshes)
            ReleaseMeshResources(resources);
        PendingRetiredMeshes.clear();
        for (auto& resources : PendingRetiredSkins)
            ReleaseGpuSkinResources(resources);
        PendingRetiredSkins.clear();
        for (auto& resources : PendingRetiredTextures)
            ReleaseTextureResources(resources);
        PendingRetiredTextures.clear();
        for (auto* pipeline : PendingRetiredPipelines)
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        PendingRetiredPipelines.clear();
        for (auto& resources : PendingRetiredForwardPlus)
            ReleaseForwardPlusResources(resources);
        PendingRetiredForwardPlus.clear();
        for (auto* buffer : FrameTransientBuffers)
            SDL_ReleaseGPUBuffer(Device, buffer);
        FrameTransientBuffers.clear();
        for (auto* transfer : FrameUploadTransfers)
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
        FrameUploadTransfers.clear();
        FrameUploadPass = nullptr;
        FrameUploadCommands = nullptr;
        for (auto& frame : InFlight)
        {
            for (auto& resources : frame.Retired)
                ReleaseResources(resources);
            for (auto& resources : frame.RetiredMeshes)
                ReleaseMeshResources(resources);
            for (auto& resources : frame.RetiredSkins)
                ReleaseGpuSkinResources(resources);
            for (auto& resources : frame.RetiredTextures)
                ReleaseTextureResources(resources);
            for (auto* pipeline : frame.RetiredPipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            for (auto& resources : frame.RetiredForwardPlus)
                ReleaseForwardPlusResources(resources);
            for (auto* buffer : frame.TransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            for (auto* transfer : frame.TransientTransferBuffers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (Device && frame.Fence)
                SDL_ReleaseGPUFence(Device, frame.Fence);
        }
        InFlight.clear();
        Requests.clear();

        for (auto& pipelines : Pipelines)
        {
            if (pipelines.GpuVfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.GpuVfx);
            if (pipelines.Vfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Vfx);
            if (pipelines.Sky)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Sky);
            if (pipelines.Grid)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Grid);
            if (pipelines.Cube)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Cube);
        }
        Pipelines.clear();
        for (auto& [id, entry] : MeshCache)
        {
            (void)id;
            ReleaseMeshResources(entry.Resources);
        }
        MeshCache.clear();
        for (auto& [id, entry] : SkinCache)
        {
            (void)id;
            ReleaseGpuSkinResources(entry.Resources);
        }
        SkinCache.clear();
        for (auto& [id, entry] : TextureCache)
        {
            (void)id;
            ReleaseTextureResources(entry.Resources);
        }
        TextureCache.clear();
        MaterialCache.clear();
        for (auto& [id, entry] : ShaderCache)
        {
            (void)id;
            for (const auto& pipeline : entry.Pipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline.Handle);
        }
        ShaderCache.clear();
        ReleaseTextureResources(CheckerboardTexture);
        ReleaseTextureResources(DefaultSkyTexture);
        ReleaseTextureResources(WhiteTexture);
        ReleaseTextureResources(FlatNormalTexture);
        ReleaseTextureResources(NeutralOrmTexture);
        ReleaseTextureResources(BlackTexture);
        ReleaseTextureResources(BlackDataTexture);
        ReleaseTextureResources(WhiteDataTexture);
        for (const auto& [description, sampler] : SamplerCache)
        {
            (void)description;
            SDL_ReleaseGPUSampler(Device, sampler);
        }
        SamplerCache.clear();
        if (ShadowSampler)
            SDL_ReleaseGPUSampler(Device, ShadowSampler);
        ShadowSampler = nullptr;
        if (ToneMapSampler)
            SDL_ReleaseGPUSampler(Device, ToneMapSampler);
        ToneMapSampler = nullptr;
        if (EmptyShadowTexture)
            SDL_ReleaseGPUTexture(Device, EmptyShadowTexture);
        EmptyShadowTexture = nullptr;
        if (ShadowPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ShadowPipeline);
        ShadowPipeline = nullptr;
        if (SceneDepthPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, SceneDepthPipeline);
        SceneDepthPipeline = nullptr;
        if (SkinningPipeline)
            SDL_ReleaseGPUComputePipeline(Device, SkinningPipeline);
        SkinningPipeline = nullptr;
        SkinningPipelineAttempted = false;
        for (auto& [world, resources] : GpuVfxWorlds)
        {
            (void)world;
            ReleaseGpuVfxWorld(resources);
        }
        GpuVfxWorlds.clear();
        if (VfxFinalizePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxFinalizePipeline);
        if (VfxSpawnPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxSpawnPipeline);
        if (VfxSimulatePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxSimulatePipeline);
        if (VfxTransformPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxTransformPipeline);
        if (VfxKillPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxKillPipeline);
        if (VfxResetPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxResetPipeline);
        if (VfxInitializePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxInitializePipeline);
        VfxFinalizePipeline = nullptr;
        VfxSpawnPipeline = nullptr;
        VfxSimulatePipeline = nullptr;
        VfxTransformPipeline = nullptr;
        VfxKillPipeline = nullptr;
        VfxResetPipeline = nullptr;
        VfxInitializePipeline = nullptr;
        VfxPipelinesAttempted = false;
        if (ToneMapPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ToneMapPipeline);
        ToneMapPipeline = nullptr;
        ReleaseMeshResources(ErrorMesh);
        ReleaseMeshResources(DefaultMesh);
        if (GridBuffer)
            SDL_ReleaseGPUBuffer(Device, GridBuffer);
        GridBuffer = nullptr;
        GridVertexCount = 0;

        if (WindowClaimed && Device && NativeWindow)
            SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
        WindowClaimed = false;
        if (Device)
            SDL_DestroyGPUDevice(Device);
        Device = nullptr;
        NativeWindow = nullptr;
        Window.Reset();
        Windows.Reset();
        Assets.Reset();
    }
} // namespace Keire::RenderBackend
