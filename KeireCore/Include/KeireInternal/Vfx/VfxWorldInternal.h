#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace Keire::Internal
{
    struct ResolvedProgramState
    {
        VfxCompiledProgram Program;
        std::shared_ptr<const VfxEffectDefinition> Source;
        VfxEffectDefinition Definition;
        std::vector<VfxParameterOverride> Overrides;
        std::vector<VfxParameterValue> Parameters;
        std::vector<VfxGpuEmitter::CustomInstruction> CustomInstructions;
        std::shared_ptr<const VfxGpuExecutionPayload> GpuExecution;
    };

    [[nodiscard]] inline const VfxModuleDefinition* FindCompiledModule(const VfxEffectDefinition& definition,
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
    [[nodiscard]] inline const T* FindCompiledModule(const VfxEffectDefinition& definition,
                                                     const VfxCompiledProgram& program) noexcept
    {
        for (std::uint32_t index = 0; index < program.Modules.size(); ++index)
            if (const auto* module = FindCompiledModule(definition, program, index))
                if (const auto* result = std::get_if<T>(&module->Payload))
                    return result;
        return nullptr;
    }

    inline void SaturatingAdd(std::uint64_t& destination, const std::uint64_t value) noexcept
    {
        destination += std::min(value, std::numeric_limits<std::uint64_t>::max() - destination);
    }

    [[nodiscard]] bool IsGpuParticleModuleProperty(VfxModuleProperty property) noexcept;
    [[nodiscard]] bool ParameterValueMatches(VfxValueType type, const VfxParameterValue& value) noexcept;
    [[nodiscard]] Vector4 ParameterVector(VfxValueType type, const VfxParameterValue& value);
    [[nodiscard]] std::vector<VfxParameterValue> ResolveParameters(const VfxCompiledProgram& program,
                                                                   std::span<const VfxParameterOverride> overrides);
    [[nodiscard]] std::shared_ptr<const VfxGpuExecutionPayload>
    BuildGpuExecutionPayload(const VfxCompiledProgram& program, const VfxEffectDefinition& definition,
                             std::span<const VfxParameterValue> parameters,
                             std::span<const VfxGpuEmitter::CustomInstruction> customInstructions);
    [[nodiscard]] std::vector<VfxGpuEmitter::CustomInstruction>
    ResolveCustomInstructions(const VfxCompiledProgram& program, std::span<const VfxParameterValue> parameters);
    [[nodiscard]] std::vector<ResolvedProgramState> ResolvePrograms(const VfxEffectAsset& effect, VfxBackend backend,
                                                                    std::span<const VfxParameterOverride> overrides,
                                                                    const VfxSubgraphResolver& resolver);
} // namespace Keire::Internal

namespace Keire
{
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
            std::shared_ptr<const VfxEffectDefinition> SourceDefinition;
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
        explicit Impl(VfxWorldSpecification specification);
        [[nodiscard]] bool IsAlive(const VfxHandle handle) const noexcept;
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
                                                          const VfxCompiledProgram& program) const noexcept;
        void RestartEffectState(EffectSlot& slot) noexcept;
        void SetOverrides(EffectSlot& slot, const std::span<const VfxParameterOverride> overrides);
        void SetGroupOverrides(const std::uint32_t rootIndex, const std::span<const VfxParameterOverride> overrides);
        [[nodiscard]] std::uint32_t NextRandom(EffectSlot& slot) noexcept;
        [[nodiscard]] float UnitRandom(EffectSlot& slot) noexcept;
        [[nodiscard]] float Range(EffectSlot& slot, const float minimum, const float maximum) noexcept;
        [[nodiscard]] Vector3 Range(EffectSlot& slot, const Vector3 minimum, const Vector3 maximum) noexcept;
        [[nodiscard]] bool EvaluateValueContext(EffectSlot& slot, const VfxContextType context,
                                                const float deltaSeconds, const Particle* particle = nullptr,
                                                const std::uint64_t spawnIndex = 0) noexcept;
        [[nodiscard]] const VfxParameterValue* BindingValue(const EffectSlot& slot, const AssetId executionNode,
                                                            const VfxModuleProperty property) const noexcept;
        [[nodiscard]] std::optional<VfxModuleDefinition> BoundModule(EffectSlot& slot,
                                                                     const std::uint32_t compiledModuleIndex);
        [[nodiscard]] bool PrepareGpuUniformExpressions(EffectSlot& slot, const float deltaSeconds) noexcept;
        [[nodiscard]] Vector3 SampleShape(EffectSlot& slot, const VfxShapeModule* module) noexcept;
        [[nodiscard]] bool ApplyCustomInstruction(EffectSlot& slot, Particle& particle,
                                                  const std::uint32_t instructionIndex,
                                                  const float deltaSeconds) const noexcept;
        void ReleaseParticle(const std::uint32_t index) noexcept;
        void KillParticles(const std::uint32_t effectIndex) noexcept;
        void ReleaseEffect(const std::uint32_t index) noexcept;
        void ReleaseEffectGroup(const std::uint32_t rootIndex) noexcept;
        void SpawnOne(const std::uint32_t effectIndex, const std::uint64_t spawnIndex, const float deltaSeconds);
        [[nodiscard]] std::uint64_t CountBurst(const EffectSlot& slot, const VfxBurstModule& burst,
                                               const double previous, const double current) const noexcept;
        void Emit(const std::uint32_t effectIndex, const float deltaSeconds);
        void EmitGpu(const std::uint32_t effectIndex, const float deltaSeconds);
        [[nodiscard]] Vector3 WorldPosition(const EffectSlot& slot, const Particle& particle) const noexcept;
        [[nodiscard]] Vector3 WorldPreviousPosition(const EffectSlot& slot, const Particle& particle) const noexcept;
        [[nodiscard]] Vector3 WorldVelocity(const EffectSlot& slot, const Particle& particle) const noexcept;
        void SimulateParticle(const std::uint32_t particleIndex, const float deltaSeconds);

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
} // namespace Keire
