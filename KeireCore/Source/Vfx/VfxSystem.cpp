#include "Keire/Vfx/VfxSystem.h"

#include "VfxExecutionInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
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
        constexpr Vector3 Gravity{0.0F, -9.81F, 0.0F};

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

        template <typename T>
        [[nodiscard]] bool ModuleChanged(const VfxEffectDefinition& first, const VfxEffectDefinition& second) noexcept
        {
            const auto* left = FindEnabledModule<T>(first);
            const auto* right = FindEnabledModule<T>(second);
            return (left == nullptr) != (right == nullptr) || (left && right && *left != *right);
        }

        [[nodiscard]] bool GpuStoredParticleStateChanged(const VfxEffectDefinition& first,
                                                         const VfxEffectDefinition& second) noexcept
        {
            return ModuleChanged<VfxForceModule>(first, second) ||
                   ModuleChanged<VfxSizeOverLifetimeModule>(first, second) ||
                   ModuleChanged<VfxColorOverLifetimeModule>(first, second) ||
                   ModuleChanged<VfxRendererModule>(first, second);
        }

        [[nodiscard]] bool ParameterValueMatches(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            switch (type)
            {
            case VfxValueType::Boolean:
                return std::holds_alternative<bool>(value);
            case VfxValueType::Integer:
                return std::holds_alternative<std::int64_t>(value);
            case VfxValueType::Scalar:
                return std::holds_alternative<float>(value) && std::isfinite(std::get<float>(value));
            case VfxValueType::Vector2:
                return std::holds_alternative<Vector2>(value) && Math::IsFinite(std::get<Vector2>(value));
            case VfxValueType::Vector3:
                return std::holds_alternative<Vector3>(value) && Math::IsFinite(std::get<Vector3>(value));
            case VfxValueType::Color:
                return std::holds_alternative<Color>(value) && Math::IsFinite(std::get<Color>(value));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
                return std::holds_alternative<AssetId>(value);
            case VfxValueType::ParticleStream:
                return false;
            }
            return false;
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
            case VfxValueType::Color:
            {
                const auto color = std::get<Color>(value);
                return {color.Red, color.Green, color.Blue, color.Alpha};
            }
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            case VfxValueType::ParticleStream:
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

        [[nodiscard]] std::vector<VfxGpuEmitter::CustomInstruction>
        ResolveCustomInstructions(const VfxCompiledProgram& program,
                                  const std::span<const VfxParameterValue> parameters)
        {
            if (program.CustomInstructions.size() > VfxGpuEmitter::MaximumCustomInstructions)
                throw std::invalid_argument("VFX program exceeds the portable Custom HLSL instruction budget.");
            std::vector<VfxGpuEmitter::CustomInstruction> result;
            result.reserve(program.CustomInstructions.size());
            for (const auto& instruction : program.CustomInstructions)
            {
                if (instruction.ParameterSlot != ~std::uint32_t{0} && instruction.ParameterSlot >= parameters.size())
                    throw std::invalid_argument("VFX Custom HLSL parameter slot is invalid.");
                const auto& value = instruction.ParameterSlot == ~std::uint32_t{0}
                                        ? instruction.Literal
                                        : parameters[instruction.ParameterSlot];
                result.push_back({instruction.Context, instruction.Target, instruction.Operation,
                                  instruction.ScaleByDeltaTime, ParameterVector(instruction.OperandType, value)});
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
        };

        [[nodiscard]] ResolvedProgramState ResolveProgram(const VfxEffectAsset& effect, const VfxBackend backend,
                                                          const std::span<const VfxParameterOverride> overrides)
        {
            ResolvedProgramState result;
            result.Program = CompileVfxEffect(effect.Definition(), backend);
            if (!result.Program.Valid)
            {
                const auto diagnostic = std::ranges::find(
                    result.Program.Diagnostics, VfxCompileDiagnosticSeverity::Error, &VfxCompileDiagnostic::Severity);
                throw std::invalid_argument(diagnostic == result.Program.Diagnostics.end()
                                                ? "VFX graph program is invalid."
                                                : "VFX graph program is invalid: " + diagnostic->Message);
            }
            result.Overrides.assign(overrides.begin(), overrides.end());
            result.Parameters = ResolveParameters(result.Program, result.Overrides);
            result.Definition =
                Internal::ResolveVfxExecutableDefinition(effect.Definition(), result.Program, result.Parameters);
            result.CustomInstructions = ResolveCustomInstructions(result.Program, result.Parameters);
            return result;
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
            std::uint32_t Generation = 1;
            Ref<const VfxEffectAsset> Effect;
            VfxCompiledProgram Program;
            VfxEffectDefinition RuntimeDefinition;
            std::vector<VfxParameterOverride> ParameterOverrides;
            std::vector<VfxParameterValue> Parameters;
            std::vector<VfxGpuEmitter::CustomInstruction> CustomInstructions;
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
            std::uint64_t GpuSimulationRevision = 1;
            double GpuLastDeathTime = 0.0;
            float GpuSimulationDeltaSeconds = 0.0F;
            float SimulationSpeed = 1.0F;
            VfxRuntimeDiagnostic Diagnostics = VfxRuntimeDiagnostic::None;
        };

        struct Particle
        {
            bool Active = false;
            std::uint32_t EffectIndex = 0;
            Vector3 Position;
            Vector3 Velocity;
            Vector3 Rotation;
            float Age = 0.0F;
            float Lifetime = 1.0F;
            float Size = 1.0F;
            Color Tint;
            VfxRendererType Renderer = VfxRendererType::Sprite;
        };

        explicit Impl(VfxWorldSpecification specification) : Specification(std::move(specification))
        {
            if (Specification.MaximumEffects == 0 || Specification.MaximumEffects > 1'000'000 ||
                Specification.MaximumParticles == 0 || Specification.MaximumParticles > 10'000'000)
            {
                throw std::invalid_argument("VFX world capacity is invalid.");
            }

            Effects.resize(Specification.MaximumEffects);
            FreeEffects.reserve(Specification.MaximumEffects);
            for (auto index = Specification.MaximumEffects; index > 0; --index)
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
                   Effects[handle.Index()].Generation == handle.Generation();
        }

        [[nodiscard]] VfxRuntimeDiagnostic DiagnosticsFor(const VfxEffectDefinition& definition) const noexcept
        {
            auto result = VfxRuntimeDiagnostic::None;
            if (const auto* shape = FindEnabledModule<VfxShapeModule>(definition);
                shape && (shape->Shape == VfxShape::Mesh || shape->Shape == VfxShape::Volume) &&
                !Specification.ShapeSample)
            {
                result |= VfxRuntimeDiagnostic::ShapeAssetSamplerUnavailable;
            }
            if (const auto* collision = FindEnabledModule<VfxCollisionModule>(definition))
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
            slot.Emitting = true;
            const auto random = slot.RuntimeDefinition.Seed ^ slot.SeedOffset;
            slot.Random = random == 0 ? 0x9e3779b9U : random;
            slot.ActiveParticles = 0;
            slot.GpuSpawnSequence = 0;
            slot.GpuSimulationRevision = slot.GpuSimulationRevision == std::numeric_limits<std::uint64_t>::max()
                                             ? 1
                                             : slot.GpuSimulationRevision + 1;
            slot.GpuLastDeathTime = 0.0;
            slot.GpuSimulationDeltaSeconds = 0.0F;
        }

        void SetOverrides(EffectSlot& slot, const std::span<const VfxParameterOverride> overrides)
        {
            auto candidateOverrides = std::vector<VfxParameterOverride>(overrides.begin(), overrides.end());
            auto candidateParameters = ResolveParameters(slot.Program, candidateOverrides);
            auto candidateDefinition =
                Internal::ResolveVfxExecutableDefinition(slot.Effect->Definition(), slot.Program, candidateParameters);
            auto candidateInstructions = ResolveCustomInstructions(slot.Program, candidateParameters);
            const auto diagnostics = DiagnosticsFor(candidateDefinition);
            const bool restartGpuParticles = Specification.Backend == VfxBackend::Gpu &&
                                             GpuStoredParticleStateChanged(slot.RuntimeDefinition, candidateDefinition);

            slot.ParameterOverrides = std::move(candidateOverrides);
            slot.Parameters = std::move(candidateParameters);
            slot.RuntimeDefinition = std::move(candidateDefinition);
            slot.CustomInstructions = std::move(candidateInstructions);
            slot.Diagnostics = diagnostics;
            if (restartGpuParticles)
                RestartEffectState(slot);
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

        void ApplyCustomInstruction(const EffectSlot& slot, Particle& particle, const std::uint32_t instructionIndex,
                                    const float deltaSeconds) const noexcept
        {
            if (instructionIndex >= slot.CustomInstructions.size())
                return;
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
            slot.Effect.Reset();
            slot.Program = {};
            slot.RuntimeDefinition = {};
            slot.ParameterOverrides.clear();
            slot.Parameters.clear();
            slot.CustomInstructions.clear();
            slot.Revision = 0;
            slot.Generation = NextGeneration(slot.Generation);
            FreeEffects.push_back(index);
            --WorldStatistics.ActiveEffects;
        }

        void SpawnOne(const std::uint32_t effectIndex)
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
            const auto* size = FindEnabledModule<VfxSizeOverLifetimeModule>(definition);
            const auto* color = FindEnabledModule<VfxColorOverLifetimeModule>(definition);

            particle.Active = true;
            particle.EffectIndex = effectIndex;
            particle.Position = definition.Space == VfxSimulationSpace::World ? slot.Position : Vector3{};
            particle.Velocity = {};
            particle.Rotation = {};
            particle.Age = 0.0F;
            particle.Lifetime = 1.0F;
            particle.Size = size ? std::max(0.0F, size->Size.Evaluate(0.0F)) : 1.0F;
            particle.Tint = color ? color->Color.Evaluate(0.0F) : Color{};
            particle.Renderer = VfxRendererType::Sprite;

            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Spawn ||
                    operation.Kind != VfxCompiledOperationKind::CustomHlsl)
                {
                    continue;
                }
                ApplyCustomInstruction(slot, particle, operation.Index, 0.0F);
            }
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Initialize)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    ApplyCustomInstruction(slot, particle, operation.Index, 0.0F);
                    continue;
                }
                if (operation.Index >= definition.Modules.size())
                    continue;
                const auto& module = definition.Modules[operation.Index];
                if (const auto* shape = std::get_if<VfxShapeModule>(&module.Payload))
                {
                    particle.Position = SampleShape(slot, shape);
                    if (definition.Space == VfxSimulationSpace::World)
                        particle.Position = TransformPosition(slot.Position, slot.Rotation, particle.Position);
                }
                else if (const auto* initialize = std::get_if<VfxInitializeModule>(&module.Payload))
                {
                    particle.Velocity = Range(slot, initialize->VelocityMinimum, initialize->VelocityMaximum);
                    if (definition.Space == VfxSimulationSpace::World)
                        particle.Velocity = Rotate(slot.Rotation, particle.Velocity);
                    particle.Rotation = Range(slot, initialize->RotationMinimum, initialize->RotationMaximum);
                    particle.Lifetime = Range(slot, initialize->LifetimeMinimum, initialize->LifetimeMaximum);
                }
            }
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Output)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    ApplyCustomInstruction(slot, particle, operation.Index, 0.0F);
                    continue;
                }
                if (operation.Index >= definition.Modules.size())
                    continue;
                if (const auto* renderer = std::get_if<VfxRendererModule>(&definition.Modules[operation.Index].Payload))
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
            ++slot.ActiveParticles;
            ++WorldStatistics.ActiveParticles;
        }

        [[nodiscard]] std::uint64_t CountBurst(const EffectSlot& slot, const VfxBurstModule& burst,
                                               const double previous, const double current) const noexcept
        {
            if (current < previous)
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
            if (!slot.Emitting)
                return;
            const auto& definition = slot.RuntimeDefinition;
            const auto previous = slot.Elapsed;
            auto effectiveDelta = static_cast<double>(deltaSeconds);
            if (!definition.Loop)
                effectiveDelta = std::min(effectiveDelta, std::max(0.0, definition.Duration - slot.Elapsed));
            const auto current = previous + effectiveDelta;

            std::uint64_t requested = 0;
            if (const auto* rate = FindEnabledModule<VfxEmissionRateModule>(definition))
            {
                slot.RateAccumulator += effectiveDelta * rate->ParticlesPerSecond;
                const auto whole = std::floor(slot.RateAccumulator);
                requested += static_cast<std::uint64_t>(
                    std::min(whole, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
                slot.RateAccumulator -= whole;
            }
            for (const auto& module : definition.Modules)
            {
                if (!module.Enabled)
                    continue;
                if (const auto* burst = std::get_if<VfxBurstModule>(&module.Payload))
                {
                    const auto burstCount = CountBurst(slot, *burst, previous, current);
                    requested =
                        std::min<std::uint64_t>(std::numeric_limits<std::uint64_t>::max() - requested, burstCount) +
                        requested;
                }
            }

            const auto availableForEffect =
                definition.Capacity > slot.ActiveParticles ? definition.Capacity - slot.ActiveParticles : 0U;
            const auto available = std::min<std::uint64_t>(availableForEffect, FreeParticles.size());
            const auto spawnCount = std::min(requested, available);
            for (std::uint64_t index = 0; index < spawnCount; ++index)
                SpawnOne(effectIndex);
            if (requested > spawnCount)
            {
                const auto dropped = requested - spawnCount;
                SaturatingAdd(slot.DroppedParticles, dropped);
                SaturatingAdd(WorldStatistics.DroppedParticles, dropped);
            }

            slot.Elapsed = current;
            slot.FirstUpdate = false;
            if (!definition.Loop && slot.Elapsed >= definition.Duration)
                slot.Emitting = false;
        }

        void EmitGpu(const std::uint32_t effectIndex, const float deltaSeconds)
        {
            auto& slot = Effects[effectIndex];
            if (!slot.Emitting)
            {
                slot.Elapsed += deltaSeconds;
                return;
            }
            const auto& definition = slot.RuntimeDefinition;
            const auto previous = slot.Elapsed;
            auto effectiveDelta = static_cast<double>(deltaSeconds);
            if (!definition.Loop)
                effectiveDelta = std::min(effectiveDelta, std::max(0.0, definition.Duration - slot.Elapsed));
            const auto current = previous + effectiveDelta;

            std::uint64_t requested = 0;
            if (const auto* rate = FindEnabledModule<VfxEmissionRateModule>(definition))
            {
                slot.RateAccumulator += effectiveDelta * rate->ParticlesPerSecond;
                const auto whole = std::floor(slot.RateAccumulator);
                requested += static_cast<std::uint64_t>(
                    std::min(whole, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
                slot.RateAccumulator -= whole;
            }
            for (const auto& module : definition.Modules)
            {
                if (!module.Enabled)
                    continue;
                if (const auto* burst = std::get_if<VfxBurstModule>(&module.Payload))
                {
                    const auto count = CountBurst(slot, *burst, previous, current);
                    requested = std::min(std::numeric_limits<std::uint64_t>::max() - requested, count) + requested;
                }
            }

            SaturatingAdd(slot.GpuSpawnSequence, requested);
            const auto* initialize = FindEnabledModule<VfxInitializeModule>(definition);
            const auto lifetime = initialize ? initialize->LifetimeMaximum : 1.0F;
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
            if (!definition.Loop && slot.Elapsed >= definition.Duration)
                slot.Emitting = false;
        }

        [[nodiscard]] Vector3 WorldPosition(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local
                       ? TransformPosition(slot.Position, slot.Rotation, particle.Position)
                       : particle.Position;
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
            bool moved = false;
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Update)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    ApplyCustomInstruction(slot, particle, operation.Index, deltaSeconds);
                    continue;
                }
                if (operation.Index >= definition.Modules.size())
                    continue;
                const auto& module = definition.Modules[operation.Index];
                if (const auto* force = std::get_if<VfxForceModule>(&module.Payload))
                {
                    const auto acceleration = Add(force->Force, Multiply(Gravity, force->GravityMultiplier));
                    particle.Velocity = Add(particle.Velocity, Multiply(acceleration, deltaSeconds));
                }
                else if (const auto* size = std::get_if<VfxSizeOverLifetimeModule>(&module.Payload))
                {
                    particle.Size = std::max(0.0F, size->Size.Evaluate(normalizedAge));
                }
                else if (const auto* color = std::get_if<VfxColorOverLifetimeModule>(&module.Payload))
                {
                    particle.Tint = color->Color.Evaluate(normalizedAge);
                }
                else if (const auto* collision = std::get_if<VfxCollisionModule>(&module.Payload))
                {
                    if (!moveParticle(collision))
                        return;
                    moved = true;
                }
            }
            if (!moved && !moveParticle(nullptr))
                return;
            for (const auto& operation : slot.Program.Operations)
            {
                if (operation.Context != VfxContextType::Output)
                    continue;
                if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                {
                    ApplyCustomInstruction(slot, particle, operation.Index, deltaSeconds);
                    continue;
                }
                if (operation.Index >= definition.Modules.size())
                    continue;
                if (const auto* renderer = std::get_if<VfxRendererModule>(&definition.Modules[operation.Index].Payload))
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
        if (m_Impl->FreeEffects.empty())
        {
            SaturatingAdd(m_Impl->WorldStatistics.DroppedEffects, 1);
            return {};
        }

        const auto normalizedRotation = Math::Normalize(activation.Rotation);
        auto resolved =
            ResolveProgram(*activation.Effect, m_Impl->Specification.Backend, activation.ParameterOverrides);
        const auto random = resolved.Definition.Seed ^ activation.SeedOffset;
        const auto diagnostics = m_Impl->DiagnosticsFor(resolved.Definition);
        const auto index = m_Impl->FreeEffects.back();
        auto& slot = m_Impl->Effects[index];

        m_Impl->FreeEffects.pop_back();
        slot.Active = true;
        slot.Emitting = true;
        slot.FirstUpdate = true;
        slot.Effect = activation.Effect;
        slot.Program = std::move(resolved.Program);
        slot.RuntimeDefinition = std::move(resolved.Definition);
        slot.ParameterOverrides = std::move(resolved.Overrides);
        slot.Parameters = std::move(resolved.Parameters);
        slot.CustomInstructions = std::move(resolved.CustomInstructions);
        slot.Revision = activation.Revision;
        slot.Position = activation.Position;
        slot.Rotation = normalizedRotation;
        slot.Elapsed = 0.0;
        slot.RateAccumulator = 0.0;
        slot.Random = random == 0 ? 0x9e3779b9U : random;
        slot.SeedOffset = activation.SeedOffset;
        slot.ActiveParticles = 0;
        slot.DroppedParticles = 0;
        slot.GpuSpawnSequence = 0;
        slot.GpuSimulationRevision = 1;
        slot.GpuLastDeathTime = 0.0;
        slot.GpuSimulationDeltaSeconds = 0.0F;
        slot.SimulationSpeed = 1.0F;
        slot.Diagnostics = diagnostics;
        ++m_Impl->WorldStatistics.ActiveEffects;
        ++m_Impl->SnapshotRevision;
        return VfxHandle(index, slot.Generation);
    }

    bool VfxWorld::IsAlive(const VfxHandle handle) const noexcept { return m_Impl->IsAlive(handle); }

    void VfxWorld::Stop(const VfxHandle handle)
    {
        if (!m_Impl->IsAlive(handle))
            return;
        m_Impl->ReleaseEffect(handle.Index());
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetTransform(const VfxHandle handle, const Vector3 position, const Quaternion rotation)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot transform a stale VFX handle.");
        if (!Math::IsFinite(position) || !Math::IsFinite(rotation) || Math::Length(rotation) <= 0.000001F)
            throw std::invalid_argument("VFX transform is invalid.");
        auto& slot = m_Impl->Effects[handle.Index()];
        slot.Position = position;
        slot.Rotation = Math::Normalize(rotation);
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetSimulationSpeed(const VfxHandle handle, const float speed)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        if (!std::isfinite(speed) || speed < 0.0F || speed > 8.0F)
            throw std::invalid_argument("VFX simulation speed must be finite and in the range 0..8.");
        m_Impl->Effects[handle.Index()].SimulationSpeed = speed;
    }

    void VfxWorld::SetParameterOverrides(const VfxHandle handle, const std::span<const VfxParameterOverride> overrides)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        m_Impl->SetOverrides(m_Impl->Effects[handle.Index()], overrides);
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::SetParameter(const VfxHandle handle, const AssetId parameter, VfxParameterValue value)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        auto& slot = m_Impl->Effects[handle.Index()];
        auto overrides = slot.ParameterOverrides;
        const auto existing = std::ranges::find(overrides, parameter, &VfxParameterOverride::Parameter);
        if (existing == overrides.end())
            overrides.push_back({parameter, std::move(value)});
        else
            existing->Value = std::move(value);
        m_Impl->SetOverrides(slot, overrides);
        ++m_Impl->SnapshotRevision;
    }

    void VfxWorld::ResetParameter(const VfxHandle handle, const AssetId parameter)
    {
        if (!m_Impl->IsAlive(handle))
            throw std::invalid_argument("Cannot configure a stale VFX handle.");
        auto& slot = m_Impl->Effects[handle.Index()];
        auto overrides = slot.ParameterOverrides;
        const auto erased = std::erase_if(overrides, [parameter](const VfxParameterOverride& value)
                                          { return value.Parameter == parameter; });
        if (erased == 0)
            return;
        m_Impl->SetOverrides(slot, overrides);
        ++m_Impl->SnapshotRevision;
    }

    bool VfxWorld::Reload(const VfxHandle handle, Ref<const VfxEffectAsset> effect, const std::uint64_t revision)
    {
        if (!m_Impl->IsAlive(handle) || !effect)
            return false;
        auto& slot = m_Impl->Effects[handle.Index()];
        if (revision <= slot.Revision)
            return false;

        const auto candidateProgram = CompileVfxEffect(effect->Definition(), m_Impl->Specification.Backend);
        if (!candidateProgram.Valid)
            throw std::invalid_argument("Cannot reload an invalid VFX graph program.");
        std::vector<VfxParameterOverride> preservedOverrides;
        preservedOverrides.reserve(slot.ParameterOverrides.size());
        bool rejectedOverride = false;
        for (const auto& overrideValue : slot.ParameterOverrides)
        {
            const auto parameter = std::ranges::find(candidateProgram.Parameters, overrideValue.Parameter,
                                                     &VfxCompiledParameter::Parameter);
            if (parameter != candidateProgram.Parameters.end() && parameter->Exposed &&
                ParameterValueMatches(parameter->Type, overrideValue.Value))
            {
                preservedOverrides.push_back(overrideValue);
            }
            else
            {
                rejectedOverride = true;
            }
        }
        auto resolved = ResolveProgram(*effect, m_Impl->Specification.Backend, preservedOverrides);
        const auto compatible = slot.RuntimeDefinition.EmitterId == resolved.Definition.EmitterId &&
                                slot.Program.StateLayoutHash == resolved.Program.StateLayoutHash &&
                                (m_Impl->Specification.Backend != VfxBackend::Gpu ||
                                 !GpuStoredParticleStateChanged(slot.RuntimeDefinition, resolved.Definition));
        auto diagnostics = m_Impl->DiagnosticsFor(resolved.Definition);
        if (rejectedOverride)
            diagnostics |= VfxRuntimeDiagnostic::ParameterOverrideRejected;
        slot.Effect = std::move(effect);
        slot.Program = std::move(resolved.Program);
        slot.RuntimeDefinition = std::move(resolved.Definition);
        slot.ParameterOverrides = std::move(resolved.Overrides);
        slot.Parameters = std::move(resolved.Parameters);
        slot.CustomInstructions = std::move(resolved.CustomInstructions);
        slot.Revision = revision;
        slot.Diagnostics = diagnostics;
        if (!compatible)
        {
            m_Impl->KillParticles(handle.Index());
            m_Impl->RestartEffectState(slot);
        }
        else if (m_Impl->Specification.Backend == VfxBackend::Cpu)
        {
            for (auto index = m_Impl->Particles.size();
                 index > 0 && slot.ActiveParticles > slot.RuntimeDefinition.Capacity; --index)
            {
                const auto particleIndex = static_cast<std::uint32_t>(index - 1);
                if (m_Impl->Particles[particleIndex].Active &&
                    m_Impl->Particles[particleIndex].EffectIndex == handle.Index())
                {
                    m_Impl->ReleaseParticle(particleIndex);
                }
            }
            if (!slot.RuntimeDefinition.Loop && slot.Elapsed >= slot.RuntimeDefinition.Duration)
                slot.Emitting = false;
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
            if (gpuFinished || (!slot.Emitting && slot.ActiveParticles == 0))
                m_Impl->ReleaseEffect(index);
        }
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
            const auto* renderer = FindEnabledModule<VfxRendererModule>(slot.RuntimeDefinition);
            destination[result.Written++] = {
                VfxHandle(particle.EffectIndex, slot.Generation),
                m_Impl->WorldPosition(slot, particle),
                particle.Rotation,
                particle.Size,
                particle.Tint,
                particle.Renderer,
                renderer ? renderer->Sprite : AssetId{},
                renderer ? renderer->Mesh : AssetId{},
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
                const auto* shape = FindEnabledModule<VfxShapeModule>(definition);
                const auto* initialize = FindEnabledModule<VfxInitializeModule>(definition);
                const auto* force = FindEnabledModule<VfxForceModule>(definition);
                const auto* size = FindEnabledModule<VfxSizeOverLifetimeModule>(definition);
                const auto* color = FindEnabledModule<VfxColorOverLifetimeModule>(definition);
                const auto* renderer = FindEnabledModule<VfxRendererModule>(definition);
                VfxGpuEmitter emitter{VfxHandle(index, slot.Generation),
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
                                      slot.GpuSimulationRevision};
                emitter.CustomInstructionCount = static_cast<std::uint32_t>(
                    std::min(slot.CustomInstructions.size(), emitter.CustomInstructions.size()));
                std::ranges::copy_n(slot.CustomInstructions.begin(), emitter.CustomInstructionCount,
                                    emitter.CustomInstructions.begin());
                for (const auto& operation : slot.Program.Operations)
                {
                    if (operation.Kind == VfxCompiledOperationKind::CustomHlsl)
                    {
                        if (operation.Index >= emitter.CustomInstructionCount)
                            throw std::logic_error("VFX program contains an invalid GPU Custom HLSL operation.");
                        if (emitter.ParticleOperationCount >= emitter.ParticleOperations.size())
                            throw std::logic_error("VFX program exceeds the GPU particle-operation budget.");
                        emitter.ParticleOperations[emitter.ParticleOperationCount++] = {
                            operation.Context, VfxGpuEmitter::ParticleOperationKind::CustomHlsl, operation.Index};
                        continue;
                    }
                    if (operation.Index >= definition.Modules.size())
                        throw std::logic_error("VFX program contains an invalid GPU module operation.");
                    const auto kind = GpuParticleOperationKind(definition.Modules[operation.Index].Payload);
                    if (kind)
                    {
                        if (emitter.ParticleOperationCount >= emitter.ParticleOperations.size())
                            throw std::logic_error("VFX program exceeds the GPU particle-operation budget.");
                        emitter.ParticleOperations[emitter.ParticleOperationCount++] = {operation.Context, *kind, 0};
                    }
                }
                emitter.SimulationDeltaSeconds = slot.GpuSimulationDeltaSeconds;
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
            result.Effects[result.EffectCount++] = {VfxHandle(index, slot.Generation),
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
            const auto position = m_Impl->WorldPosition(slot, particle);
            const auto velocity = m_Impl->WorldVelocity(slot, particle);
            result.Particles[result.ParticleCount++] = {
                VfxHandle(particle.EffectIndex, slot.Generation),
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

    void VfxWorld::Clear() noexcept
    {
        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
            m_Impl->ReleaseEffect(index);
        if (m_Impl->Specification.Backend == VfxBackend::Gpu)
            ++m_Impl->ResetRevision;
        ++m_Impl->SnapshotRevision;
    }
} // namespace Keire
