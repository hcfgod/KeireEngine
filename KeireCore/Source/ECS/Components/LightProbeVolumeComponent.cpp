#include "Keire/ECS/Components/LightProbeVolumeComponent.h"

#include "Keire/ECS/Components/TransformComponent.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        constexpr std::uint64_t MaximumProbeCount = 262'144;

        template <typename T>
        [[nodiscard]] T Read(const ComponentPropertyBag& values, const std::string_view key, const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Light Probe Volume property has an incompatible type.");
        }

        void ValidateGrid(const Vector3 extents, const Vector3 spacing)
        {
            if (!Math::IsFinite(extents) || !Math::IsFinite(spacing) || extents.X <= 0.0F || extents.Y <= 0.0F ||
                extents.Z <= 0.0F || spacing.X <= 0.0F || spacing.Y <= 0.0F || spacing.Z <= 0.0F ||
                extents.X > 100'000.0F || extents.Y > 100'000.0F || extents.Z > 100'000.0F)
                throw std::invalid_argument("Light Probe Volume bounds and spacing must be finite and positive.");
            const auto countX = static_cast<std::uint64_t>(std::ceil((extents.X * 2.0F) / spacing.X)) + 1U;
            const auto countY = static_cast<std::uint64_t>(std::ceil((extents.Y * 2.0F) / spacing.Y)) + 1U;
            const auto countZ = static_cast<std::uint64_t>(std::ceil((extents.Z * 2.0F) / spacing.Z)) + 1U;
            if (countX > MaximumProbeCount || countY > MaximumProbeCount || countZ > MaximumProbeCount ||
                countX * countY > MaximumProbeCount || countX * countY * countZ > MaximumProbeCount)
                throw std::invalid_argument("Light Probe Volume exceeds the 262144 probe limit.");
        }
    } // namespace

    LightProbeVolumeComponent::LightProbeVolumeComponent() : Component(StaticType()) {}

    void LightProbeVolumeComponent::SetBoxExtents(const Vector3 value)
    {
        ValidateGrid(value, m_Spacing);
        m_BoxExtents = value;
        NotifyChanged();
    }

    void LightProbeVolumeComponent::SetSpacing(const Vector3 value)
    {
        ValidateGrid(m_BoxExtents, value);
        m_Spacing = value;
        NotifyChanged();
    }

    void LightProbeVolumeComponent::SetPriority(const std::int32_t value)
    {
        if (value < -1000 || value > 1000)
            throw std::invalid_argument("Light Probe Volume priority must be in the range -1000..1000.");
        m_Priority = value;
        NotifyChanged();
    }

    void LightProbeVolumeComponent::SetNormalBias(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 10.0F)
            throw std::invalid_argument("Light Probe Volume normal bias must be in the range 0..10.");
        m_NormalBias = value;
        NotifyChanged();
    }

    void LightProbeVolumeComponent::SetViewBias(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 10.0F)
            throw std::invalid_argument("Light Probe Volume view bias must be in the range 0..10.");
        m_ViewBias = value;
        NotifyChanged();
    }

    void LightProbeVolumeComponent::Reset()
    {
        m_BoxExtents = {5.0F, 3.0F, 5.0F};
        m_Spacing = {1.0F, 1.0F, 1.0F};
        m_Priority = 0;
        m_NormalBias = 0.2F;
        m_ViewBias = 0.1F;
        NotifyChanged();
    }

    ComponentRegistration CreateLightProbeVolumeComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = LightProbeVolumeComponent::StaticType();
        result.Name = "Light Probe Volume";
        result.Category = "Lighting";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"boxExtents", "Box Extents", "Volume", ComponentPropertyKind::Vector3},
            {"spacing", "Probe Spacing", "Volume", ComponentPropertyKind::Vector3},
            {"priority", "Priority", "Blending", ComponentPropertyKind::Integer, false, -1000.0, 1000.0, 1.0},
            {"normalBias", "Normal Bias", "Sampling", ComponentPropertyKind::Scalar, false, 0.0, 10.0, 0.01},
            {"viewBias", "View Bias", "Sampling", ComponentPropertyKind::Scalar, false, 0.0, 10.0, 0.01}};
        result.Factory = [] { return Ref<Component>(CreateRef<LightProbeVolumeComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& volume = dynamic_cast<const LightProbeVolumeComponent&>(component);
            return ComponentPropertyBag{{"boxExtents", volume.m_BoxExtents},
                                        {"spacing", volume.m_Spacing},
                                        {"priority", static_cast<std::int64_t>(volume.m_Priority)},
                                        {"normalBias", static_cast<double>(volume.m_NormalBias)},
                                        {"viewBias", static_cast<double>(volume.m_ViewBias)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Light Probe Volume component schema version.");
            auto& volume = dynamic_cast<LightProbeVolumeComponent&>(component);
            volume.SetBoxExtents(Read(values, "boxExtents", Vector3{5.0F, 3.0F, 5.0F}));
            volume.SetSpacing(Read(values, "spacing", Vector3{1.0F, 1.0F, 1.0F}));
            const auto priority = Read(values, "priority", std::int64_t{0});
            if (priority < -1000 || priority > 1000)
                throw std::invalid_argument("Light Probe Volume priority is invalid.");
            volume.SetPriority(static_cast<std::int32_t>(priority));
            volume.SetNormalBias(static_cast<float>(Read(values, "normalBias", 0.2)));
            volume.SetViewBias(static_cast<float>(Read(values, "viewBias", 0.1)));
        };
        return result;
    }
} // namespace Keire
