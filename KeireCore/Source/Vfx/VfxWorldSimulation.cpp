#include "KeireInternal/Vfx/VfxWorldInternal.h"

#include "KeireInternal/Vfx/VfxExecutionInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr Vector3 Gravity{0.0F, -9.81F, 0.0F};
        constexpr std::uint32_t MaximumRuntimeBurstCycles = 1024;
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
    } // namespace

    using Internal::BuildGpuExecutionPayload;
    using Internal::FindCompiledModule;
    using Internal::IsGpuParticleModuleProperty;
    using Internal::ParameterVector;
    using Internal::ResolveCustomInstructions;
    using Internal::ResolvedProgramState;
    using Internal::ResolveParameters;
    using Internal::ResolvePrograms;
    using Internal::SaturatingAdd;

    VfxWorld::Impl::Impl(VfxWorldSpecification specification) : Specification(std::move(specification))
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

    [[nodiscard]] bool VfxWorld::Impl::IsAlive(const VfxHandle handle) const noexcept
    {
        return handle && handle.Index() < Effects.size() && Effects[handle.Index()].Active &&
               Effects[handle.Index()].Generation == handle.Generation() &&
               Effects[handle.Index()].RootEffectIndex == handle.Index();
    }

    [[nodiscard]] VfxRuntimeDiagnostic VfxWorld::Impl::DiagnosticsFor(const VfxEffectDefinition& definition,
                                                                      const VfxCompiledProgram& program) const noexcept
    {
        auto result = VfxRuntimeDiagnostic::None;
        if (const auto* shape = FindCompiledModule<VfxShapeModule>(definition, program);
            shape && (shape->Shape == VfxShape::Mesh || shape->Shape == VfxShape::Volume) && !Specification.ShapeSample)
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

    void VfxWorld::Impl::RestartEffectState(EffectSlot& slot) noexcept
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

    void VfxWorld::Impl::SetOverrides(EffectSlot& slot, const std::span<const VfxParameterOverride> overrides)
    {
        auto candidateOverrides = std::vector<VfxParameterOverride>(overrides.begin(), overrides.end());
        auto candidateParameters = ResolveParameters(slot.Program, candidateOverrides);
        auto candidateDefinition =
            Internal::ResolveVfxExecutableDefinition(*slot.SourceDefinition, slot.Program, candidateParameters);
        Internal::ValidateVfxResolvedBackendCapabilities(
            candidateDefinition, slot.Program, Specification.Backend,
            slot.SourceDefinition->SchemaVersion >= CurrentVfxSchemaVersion &&
                slot.SourceDefinition->ExecutionSource == VfxExecutionSource::Graph &&
                slot.SourceDefinition->CompatibilityMode == VfxCompatibilityMode::NativeSchema4);
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

    void VfxWorld::Impl::SetGroupOverrides(const std::uint32_t rootIndex,
                                           const std::span<const VfxParameterOverride> overrides)
    {
        auto& root = Effects[rootIndex];
        auto resolved = ResolvePrograms(*root.Effect, Specification.Backend, overrides, Specification.SubgraphResolver);
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
            slot.SourceDefinition = std::move(state.Source);
            slot.RuntimeDefinition = std::move(state.Definition);
            slot.ParameterOverrides = std::move(state.Overrides);
            slot.Parameters = std::move(state.Parameters);
            slot.ExpressionRegisters.assign(slot.Program.ValueRegisterCount, 0.0F);
            slot.CustomInstructions = std::move(state.CustomInstructions);
            slot.GpuExecution = std::move(state.GpuExecution);
            slot.Diagnostics = DiagnosticsFor(slot.RuntimeDefinition, slot.Program);
        }
    }

    [[nodiscard]] std::uint32_t VfxWorld::Impl::NextRandom(EffectSlot& slot) noexcept
    {
        auto value = slot.Random;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        slot.Random = value == 0 ? 0x9e3779b9U : value;
        return slot.Random;
    }

    [[nodiscard]] float VfxWorld::Impl::UnitRandom(EffectSlot& slot) noexcept
    {
        return static_cast<float>(NextRandom(slot) >> 8U) * (1.0F / 16'777'216.0F);
    }

    [[nodiscard]] float VfxWorld::Impl::Range(EffectSlot& slot, const float minimum, const float maximum) noexcept
    {
        return minimum + (maximum - minimum) * UnitRandom(slot);
    }

    [[nodiscard]] Vector3 VfxWorld::Impl::Range(EffectSlot& slot, const Vector3 minimum, const Vector3 maximum) noexcept
    {
        return {Range(slot, minimum.X, maximum.X), Range(slot, minimum.Y, maximum.Y),
                Range(slot, minimum.Z, maximum.Z)};
    }

    [[nodiscard]] bool VfxWorld::Impl::EvaluateValueContext(EffectSlot& slot, const VfxContextType context,
                                                            const float deltaSeconds, const Particle* particle,
                                                            const std::uint64_t spawnIndex) noexcept
    {
        if (slot.Program.ValueInstructions.empty())
            return true;
        const auto& source = *slot.SourceDefinition;
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
        evaluation.EmitterPosition = slot.Position;
        evaluation.EmitterRotation = slot.Rotation;
        evaluation.ResourceQuery = std::addressof(Specification.ResourceQuery);
        if (!Internal::EvaluateVfxExpressions(slot.Program, slot.Parameters, evaluation, slot.ExpressionRegisters))
        {
            slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
            return false;
        }
        return true;
    }

    [[nodiscard]] const VfxParameterValue* VfxWorld::Impl::BindingValue(const EffectSlot& slot,
                                                                        const AssetId executionNode,
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

    [[nodiscard]] std::optional<VfxModuleDefinition>
    VfxWorld::Impl::BoundModule(EffectSlot& slot, const std::uint32_t compiledModuleIndex)
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

    [[nodiscard]] bool VfxWorld::Impl::PrepareGpuUniformExpressions(EffectSlot& slot, const float deltaSeconds) noexcept
    {
        const auto hasRuntimeModuleBinding = std::ranges::any_of(
            slot.Program.Bindings, [](const VfxCompiledBinding& binding)
            { return binding.ValueRegister != ~std::uint32_t{0} && !IsGpuParticleModuleProperty(binding.Property); });
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

    [[nodiscard]] Vector3 VfxWorld::Impl::SampleShape(EffectSlot& slot, const VfxShapeModule* module) noexcept
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

    [[nodiscard]] bool VfxWorld::Impl::ApplyCustomInstruction(EffectSlot& slot, Particle& particle,
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
        const auto applyColor = [&applyScalar](Color& target, const Vector4 operand, const VfxCustomOperation operation)
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

    void VfxWorld::Impl::ReleaseParticle(const std::uint32_t index) noexcept
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

    void VfxWorld::Impl::KillParticles(const std::uint32_t effectIndex) noexcept
    {
        for (std::uint32_t index = 0; index < Particles.size(); ++index)
            if (Particles[index].Active && Particles[index].EffectIndex == effectIndex)
                ReleaseParticle(index);
    }

    void VfxWorld::Impl::ReleaseEffect(const std::uint32_t index) noexcept
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
        slot.SourceDefinition.reset();
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

    void VfxWorld::Impl::ReleaseEffectGroup(const std::uint32_t rootIndex) noexcept
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

    void VfxWorld::Impl::SpawnOne(const std::uint32_t effectIndex, const std::uint64_t spawnIndex,
                                  const float deltaSeconds)
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
            if (operation.Context != VfxContextType::Spawn || operation.Kind != VfxCompiledOperationKind::CustomHlsl)
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

    [[nodiscard]] std::uint64_t VfxWorld::Impl::CountBurst(const EffectSlot& slot, const VfxBurstModule& burst,
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
                if ((offset > previous && offset <= current) || (slot.FirstUpdate && previous == 0.0 && offset == 0.0))
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

    void VfxWorld::Impl::Emit(const std::uint32_t effectIndex, const float deltaSeconds)
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
            requested =
                std::min<std::uint64_t>(std::numeric_limits<std::uint64_t>::max() - requested, burstCount) + requested;
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

    void VfxWorld::Impl::EmitGpu(const std::uint32_t effectIndex, const float deltaSeconds)
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

    [[nodiscard]] Vector3 VfxWorld::Impl::WorldPosition(const EffectSlot& slot, const Particle& particle) const noexcept
    {
        return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local
                   ? TransformPosition(slot.Position, slot.Rotation, particle.Position)
                   : particle.Position;
    }

    [[nodiscard]] Vector3 VfxWorld::Impl::WorldPreviousPosition(const EffectSlot& slot,
                                                                const Particle& particle) const noexcept
    {
        return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local
                   ? TransformPosition(slot.Position, slot.Rotation, particle.PreviousPosition)
                   : particle.PreviousPosition;
    }

    [[nodiscard]] Vector3 VfxWorld::Impl::WorldVelocity(const EffectSlot& slot, const Particle& particle) const noexcept
    {
        return slot.RuntimeDefinition.Space == VfxSimulationSpace::Local ? Rotate(slot.Rotation, particle.Velocity)
                                                                         : particle.Velocity;
    }

    void VfxWorld::Impl::SimulateParticle(const std::uint32_t particleIndex, const float deltaSeconds)
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
                    velocity =
                        Subtract(velocity, Multiply(normal, (1.0F + collision->Restitution) * Dot(velocity, normal)));
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
            else if (const auto* killShape = std::get_if<VfxKillShapeModule>(&module->Payload))
            {
                const auto relative = Subtract(particle.Position, killShape->Center);
                const auto inside = killShape->Shape == VfxShape::Box
                                        ? std::abs(relative.X) <= killShape->BoxHalfExtent.X &&
                                              std::abs(relative.Y) <= killShape->BoxHalfExtent.Y &&
                                              std::abs(relative.Z) <= killShape->BoxHalfExtent.Z
                                        : Dot(relative, relative) <= killShape->Radius * killShape->Radius;
                const auto kill = killShape->Mode == VfxKillShapeMode::Solid ? inside : !inside;
                if (kill)
                {
                    ReleaseParticle(particleIndex);
                    return;
                }
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

} // namespace Keire
