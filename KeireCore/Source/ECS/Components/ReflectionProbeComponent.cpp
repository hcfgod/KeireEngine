#include "Keire/ECS/Components/ReflectionProbeComponent.h"

#include "Keire/ECS/Components/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T Read(const ComponentPropertyBag& values, const std::string_view key, const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Reflection Probe property has an incompatible type.");
        }

        [[nodiscard]] bool ValidResolution(const std::int64_t value) noexcept
        {
            return value == 64 || value == 128 || value == 256 || value == 512;
        }
    } // namespace

    ReflectionProbeComponent::ReflectionProbeComponent() : Component(StaticType()) {}

    void ReflectionProbeComponent::SetCaptureMode(const ReflectionProbeCaptureMode value)
    {
        m_CaptureMode = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetResolution(const ReflectionProbeResolution value)
    {
        if (!ValidResolution(static_cast<std::int64_t>(value)))
            throw std::invalid_argument("Reflection Probe resolution is unsupported.");
        m_Resolution = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetBoxExtents(const Vector3 value)
    {
        if (!Math::IsFinite(value) || value.X <= 0.0F || value.Y <= 0.0F || value.Z <= 0.0F || value.X > 100'000.0F ||
            value.Y > 100'000.0F || value.Z > 100'000.0F)
            throw std::invalid_argument("Reflection Probe box extents must be finite and in the range (0, 100000].");
        m_BoxExtents = value;
        m_BlendDistance = std::min(m_BlendDistance, std::min({value.X, value.Y, value.Z}));
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetBlendDistance(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > std::min({m_BoxExtents.X, m_BoxExtents.Y, m_BoxExtents.Z}))
            throw std::invalid_argument("Reflection Probe blend distance must fit inside its box extents.");
        m_BlendDistance = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetImportance(const std::int32_t value)
    {
        if (value < -1000 || value > 1000)
            throw std::invalid_argument("Reflection Probe importance must be in the range -1000..1000.");
        m_Importance = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetIntensity(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100.0F)
            throw std::invalid_argument("Reflection Probe intensity must be in the range 0..100.");
        m_Intensity = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetBoxProjection(const bool value)
    {
        m_BoxProjection = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::SetIncludeSky(const bool value)
    {
        m_IncludeSky = value;
        NotifyChanged();
    }

    void ReflectionProbeComponent::Reset()
    {
        m_CaptureMode = ReflectionProbeCaptureMode::Baked;
        m_Resolution = ReflectionProbeResolution::Size128;
        m_BoxExtents = {5.0F, 5.0F, 5.0F};
        m_BlendDistance = 1.0F;
        m_Importance = 0;
        m_Intensity = 1.0F;
        m_BoxProjection = true;
        m_IncludeSky = true;
        NotifyChanged();
    }

    ComponentRegistration CreateReflectionProbeComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = ReflectionProbeComponent::StaticType();
        result.Name = "Reflection Probe";
        result.Category = "Lighting";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"captureMode", "Capture Mode", "Capture", ComponentPropertyKind::Integer},
            {"resolution", "Resolution", "Capture", ComponentPropertyKind::Integer},
            {"boxExtents", "Box Extents", "Influence", ComponentPropertyKind::Vector3},
            {"blendDistance", "Blend Distance", "Influence", ComponentPropertyKind::Scalar, false, 0.0, 100'000.0,
             0.05},
            {"importance", "Importance", "Influence", ComponentPropertyKind::Integer, false, -1000.0, 1000.0, 1.0},
            {"intensity", "Intensity", "Capture", ComponentPropertyKind::Scalar, false, 0.0, 100.0, 0.05},
            {"boxProjection", "Box Projection", "Influence", ComponentPropertyKind::Boolean},
            {"includeSky", "Include Sky", "Capture", ComponentPropertyKind::Boolean}};
        result.Factory = [] { return Ref<Component>(CreateRef<ReflectionProbeComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& probe = dynamic_cast<const ReflectionProbeComponent&>(component);
            return ComponentPropertyBag{{"captureMode", static_cast<std::int64_t>(probe.m_CaptureMode)},
                                        {"resolution", static_cast<std::int64_t>(probe.m_Resolution)},
                                        {"boxExtents", probe.m_BoxExtents},
                                        {"blendDistance", static_cast<double>(probe.m_BlendDistance)},
                                        {"importance", static_cast<std::int64_t>(probe.m_Importance)},
                                        {"intensity", static_cast<double>(probe.m_Intensity)},
                                        {"boxProjection", probe.m_BoxProjection},
                                        {"includeSky", probe.m_IncludeSky}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Reflection Probe component schema version.");
            auto& probe = dynamic_cast<ReflectionProbeComponent&>(component);
            const auto captureMode = Read(values, "captureMode", std::int64_t{0});
            const auto resolution = Read(values, "resolution", std::int64_t{128});
            if (captureMode < 0 || captureMode > 1 || !ValidResolution(resolution))
                throw std::invalid_argument("Reflection Probe capture settings are invalid.");
            probe.SetCaptureMode(static_cast<ReflectionProbeCaptureMode>(captureMode));
            probe.SetResolution(static_cast<ReflectionProbeResolution>(resolution));
            probe.SetBoxExtents(Read(values, "boxExtents", Vector3{5.0F, 5.0F, 5.0F}));
            probe.SetBlendDistance(static_cast<float>(Read(values, "blendDistance", 1.0)));
            const auto importance = Read(values, "importance", std::int64_t{0});
            if (importance < -1000 || importance > 1000)
                throw std::invalid_argument("Reflection Probe importance is invalid.");
            probe.SetImportance(static_cast<std::int32_t>(importance));
            probe.SetIntensity(static_cast<float>(Read(values, "intensity", 1.0)));
            probe.SetBoxProjection(Read(values, "boxProjection", true));
            probe.SetIncludeSky(Read(values, "includeSky", true));
        };
        return result;
    }
} // namespace Keire
