#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Math/Math.h"

#include <cstdint>

namespace Keire
{
    enum class VfxQualityTier : std::uint8_t
    {
        Low,
        Medium,
        High,
        Cinematic
    };

    enum class VfxCullingMode : std::uint8_t
    {
        Automatic,
        FixedBounds,
        AlwaysSimulate
    };

    class KEIRE_API VfxEmitterComponent final : public Component
    {
      public:
        VfxEmitterComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245564658ULL, 0x454d495454455201ULL));
        }

        [[nodiscard]] AssetId Effect() const noexcept { return m_Effect; }
        [[nodiscard]] bool PlayOnAwake() const noexcept { return m_PlayOnAwake; }
        [[nodiscard]] bool AutoDestroy() const noexcept { return m_AutoDestroy; }
        [[nodiscard]] float SimulationSpeed() const noexcept { return m_SimulationSpeed; }
        [[nodiscard]] std::uint32_t SeedOffset() const noexcept { return m_SeedOffset; }
        [[nodiscard]] VfxQualityTier Quality() const noexcept { return m_Quality; }
        [[nodiscard]] VfxCullingMode Culling() const noexcept { return m_Culling; }
        [[nodiscard]] Vector3 BoundsCenter() const noexcept { return m_BoundsCenter; }
        [[nodiscard]] Vector3 BoundsExtent() const noexcept { return m_BoundsExtent; }
        [[nodiscard]] bool EditModePreview() const noexcept { return m_EditModePreview; }

        void SetEffect(AssetId effect);
        void SetPlayOnAwake(bool value);
        void SetAutoDestroy(bool value);
        void SetSimulationSpeed(float value);
        void SetSeedOffset(std::uint32_t value);
        void SetQuality(VfxQualityTier value);
        void SetCulling(VfxCullingMode value);
        void SetBounds(Vector3 center, Vector3 extent);
        void SetEditModePreview(bool value);

      private:
        friend ComponentRegistration CreateVfxEmitterComponentRegistration();

        AssetId m_Effect;
        bool m_PlayOnAwake = true;
        bool m_AutoDestroy = false;
        float m_SimulationSpeed = 1.0F;
        std::uint32_t m_SeedOffset = 0;
        VfxQualityTier m_Quality = VfxQualityTier::High;
        VfxCullingMode m_Culling = VfxCullingMode::Automatic;
        Vector3 m_BoundsCenter;
        Vector3 m_BoundsExtent{5.0F, 5.0F, 5.0F};
        bool m_EditModePreview = false;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateVfxEmitterComponentRegistration();
} // namespace Keire
