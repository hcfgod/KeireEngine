#pragma once

#include "Keire/ECS/Components/DirectionalLightComponent.h"

namespace Keire
{
    class KEIRE_API PointLightComponent final : public Component
    {
      public:
        PointLightComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245504f49ULL, 0x4e544c4947485401ULL));
        }

        [[nodiscard]] Color LightColor() const noexcept { return m_Color; }
        [[nodiscard]] float Intensity() const noexcept { return m_Intensity; }
        [[nodiscard]] float Range() const noexcept { return m_Range; }
        [[nodiscard]] ShadowQuality Shadows() const noexcept { return m_Shadows; }
        [[nodiscard]] float ShadowStrength() const noexcept { return m_ShadowStrength; }
        [[nodiscard]] float ShadowBias() const noexcept { return m_ShadowBias; }
        [[nodiscard]] LightBakeMode BakeMode() const noexcept { return m_BakeMode; }
        [[nodiscard]] ShadowResolutionHint ShadowResolution() const noexcept { return m_ShadowResolution; }
        [[nodiscard]] AssetId Cookie() const noexcept { return m_Cookie; }
        [[nodiscard]] bool ContactShadows() const noexcept { return m_ContactShadows; }
        [[nodiscard]] float IndirectMultiplier() const noexcept { return m_IndirectMultiplier; }

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetRange(float value);
        void SetShadows(ShadowQuality value);
        void SetShadowStrength(float value);
        void SetShadowBias(float value);
        void SetBakeMode(LightBakeMode value);
        void SetShadowResolution(ShadowResolutionHint value);
        void SetCookie(AssetId value);
        void SetContactShadows(bool value);
        void SetIndirectMultiplier(float value);
        void Reset();

      private:
        friend ComponentRegistration CreatePointLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        float m_Range = 10.0F;
        ShadowQuality m_Shadows = ShadowQuality::Soft;
        float m_ShadowStrength = 1.0F;
        float m_ShadowBias = 0.0025F;
        LightBakeMode m_BakeMode = LightBakeMode::Realtime;
        ShadowResolutionHint m_ShadowResolution = ShadowResolutionHint::Medium;
        AssetId m_Cookie;
        bool m_ContactShadows = false;
        float m_IndirectMultiplier = 1.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreatePointLightComponentRegistration();
} // namespace Keire
