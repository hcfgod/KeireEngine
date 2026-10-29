#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline std::optional<std::size_t>
    FindSkyColorKeyAtPhase(const Keire::Detail::EnvironmentColorTrack& track, const double phase)
    {
        if (!std::isfinite(phase))
        {
            return std::nullopt;
        }
        const auto found =
            std::lower_bound(track.Keys().begin(), track.Keys().end(), phase,
                             [](const auto& key, const double value) { return key.DayFraction < value; });
        if (found == track.Keys().end() || found->DayFraction != phase)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - track.Keys().begin());
    }

    [[nodiscard]] inline std::optional<std::size_t> PickSkyColorKey(const Keire::Detail::EnvironmentColorTrack& track,
                                                                    const double phase, const double width)
    {
        if (!std::isfinite(phase) || !std::isfinite(width) || width <= 0.0)
        {
            return std::nullopt;
        }
        constexpr double hitRadiusPixels = 8.0;
        double closestDistance = hitRadiusPixels;
        std::optional<std::size_t> closest;
        for (std::size_t index = 0; index < track.Keys().size(); ++index)
        {
            const double distance = std::abs(track.Keys()[index].DayFraction - phase) * width;
            if (distance <= hitRadiusPixels && (!closest || distance < closestDistance))
            {
                closest = index;
                closestDistance = distance;
            }
        }
        return closest;
    }

    [[nodiscard]] inline Keire::Detail::EnvironmentLightingProfile
    ReplaceSkyProfile(const Keire::Detail::EnvironmentLightingProfile& profile,
                      std::optional<Keire::Detail::EnvironmentSkyProfile> sky)
    {
        return Keire::Detail::EnvironmentLightingProfile(profile.Ownership(), profile.Keyframes(), profile.Shadows(),
                                                         std::move(sky), profile.DirectionFrame());
    }

    [[nodiscard]] inline Keire::Detail::EnvironmentSkyProfile DaylightSkyPreset()
    {
        Keire::Detail::EnvironmentSkyProfile sky;
        sky.Enabled = true;
        sky.Horizon = Keire::Detail::EnvironmentColorTrack({{0.0, {0.008, 0.012, 0.03}},
                                                            {0.25, {0.8, 0.18, 0.06}},
                                                            {0.5, {0.6, 0.85, 1.2}},
                                                            {0.75, {0.9, 0.15, 0.04}}});
        sky.Zenith = Keire::Detail::EnvironmentColorTrack({{0.0, {0.001, 0.002, 0.008}},
                                                           {0.25, {0.04, 0.08, 0.2}},
                                                           {0.5, {0.04, 0.2, 0.65}},
                                                           {0.75, {0.04, 0.02, 0.12}}});
        return sky;
    }

    [[nodiscard]] inline Keire::Detail::EnvironmentColorTrack
    InsertSkyColorKey(const Keire::Detail::EnvironmentColorTrack& track, const double phase)
    {
        if (track.Keys().empty() || !std::isfinite(phase) || phase < 0.0 || phase >= 1.0 ||
            track.Keys().size() >= Keire::Detail::EnvironmentColorTrack::MaximumKeys)
        {
            throw std::invalid_argument("Choose a time before 24:00 and keep at most 128 sky keys.");
        }
        auto keys = track.Keys();
        const auto next = std::lower_bound(keys.begin(), keys.end(), phase,
                                           [](const auto& key, const double value) { return key.DayFraction < value; });
        if (next != keys.end() && next->DayFraction == phase)
        {
            throw std::invalid_argument("A sky key already exists at this time. Select it to edit its color.");
        }
        const auto interpolation = (next == keys.begin() ? keys.back() : *std::prev(next)).Interpolation;
        keys.insert(next, {phase, track.Evaluate({.DayFraction = phase}), interpolation});
        return Keire::Detail::EnvironmentColorTrack(std::move(keys));
    }

    [[nodiscard]] inline double SkyColorKeyPhase(const Keire::Detail::EnvironmentColorTrack& track,
                                                 const std::size_t index, const double hours)
    {
        if (index >= track.Keys().size() || !std::isfinite(hours))
        {
            throw std::invalid_argument("Select an existing sky key and a finite time.");
        }
        const auto& keys = track.Keys();
        const double lower = index == 0 ? 0.0 : std::nextafter(keys[index - 1].DayFraction, 1.0);
        const double upper =
            index + 1 == keys.size() ? std::nextafter(1.0, 0.0) : std::nextafter(keys[index + 1].DayFraction, 0.0);
        return std::clamp(hours / 24.0, lower, upper);
    }
} // namespace KeireEditor::Detail
