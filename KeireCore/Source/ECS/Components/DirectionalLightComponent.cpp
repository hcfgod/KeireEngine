#include "Keire/ECS/Components/DirectionalLightComponent.h"

#include "Keire/Assets/RenderingAssets.h"

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

    void DirectionalLightComponent::SetBakeMode(const LightBakeMode value)
    {
        m_BakeMode = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetShadowResolution(const ShadowResolutionHint value)
    {
        m_ShadowResolution = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetCookie(const AssetId value)
    {
        m_Cookie = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetCookieTransform(const Vector2 scale, const Vector2 offset,
                                                       const float rotationDegrees)
    {
        if (!Math::IsFinite(scale) || !Math::IsFinite(offset) || !std::isfinite(rotationDegrees) || scale.X <= 0.0F ||
            scale.Y <= 0.0F)
            throw std::invalid_argument("Directional Light cookie transform must be finite with positive scale.");
        m_CookieScale = scale;
        m_CookieOffset = offset;
        m_CookieRotationDegrees = rotationDegrees;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetContactShadows(const bool value)
    {
        m_ContactShadows = value;
        NotifyChanged();
    }

    void DirectionalLightComponent::SetIndirectMultiplier(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100.0F)
            throw std::invalid_argument("Directional Light indirect multiplier must be in the range 0..100.");
        m_IndirectMultiplier = value;
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
        m_BakeMode = LightBakeMode::Realtime;
        m_ShadowResolution = ShadowResolutionHint::High;
        m_Cookie = {};
        m_CookieScale = {1.0F, 1.0F};
        m_CookieOffset = {};
        m_CookieRotationDegrees = 0.0F;
        m_ContactShadows = false;
        m_IndirectMultiplier = 1.0F;
        NotifyChanged();
    }

    ComponentRegistration CreateDirectionalLightComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = DirectionalLightComponent::StaticType();
        result.Name = "Directional Light";
        result.Category = "Lighting";
        result.SchemaVersion = 2;
        result.Properties = {
            {"color", "Color", "Light", ComponentPropertyKind::Color},
            {"intensity", "Intensity", "Light", ComponentPropertyKind::Scalar, false, 0.0, 100'000.0, 0.05},
            {"useTemperature", "Use Color Temperature", "Temperature", ComponentPropertyKind::Boolean},
            {"temperature", "Temperature", "Temperature", ComponentPropertyKind::Scalar, false, 1000.0, 20'000.0, 50.0},
            {"shadows", "Shadows", "Shadows", ComponentPropertyKind::Integer},
            {"shadowStrength", "Strength", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"shadowBias", "Bias", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.001},
            {"bakeMode", "Mode", "Baking", ComponentPropertyKind::Integer},
            {"shadowResolution", "Resolution", "Shadows", ComponentPropertyKind::Integer},
            {"cookie",
             "Cookie",
             "Cookie",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             Texture2DAsset::StaticType()},
            {"cookieScale", "Scale", "Cookie", ComponentPropertyKind::Vector2},
            {"cookieOffset", "Offset", "Cookie", ComponentPropertyKind::Vector2},
            {"cookieRotation", "Rotation", "Cookie", ComponentPropertyKind::Scalar},
            {"contactShadows", "Contact Shadows", "Shadows", ComponentPropertyKind::Boolean},
            {"indirectMultiplier", "Indirect Multiplier", "Baking", ComponentPropertyKind::Scalar, false, 0.0, 100.0,
             0.05}};
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
                                        {"shadowBias", static_cast<double>(light.m_ShadowBias)},
                                        {"bakeMode", static_cast<std::int64_t>(light.m_BakeMode)},
                                        {"shadowResolution", static_cast<std::int64_t>(light.m_ShadowResolution)},
                                        {"cookie", light.m_Cookie},
                                        {"cookieScale", light.m_CookieScale},
                                        {"cookieOffset", light.m_CookieOffset},
                                        {"cookieRotation", static_cast<double>(light.m_CookieRotationDegrees)},
                                        {"contactShadows", light.m_ContactShadows},
                                        {"indirectMultiplier", static_cast<double>(light.m_IndirectMultiplier)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 2)
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
            const auto bakeMode = Read(values, "bakeMode", std::int64_t{0});
            const auto resolution = Read(values, "shadowResolution", std::int64_t{2});
            if (bakeMode < 0 || bakeMode > 2 || resolution < 0 || resolution > 3)
                throw std::invalid_argument("Directional Light baking properties are invalid.");
            light.SetBakeMode(static_cast<LightBakeMode>(bakeMode));
            light.SetShadowResolution(static_cast<ShadowResolutionHint>(resolution));
            light.SetCookie(Read(values, "cookie", AssetId{}));
            light.SetCookieTransform(Read(values, "cookieScale", Vector2{1.0F, 1.0F}),
                                     Read(values, "cookieOffset", Vector2{}),
                                     static_cast<float>(Read(values, "cookieRotation", 0.0)));
            light.SetContactShadows(Read(values, "contactShadows", false));
            light.SetIndirectMultiplier(static_cast<float>(Read(values, "indirectMultiplier", 1.0)));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Directional Light component schema migration.");
            auto migrated = values;
            migrated.emplace("bakeMode", std::int64_t{0});
            migrated.emplace("shadowResolution", std::int64_t{2});
            migrated.emplace("cookie", AssetId{});
            migrated.emplace("cookieScale", Vector2{1.0F, 1.0F});
            migrated.emplace("cookieOffset", Vector2{});
            migrated.emplace("cookieRotation", 0.0);
            migrated.emplace("contactShadows", false);
            migrated.emplace("indirectMultiplier", 1.0);
            return migrated;
        };
        return result;
    }
} // namespace Keire
