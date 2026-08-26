#pragma once

#include "Keire/ECS/Component.h"

namespace Keire
{
    class KEIRE_API LightProbeVolumeComponent final : public Component
    {
      public:
        LightProbeVolumeComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b454952454c5056ULL, 0x4f4c554d45000001ULL));
        }

        [[nodiscard]] Vector3 BoxExtents() const noexcept { return m_BoxExtents; }
        [[nodiscard]] Vector3 Spacing() const noexcept { return m_Spacing; }
        [[nodiscard]] std::int32_t Priority() const noexcept { return m_Priority; }
        [[nodiscard]] float NormalBias() const noexcept { return m_NormalBias; }
        [[nodiscard]] float ViewBias() const noexcept { return m_ViewBias; }

        void ConfigureGrid(Vector3 boxExtents, Vector3 spacing);
        void SetBoxExtents(Vector3 value);
        void SetSpacing(Vector3 value);
        void SetPriority(std::int32_t value);
        void SetNormalBias(float value);
        void SetViewBias(float value);
        void Reset();

      private:
        friend ComponentRegistration CreateLightProbeVolumeComponentRegistration();
        Vector3 m_BoxExtents{5.0F, 3.0F, 5.0F};
        Vector3 m_Spacing{1.0F, 1.0F, 1.0F};
        std::int32_t m_Priority = 0;
        float m_NormalBias = 0.2F;
        float m_ViewBias = 0.1F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateLightProbeVolumeComponentRegistration();
} // namespace Keire
