#include "KeireInternal/Vfx/VfxWorldInternal.h"

#include "Keire/Vfx/VfxSubgraph.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    using Internal::ParameterValueMatches;
    using Internal::ResolvedProgramState;
    using Internal::ResolvePrograms;
    using Internal::SaturatingAdd;

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
        auto resolved = ResolvePrograms(*activation.Effect, m_Impl->Specification.Backend,
                                        activation.ParameterOverrides, m_Impl->Specification.SubgraphResolver);
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
            slot.SourceDefinition = std::move(state.Source);
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

    bool VfxWorld::Reload(const VfxHandle handle, const Ref<const VfxEffectAsset>& effect, const std::uint64_t revision)
    {
        if (!m_Impl->IsAlive(handle) || !effect)
            return false;
        auto& root = m_Impl->Effects[handle.Index()];
        if (revision <= root.Revision)
            return false;

        const auto candidateDefinition =
            ExpandVfxSubgraphs(effect->Definition(), m_Impl->Specification.SubgraphResolver);
        const auto candidatePrograms = CompileVfxEffectSystems(candidateDefinition, m_Impl->Specification.Backend);
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
        auto resolved = ResolvePrograms(*effect, m_Impl->Specification.Backend, preservedOverrides,
                                        m_Impl->Specification.SubgraphResolver);
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

        const auto& retainedEffect = effect;
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
            slot.SourceDefinition = std::move(state->Source);
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

    VfxBackend VfxWorld::Backend() const noexcept { return m_Impl->Specification.Backend; }

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
