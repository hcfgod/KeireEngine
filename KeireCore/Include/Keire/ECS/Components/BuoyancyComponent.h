#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Water/WaterHydrodynamics.h"

namespace Keire
{
    /// Explicit opt-in for native dynamic bodies. Probe offsets are measured from their native COM.
    class KEIRE_API BuoyancyComponent final : public Component
    {
      public:
        BuoyancyComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b4549524542554fULL, 0x59414e4359000001ULL));
        }

        [[nodiscard]] const WaterHydrodynamicSettings& Settings() const noexcept { return m_Settings; }

        void Configure(const WaterHydrodynamicSettings& settings);
        void Reset();

      private:
        WaterHydrodynamicSettings m_Settings;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateBuoyancyComponentRegistration();
} // namespace Keire
