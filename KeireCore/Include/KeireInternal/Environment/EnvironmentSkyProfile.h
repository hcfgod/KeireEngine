#pragma once

#include "Keire/Rendering/EnvironmentLighting.h"
#include "KeireInternal/Environment/EnvironmentColorTrack.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    struct EnvironmentSkyProfile
    {
        bool Enabled = false;
        EnvironmentColorTrack Horizon;
        EnvironmentColorTrack Zenith;
        double BlendWeight = 1.0;
        double GradientExponent = 1.0;

        bool operator==(const EnvironmentSkyProfile&) const noexcept = default;

        void Validate() const
        {
            if (!std::isfinite(BlendWeight) || BlendWeight < 0.0 || BlendWeight > 1.0 ||
                !std::isfinite(GradientExponent) || GradientExponent < 0.1 || GradientExponent > 16.0 ||
                Horizon.Keys().empty() || Zenith.Keys().empty())
            {
                throw std::invalid_argument(
                    "Sky gradients require valid tracks, weight [0, 1] and exponent [0.1, 16].");
            }
        }

        [[nodiscard]] std::optional<EnvironmentSkyColors> Evaluate(const EnvironmentClockState clock) const
        {
            Validate();
            (void)EnvironmentClock(clock);
            if (!Enabled)
            {
                return std::nullopt;
            }
            EnvironmentSkyColors result;
            const auto horizon = Horizon.Evaluate(clock);
            const auto zenith = Zenith.Evaluate(clock);
            for (std::size_t channel = 0; channel < horizon.size(); ++channel)
            {
                result.Horizon[channel] = static_cast<float>(horizon[channel]);
                result.Zenith[channel] = static_cast<float>(zenith[channel]);
            }
            result.BlendWeight = static_cast<float>(BlendWeight);
            result.GradientExponent = static_cast<float>(GradientExponent);
            return result;
        }
    };
} // namespace Keire::Detail
