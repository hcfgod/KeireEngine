#include "Keire/ECS/Components/DirectionalLightComponent.h"

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
            throw std::invalid_argument("Directional Light property has an incompatible type.");
        }
    } // namespace

    DirectionalLightComponent::DirectionalLightComponent() : Component(StaticType()) {}

    void DirectionalLightComponent::SetLightColor(const Color value)
    {
        if (!Math::IsFinite(value) || value.Red < 0.0F || value.Green < 0.0F || value.Blue < 0.0F ||
            value.Alpha < 0.0F || value.Alpha > 1.0F)
            throw std::invalid_argument("Directional Light color must be finite, linear, and non-negative.");
        m_Color = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetIntensity(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100'000.0F)
            throw std::invalid_argument("Directional Light intensity must be in the range 0..100000.");
        m_Intensity = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetUseColorTemperature(const bool value)
    {
        m_UseColorTemperature = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetColorTemperatureKelvin(const float value)
    {
        if (!std::isfinite(value) || value < 1000.0F || value > 20'000.0F)
            throw std::invalid_argument("Color temperature must be in the range 1000..20000 Kelvin.");
        m_ColorTemperatureKelvin = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetShadows(const ShadowQuality value)
    {
        m_Shadows = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetShadowStrength(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Shadow strength must be in the range 0..1.");
        m_ShadowStrength = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetShadowBias(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Shadow bias must be in the range 0..1.");
        m_ShadowBias = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::Reset()
    {
        m_Color = {};
        m_Intensity = 1.0F;
        m_UseColorTemperature = false;
        m_ColorTemperatureKelvin = 6500.0F;
        m_Shadows = ShadowQuality::Soft;
        m_ShadowStrength = 1.0F;
        m_ShadowBias = 0.005F;
        NotifyChanged();
    }

    ComponentRegistration CreateDirectionalLightComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = DirectionalLightComponent::StaticType();
        result.Name = "Directional Light";
        result.Category = "Lighting";
        result.Properties = {
            {"color", "Color", "Light", ComponentPropertyKind::Color},
            {"intensity", "Intensity", "Light", ComponentPropertyKind::Scalar, false, 0.0, 100'000.0, 0.05},
            {"useTemperature", "Use Color Temperature", "Temperature", ComponentPropertyKind::Boolean},
            {"temperature", "Temperature", "Temperature", ComponentPropertyKind::Scalar, false, 1000.0, 20'000.0, 50.0},
            {"shadows", "Shadows", "Shadows", ComponentPropertyKind::Integer},
            {"shadowStrength", "Strength", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"shadowBias", "Bias", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.001}};
        result.Factory = [] { return Ref<Component>(CreateRef<DirectionalLightComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& light = dynamic_cast<const DirectionalLightComponent&>(component);
            return ComponentPropertyBag{{"color", light.m_Color},
                                        {"intensity", static_cast<double>(light.m_Intensity)},
                                        {"useTemperature", light.m_UseColorTemperature},
                                        {"temperature", static_cast<double>(light.m_ColorTemperatureKelvin)},
                                        {"shadows", static_cast<std::int64_t>(light.m_Shadows)},
                                        {"shadowStrength", static_cast<double>(light.m_ShadowStrength)},
                                        {"shadowBias", static_cast<double>(light.m_ShadowBias)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Directional Light component schema version.");
            auto& light = dynamic_cast<DirectionalLightComponent&>(component);
            light.SetLightColor(Read(values, "color", Color{}));
            light.SetIntensity(static_cast<float>(Read(values, "intensity", 1.0)));
            light.SetUseColorTemperature(Read(values, "useTemperature", false));
            light.SetColorTemperatureKelvin(static_cast<float>(Read(values, "temperature", 6500.0)));
            const auto shadows = Read(values, "shadows", std::int64_t{2});
            if (shadows < 0 || shadows > 2)
                throw std::invalid_argument("Directional Light shadow quality is invalid.");
            light.SetShadows(static_cast<ShadowQuality>(shadows));
            light.SetShadowStrength(static_cast<float>(Read(values, "shadowStrength", 1.0)));
            light.SetShadowBias(static_cast<float>(Read(values, "shadowBias", 0.005)));
        };
        return result;
    }
} // namespace Keire
