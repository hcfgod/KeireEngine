#pragma once

#include "KeireInternal/Environment/EnvironmentCloudPhase.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    /// Cycles from the same explicit day-zero epoch as clouds; never multiply the signed day by seconds.
    /// The modular arithmetic preserves fractional preview changes even at extreme day indices.
    [[nodiscard]] inline double EvaluateEnvironmentStarPhase(const double frequency, const EnvironmentClockState& clock)
    {
        if (!std::isfinite(frequency) || frequency < 0.0 || frequency > 10.0)
        {
            throw std::invalid_argument("Star twinkle frequency must be finite and in 0..10 Hz.");
        }
        const EnvironmentClock validatedClock(clock);
        (void)validatedClock;
        const double daily = CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, frequency, 1.0, 1.0);
        const double fraction = CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, frequency, 1.0, clock.DayFraction);
        return CloudPhaseDetail::Wrap(CloudPhaseDetail::WholeDayPhase(daily, clock.Day) + fraction);
    }
} // namespace Keire::Detail
