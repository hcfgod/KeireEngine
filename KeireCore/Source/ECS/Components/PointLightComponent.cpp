#include "Keire/ECS/Components/PointLightComponent.h"

#include "Keire/Assets/RenderingAssets.h"

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
            throw std::invalid_argument("Point Light property has an incompatible type.");
        }

        void ValidateColor(const Color value)
        {
            if (!Math::IsFinite(value) || value.Red < 0.0F || value.Green < 0.0F || value.Blue < 0.0F ||
                value.Alpha < 0.0F || value.Alpha > 1.0F)
                throw std::invalid_argument("Point Light color must be finite, linear, and non-negative.");
        }
    } // namespace

    PointLightComponent::PointLightComponent() : Component(StaticType()) {}

    void PointLightComponent::SetLightColor(const Color value)
    {
        ValidateColor(value);
        m_Color = value;
        NotifyChanged();
    }

    void PointLightComponent::SetIntensity(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100'000.0F)
            throw std::invalid_argument("Point Light intensity must be in the range 0..100000.");
        m_Intensity = value;
        NotifyChanged();
    }

    void PointLightComponent::SetRange(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F || value > 100'000.0F)
            throw std::invalid_argument("Point Light range must be in the range (0, 100000].");
        m_Range = value;
        NotifyChanged();
    }

    void PointLightComponent::SetShadows(const ShadowQuality value)
    {
        m_Shadows = value;
        NotifyChanged();
    }

    void PointLightComponent::SetShadowStrength(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Point Light shadow strength must be in the range 0..1.");
        m_ShadowStrength = value;
        NotifyChanged();
    }

    void PointLightComponent::SetShadowBias(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Point Light shadow bias must be in the range 0..1.");
        m_ShadowBias = value;
        NotifyChanged();
    }

    void PointLightComponent::SetBakeMode(const LightBakeMode value)
    {
        m_BakeMode = value;
        NotifyChanged();
    }

    void PointLightComponent::SetShadowResolution(const ShadowResolutionHint value)
    {
        m_ShadowResolution = value;
        NotifyChanged();
    }

    void PointLightComponent::SetCookie(const AssetId value)
    {
        m_Cookie = value;
        NotifyChanged();
    }

    void PointLightComponent::SetContactShadows(const bool value)
    {
        m_ContactShadows = value;
        NotifyChanged();
    }

    void PointLightComponent::SetIndirectMultiplier(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100.0F)
            throw std::invalid_argument("Point Light indirect multiplier must be in the range 0..100.");
        m_IndirectMultiplier = value;
        NotifyChanged();
    }

    void PointLightComponent::Reset()
    {
        m_Color = {};
        m_Intensity = 1.0F;
        m_Range = 10.0F;
        m_Shadows = ShadowQuality::Soft;
        m_ShadowStrength = 1.0F;
        m_ShadowBias = 0.0025F;
        m_BakeMode = LightBakeMode::Realtime;
        m_ShadowResolution = ShadowResolutionHint::Medium;
        m_Cookie = {};
        m_ContactShadows = false;
        m_IndirectMultiplier = 1.0F;
        NotifyChanged();
    }

    ComponentRegistration CreatePointLightComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = PointLightComponent::StaticType();
        result.Name = "Point Light";
        result.Category = "Lighting";
        result.SchemaVersion = 2;
        result.Properties = {
            {"color", "Color", "Light", ComponentPropertyKind::Color},
            {"intensity", "Intensity", "Light", ComponentPropertyKind::Scalar, false, 0.0, 100'000.0, 0.05},
            {"range", "Range", "Light", ComponentPropertyKind::Scalar, false, 0.01, 100'000.0, 0.1},
            {"shadows", "Shadows", "Shadows", ComponentPropertyKind::Integer},
            {"shadowStrength", "Strength", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"shadowBias", "Bias", "Shadows", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.001},
            {"bakeMode", "Mode", "Baking", ComponentPropertyKind::Integer},
            {"shadowResolution", "Resolution", "Shadows", ComponentPropertyKind::Integer},
            {"cookie", "Cookie", "Cookie", ComponentPropertyKind::Asset},
            {"contactShadows", "Contact Shadows", "Shadows", ComponentPropertyKind::Boolean},
            {"indirectMultiplier", "Indirect Multiplier", "Baking", ComponentPropertyKind::Scalar, false, 0.0, 100.0,
             0.05}};
        result.Factory = [] { return Ref<Component>(CreateRef<PointLightComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& light = dynamic_cast<const PointLightComponent&>(component);
            return ComponentPropertyBag{{"color", light.m_Color},
                                        {"intensity", static_cast<double>(light.m_Intensity)},
                                        {"range", static_cast<double>(light.m_Range)},
                                        {"shadows", static_cast<std::int64_t>(light.m_Shadows)},
                                        {"shadowStrength", static_cast<double>(light.m_ShadowStrength)},
                                        {"shadowBias", static_cast<double>(light.m_ShadowBias)},
                                        {"bakeMode", static_cast<std::int64_t>(light.m_BakeMode)},
                                        {"shadowResolution", static_cast<std::int64_t>(light.m_ShadowResolution)},
                                        {"cookie", light.m_Cookie},
                                        {"contactShadows", light.m_ContactShadows},
                                        {"indirectMultiplier", static_cast<double>(light.m_IndirectMultiplier)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 2)
                throw std::invalid_argument("Unsupported Point Light component schema version.");
            auto& light = dynamic_cast<PointLightComponent&>(component);
            light.SetLightColor(Read(values, "color", Color{}));
            light.SetIntensity(static_cast<float>(Read(values, "intensity", 1.0)));
            light.SetRange(static_cast<float>(Read(values, "range", 10.0)));
            const auto shadows = Read(values, "shadows", std::int64_t{2});
            if (shadows < 0 || shadows > 2)
                throw std::invalid_argument("Point Light shadow quality is invalid.");
            light.SetShadows(static_cast<ShadowQuality>(shadows));
            light.SetShadowStrength(static_cast<float>(Read(values, "shadowStrength", 1.0)));
            light.SetShadowBias(static_cast<float>(Read(values, "shadowBias", 0.0025)));
            const auto bakeMode = Read(values, "bakeMode", std::int64_t{0});
            const auto resolution = Read(values, "shadowResolution", std::int64_t{1});
            if (bakeMode < 0 || bakeMode > 2 || resolution < 0 || resolution > 3)
                throw std::invalid_argument("Point Light baking properties are invalid.");
            light.SetBakeMode(static_cast<LightBakeMode>(bakeMode));
            light.SetShadowResolution(static_cast<ShadowResolutionHint>(resolution));
            light.SetCookie(Read(values, "cookie", AssetId{}));
            light.SetContactShadows(Read(values, "contactShadows", false));
            light.SetIndirectMultiplier(static_cast<float>(Read(values, "indirectMultiplier", 1.0)));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Point Light component schema migration.");
            auto migrated = values;
            migrated.emplace("bakeMode", std::int64_t{0});
            migrated.emplace("shadowResolution", std::int64_t{1});
            migrated.emplace("cookie", AssetId{});
            migrated.emplace("contactShadows", false);
            migrated.emplace("indirectMultiplier", 1.0);
            return migrated;
        };
        return result;
    }
} // namespace Keire
