#pragma once

#include "KeireInternal/Spatial/FrameRegistry.h"
#include "KeireInternal/Spatial/PlanetDefinition.h"

#include <algorithm>
#include <span>
#include <vector>

namespace Keire::Detail
{
    // Owned with immutable planet definitions; reset when replacing the definitions.
    class PlanetFrameCache final
    {
      public:
        [[nodiscard]] std::span<const BoundFrameSnapshot>
        Resolve(FrameRegistry& registry, std::span<const PlanetDefinition> planets, double time)
        {
            if (!m_Snapshots.empty() && m_Snapshots.size() == planets.size() &&
                std::ranges::all_of(m_Snapshots,
                                    [&](const auto& snapshot) { return registry.IsCurrent(snapshot, time); }))
            {
                return m_Snapshots;
            }

            std::vector<FrameSnapshot> evaluations;
            evaluations.reserve(planets.size());
            for (const auto& planet : planets)
            {
                evaluations.push_back(EvaluatePlanetFrame(planet, time, registry.Revision()));
            }
            m_Snapshots = registry.Publish(evaluations, time);
            return m_Snapshots;
        }

      private:
        std::vector<BoundFrameSnapshot> m_Snapshots;
    };
} // namespace Keire::Detail
