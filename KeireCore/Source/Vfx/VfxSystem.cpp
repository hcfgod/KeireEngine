#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxExecutionInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <type_traits>
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

        constexpr Vector3 Gravity{0.0F, -9.81F, 0.0F};
        constexpr std::uint32_t MaximumRuntimeBurstCycles = 1024;
        constexpr std::array<char, 8> VfxCheckpointMagic{'K', 'R', 'V', 'F', 'X', 'C', 'P', '\0'};
        constexpr std::uint32_t VfxCheckpointVersion = 1;

        class VfxCheckpointWriter final
        {
          public:
            template <typename T> void Unsigned(const T value)
            {
                static_assert(std::is_unsigned_v<T>);
                for (std::size_t index = 0; index < sizeof(T); ++index)
                    Bytes.push_back(static_cast<std::byte>(value >> (index * 8U)));
            }

            void Boolean(const bool value) { Unsigned<std::uint8_t>(value ? 1U : 0U); }
            void Float(const float value) { Unsigned(std::bit_cast<std::uint32_t>(value)); }
            void Double(const double value) { Unsigned(std::bit_cast<std::uint64_t>(value)); }
            void Id(const AssetId value)
            {
                Unsigned(value.High());
                Unsigned(value.Low());
            }

            std::vector<std::byte> Bytes;
        };

        class VfxCheckpointReader final
        {
          public:
            explicit VfxCheckpointReader(const std::span<const std::byte> bytes) : Bytes(bytes) {}

            template <typename T> [[nodiscard]] T Unsigned()
            {
                static_assert(std::is_unsigned_v<T>);
                if (Offset > Bytes.size() || sizeof(T) > Bytes.size() - Offset)
                    throw std::runtime_error("VFX checkpoint is truncated.");
                std::uintmax_t result = 0;
                for (std::size_t index = 0; index < sizeof(T); ++index)
                    result |= static_cast<std::uintmax_t>(std::to_integer<std::uint8_t>(Bytes[Offset++]))
                              << (index * 8U);
                return static_cast<T>(result);
            }

            [[nodiscard]] bool Boolean()
            {
                const auto value = Unsigned<std::uint8_t>();
                if (value > 1U)
                    throw std::runtime_error("VFX checkpoint contains an invalid Boolean.");
                return value != 0U;
            }
            [[nodiscard]] float Float() { return std::bit_cast<float>(Unsigned<std::uint32_t>()); }
            [[nodiscard]] double Double() { return std::bit_cast<double>(Unsigned<std::uint64_t>()); }
            [[nodiscard]] AssetId Id() { return {Unsigned<std::uint64_t>(), Unsigned<std::uint64_t>()}; }
            [[nodiscard]] bool Complete() const noexcept { return Offset == Bytes.size(); }

          private:
            std::span<const std::byte> Bytes;
            std::size_t Offset = 0;
        };

        [[nodiscard]] bool ValidRuntimeBurst(const VfxEffectDefinition& definition,
                                             const VfxBurstModule& burst) noexcept
        {
            return std::isfinite(burst.Time) && burst.Time >= 0.0F && burst.Time < definition.Duration &&
                   burst.Count >= 1 && burst.Count <= 1'000'000 && burst.Cycles >= 1 &&
                   burst.Cycles <= MaximumRuntimeBurstCycles && std::isfinite(burst.Interval) &&
                   burst.Interval >= 0.0F && (burst.Cycles == 1 || burst.Interval > 0.0F) &&
                   burst.Time + static_cast<float>(burst.Cycles - 1) * burst.Interval < definition.Duration;
        }

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] Vector3 Multiply(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] float Length(const Vector3 value) noexcept { return std::sqrt(Dot(value, value)); }

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const auto length = Length(value);
            return length > 0.000001F ? Multiply(value, 1.0F / length) : Vector3{0.0F, 1.0F, 0.0F};
        }

        [[nodiscard]] Quaternion Conjugate(const Quaternion value) noexcept
        {
            return {-value.X, -value.Y, -value.Z, value.W};
        }

        [[nodiscard]] Vector3 Rotate(const Quaternion rotation, const Vector3 value) noexcept
        {
            const Vector3 axis{rotation.X, rotation.Y, rotation.Z};
            const auto firstCross = Vector3{axis.Y * value.Z - axis.Z * value.Y, axis.Z * value.X - axis.X * value.Z,
                                            axis.X * value.Y - axis.Y * value.X};
            const auto secondCross =
                Vector3{axis.Y * firstCross.Z - axis.Z * firstCross.Y, axis.Z * firstCross.X - axis.X * firstCross.Z,
                        axis.X * firstCross.Y - axis.Y * firstCross.X};
            return Add(value, Add(Multiply(firstCross, 2.0F * rotation.W), Multiply(secondCross, 2.0F)));
        }

        [[nodiscard]] Vector3 TransformPosition(const Vector3 emitterPosition, const Quaternion emitterRotation,
                                                const Vector3 localPosition) noexcept
        {
            return Add(emitterPosition, Rotate(emitterRotation, localPosition));
        }

        [[nodiscard]] std::uint32_t NextGeneration(const std::uint32_t generation) noexcept
        {
            auto result = generation + 1U;
            if (result == 0)
                result = 1;
            return result;
        }

        void SaturatingAdd(std::uint64_t& destination, const std::uint64_t value) noexcept
        {
            destination += std::min(value, std::numeric_limits<std::uint64_t>::max() - destination);
        }

        template <typename T> [[nodiscard]] const T* FindEnabledModule(const VfxEffectDefinition& definition) noexcept
        {
            for (const auto& module : definition.Modules)
                if (module.Enabled)
                    if (const auto* result = std::get_if<T>(&module.Payload))
                        return result;
            return nullptr;
        }

        [[nodiscard]] const VfxModuleDefinition* FindCompiledModule(const VfxEffectDefinition& definition,
                                                                    const VfxCompiledProgram& program,
                                                                    const std::uint32_t compiledModuleIndex) noexcept
        {
            if (compiledModuleIndex >= program.Modules.size())
                return nullptr;
            const auto& compiled = program.Modules[compiledModuleIndex];
            if (compiledModuleIndex < definition.Modules.size() &&
                definition.Modules[compiledModuleIndex].Id == compiled.Node)
            {
                return std::addressof(definition.Modules[compiledModuleIndex]);
            }
            if (compiled.ModuleIndex < definition.Modules.size() &&
                (definition.Modules[compiled.ModuleIndex].Id == compiled.Module ||
                 definition.Modules[compiled.ModuleIndex].Id == compiled.Node))
            {
                return std::addressof(definition.Modules[compiled.ModuleIndex]);
            }
            const auto found = std::ranges::find(definition.Modules, compiled.Module, &VfxModuleDefinition::Id);
            if (found != definition.Modules.end())
                return std::addressof(*found);
            const auto execution = std::ranges::find(definition.Modules, compiled.Node, &VfxModuleDefinition::Id);
            return execution == definition.Modules.end() ? nullptr : std::addressof(*execution);
        }

        template <typename T>
        [[nodiscard]] const T* FindCompiledModule(const VfxEffectDefinition& definition,
                                                  const VfxCompiledProgram& program) noexcept
        {
            for (std::uint32_t index = 0; index < program.Modules.size(); ++index)
                if (const auto* module = FindCompiledModule(definition, program, index))
                    if (const auto* result = std::get_if<T>(&module->Payload))
                        return result;
            return nullptr;
        }

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
                    else
                        return std::nullopt;
                },
                payload);
        }

        [[nodiscard]] constexpr bool IsGpuParticleModuleProperty(const VfxModuleProperty property) noexcept
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

        [[nodiscard]] bool ParameterValueMatches(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            return VfxValueMatchesType(type, value) && IsFiniteVfxValue(value) && HasCanonicalRangeEndpoints(value);
        }

        [[nodiscard]] Vector4 ParameterVector(const VfxValueType type, const VfxParameterValue& value)
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
        ResolveParameters(const VfxCompiledProgram& program, const std::span<const VfxParameterOverride> overrides)
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
                    !ParameterValueMatches(parameter->Type, overrideValue.Value))
                {
                    throw std::invalid_argument("VFX parameter override is unknown, hidden, or type-mismatched.");
                }
                result[parameter->Slot] = overrideValue.Value;
            }
            return result;
        }

        [[nodiscard]] std::shared_ptr<const VfxGpuExecutionPayload>
        BuildGpuExecutionPayload(const VfxCompiledProgram& program, const VfxEffectDefinition& definition,
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
        ResolveCustomInstructions(const VfxCompiledProgram& program,
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
                                  ParameterVector(instruction.OperandType, value), instruction.ValueRegister});
            }
            return result;
        }

        struct ResolvedProgramState
        {
            VfxCompiledProgram Program;
            VfxEffectDefinition Definition;
            std::vector<VfxParameterOverride> Overrides;
            std::vector<VfxParameterValue> Parameters;
            std::vector<VfxGpuEmitter::CustomInstruction> CustomInstructions;
            std::shared_ptr<const VfxGpuExecutionPayload> GpuExecution;
        };

        [[nodiscard]] std::vector<ResolvedProgramState>
        ResolvePrograms(const VfxEffectAsset& effect, const VfxBackend backend,
                        const std::span<const VfxParameterOverride> overrides)
        {
            auto programs = CompileVfxEffectSystems(effect.Definition(), backend);
            std::vector<ResolvedProgramState> results;
            results.reserve(programs.size());
            for (auto& program : programs)
            {
                ResolvedProgramState result;
                result.Program = std::move(program);
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
                result.Parameters = ResolveParameters(result.Program, result.Overrides);
                result.Definition =
                    Internal::ResolveVfxExecutableDefinition(effect.Definition(), result.Program, result.Parameters);
                Internal::ValidateVfxResolvedBackendCapabilities(
                    result.Definition, result.Program, backend,
                    effect.Definition().SchemaVersion >= CurrentVfxSchemaVersion &&
                        effect.Definition().ExecutionSource == VfxExecutionSource::Graph &&
                        effect.Definition().CompatibilityMode == VfxCompatibilityMode::NativeSchema4);
                result.CustomInstructions = ResolveCustomInstructions(result.Program, result.Parameters);
                result.GpuExecution = BuildGpuExecutionPayload(result.Program, result.Definition, result.Parameters,
                                                               result.CustomInstructions);
                results.push_back(std::move(result));
            }
            return results;
        }
    } // namespace

    class VfxWorld::Impl final
    {
      public:
        struct EffectSlot
        {
            bool Active = false;
            bool Emitting = false;
            bool FirstUpdate = true;
            bool Finished = false;
            std::uint32_t Generation = 1;
            std::uint32_t RootEffectIndex = ~std::uint32_t{0};
            std::vector<std::uint32_t> SystemEffects;
            Ref<const VfxEffectAsset> Effect;
            VfxCompiledProgram Program;
            VfxEffectDefinition RuntimeDefinition;
            std::vector<VfxParameterOverride> ParameterOverrides;
            std::vector<VfxParameterValue> Parameters;
            std::vector<VfxParameterValue> ExpressionRegisters;
            std::vector<VfxGpuEmitter::CustomInstruction> CustomInstructions;
            std::shared_ptr<const VfxGpuExecutionPayload> GpuExecution;
            std::uint64_t Revision = 0;
            Vector3 Position;
            Quaternion Rotation;
            double Elapsed = 0.0;
            double RateAccumulator = 0.0;
            std::uint32_t Random = 1;
            std::uint32_t SeedOffset = 0;
            std::uint32_t ActiveParticles = 0;
            std::uint64_t DroppedParticles = 0;
            std::uint64_t GpuSpawnSequence = 0;
            std::uint64_t SpawnSequence = 0;
            std::uint64_t PendingEventSpawns = 0;
            std::uint64_t NextParticleId = 1;
            std::uint64_t GpuSimulationRevision = 1;
            double GpuLastDeathTime = 0.0;
            float GpuSimulationDeltaSeconds = 0.0F;
            float GpuEffectTime = 0.0F;
            float SimulationSpeed = 1.0F;
            VfxRuntimeDiagnostic Diagnostics = VfxRuntimeDiagnostic::None;
        };

        struct Particle
        {
            bool Active = false;
            std::uint32_t EffectIndex = 0;
            std::uint64_t Id = 0;
            std::uint64_t SpawnIndex = 0;
            Vector3 Position;
            Vector3 PreviousPosition;
            Vector3 Velocity;
            Vector3 Rotation;
            float Age = 0.0F;
            float Lifetime = 1.0F;
            float Size = 1.0F;
            Color Tint;
            VfxRendererType Renderer = VfxRendererType::Sprite;
            std::uint64_t StripId = 0;
            std::uint32_t ParticleIndexInStrip = 0;
        };

        explicit Impl(VfxWorldSpecification specification) : Specification(std::move(specification))
        {
            if (Specification.MaximumEffects == 0 || Specification.MaximumEffects > 1'000'000 ||
                Specification.MaximumSystemsPerEffect == 0 || Specification.MaximumSystemsPerEffect > 256 ||
                Specification.MaximumEffects >
                    10'000'000U / static_cast<std::uint64_t>(Specification.MaximumSystemsPerEffect) ||
                Specification.MaximumParticles == 0 || Specification.MaximumParticles > 10'000'000)
            {
                throw std::invalid_argument("VFX world capacity is invalid.");
            }

            const auto maximumSystemSlots = Specification.MaximumEffects * Specification.MaximumSystemsPerEffect;
            Effects.resize(maximumSystemSlots);
            FreeEffects.reserve(maximumSystemSlots);
            for (auto index = maximumSystemSlots; index > 0; --index)
                FreeEffects.push_back(index - 1);
            if (Specification.Backend == VfxBackend::Cpu)
            {
                Particles.resize(Specification.MaximumParticles);
                FreeParticles.reserve(Specification.MaximumParticles);
                for (auto index = Specification.MaximumParticles; index > 0; --index)
                    FreeParticles.push_back(index - 1);
            }
            static std::atomic<std::uint64_t> nextWorldId{1};
            WorldId = nextWorldId.fetch_add(1, std::memory_order_relaxed);
            if (WorldId == 0)
                WorldId = nextWorldId.fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] bool IsAlive(const VfxHandle handle) const noexcept
        {
            return handle && handle.Index() < Effects.size() && Effects[handle.Index()].Active &&
                   Effects[handle.Index()].Generation == handle.Generation() &&
                   Effects[handle.Index()].RootEffectIndex == handle.Index();
        }

        template <typename Callback> void ForEachSystem(const std::uint32_t rootIndex, Callback&& callback)
        {
            if (rootIndex >= Effects.size())
                return;
            auto& root = Effects[rootIndex];
            for (const auto index : root.SystemEffects)
                if (index < Effects.size() && Effects[index].Active && Effects[index].RootEffectIndex == rootIndex)
                    callback(index, Effects[index]);
        }

        [[nodiscard]] VfxRuntimeDiagnostic DiagnosticsFor(const VfxEffectDefinition& definition,
                                                          const VfxCompiledProgram& program) const noexcept
        {
            auto result = VfxRuntimeDiagnostic::None;
            if (const auto* shape = FindCompiledModule<VfxShapeModule>(definition, program);
                shape && (shape->Shape == VfxShape::Mesh || shape->Shape == VfxShape::Volume) &&
                !Specification.ShapeSample)
            {
                result |= VfxRuntimeDiagnostic::ShapeAssetSamplerUnavailable;
            }
            if (const auto* collision = FindCompiledModule<VfxCollisionModule>(definition, program))
            {
                if (collision->Mode == VfxCollisionMode::GpuDepth)
                    result |= VfxRuntimeDiagnostic::GpuDepthFellBackToCpu;
                if (collision->Mode == VfxCollisionMode::ScenePhysics)
                    result |= VfxRuntimeDiagnostic::ScenePhysicsSelectedCpu;
                if (collision->Mode != VfxCollisionMode::None && !Specification.CollisionQuery)
                    result |= VfxRuntimeDiagnostic::CollisionQueryUnavailable;
            }
            return result;
        }

        void RestartEffectState(EffectSlot& slot) noexcept
        {
            slot.Elapsed = 0.0;
            slot.RateAccumulator = 0.0;
            slot.FirstUpdate = true;
            slot.Emitting = slot.Program.EventName.empty();
            slot.Finished = false;
            const auto random = slot.RuntimeDefinition.Seed ^ slot.SeedOffset;
            slot.Random = random == 0 ? 0x9e3779b9U : random;
            slot.ActiveParticles = 0;
            slot.GpuSpawnSequence = 0;
            slot.SpawnSequence = 0;
            slot.PendingEventSpawns = slot.Program.EventName == "OnPlay" ? 1U : 0U;
            slot.NextParticleId = 1;
            slot.GpuSimulationRevision = slot.GpuSimulationRevision == std::numeric_limits<std::uint64_t>::max()
                                             ? 1
                                             : slot.GpuSimulationRevision + 1;
            slot.GpuLastDeathTime = 0.0;
            slot.GpuSimulationDeltaSeconds = 0.0F;
            slot.GpuEffectTime = 0.0F;
        }

        void SetOverrides(EffectSlot& slot, const std::span<const VfxParameterOverride> overrides)
        {
            auto candidateOverrides = std::vector<VfxParameterOverride>(overrides.begin(), overrides.end());
            auto candidateParameters = ResolveParameters(slot.Program, candidateOverrides);
            auto candidateDefinition =
                Internal::ResolveVfxExecutableDefinition(slot.Effect->Definition(), slot.Program, candidateParameters);
            Internal::ValidateVfxResolvedBackendCapabilities(
                candidateDefinition, slot.Program, Specification.Backend,
                slot.Effect->Definition().SchemaVersion >= CurrentVfxSchemaVersion &&
                    slot.Effect->Definition().ExecutionSource == VfxExecutionSource::Graph &&
                    slot.Effect->Definition().CompatibilityMode == VfxCompatibilityMode::NativeSchema4);
            auto candidateInstructions = ResolveCustomInstructions(slot.Program, candidateParameters);
            auto candidateGpuExecution =
                BuildGpuExecutionPayload(slot.Program, candidateDefinition, candidateParameters, candidateInstructions);
            const auto diagnostics = DiagnosticsFor(candidateDefinition, slot.Program);
            slot.ParameterOverrides = std::move(candidateOverrides);
            slot.Parameters = std::move(candidateParameters);
            slot.RuntimeDefinition = std::move(candidateDefinition);
            slot.CustomInstructions = std::move(candidateInstructions);
            slot.GpuExecution = std::move(candidateGpuExecution);
            slot.Diagnostics = diagnostics;
        }

        void SetGroupOverrides(const std::uint32_t rootIndex, const std::span<const VfxParameterOverride> overrides)
        {
            auto& root = Effects[rootIndex];
            auto resolved = ResolvePrograms(*root.Effect, Specification.Backend, overrides);
            if (resolved.size() != root.SystemEffects.size())
                throw std::logic_error("VFX system topology changed while applying parameter overrides.");
            std::vector<ResolvedProgramState*> ordered;
            ordered.reserve(root.SystemEffects.size());
            for (const auto effectIndex : root.SystemEffects)
            {
                const auto found =
                    std::ranges::find(resolved, Effects[effectIndex].Program.System,
                                      [](const ResolvedProgramState& state) { return state.Program.System; });
                if (found == resolved.end())
                    throw std::logic_error("VFX system identity changed while applying parameter overrides.");
                ordered.push_back(std::addressof(*found));
            }
            for (std::size_t index = 0; index < root.SystemEffects.size(); ++index)
            {
                auto& slot = Effects[root.SystemEffects[index]];
                auto& state = *ordered[index];
                slot.Program = std::move(state.Program);
                slot.RuntimeDefinition = std::move(state.Definition);
                slot.ParameterOverrides = std::move(state.Overrides);
                slot.Parameters = std::move(state.Parameters);
                slot.ExpressionRegisters.assign(slot.Program.ValueRegisterCount, 0.0F);
                slot.CustomInstructions = std::move(state.CustomInstructions);
                slot.GpuExecution = std::move(state.GpuExecution);
                slot.Diagnostics = DiagnosticsFor(slot.RuntimeDefinition, slot.Program);
            }
        }

        [[nodiscard]] std::uint32_t NextRandom(EffectSlot& slot) noexcept
        {
            auto value = slot.Random;
            value ^= value << 13U;
            value ^= value >> 17U;
            value ^= value << 5U;
            slot.Random = value == 0 ? 0x9e3779b9U : value;
            return slot.Random;
        }

        [[nodiscard]] float UnitRandom(EffectSlot& slot) noexcept
        {
            return static_cast<float>(NextRandom(slot) >> 8U) * (1.0F / 16'777'216.0F);
        }

        [[nodiscard]] float Range(EffectSlot& slot, const float minimum, const float maximum) noexcept
        {
            return minimum + (maximum - minimum) * UnitRandom(slot);
        }

        [[nodiscard]] Vector3 Range(EffectSlot& slot, const Vector3 minimum, const Vector3 maximum) noexcept
        {
            return {Range(slot, minimum.X, maximum.X), Range(slot, minimum.Y, maximum.Y),
                    Range(slot, minimum.Z, maximum.Z)};
        }

        [[nodiscard]] bool EvaluateValueContext(EffectSlot& slot, const VfxContextType context,
                                                const float deltaSeconds, const Particle* particle = nullptr,
                                                const std::uint64_t spawnIndex = 0) noexcept
        {
            if (slot.Program.ValueInstructions.empty())
                return true;
            const auto& source = slot.Effect->Definition();
            Internal::VfxExpressionEvaluationContext evaluation;
            evaluation.EffectSeed = slot.RuntimeDefinition.Seed;
            evaluation.SeedOffset = slot.SeedOffset;
            evaluation.System = source.Systems.empty() ? source.EmitterId : source.Systems.front().Id;
            evaluation.Context = context;
            evaluation.ParticleId = particle ? particle->Id : 0;
            evaluation.StripId =
                particle ? particle->StripId : spawnIndex / std::max<std::uint32_t>(slot.Program.ParticlesPerStrip, 1U);
            evaluation.SpawnIndex = particle ? particle->SpawnIndex : spawnIndex;
            evaluation.SimulationStep = SimulationStepRevision;
            evaluation.EffectTime = static_cast<float>(slot.Elapsed);
            evaluation.DeltaTime = deltaSeconds;
            evaluation.Age = particle ? particle->Age : 0.0F;
            evaluation.Lifetime = particle ? particle->Lifetime : 1.0F;
            evaluation.Position = particle ? particle->Position : Vector3{};
            evaluation.PreviousPosition = particle ? particle->PreviousPosition : Vector3{};
            evaluation.Velocity = particle ? particle->Velocity : Vector3{};
            evaluation.Rotation = particle ? particle->Rotation : Vector3{};
            evaluation.Tint = particle ? particle->Tint : Color{};
            evaluation.Size = particle ? particle->Size : 1.0F;
            evaluation.ParticleIndexInStrip = particle ? particle->ParticleIndexInStrip : 0U;
            evaluation.ParticlesPerStrip = std::max<std::uint32_t>(slot.Program.ParticlesPerStrip, 1U);
            if (!Internal::EvaluateVfxExpressions(slot.Program, slot.Parameters, evaluation, slot.ExpressionRegisters))
            {
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                return false;
            }
            return true;
        }

        [[nodiscard]] const VfxParameterValue* BindingValue(const EffectSlot& slot, const AssetId executionNode,
                                                            const VfxModuleProperty property) const noexcept
        {
            const auto binding =
                std::ranges::find_if(slot.Program.Bindings, [executionNode, property](const VfxCompiledBinding& value)
                                     { return value.Node == executionNode && value.Property == property; });
            if (binding == slot.Program.Bindings.end())
                return nullptr;
            const VfxParameterValue* result = nullptr;
            if (binding->LiteralValue)
                result = std::addressof(*binding->LiteralValue);
            else if (binding->ValueRegister != ~std::uint32_t{0})
            {
                if (binding->ValueRegister < slot.ExpressionRegisters.size())
                    result = std::addressof(slot.ExpressionRegisters[binding->ValueRegister]);
            }
            else if (binding->ParameterSlot < slot.Parameters.size())
                result = std::addressof(slot.Parameters[binding->ParameterSlot]);
            return result && VfxValueMatchesType(binding->Type, *result) ? result : nullptr;
        }

        [[nodiscard]] std::optional<VfxModuleDefinition> BoundModule(EffectSlot& slot,
                                                                     const std::uint32_t compiledModuleIndex)
        {
            if (compiledModuleIndex >= slot.Program.Modules.size())
                return std::nullopt;
            const auto* module = FindCompiledModule(slot.RuntimeDefinition, slot.Program, compiledModuleIndex);
            if (!module)
                return std::nullopt;
            auto result = *module;
            try
            {
                for (const auto& binding : slot.Program.Bindings)
                {
                    if (binding.Node != slot.Program.Modules[compiledModuleIndex].Node)
                        continue;
                    const auto* value = BindingValue(slot, binding.Node, binding.Property);
                    if (!value)
                        throw std::invalid_argument("VFX runtime binding value is unavailable.");
                    Internal::ApplyVfxModuleProperty(result, binding.Property, *value);
                }
                return result;
            }
            catch (...)
            {
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                return std::nullopt;
            }
        }

        [[nodiscard]] bool PrepareGpuUniformExpressions(EffectSlot& slot, const float deltaSeconds) noexcept
        {
            const auto hasRuntimeModuleBinding = std::ranges::any_of(
                slot.Program.Bindings,
                [](const VfxCompiledBinding& binding) {
                    return binding.ValueRegister != ~std::uint32_t{0} && !IsGpuParticleModuleProperty(binding.Property);
                });
            if (slot.Program.ValueInstructions.empty() || !hasRuntimeModuleBinding)
                return true;

            const auto isParticleRegister = [&slot](const std::uint32_t valueRegister) noexcept
            {
                const auto instruction = std::ranges::find(slot.Program.ValueInstructions, valueRegister,
                                                           &VfxCompiledValueInstruction::OutputRegister);
                return instruction != slot.Program.ValueInstructions.end() &&
                       instruction->Domain > VfxEvaluationDomain::PerFrame;
            };

            constexpr std::array contexts{VfxContextType::Spawn, VfxContextType::Initialize, VfxContextType::Update,
                                          VfxContextType::Output};
            for (const auto context : contexts)
                if (!EvaluateValueContext(slot, context, deltaSeconds))
                    return false;

            try
            {
                for (const auto& binding : slot.Program.Bindings)
                {
                    if (binding.ValueRegister == ~std::uint32_t{0})
                        continue;
                    if (IsGpuParticleModuleProperty(binding.Property))
                        continue;
                    if (isParticleRegister(binding.ValueRegister))
                    {
                        throw std::invalid_argument(
                            "VFX GPU particle-domain Block binding reached the uniform materialization path.");
                    }
                    const auto module =
                        std::ranges::find(slot.RuntimeDefinition.Modules, binding.Node, &VfxModuleDefinition::Id);
                    const auto* value = BindingValue(slot, binding.Node, binding.Property);
                    if (module == slot.RuntimeDefinition.Modules.end() || !value)
                        throw std::invalid_argument("VFX GPU uniform binding is unavailable.");
                    Internal::ApplyVfxModuleProperty(*module, binding.Property, *value);
                }

                ValidateVfxEffect(slot.RuntimeDefinition);
                return true;
            }
            catch (...)
            {
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                return false;
            }
        }

        [[nodiscard]] Vector3 SampleShape(EffectSlot& slot, const VfxShapeModule* module) noexcept
        {
            if (!module)
                return {};
            switch (module->Shape)
            {
            case VfxShape::Point:
                return {};
            case VfxShape::Box:
                return {Range(slot, -module->BoxHalfExtent.X, module->BoxHalfExtent.X),
                        Range(slot, -module->BoxHalfExtent.Y, module->BoxHalfExtent.Y),
                        Range(slot, -module->BoxHalfExtent.Z, module->BoxHalfExtent.Z)};
            case VfxShape::Sphere:
            {
                const auto z = Range(slot, -1.0F, 1.0F);
                const auto azimuth = Range(slot, 0.0F, 2.0F * std::numbers::pi_v<float>);
                const auto radial = std::sqrt(std::max(0.0F, 1.0F - z * z));
                const auto radius = std::cbrt(UnitRandom(slot)) * module->Radius;
                return {std::cos(azimuth) * radial * radius, z * radius, std::sin(azimuth) * radial * radius};
            }
            case VfxShape::Cone:
            {
                const auto distance = UnitRandom(slot) * module->ConeLength;
                const auto maximumRadius =
                    std::tan(module->ConeAngleDegrees * std::numbers::pi_v<float> / 180.0F) * distance;
                const auto radius = std::sqrt(UnitRandom(slot)) * maximumRadius;
                const auto azimuth = Range(slot, 0.0F, 2.0F * std::numbers::pi_v<float>);
                return {std::cos(azimuth) * radius, distance, std::sin(azimuth) * radius};
            }
            case VfxShape::Mesh:
            case VfxShape::Volume:
            {
                if (!Specification.ShapeSample)
                    return {};
                const auto asset = module->Shape == VfxShape::Mesh ? module->Mesh : module->Volume;
                try
                {
                    const auto sample = Specification.ShapeSample(asset, NextRandom(slot));
                    if (sample && Math::IsFinite(*sample))
                        return *sample;
                }
                catch (...)
                {
                }
                slot.Diagnostics |= VfxRuntimeDiagnostic::ShapeAssetSamplerUnavailable;
                return {};
            }
            }
            return {};
        }

        [[nodiscard]] bool ApplyCustomInstruction(EffectSlot& slot, Particle& particle,
                                                  const std::uint32_t instructionIndex,
                                                  const float deltaSeconds) const noexcept
        {
            if (instructionIndex >= slot.CustomInstructions.size() ||
                instructionIndex >= slot.Program.CustomInstructions.size())
            {
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                return false;
            }
            const auto applyScalar = [](float& target, const float operand, const VfxCustomOperation operation)
            {
                switch (operation)
                {
                case VfxCustomOperation::Assign:
                    target = operand;
                    break;
                case VfxCustomOperation::Add:
                    target += operand;
                    break;
                case VfxCustomOperation::Multiply:
                    target *= operand;
                    break;
                }
            };
            const auto applyVector =
                [&applyScalar](Vector3& target, const Vector4 operand, const VfxCustomOperation operation)
            {
                applyScalar(target.X, operand.X, operation);
                applyScalar(target.Y, operand.Y, operation);
                applyScalar(target.Z, operand.Z, operation);
            };
            const auto applyColor =
                [&applyScalar](Color& target, const Vector4 operand, const VfxCustomOperation operation)
            {
                applyScalar(target.Red, operand.X, operation);
                applyScalar(target.Green, operand.Y, operation);
                applyScalar(target.Blue, operand.Z, operation);
                applyScalar(target.Alpha, operand.W, operation);
            };

            const auto& instruction = slot.CustomInstructions[instructionIndex];
            auto operand = instruction.Operand;
            const auto& compiled = slot.Program.CustomInstructions[instructionIndex];
            if (compiled.ValueRegister != ~std::uint32_t{0})
            {
                if (compiled.ValueRegister >= slot.ExpressionRegisters.size())
                {
                    slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                    return false;
                }
                try
                {
                    operand = ParameterVector(compiled.OperandType, slot.ExpressionRegisters[compiled.ValueRegister]);
                }
                catch (...)
                {
                    slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                    return false;
                }
            }
            if (instruction.ScaleByDeltaTime)
            {
                operand.X *= deltaSeconds;
                operand.Y *= deltaSeconds;
                operand.Z *= deltaSeconds;
                operand.W *= deltaSeconds;
            }
            switch (instruction.Target)
            {
            case VfxCustomTarget::Position:
                applyVector(particle.Position, operand, instruction.Operation);
                break;
            case VfxCustomTarget::Velocity:
                applyVector(particle.Velocity, operand, instruction.Operation);
                break;
            case VfxCustomTarget::Rotation:
                applyScalar(particle.Rotation.Z, operand.X, instruction.Operation);
                break;
            case VfxCustomTarget::Tint:
                applyColor(particle.Tint, operand, instruction.Operation);
                break;
            case VfxCustomTarget::Size:
                applyScalar(particle.Size, operand.X, instruction.Operation);
                break;
            }
            return true;
        }

        void ReleaseParticle(const std::uint32_t index) noexcept
        {
            auto& particle = Particles[index];
            if (!particle.Active)
                return;
            if (particle.EffectIndex < Effects.size())
            {
                auto& slot = Effects[particle.EffectIndex];
                if (slot.Active && slot.ActiveParticles > 0)
                    --slot.ActiveParticles;
            }
            particle.Active = false;
            FreeParticles.push_back(index);
            --WorldStatistics.ActiveParticles;
        }

        void KillParticles(const std::uint32_t effectIndex) noexcept
        {
            for (std::uint32_t index = 0; index < Particles.size(); ++index)
                if (Particles[index].Active && Particles[index].EffectIndex == effectIndex)
                    ReleaseParticle(index);
        }

        void ReleaseEffect(const std::uint32_t index) noexcept
        {
            auto& slot = Effects[index];
            if (!slot.Active)
                return;
            KillParticles(index);
            slot.Active = false;
            slot.Emitting = false;
            slot.Finished = false;
            slot.Effect.Reset();
            slot.Program = {};
            slot.RuntimeDefinition = {};
            slot.ParameterOverrides.clear();
            slot.Parameters.clear();
            slot.ExpressionRegisters.clear();
            slot.CustomInstructions.clear();
            slot.GpuExecution.reset();
            slot.SystemEffects.clear();
            slot.RootEffectIndex = ~std::uint32_t{0};
            slot.PendingEventSpawns = 0;
            slot.Revision = 0;
            slot.Generation = NextGeneration(slot.Generation);
            FreeEffects.push_back(index);
        }

        void ReleaseEffectGroup(const std::uint32_t rootIndex) noexcept
        {
            if (rootIndex >= Effects.size() || !Effects[rootIndex].Active ||
                Effects[rootIndex].RootEffectIndex != rootIndex)
            {
                return;
            }
            const auto systems = Effects[rootIndex].SystemEffects;
            for (const auto index : systems)
                if (index < Effects.size())
                    ReleaseEffect(index);
            if (WorldStatistics.ActiveEffects > 0)
                --WorldStatistics.ActiveEffects;
        }

        void SpawnOne(const std::uint32_t effectIndex, const std::uint64_t spawnIndex, const float deltaSeconds)
        {
            auto& slot = Effects[effectIndex];
            const auto& definition = slot.RuntimeDefinition;
            if (slot.ActiveParticles >= definition.Capacity || FreeParticles.empty())
            {
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }

            const auto particleIndex = FreeParticles.back();
            FreeParticles.pop_back();
            auto& particle = Particles[particleIndex];
            const auto* size = FindCompiledModule<VfxSizeOverLifetimeModule>(definition, slot.Program);
            const auto* color = FindCompiledModule<VfxColorOverLifetimeModule>(definition, slot.Program);

            particle.Active = true;
            particle.EffectIndex = effectIndex;
            particle.Id = slot.NextParticleId++;
            if (slot.NextParticleId == 0)
                slot.NextParticleId = 1;
            particle.SpawnIndex = spawnIndex;
            particle.StripId = spawnIndex / std::max<std::uint32_t>(slot.Program.ParticlesPerStrip, 1U);
            particle.ParticleIndexInStrip =
                static_cast<std::uint32_t>(spawnIndex % std::max<std::uint32_t>(slot.Program.ParticlesPerStrip, 1U));
            particle.Position = definition.Space == VfxSimulationSpace::World ? slot.Position : Vector3{};
            particle.PreviousPosition = particle.Position;
            particle.Velocity = {};
            particle.Rotation = {};
            particle.Age = 0.0F;
            particle.Lifetime = 1.0F;
            particle.Size = size ? std::max(0.0F, size->Size.Evaluate(0.0F)) : 1.0F;
            particle.Tint = color ? color->Color.Evaluate(0.0F) : Color{};
            particle.Renderer = VfxRendererType::Sprite;

            if (!EvaluateValueContext(slot, VfxContextType::Spawn, deltaSeconds, std::addressof(particle), spawnIndex))
            {
                particle.Active = false;
                FreeParticles.push_back(particleIndex);
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }

            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Spawn ||
                    operation.Kind != VfxCompiledOperationKind::CustomHlsl)
                {
                    continue;
                }
                if (!ApplyCustomInstruction(slot, particle, operation.Index, 0.0F))
                {
                    particle.Active = false;
                    FreeParticles.push_back(particleIndex);
                    SaturatingAdd(slot.DroppedParticles, 1);
                    SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                    return;
                }
            }
            if (!EvaluateValueContext(slot, VfxContextType::Initialize, 0.0F, std::addressof(particle), spawnIndex))
            {
                particle.Active = false;
                FreeParticles.push_back(particleIndex);
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Initialize)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    if (!ApplyCustomInstruction(slot, particle, operation.Index, 0.0F))
                    {
                        particle.Active = false;
                        FreeParticles.push_back(particleIndex);
                        SaturatingAdd(slot.DroppedParticles, 1);
                        SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                        return;
                    }
                    continue;
                }
                auto module = BoundModule(slot, operation.Index);
                if (!module)
                    continue;
                if (const auto* shape = std::get_if<VfxShapeModule>(&module->Payload))
                {
                    particle.Position = SampleShape(slot, shape);
                    if (definition.Space == VfxSimulationSpace::World)
                        particle.Position = TransformPosition(slot.Position, slot.Rotation, particle.Position);
                }
                else if (const auto* initialize = std::get_if<VfxInitializeModule>(&module->Payload))
                {
                    particle.Velocity = Range(slot, initialize->VelocityMinimum, initialize->VelocityMaximum);
                    if (definition.Space == VfxSimulationSpace::World)
                        particle.Velocity = Rotate(slot.Rotation, particle.Velocity);
                    particle.Rotation = Range(slot, initialize->RotationMinimum, initialize->RotationMaximum);
                    particle.Lifetime = Range(slot, initialize->LifetimeMinimum, initialize->LifetimeMaximum);
                }
            }
            if (!EvaluateValueContext(slot, VfxContextType::Output, 0.0F, std::addressof(particle), spawnIndex))
            {
                particle.Active = false;
                FreeParticles.push_back(particleIndex);
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Output)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    if (!ApplyCustomInstruction(slot, particle, operation.Index, 0.0F))
                    {
                        particle.Active = false;
                        FreeParticles.push_back(particleIndex);
                        SaturatingAdd(slot.DroppedParticles, 1);
                        SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                        return;
                    }
                    continue;
                }
                const auto module = BoundModule(slot, operation.Index);
                if (module)
                    if (const auto* renderer = std::get_if<VfxRendererModule>(&module->Payload))
                    {
                        particle.Renderer = renderer->Type;
                    }
            }
            if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.Velocity) ||
                !Math::IsFinite(particle.Rotation) || !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Size))
            {
                particle.Active = false;
                FreeParticles.push_back(particleIndex);
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }
            particle.PreviousPosition = particle.Position;
            ++slot.ActiveParticles;
            ++WorldStatistics.ActiveParticles;
        }

        [[nodiscard]] std::uint64_t CountBurst(const EffectSlot& slot, const VfxBurstModule& burst,
                                               const double previous, const double current) const noexcept
        {
            if (current < previous || !ValidRuntimeBurst(slot.RuntimeDefinition, burst))
                return 0;
            std::uint64_t count = 0;
            const auto& definition = slot.RuntimeDefinition;
            for (std::uint32_t cycle = 0; cycle < burst.Cycles; ++cycle)
            {
                const auto offset = static_cast<double>(burst.Time) + static_cast<double>(cycle) * burst.Interval;
                if (!definition.Loop)
                {
                    if ((offset > previous && offset <= current) ||
                        (slot.FirstUpdate && previous == 0.0 && offset == 0.0))
                    {
                        ++count;
                    }
                    continue;
                }

                const auto period = static_cast<double>(definition.Duration);
                auto firstLoop = static_cast<std::int64_t>(std::floor((previous - offset) / period)) + 1;
                const auto lastLoop = static_cast<std::int64_t>(std::floor((current - offset) / period));
                firstLoop = std::max<std::int64_t>(firstLoop, 0);
                if (lastLoop >= firstLoop)
                    count += static_cast<std::uint64_t>(lastLoop - firstLoop + 1);
                if (slot.FirstUpdate && previous == 0.0 && offset == 0.0)
                    ++count;
            }
            if (count > std::numeric_limits<std::uint64_t>::max() / burst.Count)
                return std::numeric_limits<std::uint64_t>::max();
            return count * burst.Count;
        }

        void Emit(const std::uint32_t effectIndex, const float deltaSeconds)
        {
            auto& slot = Effects[effectIndex];
            const auto eventDriven = !slot.Program.EventName.empty();
            if (!slot.Emitting && (!eventDriven || slot.PendingEventSpawns == 0))
                return;
            const auto& definition = slot.RuntimeDefinition;
            const auto previous = slot.Elapsed;
            auto effectiveDelta = static_cast<double>(deltaSeconds);
            if (!definition.Loop && !eventDriven)
                effectiveDelta = std::min(effectiveDelta, std::max(0.0, definition.Duration - slot.Elapsed));
            const auto current = previous + effectiveDelta;

            std::uint64_t requested = std::exchange(slot.PendingEventSpawns, 0);
            const auto expressionsValid =
                EvaluateValueContext(slot, VfxContextType::Spawn, deltaSeconds, nullptr, slot.SpawnSequence);
            for (std::uint32_t moduleIndex = 0; expressionsValid && moduleIndex < slot.Program.Modules.size();
                 ++moduleIndex)
            {
                const auto module = BoundModule(slot, moduleIndex);
                const auto* rate = module ? std::get_if<VfxEmissionRateModule>(&module->Payload) : nullptr;
                if (!rate)
                    continue;
                if (!rate || !std::isfinite(rate->ParticlesPerSecond) || rate->ParticlesPerSecond < 0.0F ||
                    rate->ParticlesPerSecond > 1'000'000.0F)
                {
                    slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                    continue;
                }
                slot.RateAccumulator += effectiveDelta * rate->ParticlesPerSecond;
                const auto whole = std::floor(slot.RateAccumulator);
                requested += static_cast<std::uint64_t>(
                    std::min(whole, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
                slot.RateAccumulator -= whole;
            }
            for (std::uint32_t moduleIndex = 0; expressionsValid && moduleIndex < slot.Program.Modules.size();
                 ++moduleIndex)
            {
                const auto module = BoundModule(slot, moduleIndex);
                const auto* burst = module ? std::get_if<VfxBurstModule>(&module->Payload) : nullptr;
                if (!burst)
                    continue;
                if (!burst || !ValidRuntimeBurst(definition, *burst))
                {
                    slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                    continue;
                }
                const auto burstCount = CountBurst(slot, *burst, previous, current);
                requested = std::min<std::uint64_t>(std::numeric_limits<std::uint64_t>::max() - requested, burstCount) +
                            requested;
            }

            const auto availableForEffect =
                definition.Capacity > slot.ActiveParticles ? definition.Capacity - slot.ActiveParticles : 0U;
            const auto available = std::min<std::uint64_t>(availableForEffect, FreeParticles.size());
            const auto spawnCount = std::min(requested, available);
            for (std::uint64_t index = 0; index < spawnCount; ++index)
            {
                SpawnOne(effectIndex, slot.SpawnSequence, deltaSeconds);
                SaturatingAdd(slot.SpawnSequence, 1);
            }
            if (requested > spawnCount)
            {
                const auto dropped = requested - spawnCount;
                // Spawn identity is based on the requested stream, not only accepted particles. Advancing across
                // capacity drops keeps later CPU identities aligned with the GPU allocator's request sequence.
                SaturatingAdd(slot.SpawnSequence, dropped);
                SaturatingAdd(slot.NextParticleId, dropped);
                SaturatingAdd(slot.DroppedParticles, dropped);
                SaturatingAdd(WorldStatistics.DroppedParticles, dropped);
            }

            slot.Elapsed = current;
            slot.FirstUpdate = false;
            if (!eventDriven && !definition.Loop && slot.Elapsed >= definition.Duration)
                slot.Emitting = false;
        }

        void EmitGpu(const std::uint32_t effectIndex, const float deltaSeconds)
        {
            auto& slot = Effects[effectIndex];
            slot.GpuEffectTime = static_cast<float>(slot.Elapsed);
            const auto eventDriven = !slot.Program.EventName.empty();
            if (!slot.Emitting && (!eventDriven || slot.PendingEventSpawns == 0))
            {
                (void)PrepareGpuUniformExpressions(slot, deltaSeconds);
                slot.Elapsed += deltaSeconds;
                return;
            }
            const auto previous = slot.Elapsed;
            const auto& authoredDefinition = slot.RuntimeDefinition;
            auto effectiveDelta = static_cast<double>(deltaSeconds);
            if (!authoredDefinition.Loop && !eventDriven)
                effectiveDelta = std::min(effectiveDelta, std::max(0.0, authoredDefinition.Duration - slot.Elapsed));
            const auto current = previous + effectiveDelta;

            if (!PrepareGpuUniformExpressions(slot, deltaSeconds))
            {
                slot.Elapsed = current;
                slot.FirstUpdate = false;
                if (!slot.RuntimeDefinition.Loop && slot.Elapsed >= slot.RuntimeDefinition.Duration)
                    slot.Emitting = false;
                return;
            }
            const auto& definition = slot.RuntimeDefinition;

            std::uint64_t requested = std::exchange(slot.PendingEventSpawns, 0);
            for (std::uint32_t moduleIndex = 0; moduleIndex < slot.Program.Modules.size(); ++moduleIndex)
            {
                const auto module = BoundModule(slot, moduleIndex);
                const auto* rate = module ? std::get_if<VfxEmissionRateModule>(&module->Payload) : nullptr;
                if (!rate)
                    continue;
                slot.RateAccumulator += effectiveDelta * rate->ParticlesPerSecond;
                const auto whole = std::floor(slot.RateAccumulator);
                const auto emitted = static_cast<std::uint64_t>(
                    std::min(whole, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
                requested = std::min(std::numeric_limits<std::uint64_t>::max() - requested, emitted) + requested;
                slot.RateAccumulator -= whole;
            }
            for (std::uint32_t moduleIndex = 0; moduleIndex < slot.Program.Modules.size(); ++moduleIndex)
            {
                const auto module = BoundModule(slot, moduleIndex);
                if (const auto* burst = module ? std::get_if<VfxBurstModule>(&module->Payload) : nullptr)
                {
                    const auto count = CountBurst(slot, *burst, previous, current);
                    requested = std::min(std::numeric_limits<std::uint64_t>::max() - requested, count) + requested;
                }
            }

            SaturatingAdd(slot.GpuSpawnSequence, requested);
            auto lifetime = 1.0F;
            for (std::uint32_t moduleIndex = 0; moduleIndex < slot.Program.Modules.size(); ++moduleIndex)
            {
                const auto module = BoundModule(slot, moduleIndex);
                if (const auto* initialize = module ? std::get_if<VfxInitializeModule>(&module->Payload) : nullptr)
                {
                    lifetime = initialize->LifetimeMaximum;
                    break;
                }
            }
            if (requested != 0)
                slot.GpuLastDeathTime = std::max(slot.GpuLastDeathTime, current + lifetime);
            slot.ActiveParticles =
                requested == 0
                    ? slot.ActiveParticles
                    : std::min(definition.Capacity,
                               slot.ActiveParticles +
                                   static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, definition.Capacity)));
            slot.Elapsed = current;
            slot.FirstUpdate = false;
            if (!eventDriven && !definition.Loop && slot.Elapsed >= definition.Duration)
                slot.Emitting = false;
        }

        [[nodiscard]] Vector3 WorldPosition(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local
                       ? TransformPosition(slot.Position, slot.Rotation, particle.Position)
                       : particle.Position;
        }

        [[nodiscard]] Vector3 WorldPreviousPosition(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local
                       ? TransformPosition(slot.Position, slot.Rotation, particle.PreviousPosition)
                       : particle.PreviousPosition;
        }

        [[nodiscard]] Vector3 WorldVelocity(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local ? Rotate(slot.Rotation, particle.Velocity)
                                                                             : particle.Velocity;
        }

        void SimulateParticle(const std::uint32_t particleIndex, const float deltaSeconds)
        {
            auto& particle = Particles[particleIndex];
            auto& slot = Effects[particle.EffectIndex];
            const auto& definition = slot.RuntimeDefinition;
            particle.Age += deltaSeconds;
            if (particle.Age >= particle.Lifetime)
            {
                ReleaseParticle(particleIndex);
                return;
            }
            if (!EvaluateValueContext(slot, VfxContextType::Update, deltaSeconds, std::addressof(particle),
                                      particle.SpawnIndex))
            {
                ReleaseParticle(particleIndex);
                return;
            }

            const auto moveParticle = [&](const VfxCollisionModule* collision)
            {
                auto next = Add(particle.Position, Multiply(particle.Velocity, deltaSeconds));
                if (collision && collision->Mode != VfxCollisionMode::None && Specification.CollisionQuery)
                {
                    const auto startWorld = WorldPosition(slot, particle);
                    auto endWorld = next;
                    if (definition.Space == VfxSimulationSpace::Local)
                        endWorld = TransformPosition(slot.Position, slot.Rotation, next);
                    std::optional<VfxCollisionHit> hit;
                    try
                    {
                        hit = Specification.CollisionQuery(startWorld, endWorld);
                    }
                    catch (...)
                    {
                        slot.Diagnostics |= VfxRuntimeDiagnostic::CollisionQueryUnavailable;
                    }
                    if (hit && (!Math::IsFinite(hit->Position) || !Math::IsFinite(hit->Normal)))
                    {
                        slot.Diagnostics |= VfxRuntimeDiagnostic::CollisionQueryUnavailable;
                        hit.reset();
                    }
                    if (hit)
                    {
                        if (collision->KillOnCollision)
                        {
                            ReleaseParticle(particleIndex);
                            return false;
                        }
                        auto normal = Normalize(hit->Normal);
                        auto velocity = WorldVelocity(slot, particle);
                        velocity = Subtract(velocity,
                                            Multiply(normal, (1.0F + collision->Restitution) * Dot(velocity, normal)));
                        if (definition.Space == VfxSimulationSpace::Local)
                        {
                            const auto inverse = Conjugate(slot.Rotation);
                            particle.Velocity = Rotate(inverse, velocity);
                            next = Rotate(inverse, Subtract(hit->Position, slot.Position));
                        }
                        else
                        {
                            particle.Velocity = velocity;
                            next = hit->Position;
                        }
                    }
                }
                particle.Position = next;
                return true;
            };

            const auto normalizedAge = std::clamp(particle.Age / particle.Lifetime, 0.0F, 1.0F);
            particle.PreviousPosition = particle.Position;
            bool moved = false;
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Update)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    if (!ApplyCustomInstruction(slot, particle, operation.Index, deltaSeconds))
                    {
                        ReleaseParticle(particleIndex);
                        return;
                    }
                    continue;
                }
                auto module = BoundModule(slot, operation.Index);
                if (!module)
                    continue;
                if (const auto* size = std::get_if<VfxSizeOverLifetimeModule>(&module->Payload))
                {
                    particle.Size = std::max(0.0F, size->Size.Evaluate(normalizedAge));
                    continue;
                }
                if (const auto* color = std::get_if<VfxColorOverLifetimeModule>(&module->Payload))
                {
                    particle.Tint = color->Color.Evaluate(normalizedAge);
                    continue;
                }
                if (const auto* force = std::get_if<VfxForceModule>(&module->Payload))
                {
                    const auto acceleration = Add(force->Force, Multiply(Gravity, force->GravityMultiplier));
                    particle.Velocity = Add(particle.Velocity, Multiply(acceleration, deltaSeconds));
                }
                else if (const auto* collision = std::get_if<VfxCollisionModule>(&module->Payload))
                {
                    if (!moveParticle(collision))
                        return;
                    moved = true;
                }
            }
            if (!moved && !moveParticle(nullptr))
                return;
            if (!EvaluateValueContext(slot, VfxContextType::Output, deltaSeconds, std::addressof(particle),
                                      particle.SpawnIndex))
            {
                ReleaseParticle(particleIndex);
                return;
            }
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Output)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    if (!ApplyCustomInstruction(slot, particle, operation.Index, deltaSeconds))
                    {
                        ReleaseParticle(particleIndex);
                        return;
                    }
                    continue;
                }
                const auto module = BoundModule(slot, operation.Index);
                if (module)
                    if (const auto* renderer = std::get_if<VfxRendererModule>(&module->Payload))
                    {
                        particle.Renderer = renderer->Type;
                    }
            }
            if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.Velocity) ||
                !Math::IsFinite(particle.Rotation) || !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Size))
            {
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                ReleaseParticle(particleIndex);
            }
        }

        VfxWorldSpecification Specification;
        std::vector<EffectSlot> Effects;
        std::vector<Particle> Particles;
        std::vector<std::uint32_t> FreeEffects;
        std::vector<std::uint32_t> FreeParticles;
        VfxWorldStatistics WorldStatistics;
        std::uint64_t SnapshotRevision = 0;
        std::uint64_t SimulationStepRevision = 0;
        std::uint64_t ResetRevision = 1;
        std::uint64_t WorldId = 0;
        float LastDeltaSeconds = 0.0F;
    };

    VfxWorld::VfxWorld(VfxWorldSpecification specification) : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    VfxWorld::~VfxWorld() = default;

    VfxHandle VfxWorld::Activate(const VfxActivation& activation)
    {
        if (!activation.Effect || activation.Revision == 0 || !Math::IsFinite(activation.Position) ||
            !Math::IsFinite(activation.Rotation) || Math::Length(activation.Rotation) <= 0.000001F)
        {
            throw std::invalid_argument("VFX activation is invalid.");
        }
        const auto normalizedRotation = Math::Normalize(activation.Rotation);
        auto resolved =
            ResolvePrograms(*activation.Effect, m_Impl->Specification.Backend, activation.ParameterOverrides);
        if (resolved.empty() || resolved.size() > m_Impl->Specification.MaximumSystemsPerEffect ||
            m_Impl->WorldStatistics.ActiveEffects >= m_Impl->Specification.MaximumEffects ||
            m_Impl->FreeEffects.size() < resolved.size())
        {
            SaturatingAdd(m_Impl->WorldStatistics.DroppedEffects, 1);
            return {};
        }

        std::vector<std::uint32_t> indices;
        indices.reserve(resolved.size());
        for (std::size_t offset = 0; offset < resolved.size(); ++offset)
            indices.push_back(m_Impl->FreeEffects[m_Impl->FreeEffects.size() - 1U - offset]);
        const auto rootIndex = indices.front();

        std::vector<Impl::EffectSlot> prepared;
        prepared.reserve(resolved.size());
        for (std::size_t systemIndex = 0; systemIndex < resolved.size(); ++systemIndex)
        {
            auto state = std::move(resolved[systemIndex]);
            Impl::EffectSlot slot;
            slot.Active = true;
            slot.Emitting = state.Program.EventName.empty();
            slot.FirstUpdate = true;
            slot.RootEffectIndex = rootIndex;
            slot.Effect = activation.Effect;
            slot.Program = std::move(state.Program);
            slot.RuntimeDefinition = std::move(state.Definition);
            slot.ParameterOverrides = std::move(state.Overrides);
            slot.Parameters = std::move(state.Parameters);
            slot.ExpressionRegisters.assign(slot.Program.ValueRegisterCount, 0.0F);
            slot.CustomInstructions = std::move(state.CustomInstructions);
            slot.GpuExecution = std::move(state.GpuExecution);
            slot.Revision = activation.Revision;
            slot.Position = activation.Position;
            slot.Rotation = normalizedRotation;
            auto random = slot.RuntimeDefinition.Seed ^ activation.SeedOffset ^
                          static_cast<std::uint32_t>(slot.Program.System.High()) ^
                          static_cast<std::uint32_t>(slot.Program.System.Low());
            slot.Random = random == 0 ? 0x9e3779b9U : random;
            slot.SeedOffset = activation.SeedOffset;
            slot.GpuSimulationRevision = 1;
            slot.PendingEventSpawns = slot.Program.EventName == "OnPlay" ? 1U : 0U;
            slot.Diagnostics = m_Impl->DiagnosticsFor(slot.RuntimeDefinition, slot.Program);
            slot.Generation = m_Impl->Effects[indices[systemIndex]].Generation;
            prepared.push_back(std::move(slot));
        }
        prepared.front().SystemEffects = indices;
        for (std::size_t index = 0; index < indices.size(); ++index)
        {
            m_Impl->FreeEffects.pop_back();
            m_Impl->Effects[indices[index]] = std::move(prepared[index]);
        }
        ++m_Impl->WorldStatistics.ActiveEffects;
        ++m_Impl->SnapshotRevision;
        return VfxHandle(rootIndex, m_Impl->Effects[rootIndex].Generation);
    }

    bool VfxWorld::IsAlive(const VfxHandle handle) const noexcept { return m_Impl->IsAlive(handle); }

    void VfxWorld::Stop(const VfxHandle handle)
    {
        if (!m_Impl->IsAlive(handle))
            return;
        m_Impl->ReleaseEffectGroup(handle.Index());
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetTransform(const VfxHandle handle, const Vector3 position, const Quaternion rotation)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot transform a stale VFX handle.");
        if (!Math::IsFinite(position) || !Math::IsFinite(rotation) || Math::Length(rotation) <= 0.000001F)
            throw std::invalid_argument("VFX transform is invalid.");
        const auto normalized = Math::Normalize(rotation);
        m_Impl->ForEachSystem(handle.Index(),
                              [&](const std::uint32_t, Impl::EffectSlot& slot)
                              {
                                  slot.Position = position;
                                  slot.Rotation = normalized;
                              });
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetSimulationSpeed(const VfxHandle handle, const float speed)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        if (!std::isfinite(speed) || speed < 0.0F || speed > 8.0F)
            throw std::invalid_argument("VFX simulation speed must be finite and in the range 0..8.");
        m_Impl->ForEachSystem(handle.Index(),
                              [speed](const std::uint32_t, Impl::EffectSlot& slot) { slot.SimulationSpeed = speed; });
    }

    void VfxWorld::SetParameterOverrides(const VfxHandle handle, const std::span<const VfxParameterOverride> overrides)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        m_Impl->SetGroupOverrides(handle.Index(), overrides);
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetParameter(const VfxHandle handle, const AssetId parameter, VfxParameterValue value)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        auto overrides = m_Impl->Effects[handle.Index()].ParameterOverrides;
        const auto existing = std::ranges::find(overrides, parameter, &VfxParameterOverride::Parameter);
        if (existing == overrides.end())
            overrides.push_back({parameter, std::move(value)});
        else
            existing->Value = std::move(value);
        m_Impl->SetGroupOverrides(handle.Index(), overrides);
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::ResetParameter(const VfxHandle handle, const AssetId parameter)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        auto overrides = m_Impl->Effects[handle.Index()].ParameterOverrides;
        const auto erased = std::erase_if(overrides, [parameter](const VfxParameterOverride& value)
                                          { return value.Parameter == parameter; });
        if (erased == 0)
            return;
        m_Impl->SetGroupOverrides(handle.Index(), overrides);
        ++m_Impl->SnapshotRevision;
    }

    bool VfxWorld::SendEvent(const VfxHandle handle, const std::string_view eventName, const std::uint32_t spawnCount)
    {
        if (!m_Impl->IsAlive(handle) || eventName.empty() || eventName.size() > 256 || spawnCount == 0 ||
            spawnCount > 1'000'000)
        {
            return false;
        }
        bool consumed = false;
        m_Impl->ForEachSystem(handle.Index(),
                              [&](const std::uint32_t, Impl::EffectSlot& slot)
                              {
                                  if (slot.Program.EventName != eventName)
                                      return;
                                  SaturatingAdd(slot.PendingEventSpawns, spawnCount);
                                  slot.Finished = false;
                                  consumed = true;
                              });
        if (consumed)
            ++m_Impl->SnapshotRevision;
        return consumed;
    }

    bool VfxWorld::Reload(const VfxHandle handle, Ref<const VfxEffectAsset> effect, const std::uint64_t revision)
    {
        if (!m_Impl->IsAlive(handle) || !effect)
            return false;
        auto& root = m_Impl->Effects[handle.Index()];
        if (revision <= root.Revision)
            return false;

        const auto candidatePrograms = CompileVfxEffectSystems(effect->Definition(), m_Impl->Specification.Backend);
        if (candidatePrograms.empty() ||
            std::ranges::any_of(candidatePrograms, [](const VfxCompiledProgram& program) { return !program.Valid; }))
            throw std::invalid_argument("Cannot reload an invalid VFX graph program.");
        std::vector<VfxParameterOverride> preservedOverrides;
        preservedOverrides.reserve(root.ParameterOverrides.size());
        bool rejectedOverride = false;
        for (const auto& overrideValue : root.ParameterOverrides)
        {
            const auto parameter = std::ranges::find(candidatePrograms.front().Parameters, overrideValue.Parameter,
                                                     &VfxCompiledParameter::Parameter);
            if (parameter != candidatePrograms.front().Parameters.end() && parameter->Exposed &&
                ParameterValueMatches(parameter->Type, overrideValue.Value))
            {
                preservedOverrides.push_back(overrideValue);
            }
            else
            {
                rejectedOverride = true;
            }
        }
        auto resolved = ResolvePrograms(*effect, m_Impl->Specification.Backend, preservedOverrides);
        if (resolved.size() != root.SystemEffects.size())
            throw std::invalid_argument(
                "VFX hot reload cannot change the number of systems while an instance is active; stop and reactivate.");
        for (const auto effectIndex : root.SystemEffects)
            if (std::ranges::find(resolved, m_Impl->Effects[effectIndex].Program.System,
                                  [](const ResolvedProgramState& state)
                                  { return state.Program.System; }) == resolved.end())
            {
                throw std::invalid_argument(
                    "VFX hot reload cannot replace live system identities; stop and reactivate the effect.");
            }

        const auto retainedEffect = effect;
        for (const auto effectIndex : root.SystemEffects)
        {
            auto& slot = m_Impl->Effects[effectIndex];
            auto state = std::ranges::find(resolved, slot.Program.System,
                                           [](const ResolvedProgramState& value) { return value.Program.System; });
            const auto compatible = slot.RuntimeDefinition.EmitterId == state->Definition.EmitterId &&
                                    slot.Program.StateLayoutHash == state->Program.StateLayoutHash;
            auto diagnostics = m_Impl->DiagnosticsFor(state->Definition, state->Program);
            if (rejectedOverride)
                diagnostics |= VfxRuntimeDiagnostic::ParameterOverrideRejected;
            slot.Effect = retainedEffect;
            slot.Program = std::move(state->Program);
            slot.RuntimeDefinition = std::move(state->Definition);
            slot.ParameterOverrides = std::move(state->Overrides);
            slot.Parameters = std::move(state->Parameters);
            slot.ExpressionRegisters.assign(slot.Program.ValueRegisterCount, 0.0F);
            slot.CustomInstructions = std::move(state->CustomInstructions);
            slot.GpuExecution = std::move(state->GpuExecution);
            slot.Revision = revision;
            slot.Diagnostics = diagnostics;
            if (!compatible)
            {
                m_Impl->KillParticles(effectIndex);
                m_Impl->RestartEffectState(slot);
            }
            else if (m_Impl->Specification.Backend == VfxBackend::Cpu)
            {
                for (auto index = m_Impl->Particles.size();
                     index > 0 && slot.ActiveParticles > slot.RuntimeDefinition.Capacity; --index)
                {
                    const auto particleIndex = static_cast<std::uint32_t>(index - 1);
                    if (m_Impl->Particles[particleIndex].Active &&
                        m_Impl->Particles[particleIndex].EffectIndex == effectIndex)
                    {
                        m_Impl->ReleaseParticle(particleIndex);
                    }
                }
                if (!slot.RuntimeDefinition.Loop && slot.Elapsed >= slot.RuntimeDefinition.Duration &&
                    slot.Program.EventName.empty())
                    slot.Emitting = false;
            }
        }
        ++m_Impl->SnapshotRevision;
        return true;
    }

    void VfxWorld::Update(const float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || deltaSeconds > 10.0F)
            throw std::invalid_argument("VFX update delta must be finite and in the range 0..10 seconds.");
        if (deltaSeconds == 0.0F)
            return;
        m_Impl->LastDeltaSeconds = deltaSeconds;

        if (m_Impl->Specification.Backend == VfxBackend::Cpu)
            for (std::uint32_t index = 0; index < m_Impl->Particles.size(); ++index)
                if (m_Impl->Particles[index].Active)
                {
                    const auto speed = m_Impl->Effects[m_Impl->Particles[index].EffectIndex].SimulationSpeed;
                    if (speed > 0.0F)
                        m_Impl->SimulateParticle(index, deltaSeconds * speed);
                }

        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
        {
            auto& slot = m_Impl->Effects[index];
            if (!slot.Active)
                continue;
            slot.GpuSimulationDeltaSeconds = 0.0F;
            if (slot.SimulationSpeed > 0.0F)
            {
                const auto scaledDelta = deltaSeconds * slot.SimulationSpeed;
                if (m_Impl->Specification.Backend == VfxBackend::Gpu)
                {
                    slot.GpuSimulationDeltaSeconds = scaledDelta;
                    m_Impl->EmitGpu(index, scaledDelta);
                }
                else
                    m_Impl->Emit(index, scaledDelta);
            }
            const auto gpuFinished = m_Impl->Specification.Backend == VfxBackend::Gpu && !slot.Emitting &&
                                     slot.Elapsed >= slot.GpuLastDeathTime;
            const auto particlesFinished = gpuFinished || (!slot.Emitting && slot.ActiveParticles == 0);
            if (particlesFinished && slot.PendingEventSpawns == 0 &&
                (slot.Program.EventName.empty() || slot.Program.EventName == "OnPlay"))
            {
                slot.Finished = true;
            }
        }
        std::vector<std::uint32_t> completedGroups;
        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
        {
            const auto& root = m_Impl->Effects[index];
            if (!root.Active || root.RootEffectIndex != index)
                continue;
            const auto completed = std::ranges::all_of(
                root.SystemEffects, [&](const std::uint32_t systemIndex)
                { return systemIndex < m_Impl->Effects.size() && m_Impl->Effects[systemIndex].Finished; });
            if (completed)
                completedGroups.push_back(index);
        }
        for (const auto root : completedGroups)
            m_Impl->ReleaseEffectGroup(root);
        ++m_Impl->SimulationStepRevision;
        ++m_Impl->SnapshotRevision;
    }

    VfxWorldStatistics VfxWorld::Statistics() const noexcept { return m_Impl->WorldStatistics; }

    VfxRenderPacketCopyResult VfxWorld::CopyRenderPackets(const std::span<VfxRenderParticle> destination) const noexcept
    {
        VfxRenderPacketCopyResult result;
        for (const auto& particle : m_Impl->Particles)
        {
            if (!particle.Active)
                continue;
            if (result.Written >= destination.size())
            {
                ++result.Dropped;
                continue;
            }
            const auto& slot = m_Impl->Effects[particle.EffectIndex];
            const auto& root = m_Impl->Effects[slot.RootEffectIndex];
            const auto* renderer = FindCompiledModule<VfxRendererModule>(slot.RuntimeDefinition, slot.Program);
            destination[result.Written++] = {
                VfxHandle(slot.RootEffectIndex, root.Generation),
                slot.Program.System,
                m_Impl->WorldPosition(slot, particle),
                m_Impl->WorldPreviousPosition(slot, particle),
                particle.Rotation,
                particle.Size,
                particle.Tint,
                particle.Renderer,
                particle.StripId,
                particle.ParticleIndexInStrip,
                renderer ? renderer->Sprite : AssetId{},
                renderer ? renderer->Mesh : AssetId{},
                renderer ? renderer->Material : AssetId{},
            };
        }
        return result;
    }

    VfxRenderSnapshot VfxWorld::CaptureRenderSnapshot(const std::size_t maximumParticles) const
    {
        if (maximumParticles > VfxRenderSnapshot::MaximumParticles)
            throw std::invalid_argument("VFX render snapshot exceeds the supported particle bound.");
        VfxRenderSnapshot result;
        result.m_Revision = m_Impl->SnapshotRevision;
        result.m_WorldId = m_Impl->WorldId;
        result.m_ResetRevision = m_Impl->ResetRevision;
        result.m_SimulationStepRevision = m_Impl->SimulationStepRevision;
        result.m_ParticleCapacity = m_Impl->Specification.MaximumParticles;
        result.m_DeltaSeconds = m_Impl->LastDeltaSeconds;
        if (m_Impl->Specification.Backend == VfxBackend::Gpu)
        {
            result.m_GpuEmitters.reserve(m_Impl->WorldStatistics.ActiveEffects);
            for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
            {
                const auto& slot = m_Impl->Effects[index];
                if (!slot.Active || !slot.Effect)
                    continue;
                const auto& definition = slot.RuntimeDefinition;
                const auto* shape = FindCompiledModule<VfxShapeModule>(definition, slot.Program);
                const auto* initialize = FindCompiledModule<VfxInitializeModule>(definition, slot.Program);
                const auto* force = FindCompiledModule<VfxForceModule>(definition, slot.Program);
                const auto* size = FindCompiledModule<VfxSizeOverLifetimeModule>(definition, slot.Program);
                const auto* color = FindCompiledModule<VfxColorOverLifetimeModule>(definition, slot.Program);
                const auto* renderer = FindCompiledModule<VfxRendererModule>(definition, slot.Program);
                VfxGpuEmitter emitter{VfxHandle(index, slot.Generation),
                                      slot.Program.System,
                                      slot.Revision,
                                      slot.GpuSpawnSequence,
                                      slot.Position,
                                      slot.Rotation,
                                      shape ? shape->BoxHalfExtent : Vector3{},
                                      initialize ? initialize->VelocityMinimum : Vector3{},
                                      initialize ? initialize->LifetimeMinimum : 1.0F,
                                      initialize ? initialize->VelocityMaximum : Vector3{},
                                      initialize ? initialize->LifetimeMaximum : 1.0F,
                                      force ? Add(force->Force, Multiply(Gravity, force->GravityMultiplier))
                                            : Vector3{},
                                      shape ? shape->Radius : 0.0F,
                                      color ? color->Color.Evaluate(0.0F) : Color{},
                                      color ? color->Color.Evaluate(1.0F) : Color{},
                                      size ? size->Size.Evaluate(0.0F) : 1.0F,
                                      size ? size->Size.Evaluate(1.0F) : 1.0F,
                                      definition.Seed ^ slot.SeedOffset,
                                      shape ? shape->Shape : VfxShape::Point,
                                      definition.Space,
                                      renderer ? renderer->Type : VfxRendererType::Sprite,
                                      slot.Program.DataType,
                                      slot.Program.ParticlesPerStrip,
                                      renderer ? renderer->Sprite : AssetId{},
                                      renderer ? renderer->Mesh : AssetId{},
                                      renderer ? renderer->Material : AssetId{},
                                      definition.Capacity,
                                      slot.GpuSimulationRevision};
                if (!slot.GpuExecution)
                    throw std::logic_error("VFX GPU effect is missing its immutable execution payload.");
                emitter.CustomInstructionCount = static_cast<std::uint32_t>(
                    std::min(slot.GpuExecution->CustomInstructions.size(), emitter.CustomInstructions.size()));
                std::ranges::copy_n(slot.GpuExecution->CustomInstructions.begin(), emitter.CustomInstructionCount,
                                    emitter.CustomInstructions.begin());
                emitter.ParticleOperationCount = static_cast<std::uint32_t>(
                    std::min(slot.GpuExecution->ParticleOperations.size(), emitter.ParticleOperations.size()));
                std::ranges::copy_n(slot.GpuExecution->ParticleOperations.begin(), emitter.ParticleOperationCount,
                                    emitter.ParticleOperations.begin());
                emitter.SimulationDeltaSeconds = slot.GpuSimulationDeltaSeconds;
                emitter.RotationMinimum = initialize ? initialize->RotationMinimum : Vector3{};
                emitter.RotationMaximum = initialize ? initialize->RotationMaximum : Vector3{};
                emitter.ConeAngleDegrees = shape ? shape->ConeAngleDegrees : 25.0F;
                emitter.ConeLength = shape ? shape->ConeLength : 1.0F;
                for (std::size_t sample = 0; sample < VfxGpuEmitter::LifetimeSampleCount; ++sample)
                {
                    const auto normalizedAge =
                        static_cast<float>(sample) / static_cast<float>(VfxGpuEmitter::LifetimeSampleCount - 1U);
                    emitter.SizeCurveSamples[sample] = size ? size->Size.Evaluate(normalizedAge) : 1.0F;
                    emitter.ColorGradientSamples[sample] = color ? color->Color.Evaluate(normalizedAge) : Color{};
                }
                emitter.Execution = slot.GpuExecution;
                emitter.EffectTime = slot.GpuEffectTime;
                // CPU expressions evaluate before Update publishes the completed step revision. Preserve that same
                // zero-based step identity in the deferred GPU dispatch captured after the update.
                emitter.SimulationStep = m_Impl->SimulationStepRevision == 0 ? 0 : m_Impl->SimulationStepRevision - 1;
                result.m_GpuEmitters.push_back(std::move(emitter));
            }
            return result;
        }
        result.m_Particles.resize(std::min<std::size_t>(m_Impl->WorldStatistics.ActiveParticles, maximumParticles));
        const auto copied = CopyRenderPackets(result.m_Particles);
        result.m_Particles.resize(copied.Written);
        result.m_DroppedParticles = copied.Dropped;
        return result;
    }

    VfxDebugSnapshot VfxWorld::CaptureDebugSnapshot() const noexcept
    {
        VfxDebugSnapshot result;
        result.Revision = m_Impl->SnapshotRevision;
        result.Statistics = m_Impl->WorldStatistics;
        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
        {
            const auto& slot = m_Impl->Effects[index];
            if (!slot.Active)
                continue;
            if (result.EffectCount >= result.Effects.size())
            {
                ++result.DroppedEffectSamples;
                continue;
            }
            const auto& root = m_Impl->Effects[slot.RootEffectIndex];
            result.Effects[result.EffectCount++] = {VfxHandle(slot.RootEffectIndex, root.Generation),
                                                    slot.Program.System,
                                                    slot.Effect->Definition().EmitterId,
                                                    slot.Revision,
                                                    static_cast<float>(slot.Elapsed),
                                                    slot.ActiveParticles,
                                                    slot.DroppedParticles,
                                                    slot.Emitting,
                                                    slot.Diagnostics};
        }
        for (const auto& particle : m_Impl->Particles)
        {
            if (!particle.Active)
                continue;
            if (result.ParticleCount >= result.Particles.size())
            {
                ++result.DroppedParticleSamples;
                continue;
            }
            const auto& slot = m_Impl->Effects[particle.EffectIndex];
            const auto& root = m_Impl->Effects[slot.RootEffectIndex];
            const auto position = m_Impl->WorldPosition(slot, particle);
            const auto velocity = m_Impl->WorldVelocity(slot, particle);
            result.Particles[result.ParticleCount++] = {
                VfxHandle(slot.RootEffectIndex, root.Generation),
                slot.Program.System,
                position,
                velocity,
                particle.Rotation,
                particle.Tint,
                particle.Size,
                std::clamp(particle.Age / particle.Lifetime, 0.0F, 1.0F),
                particle.Renderer,
            };
        }
        return result;
    }

    VfxBackend VfxWorld::Backend() const noexcept { return m_Impl->Specification.Backend; }

    std::vector<std::byte> VfxWorld::CaptureCheckpoint() const
    {
        VfxCheckpointWriter writer;
        for (const auto value : VfxCheckpointMagic)
            writer.Unsigned(static_cast<std::uint8_t>(value));
        writer.Unsigned(VfxCheckpointVersion);
        writer.Unsigned(static_cast<std::uint8_t>(m_Impl->Specification.Backend));
        writer.Unsigned(static_cast<std::uint32_t>(m_Impl->Effects.size()));
        writer.Unsigned(static_cast<std::uint32_t>(m_Impl->Particles.size()));
        writer.Unsigned(m_Impl->SnapshotRevision);
        writer.Unsigned(m_Impl->SimulationStepRevision);
        writer.Unsigned(m_Impl->ResetRevision);
        writer.Float(m_Impl->LastDeltaSeconds);
        writer.Unsigned(m_Impl->WorldStatistics.ActiveEffects);
        writer.Unsigned(m_Impl->WorldStatistics.ActiveParticles);
        writer.Unsigned(m_Impl->WorldStatistics.DroppedEffects);
        writer.Unsigned(m_Impl->WorldStatistics.DroppedParticles);

        const auto activeEffects = std::ranges::count_if(m_Impl->Effects, &Impl::EffectSlot::Active);
        writer.Unsigned(static_cast<std::uint32_t>(activeEffects));
        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
        {
            const auto& slot = m_Impl->Effects[index];
            if (!slot.Active)
                continue;
            writer.Unsigned(index);
            writer.Unsigned(slot.Generation);
            writer.Unsigned(slot.RootEffectIndex);
            writer.Id(slot.Program.System);
            writer.Unsigned(slot.Revision);
            writer.Boolean(slot.Emitting);
            writer.Boolean(slot.FirstUpdate);
            writer.Boolean(slot.Finished);
            writer.Float(slot.Position.X);
            writer.Float(slot.Position.Y);
            writer.Float(slot.Position.Z);
            writer.Float(slot.Rotation.X);
            writer.Float(slot.Rotation.Y);
            writer.Float(slot.Rotation.Z);
            writer.Float(slot.Rotation.W);
            writer.Double(slot.Elapsed);
            writer.Double(slot.RateAccumulator);
            writer.Unsigned(slot.Random);
            writer.Unsigned(slot.SeedOffset);
            writer.Unsigned(slot.ActiveParticles);
            writer.Unsigned(slot.DroppedParticles);
            writer.Unsigned(slot.GpuSpawnSequence);
            writer.Unsigned(slot.SpawnSequence);
            writer.Unsigned(slot.PendingEventSpawns);
            writer.Unsigned(slot.NextParticleId);
            writer.Unsigned(slot.GpuSimulationRevision);
            writer.Double(slot.GpuLastDeathTime);
            writer.Float(slot.GpuSimulationDeltaSeconds);
            writer.Float(slot.GpuEffectTime);
            writer.Float(slot.SimulationSpeed);
            writer.Unsigned(static_cast<std::uint32_t>(slot.Diagnostics));
            writer.Unsigned(static_cast<std::uint32_t>(slot.ExpressionRegisters.size()));
        }

        const auto activeParticles = std::ranges::count_if(m_Impl->Particles, &Impl::Particle::Active);
        writer.Unsigned(static_cast<std::uint32_t>(activeParticles));
        for (std::uint32_t index = 0; index < m_Impl->Particles.size(); ++index)
        {
            const auto& particle = m_Impl->Particles[index];
            if (!particle.Active)
                continue;
            writer.Unsigned(index);
            writer.Unsigned(particle.EffectIndex);
            writer.Unsigned(particle.Id);
            writer.Unsigned(particle.SpawnIndex);
            for (const auto value :
                 {particle.Position.X, particle.Position.Y, particle.Position.Z, particle.PreviousPosition.X,
                  particle.PreviousPosition.Y, particle.PreviousPosition.Z, particle.Velocity.X, particle.Velocity.Y,
                  particle.Velocity.Z, particle.Rotation.X, particle.Rotation.Y, particle.Rotation.Z})
                writer.Float(value);
            writer.Float(particle.Age);
            writer.Float(particle.Lifetime);
            writer.Float(particle.Size);
            writer.Float(particle.Tint.Red);
            writer.Float(particle.Tint.Green);
            writer.Float(particle.Tint.Blue);
            writer.Float(particle.Tint.Alpha);
            writer.Unsigned(static_cast<std::uint8_t>(particle.Renderer));
            writer.Unsigned(particle.StripId);
            writer.Unsigned(particle.ParticleIndexInStrip);
        }
        return std::move(writer.Bytes);
    }

    void VfxWorld::RestoreCheckpoint(const std::span<const std::byte> checkpoint)
    {
        VfxCheckpointReader reader(checkpoint);
        for (const auto expected : VfxCheckpointMagic)
            if (reader.Unsigned<std::uint8_t>() != static_cast<std::uint8_t>(expected))
                throw std::runtime_error("VFX checkpoint signature is invalid.");
        if (reader.Unsigned<std::uint32_t>() != VfxCheckpointVersion)
            throw std::runtime_error("VFX checkpoint version is unsupported.");
        const auto backend = static_cast<VfxBackend>(reader.Unsigned<std::uint8_t>());
        if (backend != m_Impl->Specification.Backend || reader.Unsigned<std::uint32_t>() != m_Impl->Effects.size() ||
            reader.Unsigned<std::uint32_t>() != m_Impl->Particles.size())
        {
            throw std::runtime_error("VFX checkpoint does not match the active world configuration.");
        }

        const auto snapshotRevision = reader.Unsigned<std::uint64_t>();
        const auto simulationStepRevision = reader.Unsigned<std::uint64_t>();
        const auto resetRevision = reader.Unsigned<std::uint64_t>();
        const auto lastDeltaSeconds = reader.Float();
        VfxWorldStatistics statistics;
        statistics.ActiveEffects = reader.Unsigned<std::uint32_t>();
        statistics.ActiveParticles = reader.Unsigned<std::uint32_t>();
        statistics.DroppedEffects = reader.Unsigned<std::uint64_t>();
        statistics.DroppedParticles = reader.Unsigned<std::uint64_t>();
        if (!std::isfinite(lastDeltaSeconds) || lastDeltaSeconds < 0.0F)
            throw std::runtime_error("VFX checkpoint frame state is invalid.");

        auto effects = m_Impl->Effects;
        const auto activeEffectCount = reader.Unsigned<std::uint32_t>();
        if (activeEffectCount > effects.size())
            throw std::runtime_error("VFX checkpoint effect count exceeds the world capacity.");
        std::set<std::uint32_t> seenEffects;
        for (std::uint32_t count = 0; count < activeEffectCount; ++count)
        {
            const auto index = reader.Unsigned<std::uint32_t>();
            const auto generation = reader.Unsigned<std::uint32_t>();
            const auto rootIndex = reader.Unsigned<std::uint32_t>();
            const auto system = reader.Id();
            const auto revision = reader.Unsigned<std::uint64_t>();
            if (index >= effects.size() || rootIndex >= effects.size() || !seenEffects.insert(index).second ||
                !effects[index].Active || effects[index].Generation != generation ||
                effects[index].RootEffectIndex != rootIndex || effects[index].Program.System != system)
            {
                throw std::runtime_error("VFX checkpoint effect topology is stale.");
            }
            auto& slot = effects[index];
            slot.Revision = revision;
            slot.Emitting = reader.Boolean();
            slot.FirstUpdate = reader.Boolean();
            slot.Finished = reader.Boolean();
            slot.Position = {reader.Float(), reader.Float(), reader.Float()};
            slot.Rotation = {reader.Float(), reader.Float(), reader.Float(), reader.Float()};
            slot.Elapsed = reader.Double();
            slot.RateAccumulator = reader.Double();
            slot.Random = reader.Unsigned<std::uint32_t>();
            slot.SeedOffset = reader.Unsigned<std::uint32_t>();
            slot.ActiveParticles = reader.Unsigned<std::uint32_t>();
            slot.DroppedParticles = reader.Unsigned<std::uint64_t>();
            slot.GpuSpawnSequence = reader.Unsigned<std::uint64_t>();
            slot.SpawnSequence = reader.Unsigned<std::uint64_t>();
            slot.PendingEventSpawns = reader.Unsigned<std::uint64_t>();
            slot.NextParticleId = reader.Unsigned<std::uint64_t>();
            slot.GpuSimulationRevision = reader.Unsigned<std::uint64_t>();
            slot.GpuLastDeathTime = reader.Double();
            slot.GpuSimulationDeltaSeconds = reader.Float();
            slot.GpuEffectTime = reader.Float();
            slot.SimulationSpeed = reader.Float();
            slot.Diagnostics = static_cast<VfxRuntimeDiagnostic>(reader.Unsigned<std::uint32_t>());
            const auto registerCount = reader.Unsigned<std::uint32_t>();
            if (registerCount != slot.ExpressionRegisters.size())
                throw std::runtime_error("VFX checkpoint expression layout is stale.");
            for (auto& value : slot.ExpressionRegisters)
                value = 0.0F;
            if (!Math::IsFinite(slot.Position) || !Math::IsFinite(slot.Rotation) || !std::isfinite(slot.Elapsed) ||
                slot.Elapsed < 0.0 || !std::isfinite(slot.RateAccumulator) || !std::isfinite(slot.GpuLastDeathTime) ||
                !std::isfinite(slot.GpuSimulationDeltaSeconds) || !std::isfinite(slot.GpuEffectTime) ||
                !std::isfinite(slot.SimulationSpeed) || slot.SimulationSpeed < 0.0F || slot.SimulationSpeed > 8.0F)
            {
                throw std::runtime_error("VFX checkpoint effect state is invalid.");
            }
        }
        if (seenEffects.size() != static_cast<std::size_t>(std::ranges::count_if(effects, &Impl::EffectSlot::Active)))
            throw std::runtime_error("VFX checkpoint omits active effect state.");

        auto particles = m_Impl->Particles;
        for (auto& particle : particles)
            particle.Active = false;
        const auto activeParticleCount = reader.Unsigned<std::uint32_t>();
        if (activeParticleCount > particles.size())
            throw std::runtime_error("VFX checkpoint particle count exceeds the world capacity.");
        std::set<std::uint32_t> seenParticles;
        for (std::uint32_t count = 0; count < activeParticleCount; ++count)
        {
            const auto index = reader.Unsigned<std::uint32_t>();
            if (index >= particles.size() || !seenParticles.insert(index).second)
                throw std::runtime_error("VFX checkpoint particle identity is invalid.");
            auto& particle = particles[index];
            particle.Active = true;
            particle.EffectIndex = reader.Unsigned<std::uint32_t>();
            particle.Id = reader.Unsigned<std::uint64_t>();
            particle.SpawnIndex = reader.Unsigned<std::uint64_t>();
            particle.Position = {reader.Float(), reader.Float(), reader.Float()};
            particle.PreviousPosition = {reader.Float(), reader.Float(), reader.Float()};
            particle.Velocity = {reader.Float(), reader.Float(), reader.Float()};
            particle.Rotation = {reader.Float(), reader.Float(), reader.Float()};
            particle.Age = reader.Float();
            particle.Lifetime = reader.Float();
            particle.Size = reader.Float();
            particle.Tint = {reader.Float(), reader.Float(), reader.Float(), reader.Float()};
            particle.Renderer = static_cast<VfxRendererType>(reader.Unsigned<std::uint8_t>());
            particle.StripId = reader.Unsigned<std::uint64_t>();
            particle.ParticleIndexInStrip = reader.Unsigned<std::uint32_t>();
            if (particle.EffectIndex >= effects.size() || !effects[particle.EffectIndex].Active ||
                particle.Renderer > VfxRendererType::Volumetric || !Math::IsFinite(particle.Position) ||
                !Math::IsFinite(particle.PreviousPosition) || !Math::IsFinite(particle.Velocity) ||
                !Math::IsFinite(particle.Rotation) || !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Age) ||
                !std::isfinite(particle.Lifetime) || particle.Age < 0.0F || particle.Lifetime <= 0.0F ||
                !std::isfinite(particle.Size))
            {
                throw std::runtime_error("VFX checkpoint particle state is invalid.");
            }
        }
        if (!reader.Complete() || (backend == VfxBackend::Cpu && statistics.ActiveParticles != activeParticleCount))
            throw std::runtime_error("VFX checkpoint particle accounting is invalid.");

        std::vector<std::uint32_t> freeParticles;
        freeParticles.reserve(particles.size() - activeParticleCount);
        for (auto index = particles.size(); index > 0; --index)
            if (!particles[index - 1U].Active)
                freeParticles.push_back(static_cast<std::uint32_t>(index - 1U));
        m_Impl->Effects = std::move(effects);
        m_Impl->Particles = std::move(particles);
        m_Impl->FreeParticles = std::move(freeParticles);
        m_Impl->WorldStatistics = statistics;
        m_Impl->SnapshotRevision = snapshotRevision;
        m_Impl->SimulationStepRevision = simulationStepRevision;
        m_Impl->ResetRevision = resetRevision;
        m_Impl->LastDeltaSeconds = lastDeltaSeconds;
    }

    void VfxWorld::Clear() noexcept
    {
        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
            m_Impl->ReleaseEffect(index);
        m_Impl->WorldStatistics.ActiveEffects = 0;
        if (m_Impl->Specification.Backend == VfxBackend::Gpu)
            ++m_Impl->ResetRevision;
        ++m_Impl->SnapshotRevision;
    }
} // namespace Keire
