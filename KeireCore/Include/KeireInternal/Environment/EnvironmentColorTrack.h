#pragma once

#include "KeireInternal/Environment/EnvironmentClock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    enum class EnvironmentKeyInterpolation : std::uint8_t
    {
        Linear,
        Smooth,
        Hold
    };

    struct EnvironmentColorKey
    {
        double DayFraction = 0.0;
        std::array<double, 3> LinearColor{};
        // The key owns the segment leading to its successor, including the midnight segment.
        EnvironmentKeyInterpolation Interpolation = EnvironmentKeyInterpolation::Linear;

        bool operator==(const EnvironmentColorKey&) const noexcept = default;
    };

    // Internal daily HDR color curve. Directional-light ownership does not gate its evaluation.
    class EnvironmentColorTrack final
    {
      public:
        static constexpr std::size_t MaximumKeys = 128;
        static constexpr double MaximumRadiance = 65504.0;

        explicit EnvironmentColorTrack(std::vector<EnvironmentColorKey> keys = {{}}) : m_Keys(std::move(keys))
        {
            Validate();
        }

        [[nodiscard]] const std::vector<EnvironmentColorKey>& Keys() const noexcept { return m_Keys; }

        bool operator==(const EnvironmentColorTrack&) const noexcept = default;

        [[nodiscard]] std::array<double, 3> Evaluate(const EnvironmentClockState clock) const
        {
            (void)EnvironmentClock(clock);
            if (m_Keys.empty())
            {
                throw std::logic_error("Cannot evaluate a moved-from environment color track.");
            }
            if (m_Keys.size() == 1)
            {
                return m_Keys.front().LinearColor;
            }

            const auto next = std::upper_bound(m_Keys.begin(), m_Keys.end(), clock.DayFraction,
                                               [](const double phase, const EnvironmentColorKey& key)
                                               { return phase < key.DayFraction; });
            const auto& left = next == m_Keys.begin() ? m_Keys.back() : *std::prev(next);
            const auto& right = next == m_Keys.end() ? m_Keys.front() : *next;
            if (left.Interpolation == EnvironmentKeyInterpolation::Hold)
            {
                return left.LinearColor;
            }
            double duration = right.DayFraction - left.DayFraction;
            if (duration <= 0.0)
            {
                duration += 1.0;
            }
            double elapsed = clock.DayFraction - left.DayFraction;
            if (elapsed < 0.0)
            {
                elapsed += 1.0;
            }
            double blend = std::clamp(elapsed / duration, 0.0, 1.0);
            if (left.Interpolation == EnvironmentKeyInterpolation::Smooth)
            {
                blend = blend * blend * (3.0 - 2.0 * blend);
            }
            std::array<double, 3> result{};
            for (std::size_t channel = 0; channel < result.size(); ++channel)
            {
                result[channel] = std::lerp(left.LinearColor[channel], right.LinearColor[channel], blend);
            }
            return result;
        }

      private:
        void Validate() const
        {
            if (m_Keys.empty() || m_Keys.size() > MaximumKeys)
            {
                throw std::invalid_argument("Environment color tracks require 1 to 128 keys.");
            }
            double previousPhase = -1.0;
            for (const auto& key : m_Keys)
            {
                if (!std::isfinite(key.DayFraction) || key.DayFraction < 0.0 || key.DayFraction >= 1.0 ||
                    key.DayFraction <= previousPhase)
                {
                    throw std::invalid_argument("Environment color keys require strictly increasing phases in [0, 1).");
                }
                if (key.Interpolation != EnvironmentKeyInterpolation::Linear &&
                    key.Interpolation != EnvironmentKeyInterpolation::Smooth &&
                    key.Interpolation != EnvironmentKeyInterpolation::Hold)
                {
                    throw std::invalid_argument("Unknown environment color interpolation.");
                }
                for (const double channel : key.LinearColor)
                {
                    if (!std::isfinite(channel) || channel < 0.0 || channel > MaximumRadiance)
                    {
                        throw std::invalid_argument("Sky colors require finite linear HDR channels in [0, 65504].");
                    }
                }
                previousPhase = key.DayFraction;
            }
        }

        std::vector<EnvironmentColorKey> m_Keys;
    };
} // namespace Keire::Detail
