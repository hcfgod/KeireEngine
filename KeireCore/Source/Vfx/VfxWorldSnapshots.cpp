#include "KeireInternal/Vfx/VfxWorldInternal.h"

#include "KeireInternal/Vfx/VfxCheckpointInternal.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
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

        [[nodiscard]] Vector3 Multiply(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }
    } // namespace

    using Detail::VfxCheckpointMagic;
    using Detail::VfxCheckpointReader;
    using Detail::VfxCheckpointVersion;
    using Detail::VfxCheckpointWriter;
    using Internal::FindCompiledModule;

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
} // namespace Keire
