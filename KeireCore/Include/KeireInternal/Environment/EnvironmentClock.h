#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Keire::Detail
{
    struct EnvironmentClockState
    {
        std::int64_t Day = 0;
        double DayFraction = 0.0;
        double SecondsPerDay = 1200.0;
        double Rate = 1.0;
        bool Paused = false;

        bool operator==(const EnvironmentClockState&) const noexcept = default;
    };

    // Owner-serialized numerical state. Runtime ownership, persistence and event delivery are separate boundaries.
    class EnvironmentClock final
    {
      public:
        explicit EnvironmentClock(const EnvironmentClockState state = {}) : m_State(state) { Validate(state); }

        [[nodiscard]] EnvironmentClockState State() const noexcept { return m_State; }

        [[nodiscard]] std::uint64_t DiscontinuityRevision() const noexcept { return m_DiscontinuityRevision; }

        void Configure(const double secondsPerDay, const double rate, const bool paused)
        {
            auto proposed = m_State;
            proposed.SecondsPerDay = secondsPerDay;
            proposed.Rate = rate;
            proposed.Paused = paused;
            Validate(proposed);
            m_State = proposed;
        }

        // Restore and scrubbing invalidate temporal consumers even when the requested time is unchanged.
        void Restore(const EnvironmentClockState state)
        {
            Validate(state);
            if (m_DiscontinuityRevision == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error("Environment discontinuity revision exhausted.");
            }
            m_State = state;
            ++m_DiscontinuityRevision;
        }

        void Seek(const std::int64_t day, const double dayFraction)
        {
            auto proposed = m_State;
            proposed.Day = day;
            proposed.DayFraction = dayFraction;
            Restore(proposed);
        }

        void Advance(const double elapsedSeconds)
        {
            if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
            {
                throw std::invalid_argument("Environment elapsed time must be finite and nonnegative.");
            }
            if (m_State.Paused || m_State.Rate == 0.0 || elapsedSeconds == 0.0)
            {
                return;
            }
            const double elapsedDays = CalculateElapsedDays(elapsedSeconds);
            constexpr double exclusiveIntegerLimit = 9223372036854775808.0;
            const double wholeDays = std::trunc(elapsedDays);
            if (!std::isfinite(elapsedDays) || wholeDays < -exclusiveIntegerLimit || wholeDays >= exclusiveIntegerLimit)
            {
                throw std::overflow_error("Environment advancement exceeds the supported day range.");
            }
            auto carry = static_cast<std::int64_t>(wholeDays);
            // Split before adding to retain local time-of-day when advancing many whole days.
            double fraction = m_State.DayFraction + std::fmod(elapsedDays, 1.0);
            if (fraction < 0.0)
            {
                carry = CheckedAdd(carry, -1);
                fraction += 1.0;
            }
            if (fraction >= 1.0)
            {
                carry = CheckedAdd(carry, 1);
                fraction -= 1.0;
            }
            const auto day = CheckedAdd(m_State.Day, carry);
            m_State.Day = day;
            m_State.DayFraction = fraction == 0.0 ? 0.0 : fraction;
        }

      private:
        [[nodiscard]] double CalculateElapsedDays(const double elapsedSeconds) const noexcept
        {
            const double unscaledDays = elapsedSeconds / m_State.SecondsPerDay;
            if (std::isnormal(unscaledDays))
            {
                return unscaledDays * m_State.Rate;
            }

            // Scale mantissas separately when division would overflow or lose subnormal precision.
            // Unlike long double, this also provides the required exponent range on MSVC.
            int elapsedExponent = 0;
            int durationExponent = 0;
            int rateExponent = 0;
            const double elapsedMantissa = std::frexp(elapsedSeconds, &elapsedExponent);
            const double durationMantissa = std::frexp(m_State.SecondsPerDay, &durationExponent);
            const double rateMantissa = std::frexp(m_State.Rate, &rateExponent);
            return std::ldexp((elapsedMantissa / durationMantissa) * rateMantissa,
                              elapsedExponent - durationExponent + rateExponent);
        }

        static void Validate(const EnvironmentClockState& state)
        {
            if (!std::isfinite(state.DayFraction) || state.DayFraction < 0.0 || state.DayFraction >= 1.0 ||
                !std::isfinite(state.SecondsPerDay) || state.SecondsPerDay <= 0.0 || !std::isfinite(state.Rate))
            {
                throw std::invalid_argument("Invalid environment clock state.");
            }
        }

        [[nodiscard]] static std::int64_t CheckedAdd(const std::int64_t value, const std::int64_t delta)
        {
            if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
                (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta))
            {
                throw std::overflow_error("Environment day overflow.");
            }
            return value + delta;
        }

        EnvironmentClockState m_State;
        std::uint64_t m_DiscontinuityRevision = 1;
    };
} // namespace Keire::Detail
