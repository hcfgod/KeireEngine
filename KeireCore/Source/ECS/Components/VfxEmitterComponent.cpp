#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Vfx/VfxSystem.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadVfxProperty(const ComponentPropertyBag& values, const std::string_view key,
                                        const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("VFX Emitter property has an incompatible type.");
        }
    } // namespace

    VfxEmitterComponent::VfxEmitterComponent() : Component(StaticType()) {}

    void VfxEmitterComponent::SetEffect(const AssetId effect)
    {
        m_Effect = effect;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetPlayOnAwake(const bool value)
    {
        m_PlayOnAwake = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetAutoDestroy(const bool value)
    {
        m_AutoDestroy = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetSimulationSpeed(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 8.0F)
            throw std::invalid_argument("VFX Emitter simulation speed must be finite and in the range 0..8.");
        m_SimulationSpeed = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetSeedOffset(const std::uint32_t value)
    {
        m_SeedOffset = value;
        NotifyChanged();
    }

    ComponentRegistration CreateVfxEmitterComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = VfxEmitterComponent::StaticType();
        result.Name = "VFX Emitter";
        result.Category = "Effects";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"effect", "Effect", "VFX", ComponentPropertyKind::Asset, false, {}, {}, 0.1, VfxEffectAsset::StaticType()},
            {"playOnAwake", "Play On Awake", "Playback", ComponentPropertyKind::Boolean},
            {"autoDestroy", "Auto Destroy", "Playback", ComponentPropertyKind::Boolean},
            {"simulationSpeed", "Simulation Speed", "Playback", ComponentPropertyKind::Scalar, false, 0.0, 8.0, 0.05},
            {"seedOffset", "Seed Offset", "Determinism", ComponentPropertyKind::Integer, false, 0.0,
             static_cast<double>(std::numeric_limits<std::uint32_t>::max()), 1.0},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<VfxEmitterComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& emitter = dynamic_cast<const VfxEmitterComponent&>(component);
            return ComponentPropertyBag{
                {"effect", emitter.m_Effect},
                {"playOnAwake", emitter.m_PlayOnAwake},
                {"autoDestroy", emitter.m_AutoDestroy},
                {"simulationSpeed", static_cast<double>(emitter.m_SimulationSpeed)},
                {"seedOffset", static_cast<std::int64_t>(emitter.m_SeedOffset)},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported VFX Emitter component schema version.");
            auto& emitter = dynamic_cast<VfxEmitterComponent&>(component);
            const auto speed = static_cast<float>(ReadVfxProperty(values, "simulationSpeed", 1.0));
            const auto seedOffset = ReadVfxProperty(values, "seedOffset", std::int64_t{0});
            if (seedOffset < 0 || seedOffset > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("VFX Emitter seed offset is outside the supported range.");
            emitter.SetEffect(ReadVfxProperty(values, "effect", AssetId{}));
            emitter.SetPlayOnAwake(ReadVfxProperty(values, "playOnAwake", true));
            emitter.SetAutoDestroy(ReadVfxProperty(values, "autoDestroy", false));
            emitter.SetSimulationSpeed(speed);
            emitter.SetSeedOffset(static_cast<std::uint32_t>(seedOffset));
        };
        return result;
    }
} // namespace Keire
