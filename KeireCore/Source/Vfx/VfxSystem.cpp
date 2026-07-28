#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
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
            std::uint64_t Revision = 0;
            Vector3 Position;
            Quaternion Rotation;
            double Elapsed = 0.0;
            double RateAccumulator = 0.0;
            std::uint32_t Random = 1;
            std::uint32_t SeedOffset = 0;
            std::uint32_t ActiveParticles = 0;
            std::uint64_t DroppedParticles = 0;
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
            Particles.resize(Specification.MaximumParticles);
            FreeEffects.reserve(Specification.MaximumEffects);
            FreeParticles.reserve(Specification.MaximumParticles);
            for (auto index = Specification.MaximumEffects; index > 0; --index)
                FreeEffects.push_back(index - 1);
            for (auto index = Specification.MaximumParticles; index > 0; --index)
                FreeParticles.push_back(index - 1);
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
            slot.Revision = 0;
            slot.Generation = NextGeneration(slot.Generation);
            FreeEffects.push_back(index);
            --WorldStatistics.ActiveEffects;
        }

        void SpawnOne(const std::uint32_t effectIndex)
        {
            auto& slot = Effects[effectIndex];
            const auto& definition = slot.Effect->Definition();
            if (slot.ActiveParticles >= definition.Capacity || FreeParticles.empty())
            {
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }

            const auto particleIndex = FreeParticles.back();
            FreeParticles.pop_back();
            auto& particle = Particles[particleIndex];
            const auto* shape = FindEnabledModule<VfxShapeModule>(definition);
            const auto* initialize = FindEnabledModule<VfxInitializeModule>(definition);
            const auto* size = FindEnabledModule<VfxSizeOverLifetimeModule>(definition);
            const auto* color = FindEnabledModule<VfxColorOverLifetimeModule>(definition);
            const auto* renderer = FindEnabledModule<VfxRendererModule>(definition);

            auto position = SampleShape(slot, shape);
            auto velocity =
                initialize ? Range(slot, initialize->VelocityMinimum, initialize->VelocityMaximum) : Vector3{};
            if (definition.Space == VfxSimulationSpace::World)
            {
                position = TransformPosition(slot.Position, slot.Rotation, position);
                velocity = Rotate(slot.Rotation, velocity);
            }
            if (!Math::IsFinite(position) || !Math::IsFinite(velocity))
            {
                FreeParticles.push_back(particleIndex);
                slot.Diagnostics |= VfxRuntimeDiagnostic::SimulationValueInvalid;
                SaturatingAdd(slot.DroppedParticles, 1);
                SaturatingAdd(WorldStatistics.DroppedParticles, 1);
                return;
            }

            particle.Active = true;
            particle.EffectIndex = effectIndex;
            particle.Position = position;
            particle.Velocity = velocity;
            particle.Rotation =
                initialize ? Range(slot, initialize->RotationMinimum, initialize->RotationMaximum) : Vector3{};
            particle.Age = 0.0F;
            particle.Lifetime =
                initialize ? Range(slot, initialize->LifetimeMinimum, initialize->LifetimeMaximum) : 1.0F;
            particle.Size = size ? std::max(0.0F, size->Size.Evaluate(0.0F)) : 1.0F;
            particle.Tint = color ? color->Color.Evaluate(0.0F) : Color{};
            particle.Renderer = renderer ? renderer->Type : VfxRendererType::Sprite;
            ++slot.ActiveParticles;
            ++WorldStatistics.ActiveParticles;
        }

        [[nodiscard]] std::uint64_t CountBurst(const EffectSlot& slot, const VfxBurstModule& burst,
                                               const double previous, const double current) const noexcept
        {
            if (current < previous)
                return 0;
            std::uint64_t count = 0;
            const auto& definition = slot.Effect->Definition();
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
            const auto& definition = slot.Effect->Definition();
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

        [[nodiscard]] Vector3 WorldPosition(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.Effect->Definition().Space == VfxSimulationSpace::Local
                       ? TransformPosition(slot.Position, slot.Rotation, particle.Position)
                       : particle.Position;
        }

        [[nodiscard]] Vector3 WorldVelocity(const EffectSlot& slot, const Particle& particle) const noexcept
        {
            return slot.Effect->Definition().Space == VfxSimulationSpace::Local
                       ? Rotate(slot.Rotation, particle.Velocity)
                       : particle.Velocity;
        }

        void SimulateParticle(const std::uint32_t particleIndex, const float deltaSeconds)
        {
            auto& particle = Particles[particleIndex];
            auto& slot = Effects[particle.EffectIndex];
            const auto& definition = slot.Effect->Definition();
            particle.Age += deltaSeconds;
            if (particle.Age >= particle.Lifetime)
            {
                ReleaseParticle(particleIndex);
                return;
            }

            if (const auto* force = FindEnabledModule<VfxForceModule>(definition))
            {
                const auto acceleration = Add(force->Force, Multiply(Gravity, force->GravityMultiplier));
                particle.Velocity = Add(particle.Velocity, Multiply(acceleration, deltaSeconds));
            }
            auto next = Add(particle.Position, Multiply(particle.Velocity, deltaSeconds));

            if (const auto* collision = FindEnabledModule<VfxCollisionModule>(definition);
                collision && collision->Mode != VfxCollisionMode::None && Specification.CollisionQuery)
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
                        return;
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
            const auto normalizedAge = std::clamp(particle.Age / particle.Lifetime, 0.0F, 1.0F);
            if (const auto* size = FindEnabledModule<VfxSizeOverLifetimeModule>(definition))
                particle.Size = std::max(0.0F, size->Size.Evaluate(normalizedAge));
            if (const auto* color = FindEnabledModule<VfxColorOverLifetimeModule>(definition))
                particle.Tint = color->Color.Evaluate(normalizedAge);
            if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.Velocity) ||
                !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Size))
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

        const auto index = m_Impl->FreeEffects.back();
        auto& slot = m_Impl->Effects[index];
        const auto normalizedRotation = Math::Normalize(activation.Rotation);
        const auto random = activation.Effect->Definition().Seed ^ activation.SeedOffset;
        const auto diagnostics = m_Impl->DiagnosticsFor(activation.Effect->Definition());

        m_Impl->FreeEffects.pop_back();
        slot.Active = true;
        slot.Emitting = true;
        slot.FirstUpdate = true;
        slot.Effect = activation.Effect;
        slot.Revision = activation.Revision;
        slot.Position = activation.Position;
        slot.Rotation = normalizedRotation;
        slot.Elapsed = 0.0;
        slot.RateAccumulator = 0.0;
        slot.Random = random == 0 ? 0x9e3779b9U : random;
        slot.SeedOffset = activation.SeedOffset;
        slot.ActiveParticles = 0;
        slot.DroppedParticles = 0;
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

    bool VfxWorld::Reload(const VfxHandle handle, Ref<const VfxEffectAsset> effect, const std::uint64_t revision)
    {
        if (!m_Impl->IsAlive(handle) || !effect)
            return false;
        auto& slot = m_Impl->Effects[handle.Index()];
        if (revision <= slot.Revision)
            return false;

        const auto compatible = slot.Effect->Definition().EmitterId == effect->Definition().EmitterId;
        const auto diagnostics = m_Impl->DiagnosticsFor(effect->Definition());
        slot.Effect = std::move(effect);
        slot.Revision = revision;
        slot.Diagnostics = diagnostics;
        if (!compatible)
        {
            m_Impl->KillParticles(handle.Index());
            slot.Elapsed = 0.0;
            slot.RateAccumulator = 0.0;
            slot.FirstUpdate = true;
            slot.Emitting = true;
            const auto random = slot.Effect->Definition().Seed ^ slot.SeedOffset;
            slot.Random = random == 0 ? 0x9e3779b9U : random;
        }
        else
        {
            for (auto index = m_Impl->Particles.size();
                 index > 0 && slot.ActiveParticles > slot.Effect->Definition().Capacity; --index)
            {
                const auto particleIndex = static_cast<std::uint32_t>(index - 1);
                if (m_Impl->Particles[particleIndex].Active &&
                    m_Impl->Particles[particleIndex].EffectIndex == handle.Index())
                {
                    m_Impl->ReleaseParticle(particleIndex);
                }
            }
            if (!slot.Effect->Definition().Loop && slot.Elapsed >= slot.Effect->Definition().Duration)
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

        for (std::uint32_t index = 0; index < m_Impl->Particles.size(); ++index)
            if (m_Impl->Particles[index].Active)
            {
                const auto speed = m_Impl->Effects[m_Impl->Particles[index].EffectIndex].SimulationSpeed;
                if (speed > 0.0F)
                    m_Impl->SimulateParticle(index, deltaSeconds * speed);
            }

        for (std::uint32_t index = 0; index < m_Impl->Effects.size(); ++index)
        {
            if (!m_Impl->Effects[index].Active)
                continue;
            if (m_Impl->Effects[index].SimulationSpeed > 0.0F)
                m_Impl->Emit(index, deltaSeconds * m_Impl->Effects[index].SimulationSpeed);
            if (!m_Impl->Effects[index].Emitting && m_Impl->Effects[index].ActiveParticles == 0)
                m_Impl->ReleaseEffect(index);
        }
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
            const auto* renderer = FindEnabledModule<VfxRendererModule>(slot.Effect->Definition());
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
        ++m_Impl->SnapshotRevision;
    }
} // namespace Keire
