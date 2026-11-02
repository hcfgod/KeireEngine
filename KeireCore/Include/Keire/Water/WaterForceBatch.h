#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Water/WaterHydrodynamics.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire
{
    /// Scene adapter supplies monotonically increasing generations, never addresses of recycled objects.
    struct WaterSimulationStamp
    {
        std::uint64_t WorldGeneration = 0;
        std::uint64_t FrameEpoch = 0;
        std::uint64_t PhysicsTick = 0;
        Vector3d WorldOrigin;

        bool operator==(const WaterSimulationStamp&) const = default;
    };

    struct WaterForceBodySnapshot
    {
        EntityId Entity;
        PhysicsBodyId Body;
        WaterBodyKinematics Kinematics;
        WaterHydrodynamicSettings Settings;
        std::vector<Vector3d> ProbeGravity;
    };

    struct PreparedWaterBodyLoads
    {
        EntityId Entity;
        PhysicsBodyId Body;
        WaterBodyLoads Loads;
    };

    /// Immutable prepared result. Preparing this object has no effect on native force accumulators.
    /// The scene/native bridge must validate its stamp and every identity before a single additive commit.
    class KEIRE_API PreparedWaterForceBatch final
    {
      public:
        PreparedWaterForceBatch(WaterSimulationStamp stamp, std::span<const WaterForceBodySnapshot> bodies,
                                std::span<const WaterMedium> media, double timeSeconds, double deltaSeconds);

        [[nodiscard]] const WaterSimulationStamp& Stamp() const noexcept { return m_Stamp; }

        /// The commit adapter must match this exact step duration to the native capture context.
        [[nodiscard]] double DeltaSeconds() const noexcept { return m_DeltaSeconds; }

        [[nodiscard]] double TimeSeconds() const noexcept { return m_TimeSeconds; }

        [[nodiscard]] std::span<const PreparedWaterBodyLoads> Bodies() const noexcept { return m_Bodies; }

      private:
        WaterSimulationStamp m_Stamp;
        double m_TimeSeconds = 0.0;
        double m_DeltaSeconds = 0.0;
        std::vector<PreparedWaterBodyLoads> m_Bodies;
    };
} // namespace Keire
