#pragma once

#include "Keire/ECS/Component.h"

#include <cstdint>

namespace Keire
{
    enum class ShadowQuality : std::uint8_t
    {
        Disabled,
        Hard,
        Soft
    };

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

        void SetLightColor(Color value);
        void SetIntensity(float value);
        void SetUseColorTemperature(bool value);
        void SetColorTemperatureKelvin(float value);
        void SetShadows(ShadowQuality value);
        void SetShadowStrength(float value);
        void SetShadowBias(float value);
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
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateDirectionalLightComponentRegistration();
} // namespace Keire
