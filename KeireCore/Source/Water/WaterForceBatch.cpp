#include "Keire/Water/WaterForceBatch.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    PreparedWaterForceBatch::PreparedWaterForceBatch(const WaterSimulationStamp stamp,
                                                     const std::span<const WaterForceBodySnapshot> bodies,
                                                     const std::span<const WaterMedium> media, const double timeSeconds,
                                                     const double deltaSeconds)
        : m_Stamp(stamp), m_TimeSeconds(timeSeconds), m_DeltaSeconds(deltaSeconds)
    {
        if (stamp.WorldGeneration == 0 || !Math::IsFinite(stamp.WorldOrigin) || !std::isfinite(timeSeconds) ||
            !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
        {
            throw std::invalid_argument("Water force batch requires a live world generation and finite timestep.");
        }
        m_Bodies.reserve(bodies.size());
        for (const auto& body : bodies)
        {
            if (!body.Entity || !body.Body)
            {
                throw std::invalid_argument("Water force batch contains an invalid entity or native body identity.");
            }
            for (const auto& prepared : m_Bodies)
            {
                if (prepared.Entity == body.Entity || prepared.Body == body.Body)
                {
                    throw std::invalid_argument("Water force batch must contain each entity and native body once.");
                }
            }
            const auto loads = EvaluateWaterBodyLoads(body.Kinematics, body.Settings, media, body.ProbeGravity,
                                                      timeSeconds, deltaSeconds);
            m_Bodies.push_back({body.Entity, body.Body, loads});
        }
    }
} // namespace Keire
