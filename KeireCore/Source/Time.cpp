#include "Keire/Time.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        void ValidatePositiveFinite(const TimeStep value, const char* name)
        {
            if (!std::isfinite(value.Seconds()) || value.Seconds() <= 0.0)
            {
                throw std::invalid_argument(std::string(name) + " must be finite and greater than zero.");
            }
        }
    } // namespace

    Time::Time(const TimeSpecification& specification) : m_Specification(specification)
    {
        ValidatePositiveFinite(m_Specification.FixedDeltaTime, "Fixed delta time");
        ValidatePositiveFinite(m_Specification.MaximumDeltaTime, "Maximum delta time");
        ValidatePositiveFinite(m_Specification.SmoothingTimeConstant, "Smoothing time constant");
        if (m_Specification.MaximumFixedStepsPerFrame == 0)
        {
            throw std::invalid_argument("Maximum fixed steps per frame must be greater than zero.");
        }
    }

    void Time::Reset() noexcept
    {
        const auto specification = m_Specification;
        const auto scale = m_TimeScale;
        const auto paused = m_Paused;
        *this = Time(specification);
        m_TimeScale = scale;
        m_Paused = paused;
    }

    void Time::AdvanceFrame(const TimeStep rawDeltaTime, const bool suspendSimulation)
    {
        if (m_PendingFixedSteps != 0)
        {
            throw std::logic_error("All pending fixed steps must be consumed before advancing another frame.");
        }
        if (!std::isfinite(rawDeltaTime.Seconds()) || rawDeltaTime.Seconds() < 0.0)
        {
            throw std::invalid_argument("Raw delta time must be finite and non-negative.");
        }

        m_RawDeltaTime = rawDeltaTime;
        m_UnscaledDeltaTime =
            TimeStep::FromSeconds(std::min(rawDeltaTime.Seconds(), m_Specification.MaximumDeltaTime.Seconds()));
        const bool simulationPaused = m_Paused || suspendSimulation;
        m_DeltaTime = simulationPaused ? TimeStep{} : m_UnscaledDeltaTime * m_TimeScale;

        if (!m_HasSmoothedSample)
        {
            m_SmoothedUnscaledDeltaTime = m_UnscaledDeltaTime;
            m_HasSmoothedSample = true;
        }
        else if (m_UnscaledDeltaTime.Seconds() > 0.0)
        {
            const auto alpha =
                1.0 - std::exp(-m_UnscaledDeltaTime.Seconds() / m_Specification.SmoothingTimeConstant.Seconds());
            m_SmoothedUnscaledDeltaTime += (m_UnscaledDeltaTime - m_SmoothedUnscaledDeltaTime) * alpha;
        }
        m_SmoothDeltaTime = simulationPaused ? TimeStep{} : m_SmoothedUnscaledDeltaTime * m_TimeScale;

        m_RealtimeSinceStartup += m_RawDeltaTime;
        m_UnscaledTime += m_UnscaledDeltaTime;
        m_TimeSinceStartup += m_DeltaTime;
        ++m_FrameCount;

        m_FixedAccumulator += m_DeltaTime;
        const auto availableSteps = static_cast<std::uint64_t>(
            std::floor(m_FixedAccumulator.Seconds() / m_Specification.FixedDeltaTime.Seconds()));
        const auto stepsToRun = std::min<std::uint64_t>(availableSteps, m_Specification.MaximumFixedStepsPerFrame);
        const auto droppedSteps = availableSteps - stepsToRun;
        m_PendingFixedSteps = static_cast<std::uint32_t>(stepsToRun);

        if (availableSteps > 0)
        {
            m_FixedAccumulator -= m_Specification.FixedDeltaTime * static_cast<double>(availableSteps);
        }
        if (droppedSteps > 0)
        {
            m_DroppedSimulationTime += m_Specification.FixedDeltaTime * static_cast<double>(droppedSteps);
        }
        m_InterpolationAlpha = std::clamp(m_FixedAccumulator / m_Specification.FixedDeltaTime, 0.0, 1.0);
    }

    bool Time::ConsumeFixedStep() noexcept
    {
        if (m_PendingFixedSteps == 0)
        {
            return false;
        }
        --m_PendingFixedSteps;
        ++m_FixedTickCount;
        m_FixedTime += m_Specification.FixedDeltaTime;
        return true;
    }

    void Time::SetTimeScale(const double scale)
    {
        if (!std::isfinite(scale) || scale < 0.0 || scale > 100.0)
        {
            throw std::invalid_argument("Time scale must be finite and in the range 0..100.");
        }
        m_TimeScale = scale;
    }
} // namespace Keire
