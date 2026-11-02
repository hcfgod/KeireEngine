#pragma once

#include "Keire/ECS/Components/WaterBodyComponent.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    class WaterRuntimeAccess final
    {
      public:
        static void SetSimulationTime(WaterBodyComponent& water, const double seconds)
        {
            if (!std::isfinite(seconds) || seconds < 0.0)
            {
                throw std::invalid_argument("Water simulation time must be finite and nonnegative.");
            }
            // Transient solver/presentation state, not authored data; do not dirty the scene.
            water.m_SimulationTimeSeconds = seconds;
        }
    };
} // namespace Keire::Detail
