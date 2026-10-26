#pragma once

#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Environment/EnvironmentClock.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Keire::Detail
{
    namespace CloudPhaseDetail
    {
        [[nodiscard]] inline double Wrap(const double phase) noexcept
        {
            double wrapped = std::fmod(phase, 1.0);
            if (wrapped < 0.0)
            {
                wrapped += 1.0;
            }
            // Adding one to a tiny negative remainder can round to exactly one.
            return wrapped >= 1.0 || wrapped == 0.0 ? 0.0 : wrapped;
        }

        [[nodiscard]] inline double DurationPhase(const double seconds, const double wind, const double size,
                                                  const double fraction) noexcept
        {
            if (wind == 0.0 || fraction == 0.0)
            {
                return 0.0;
            }
            int secondsExponent = 0;
            int windExponent = 0;
            int sizeExponent = 0;
            int fractionExponent = 0;
            const double secondsMantissa = std::frexp(seconds, &secondsExponent);
            const double windMantissa = std::frexp(wind, &windExponent);
            const double sizeMantissa = std::frexp(size, &sizeExponent);
            const double fractionMantissa = std::frexp(fraction, &fractionExponent);
            const double mantissa = secondsMantissa * windMantissa * fractionMantissa / sizeMantissa;
            const int exponent = secondsExponent + windExponent + fractionExponent - sizeExponent;
            if (exponent <= 0)
            {
                return Wrap(std::ldexp(mantissa, exponent));
            }
            double phase = Wrap(mantissa);
            for (int bit = 0; bit < exponent && phase != 0.0; ++bit)
            {
                phase = Wrap(phase + phase);
            }
            return phase;
        }

        [[nodiscard]] inline double WholeDayPhase(double phase, const std::int64_t day) noexcept
        {
            // Unsigned magnitude also represents INT64_MIN without signed negation overflow.
            auto remaining = static_cast<std::uint64_t>(day);
            if (day < 0)
            {
                remaining = std::uint64_t{0} - remaining;
            }
            double accumulated = 0.0;
            while (remaining != 0)
            {
                if ((remaining & 1U) != 0)
                {
                    accumulated = Wrap(accumulated + phase);
                }
                phase = Wrap(phase + phase);
                remaining >>= 1U;
            }
            return day < 0 ? Wrap(-accumulated) : accumulated;
        }
    } // namespace CloudPhaseDetail

    /// Constant authored wind displacement, in pattern repeats, from the explicit day-zero epoch.
    /// Subtract these phases from observer-relative pattern coordinates. Rate/pause are already reflected in clock.
    /// Separate day/fraction evaluation preserves local scrubbing at huge days, not exact astronomical phase:
    /// modular products retain floating-point coefficient error and are not cross-platform bit-identical guarantees.
    /// Changing duration, wind or pattern size changes the epoch mapping and requires history invalidation.
    [[nodiscard]] inline std::array<std::array<double, 2>, MaximumCloudLayers>
    EvaluateEnvironmentCloudPhase(const CloudSettings& settings, const EnvironmentClockState& clock)
    {
        ValidateCloudSettings(settings);
        const EnvironmentClock validatedClock(clock);
        (void)validatedClock;
        std::array<std::array<double, 2>, MaximumCloudLayers> result{};
        for (std::size_t layerIndex = 0; layerIndex < settings.Layers.size(); ++layerIndex)
        {
            const auto& layer = settings.Layers[layerIndex];
            for (std::size_t axis = 0; axis < layer.WindMetersPerSecond.size(); ++axis)
            {
                const double wind = layer.WindMetersPerSecond[axis];
                const double daily =
                    CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, wind, layer.PatternSizeMeters, 1.0);
                const double fraction = CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, wind,
                                                                        layer.PatternSizeMeters, clock.DayFraction);
                result[layerIndex][axis] =
                    CloudPhaseDetail::Wrap(CloudPhaseDetail::WholeDayPhase(daily, clock.Day) + fraction);
            }
        }
        return result;
    }
} // namespace Keire::Detail
