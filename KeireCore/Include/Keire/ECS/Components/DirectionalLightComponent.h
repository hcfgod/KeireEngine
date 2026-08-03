#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Rendering/Lighting.h"

#include <cstdint>

namespace Keire
{
    class KEIRE_API DirectionalLightComponent final : public Component
    {
      public:
        DirectionalLightComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245444952ULL, 0x4c49474854000001ULL));
        }

        [[nodiscard]] Color LightColor() const noexcept { return m_Color; }
        [[nodiscard]] float Intensity() const noexcept { return m_Intensity; }
        [[nodiscard]] bool UseColorTemperature() const noexcept { return m_UseColorTemperature; }
        [[nodiscard]] float ColorTemperatureKelvin() const noexcept { return m_ColorTemperatureKelvin; }
        [[nodiscard]] ShadowQuality Shadows() const noexcept { return m_Shadows; }
        [[nodiscard]] float ShadowStrength() const noexcept { return m_ShadowStrength; }
        [[nodiscard]] float ShadowBias() const noexcept { return m_ShadowBias; }
        [[nodiscard]] LightBakeMode BakeMode() const noexcept { return m_BakeMode; }
        [[nodiscard]] ShadowResolutionHint ShadowResolution() const noexcept { return m_ShadowResolution; }
        [[nodiscard]] AssetId Cookie() const noexcept { return m_Cookie; }
        [[nodiscard]] Vector2 CookieScale() const noexcept { return m_CookieScale; }
        [[nodiscard]] Vector2 CookieOffset() const noexcept { return m_CookieOffset; }
        [[nodiscard]] float CookieRotationDegrees() const noexcept { return m_CookieRotationDegrees; }
        [[nodiscard]] bool ContactShadows() const noexcept { return m_ContactShadows; }
        [[nodiscard]] float IndirectMultiplier() const noexcept { return m_IndirectMultiplier; }

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetUseColorTemperature(bool value);
        void SetColorTemperatureKelvin(float value);
        void SetShadows(ShadowQuality value);
        void SetShadowStrength(float value);
        void SetShadowBias(float value);
        void SetBakeMode(LightBakeMode value);
        void SetShadowResolution(ShadowResolutionHint value);
        void SetCookie(AssetId value);
        void SetCookieTransform(Vector2 scale, Vector2 offset, float rotationDegrees);
        void SetContactShadows(bool value);
        void SetIndirectMultiplier(float value);
        void Reset();

      private:
        friend ComponentRegistration CreateDirectionalLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        bool m_UseColorTemperature = false;
        float m_ColorTemperatureKelvin = 6500.0F;
        ShadowQuality m_Shadows = ShadowQuality::Soft;
        float m_ShadowStrength = 1.0F;
        float m_ShadowBias = 0.005F;
        LightBakeMode m_BakeMode = LightBakeMode::Realtime;
        ShadowResolutionHint m_ShadowResolution = ShadowResolutionHint::High;
        AssetId m_Cookie;
        Vector2 m_CookieScale{1.0F, 1.0F};
        Vector2 m_CookieOffset;
        float m_CookieRotationDegrees = 0.0F;
        bool m_ContactShadows = false;
        float m_IndirectMultiplier = 1.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateDirectionalLightComponentRegistration();
} // namespace Keire
