#pragma once

#include "Keire/ECS/Component.h"

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

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetRange(float value);
        void Reset();

      private:
        friend ComponentRegistration CreatePointLightComponentRegistration();
        Color m_Color;
        float m_Intensity = 1.0F;
        float m_Range = 10.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreatePointLightComponentRegistration();
} // namespace Keire
