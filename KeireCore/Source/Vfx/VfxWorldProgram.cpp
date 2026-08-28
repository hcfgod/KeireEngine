#include "KeireInternal/Vfx/VfxWorldInternal.h"

#include "Keire/Vfx/VfxSubgraph.h"
#include "KeireInternal/Vfx/VfxExecutionInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        template <typename... Ts> struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

        using Internal::FindCompiledModule;

        [[nodiscard]] std::optional<VfxGpuEmitter::ParticleOperationKind>
        GpuParticleOperationKind(const VfxModulePayload& payload) noexcept
        {
            return std::visit(
                [](const auto& module) -> std::optional<VfxGpuEmitter::ParticleOperationKind>
                {
                    using T = std::decay_t<decltype(module)>;
                    if constexpr (std::is_same_v<T, VfxShapeModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Shape;
                    else if constexpr (std::is_same_v<T, VfxInitializeModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Initialize;
                    else if constexpr (std::is_same_v<T, VfxForceModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Force;
                    else if constexpr (std::is_same_v<T, VfxSizeOverLifetimeModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Size;
                    else if constexpr (std::is_same_v<T, VfxColorOverLifetimeModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Color;
                    else if constexpr (std::is_same_v<T, VfxCollisionModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Collision;
                    else if constexpr (std::is_same_v<T, VfxRendererModule>)
                        return VfxGpuEmitter::ParticleOperationKind::Renderer;
                    else if constexpr (std::is_same_v<T, VfxKillShapeModule>)
                        return VfxGpuEmitter::ParticleOperationKind::KillShape;
                    else
                        return std::nullopt;
                },
                payload);
        }

        [[nodiscard]] constexpr bool IsGpuParticleModulePropertyImpl(const VfxModuleProperty property) noexcept
        {
            switch (property)
            {
            case VfxModuleProperty::ShapeBoxHalfExtent:
            case VfxModuleProperty::ShapeRadius:
            case VfxModuleProperty::ShapeConeAngleDegrees:
            case VfxModuleProperty::ShapeConeLength:
            case VfxModuleProperty::InitializeLifetimeMinimum:
            case VfxModuleProperty::InitializeLifetimeMaximum:
            case VfxModuleProperty::InitializeVelocityMinimum:
            case VfxModuleProperty::InitializeVelocityMaximum:
            case VfxModuleProperty::InitializeRotationMinimum:
            case VfxModuleProperty::InitializeRotationMaximum:
            case VfxModuleProperty::ForceVector:
            case VfxModuleProperty::ForceGravityMultiplier:
            case VfxModuleProperty::SizeConstant:
            case VfxModuleProperty::ColorConstant:
            case VfxModuleProperty::CollisionRestitution:
            case VfxModuleProperty::CollisionKillOnCollision:
            case VfxModuleProperty::KillShapeCenter:
            case VfxModuleProperty::KillShapeBoxHalfExtent:
            case VfxModuleProperty::KillShapeRadius:
            case VfxModuleProperty::KillShapeInverted:
                return true;
            case VfxModuleProperty::None:
            case VfxModuleProperty::EmissionParticlesPerSecond:
            case VfxModuleProperty::BurstTime:
            case VfxModuleProperty::BurstCount:
            case VfxModuleProperty::BurstCycles:
            case VfxModuleProperty::BurstInterval:
            case VfxModuleProperty::ShapeMesh:
            case VfxModuleProperty::ShapeVolume:
            case VfxModuleProperty::RendererSprite:
            case VfxModuleProperty::RendererMesh:
            case VfxModuleProperty::RendererMaterial:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool HasCanonicalRangeEndpoints(const VfxParameterValue& value) noexcept
        {
            return std::visit(
                [](const auto& item) noexcept
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, VfxScalarRange> || std::is_same_v<T, VfxIntegerRange> ||
                                  std::is_same_v<T, VfxUnsignedIntegerRange>)
                    {
                        return item.Minimum <= item.Maximum;
                    }
                    else if constexpr (std::is_same_v<T, VfxVector2Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y;
                    }
                    else if constexpr (std::is_same_v<T, VfxVector3Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                               item.Minimum.Z <= item.Maximum.Z;
                    }
                    else if constexpr (std::is_same_v<T, VfxVector4Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                               item.Minimum.Z <= item.Maximum.Z && item.Minimum.W <= item.Maximum.W;
                    }
                    else if constexpr (std::is_same_v<T, VfxColorRange>)
                    {
                        return item.Minimum.Red <= item.Maximum.Red && item.Minimum.Green <= item.Maximum.Green &&
                               item.Minimum.Blue <= item.Maximum.Blue && item.Minimum.Alpha <= item.Maximum.Alpha;
                    }
                    else
                    {
                        return true;
                    }
                },
                value);
        }

        [[nodiscard]] bool ParameterValueMatchesImpl(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            return VfxValueMatchesType(type, value) && IsFiniteVfxValue(value) && HasCanonicalRangeEndpoints(value);
        }

        [[nodiscard]] Vector4 ParameterVectorImpl(const VfxValueType type, const VfxParameterValue& value)
        {
            switch (type)
            {
            case VfxValueType::Boolean:
            {
                const auto scalar = std::get<bool>(value) ? 1.0F : 0.0F;
                return {scalar, scalar, scalar, scalar};
            }
            case VfxValueType::Integer:
            {
                const auto scalar = static_cast<float>(std::get<std::int64_t>(value));
                return {scalar, scalar, scalar, scalar};
            }
            case VfxValueType::UnsignedInteger:
            {
                const auto scalar = static_cast<float>(std::get<std::uint64_t>(value));
                return {scalar, scalar, scalar, scalar};
            }
            case VfxValueType::Scalar:
            {
                const auto scalar = std::get<float>(value);
                return {scalar, scalar, scalar, scalar};
            }
            case VfxValueType::Vector2:
            {
                const auto vector = std::get<Vector2>(value);
                return {vector.X, vector.Y, 0.0F, 0.0F};
            }
            case VfxValueType::Vector3:
            {
                const auto vector = std::get<Vector3>(value);
                return {vector.X, vector.Y, vector.Z, 0.0F};
            }
            case VfxValueType::Vector4:
                return std::get<Vector4>(value);
            case VfxValueType::Quaternion:
            {
                const auto quaternion = std::get<Quaternion>(value);
                return {quaternion.X, quaternion.Y, quaternion.Z, quaternion.W};
            }
            case VfxValueType::Color:
            {
                const auto color = std::get<Color>(value);
                return {color.Red, color.Green, color.Blue, color.Alpha};
            }
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            case VfxValueType::ParticleStream:
            case VfxValueType::Matrix:
            case VfxValueType::Curve:
            case VfxValueType::Gradient:
            case VfxValueType::ScalarRange:
            case VfxValueType::IntegerRange:
            case VfxValueType::UnsignedIntegerRange:
            case VfxValueType::Vector2Range:
            case VfxValueType::Vector3Range:
            case VfxValueType::Vector4Range:
            case VfxValueType::ColorRange:
            case VfxValueType::Texture2DArray:
            case VfxValueType::Texture3D:
            case VfxValueType::TextureCube:
            case VfxValueType::Buffer:
            case VfxValueType::PointCache:
            case VfxValueType::SignedDistanceField:
                break;
            }
            throw std::invalid_argument("VFX value cannot be used by Portable Custom HLSL.");
        }

        [[nodiscard]] std::vector<VfxParameterValue>
        ResolveParametersImpl(const VfxCompiledProgram& program, const std::span<const VfxParameterOverride> overrides)
        {
            std::set<AssetId> unique;
            std::vector<VfxParameterValue> result(program.Parameters.size());
            for (const auto& parameter : program.Parameters)
            {
                if (parameter.Slot >= result.size())
                    throw std::invalid_argument("VFX compiled parameter layout is invalid.");
                result[parameter.Slot] = parameter.DefaultValue;
            }
            for (const auto& overrideValue : overrides)
            {
                if (!overrideValue.Parameter || !unique.insert(overrideValue.Parameter).second)
                    throw std::invalid_argument("VFX parameter overrides contain an invalid stable ID.");
                const auto parameter =
                    std::ranges::find(program.Parameters, overrideValue.Parameter, &VfxCompiledParameter::Parameter);
                if (parameter == program.Parameters.end() || !parameter->Exposed ||
                    !ParameterValueMatchesImpl(parameter->Type, overrideValue.Value))
                {
                    throw std::invalid_argument("VFX parameter override is unknown, hidden, or type-mismatched.");
                }
                result[parameter->Slot] = overrideValue.Value;
            }
            return result;
        }

        [[nodiscard]] std::shared_ptr<const VfxGpuExecutionPayload>
        BuildGpuExecutionPayloadImpl(const VfxCompiledProgram& program, const VfxEffectDefinition& definition,
                                     const std::span<const VfxParameterValue> parameters,
                                     const std::span<const VfxGpuEmitter::CustomInstruction> customInstructions)
        {
            if (program.Backend != VfxBackend::Gpu)
                return {};
            if (parameters.size() != program.Parameters.size())
                throw std::invalid_argument("VFX GPU parameter layout is invalid.");

            auto payload = std::make_shared<VfxGpuExecutionPayload>();
            payload->ValueProgram = program.GpuValueProgram;
            payload->CustomInstructions.assign(customInstructions.begin(), customInstructions.end());
            payload->Parameters.resize(parameters.size());
            for (const auto& parameter : program.Parameters)
            {
                if (parameter.Slot >= parameters.size() ||
                    !Internal::PackVfxGpuValue(parameter.Type, parameters[parameter.Slot],
                                               payload->Parameters[parameter.Slot]))
                {
                    throw std::invalid_argument("VFX GPU parameter value cannot be represented by the expression ABI.");
                }
            }

            const auto appendProperty = [&](const AssetId executionId, const VfxModuleProperty property,
                                            const VfxValueType type, const VfxParameterValue& defaultValue)
            {
                VfxGpuModuleProperty result;
                result.Property = property;
                result.Type = type;
                if (!Internal::PackVfxGpuValue(type, defaultValue, result.LiteralValue))
                    throw std::invalid_argument("VFX GPU module property cannot be represented by the execution ABI.");
                const auto binding =
                    std::ranges::find_if(program.Bindings, [executionId, property](const VfxCompiledBinding& candidate)
                                         { return candidate.Node == executionId && candidate.Property == property; });
                if (binding != program.Bindings.end())
                {
                    if (binding->Type != type)
                        throw std::invalid_argument("VFX GPU module property binding type is invalid.");
                    if (binding->LiteralValue)
                    {
                        result.Source = VfxGpuModulePropertySource::Literal;
                        if (!Internal::PackVfxGpuValue(type, *binding->LiteralValue, result.LiteralValue))
                            throw std::invalid_argument("VFX GPU module literal cannot be represented by the ABI.");
                    }
                    else if (binding->ValueRegister != ~std::uint32_t{0})
                    {
                        result.Source = VfxGpuModulePropertySource::Register;
                        result.Index = binding->ValueRegister;
                    }
                    else
                    {
                        if (binding->ParameterSlot >= parameters.size())
                            throw std::invalid_argument("VFX GPU module parameter binding is invalid.");
                        result.Source = VfxGpuModulePropertySource::Parameter;
                        result.Index = binding->ParameterSlot;
                    }
                }
                payload->ModuleProperties.push_back(result);
            };
            const auto appendSample = [&payload](const VfxValueType type, const VfxParameterValue& value)
            {
                VfxGpuValue packed;
                if (!Internal::PackVfxGpuValue(type, value, packed))
                    throw std::invalid_argument("VFX GPU lifetime sample cannot be represented by the execution ABI.");
                payload->LifetimeSamples.push_back(packed);
            };

            payload->ParticleOperations.reserve(program.Operations.size());
            for (const auto& operation : program.Operations)
            {
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    if (operation.Index >= customInstructions.size())
                        throw std::invalid_argument("VFX GPU operation references an invalid Custom HLSL instruction.");
                    payload->ParticleOperations.push_back(
                        {operation.Context, VfxGpuEmitter::ParticleOperationKind::CustomHlsl, operation.Index});
                    continue;
                }
                if (operation.Index >= program.Modules.size())
                    throw std::invalid_argument("VFX GPU operation references an invalid Runtime Module.");
                const auto* module = FindCompiledModule(definition, program, operation.Index);
                if (!module)
                    throw std::invalid_argument("VFX GPU operation Runtime Module identity is invalid.");
                const auto kind = GpuParticleOperationKind(module->Payload);
                if (!kind)
                    continue;

                VfxGpuParticleOperation gpuOperation;
                gpuOperation.Context = operation.Context;
                gpuOperation.Kind = *kind;
                gpuOperation.FirstProperty = static_cast<std::uint32_t>(payload->ModuleProperties.size());
                gpuOperation.FirstSample = static_cast<std::uint32_t>(payload->LifetimeSamples.size());
                std::visit(
                    Overloaded{
                        [&](const VfxShapeModule& value)
                        {
                            gpuOperation.Setting = static_cast<std::uint32_t>(value.Shape);
                            if (value.Shape == VfxShape::Mesh || value.Shape == VfxShape::Volume)
                            {
                                const auto asset = value.Shape == VfxShape::Mesh ? value.Mesh : value.Volume;
                                const VfxGpuShapeResource resource{value.Shape, asset};
                                auto found = std::ranges::find(payload->ShapeResources, resource);
                                if (found == payload->ShapeResources.end())
                                {
                                    payload->ShapeResources.push_back(resource);
                                    found = std::prev(payload->ShapeResources.end());
                                }
                                const auto index =
                                    static_cast<std::size_t>(std::distance(payload->ShapeResources.begin(), found));
                                if (index >= std::numeric_limits<std::uint32_t>::max())
                                    throw std::invalid_argument("VFX GPU shape resource table exceeds the ABI limit.");
                                gpuOperation.Index = static_cast<std::uint32_t>(index + 1U);
                            }
                            appendProperty(operation.Node, VfxModuleProperty::ShapeBoxHalfExtent, VfxValueType::Vector3,
                                           value.BoxHalfExtent);
                            appendProperty(operation.Node, VfxModuleProperty::ShapeRadius, VfxValueType::Scalar,
                                           value.Radius);
                            appendProperty(operation.Node, VfxModuleProperty::ShapeConeAngleDegrees,
                                           VfxValueType::Scalar, value.ConeAngleDegrees);
                            appendProperty(operation.Node, VfxModuleProperty::ShapeConeLength, VfxValueType::Scalar,
                                           value.ConeLength);
                        },
                        [&](const VfxInitializeModule& value)
                        {
                            appendProperty(operation.Node, VfxModuleProperty::InitializeLifetimeMinimum,
                                           VfxValueType::Scalar, value.LifetimeMinimum);
                            appendProperty(operation.Node, VfxModuleProperty::InitializeLifetimeMaximum,
                                           VfxValueType::Scalar, value.LifetimeMaximum);
                            appendProperty(operation.Node, VfxModuleProperty::InitializeVelocityMinimum,
                                           VfxValueType::Vector3, value.VelocityMinimum);
                            appendProperty(operation.Node, VfxModuleProperty::InitializeVelocityMaximum,
                                           VfxValueType::Vector3, value.VelocityMaximum);
                            appendProperty(operation.Node, VfxModuleProperty::InitializeRotationMinimum,
                                           VfxValueType::Vector3, value.RotationMinimum);
                            appendProperty(operation.Node, VfxModuleProperty::InitializeRotationMaximum,
                                           VfxValueType::Vector3, value.RotationMaximum);
                        },
                        [&](const VfxForceModule& value)
                        {
                            appendProperty(operation.Node, VfxModuleProperty::ForceVector, VfxValueType::Vector3,
                                           value.Force);
                            appendProperty(operation.Node, VfxModuleProperty::ForceGravityMultiplier,
                                           VfxValueType::Scalar, value.GravityMultiplier);
                        },
                        [&](const VfxSizeOverLifetimeModule& value)
                        {
                            appendProperty(operation.Node, VfxModuleProperty::SizeConstant, VfxValueType::Scalar,
                                           value.Size.Evaluate(0.0F));
                            for (std::size_t sample = 0; sample < VfxGpuEmitter::LifetimeSampleCount; ++sample)
                            {
                                const auto time = static_cast<float>(sample) /
                                                  static_cast<float>(VfxGpuEmitter::LifetimeSampleCount - 1U);
                                appendSample(VfxValueType::Scalar, value.Size.Evaluate(time));
                            }
                        },
                        [&](const VfxColorOverLifetimeModule& value)
                        {
                            appendProperty(operation.Node, VfxModuleProperty::ColorConstant, VfxValueType::Color,
                                           value.Color.Evaluate(0.0F));
                            for (std::size_t sample = 0; sample < VfxGpuEmitter::LifetimeSampleCount; ++sample)
                            {
                                const auto time = static_cast<float>(sample) /
                                                  static_cast<float>(VfxGpuEmitter::LifetimeSampleCount - 1U);
                                appendSample(VfxValueType::Color, value.Color.Evaluate(time));
                            }
                        },
                        [&](const VfxCollisionModule& value)
                        {
                            gpuOperation.Setting = static_cast<std::uint32_t>(value.Mode);
                            appendProperty(operation.Node, VfxModuleProperty::CollisionRestitution,
                                           VfxValueType::Scalar, value.Restitution);
                            appendProperty(operation.Node, VfxModuleProperty::CollisionKillOnCollision,
                                           VfxValueType::Boolean, value.KillOnCollision);
                        },
                        [&](const VfxKillShapeModule& value)
                        {
                            gpuOperation.Setting = static_cast<std::uint32_t>(value.Shape);
                            appendProperty(operation.Node, VfxModuleProperty::KillShapeCenter, VfxValueType::Vector3,
                                           value.Center);
                            appendProperty(operation.Node, VfxModuleProperty::KillShapeBoxHalfExtent,
                                           VfxValueType::Vector3, value.BoxHalfExtent);
                            appendProperty(operation.Node, VfxModuleProperty::KillShapeRadius, VfxValueType::Scalar,
                                           value.Radius);
                            appendProperty(operation.Node, VfxModuleProperty::KillShapeInverted, VfxValueType::Boolean,
                                           value.Mode == VfxKillShapeMode::Inverted);
                        },
                        [&](const VfxRendererModule& value)
                        { gpuOperation.Setting = static_cast<std::uint32_t>(value.Type); },
                        [](const auto&) {},
                    },
                    module->Payload);
                gpuOperation.PropertyCount =
                    static_cast<std::uint32_t>(payload->ModuleProperties.size()) - gpuOperation.FirstProperty;
                gpuOperation.SampleCount =
                    static_cast<std::uint32_t>(payload->LifetimeSamples.size()) - gpuOperation.FirstSample;
                payload->ParticleOperations.push_back(gpuOperation);
            }
            return payload;
        }
        [[nodiscard]] std::vector<VfxGpuEmitter::CustomInstruction>
        ResolveCustomInstructionsImpl(const VfxCompiledProgram& program,
                                      const std::span<const VfxParameterValue> parameters)
        {
            std::vector<VfxGpuEmitter::CustomInstruction> result;
            result.reserve(program.CustomInstructions.size());
            for (const auto& instruction : program.CustomInstructions)
            {
                if (instruction.ParameterSlot != ~std::uint32_t{0} && instruction.ValueRegister != ~std::uint32_t{0})
                {
                    throw std::invalid_argument("VFX Custom HLSL instruction has multiple operand sources.");
                }
                if (instruction.ParameterSlot != ~std::uint32_t{0} && instruction.ParameterSlot >= parameters.size())
                    throw std::invalid_argument("VFX Custom HLSL parameter slot is invalid.");
                if (instruction.ValueRegister != ~std::uint32_t{0} &&
                    instruction.ValueRegister >= program.ValueRegisterCount)
                {
                    throw std::invalid_argument("VFX Custom HLSL expression register is invalid.");
                }
                const auto& value =
                    instruction.ValueRegister != ~std::uint32_t{0}   ? DefaultVfxValue(instruction.OperandType)
                    : instruction.ParameterSlot == ~std::uint32_t{0} ? instruction.Literal
                                                                     : parameters[instruction.ParameterSlot];
                result.push_back({instruction.Context, instruction.Target, instruction.Operation,
                                  instruction.ScaleByDeltaTime, instruction.OperandType,
                                  ParameterVectorImpl(instruction.OperandType, value), instruction.ValueRegister});
            }
            return result;
        }
        [[nodiscard]] std::vector<Internal::ResolvedProgramState>
        ResolveProgramsImpl(const VfxEffectAsset& effect, const VfxBackend backend,
                            const std::span<const VfxParameterOverride> overrides, const VfxSubgraphResolver& resolver)
        {
            auto source =
                std::make_shared<const VfxEffectDefinition>(ExpandVfxSubgraphs(effect.Definition(), resolver));
            auto programs = CompileVfxEffectSystems(*source, backend);
            std::vector<Internal::ResolvedProgramState> results;
            results.reserve(programs.size());
            for (auto& program : programs)
            {
                Internal::ResolvedProgramState result;
                result.Program = std::move(program);
                result.Source = source;
                if (!result.Program.Valid)
                {
                    const auto diagnostic =
                        std::ranges::find(result.Program.Diagnostics, VfxCompileDiagnosticSeverity::Error,
                                          &VfxCompileDiagnostic::Severity);
                    throw std::invalid_argument(diagnostic == result.Program.Diagnostics.end()
                                                    ? "VFX graph program is invalid."
                                                    : "VFX graph program is invalid: " + diagnostic->Message);
                }
                result.Overrides.assign(overrides.begin(), overrides.end());
                result.Parameters = ResolveParametersImpl(result.Program, result.Overrides);
                result.Definition =
                    Internal::ResolveVfxExecutableDefinition(*source, result.Program, result.Parameters);
                Internal::ValidateVfxResolvedBackendCapabilities(
                    result.Definition, result.Program, backend,
                    source->SchemaVersion >= CurrentVfxSchemaVersion &&
                        source->ExecutionSource == VfxExecutionSource::Graph &&
                        source->CompatibilityMode == VfxCompatibilityMode::NativeSchema4);
                result.CustomInstructions = ResolveCustomInstructionsImpl(result.Program, result.Parameters);
                result.GpuExecution = BuildGpuExecutionPayloadImpl(result.Program, result.Definition, result.Parameters,
                                                                   result.CustomInstructions);
                results.push_back(std::move(result));
            }
            return results;
        }
    } // namespace

    namespace Internal
    {
        bool IsGpuParticleModuleProperty(const VfxModuleProperty property) noexcept
        {
            return IsGpuParticleModulePropertyImpl(property);
        }

        bool ParameterValueMatches(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            return ParameterValueMatchesImpl(type, value);
        }

        Vector4 ParameterVector(const VfxValueType type, const VfxParameterValue& value)
        {
            return ParameterVectorImpl(type, value);
        }

        std::vector<VfxParameterValue> ResolveParameters(const VfxCompiledProgram& program,
                                                         const std::span<const VfxParameterOverride> overrides)
        {
            return ResolveParametersImpl(program, overrides);
        }

        std::shared_ptr<const VfxGpuExecutionPayload>
        BuildGpuExecutionPayload(const VfxCompiledProgram& program, const VfxEffectDefinition& definition,
                                 const std::span<const VfxParameterValue> parameters,
                                 const std::span<const VfxGpuEmitter::CustomInstruction> customInstructions)
        {
            return BuildGpuExecutionPayloadImpl(program, definition, parameters, customInstructions);
        }

        std::vector<VfxGpuEmitter::CustomInstruction>
        ResolveCustomInstructions(const VfxCompiledProgram& program,
                                  const std::span<const VfxParameterValue> parameters)
        {
            return ResolveCustomInstructionsImpl(program, parameters);
        }

        std::vector<ResolvedProgramState> ResolvePrograms(const VfxEffectAsset& effect, const VfxBackend backend,
                                                          const std::span<const VfxParameterOverride> overrides,
                                                          const VfxSubgraphResolver& resolver)
        {
            return ResolveProgramsImpl(effect, backend, overrides, resolver);
        }
    } // namespace Internal
} // namespace Keire
