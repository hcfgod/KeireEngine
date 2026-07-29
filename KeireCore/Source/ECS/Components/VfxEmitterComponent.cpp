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

    void VfxEmitterComponent::SetQuality(const VfxQualityTier value)
    {
        if (value > VfxQualityTier::Cinematic)
            throw std::invalid_argument("VFX Emitter quality tier is invalid.");
        m_Quality = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetCulling(const VfxCullingMode value)
    {
        if (value > VfxCullingMode::AlwaysSimulate)
            throw std::invalid_argument("VFX Emitter culling mode is invalid.");
        m_Culling = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetBounds(const Vector3 center, const Vector3 extent)
    {
        if (!Math::IsFinite(center) || !Math::IsFinite(extent) || extent.X <= 0.0F || extent.Y <= 0.0F ||
            extent.Z <= 0.0F)
            throw std::invalid_argument("VFX Emitter bounds must be finite and have positive extents.");
        m_BoundsCenter = center;
        m_BoundsExtent = extent;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetEditModePreview(const bool value)
    {
        m_EditModePreview = value;
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
            {"quality", "Quality Tier", "Rendering", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"culling", "Culling Mode", "Rendering", ComponentPropertyKind::Integer, false, 0.0, 2.0, 1.0},
            {"boundsCenter", "Bounds Center", "Culling", ComponentPropertyKind::Vector3},
            {"boundsExtent", "Bounds Extent", "Culling", ComponentPropertyKind::Vector3},
            {"editModePreview", "Preview In Edit Mode", "Authoring", ComponentPropertyKind::Boolean},
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
                {"quality", static_cast<std::int64_t>(emitter.m_Quality)},
                {"culling", static_cast<std::int64_t>(emitter.m_Culling)},
                {"boundsCenter", emitter.m_BoundsCenter},
                {"boundsExtent", emitter.m_BoundsExtent},
                {"editModePreview", emitter.m_EditModePreview},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported VFX Emitter component schema version.");
            auto& emitter = dynamic_cast<VfxEmitterComponent&>(component);
            const auto speed = static_cast<float>(ReadVfxProperty(values, "simulationSpeed", 1.0));
            const auto seedOffset = ReadVfxProperty(values, "seedOffset", std::int64_t{0});
            const auto quality = ReadVfxProperty(values, "quality", std::int64_t{2});
            const auto culling = ReadVfxProperty(values, "culling", std::int64_t{0});
            if (seedOffset < 0 || seedOffset > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("VFX Emitter seed offset is outside the supported range.");
            if (quality < 0 || quality > static_cast<std::int64_t>(VfxQualityTier::Cinematic) || culling < 0 ||
                culling > static_cast<std::int64_t>(VfxCullingMode::AlwaysSimulate))
                throw std::invalid_argument("VFX Emitter quality or culling value is invalid.");
            emitter.SetEffect(ReadVfxProperty(values, "effect", AssetId{}));
            emitter.SetPlayOnAwake(ReadVfxProperty(values, "playOnAwake", true));
            emitter.SetAutoDestroy(ReadVfxProperty(values, "autoDestroy", false));
            emitter.SetSimulationSpeed(speed);
            emitter.SetSeedOffset(static_cast<std::uint32_t>(seedOffset));
            emitter.SetQuality(static_cast<VfxQualityTier>(quality));
            emitter.SetCulling(static_cast<VfxCullingMode>(culling));
            emitter.SetBounds(ReadVfxProperty(values, "boundsCenter", Vector3{}),
                              ReadVfxProperty(values, "boundsExtent", Vector3{5.0F, 5.0F, 5.0F}));
            emitter.SetEditModePreview(ReadVfxProperty(values, "editModePreview", false));
        };
        return result;
    }
} // namespace Keire
