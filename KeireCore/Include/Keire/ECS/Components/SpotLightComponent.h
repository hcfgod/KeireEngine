#pragma once

#include "Keire/ECS/Component.h"

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

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetRange(float value);
        void SetConeAngles(float innerDegrees, float outerDegrees);
        void Reset();

      private:
        friend ComponentRegistration CreateSpotLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        float m_Range = 10.0F;
        float m_InnerAngleDegrees = 25.0F;
        float m_OuterAngleDegrees = 35.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateSpotLightComponentRegistration();
} // namespace Keire
