#pragma once

#include "Keire/ECS/Components/DirectionalLightComponent.h"

namespace Keire
{
    class KEIRE_API SpotLightComponent final : public Component
    {
      public:
        SpotLightComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b4549524553504fULL, 0x544c494748540001ULL));
        }

        [[nodiscard]] Color LightColor() const noexcept { return m_Color; }
        [[nodiscard]] float Intensity() const noexcept { return m_Intensity; }
        [[nodiscard]] float Range() const noexcept { return m_Range; }
        [[nodiscard]] float InnerAngleDegrees() const noexcept { return m_InnerAngleDegrees; }
        [[nodiscard]] float OuterAngleDegrees() const noexcept { return m_OuterAngleDegrees; }
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
        void SetRange(float value);
        void SetConeAngles(float innerDegrees, float outerDegrees);
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
        friend ComponentRegistration CreateSpotLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        float m_Range = 10.0F;
        float m_InnerAngleDegrees = 25.0F;
        float m_OuterAngleDegrees = 35.0F;
        ShadowQuality m_Shadows = ShadowQuality::Soft;
        float m_ShadowStrength = 1.0F;
        float m_ShadowBias = 0.0025F;
        LightBakeMode m_BakeMode = LightBakeMode::Realtime;
        ShadowResolutionHint m_ShadowResolution = ShadowResolutionHint::Medium;
        AssetId m_Cookie;
        Vector2 m_CookieScale{1.0F, 1.0F};
        Vector2 m_CookieOffset;
        float m_CookieRotationDegrees = 0.0F;
        bool m_ContactShadows = false;
        float m_IndirectMultiplier = 1.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateSpotLightComponentRegistration();
} // namespace Keire
