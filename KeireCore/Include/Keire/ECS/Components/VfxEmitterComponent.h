#pragma once

#include "Keire/ECS/Component.h"

#include <cstdint>

namespace Keire
{
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

        void SetEffect(AssetId effect);
        void SetPlayOnAwake(bool value);
        void SetAutoDestroy(bool value);
        void SetSimulationSpeed(float value);
        void SetSeedOffset(std::uint32_t value);

      private:
        friend ComponentRegistration CreateVfxEmitterComponentRegistration();

        AssetId m_Effect;
        bool m_PlayOnAwake = true;
        bool m_AutoDestroy = false;
        float m_SimulationSpeed = 1.0F;
        std::uint32_t m_SeedOffset = 0;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateVfxEmitterComponentRegistration();
} // namespace Keire
