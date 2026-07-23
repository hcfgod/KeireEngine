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

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetRange(float value);
        void SetShadows(ShadowQuality value);
        void SetShadowStrength(float value);
        void SetShadowBias(float value);
        void Reset();

      private:
        friend ComponentRegistration CreatePointLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        float m_Range = 10.0F;
        ShadowQuality m_Shadows = ShadowQuality::Soft;
        float m_ShadowStrength = 1.0F;
        float m_ShadowBias = 0.0025F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreatePointLightComponentRegistration();
} // namespace Keire
