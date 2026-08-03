#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Rendering/Lighting.h"

namespace Keire
{
    class KEIRE_API ReflectionProbeComponent final : public Component
    {
      public:
        ReflectionProbeComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245524546ULL, 0x4c50524f42450001ULL));
        }

        [[nodiscard]] ReflectionProbeCaptureMode CaptureMode() const noexcept { return m_CaptureMode; }
        [[nodiscard]] ReflectionProbeResolution Resolution() const noexcept { return m_Resolution; }
        [[nodiscard]] Vector3 BoxExtents() const noexcept { return m_BoxExtents; }
        [[nodiscard]] float BlendDistance() const noexcept { return m_BlendDistance; }
        [[nodiscard]] std::int32_t Importance() const noexcept { return m_Importance; }
        [[nodiscard]] float Intensity() const noexcept { return m_Intensity; }
        [[nodiscard]] bool BoxProjection() const noexcept { return m_BoxProjection; }
        [[nodiscard]] bool IncludeSky() const noexcept { return m_IncludeSky; }

        void SetCaptureMode(ReflectionProbeCaptureMode value);
        void SetResolution(ReflectionProbeResolution value);
        void SetBoxExtents(Vector3 value);
        void SetBlendDistance(float value);
        void SetImportance(std::int32_t value);
        void SetIntensity(float value);
        void SetBoxProjection(bool value);
        void SetIncludeSky(bool value);
        void Reset();

      private:
        friend ComponentRegistration CreateReflectionProbeComponentRegistration();
        ReflectionProbeCaptureMode m_CaptureMode = ReflectionProbeCaptureMode::Baked;
        ReflectionProbeResolution m_Resolution = ReflectionProbeResolution::Size128;
        Vector3 m_BoxExtents{5.0F, 5.0F, 5.0F};
        float m_BlendDistance = 1.0F;
        std::int32_t m_Importance = 0;
        float m_Intensity = 1.0F;
        bool m_BoxProjection = true;
        bool m_IncludeSky = true;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateReflectionProbeComponentRegistration();
} // namespace Keire
