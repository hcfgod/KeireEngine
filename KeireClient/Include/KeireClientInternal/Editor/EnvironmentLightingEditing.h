#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace KeireEditor::Detail
{
    using LightingProfile = Keire::Detail::EnvironmentLightingProfile;
    using LightingOwnership = Keire::Detail::EnvironmentDirectionalOwnership;

    [[nodiscard]] inline std::optional<std::size_t> LightingKeyAtPhase(const LightingProfile& profile,
                                                                       const double phase)
    {
        if (!std::isfinite(phase))
        {
            return std::nullopt;
        }
        const auto& keys = profile.Keyframes();
        const auto found = std::lower_bound(keys.begin(), keys.end(), phase, [](const auto& key, const double value)
                                            { return key.DayFraction < value; });
        if (found == keys.end() || found->DayFraction != phase)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - keys.begin());
    }

    [[nodiscard]] inline double LightingKeyPhaseFromHours(const LightingProfile& profile, const std::size_t index,
                                                          const double hours)
    {
        const auto& keys = profile.Keyframes();
        if (index >= keys.size())
        {
            throw std::out_of_range("Lighting key no longer exists.");
        }
        if (!std::isfinite(hours))
        {
            throw std::invalid_argument("Key time must be finite.");
        }
        const double minimum = index == 0 ? 0.0 : std::nextafter(keys[index - 1].DayFraction, 1.0);
        const double maximum =
            index + 1 == keys.size() ? std::nextafter(1.0, 0.0) : std::nextafter(keys[index + 1].DayFraction, 0.0);
        return std::clamp(hours / 24.0, minimum, maximum);
    }

    [[nodiscard]] inline LightingProfile SetAmbientAnimation(const LightingProfile& profile, const bool enabled,
                                                             const Keire::EnvironmentAmbientLight baseline)
    {
        auto keys = profile.Keyframes();
        for (auto& key : keys)
        {
            key.Ambient = enabled ? std::optional(baseline) : std::nullopt;
        }
        return LightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(), profile.Sky(),
                               profile.DirectionFrame());
    }

    [[nodiscard]] inline LightingProfile ResetLightingShadows(const LightingProfile& profile)
    {
        return LightingProfile(profile.Ownership(), profile.Keyframes(), {}, profile.Sky(), profile.DirectionFrame());
    }

    [[nodiscard]] inline LightingProfile
    ReplaceLightingKey(const LightingProfile& profile, const std::size_t index,
                       const Keire::Detail::EnvironmentLightingKeyframe& replacement)
    {
        const auto& existing = profile.Keyframes();
        if (index >= existing.size())
        {
            throw std::out_of_range("Lighting key no longer exists.");
        }
        const double phase = replacement.DayFraction;
        if (!std::isfinite(phase) || phase < 0.0 || phase >= 1.0)
        {
            throw std::invalid_argument("Day fraction must be finite, at least 0 and less than 1.");
        }
        if ((index > 0 && phase <= existing[index - 1].DayFraction) ||
            (index + 1 < existing.size() && phase >= existing[index + 1].DayFraction))
        {
            throw std::invalid_argument("Keep this key's day fraction strictly between its neighboring keys.");
        }
        auto keys = existing;
        keys[index] = replacement;
        return LightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(), profile.Sky(),
                               profile.DirectionFrame());
    }

    [[nodiscard]] inline LightingProfile SetLightingOwnership(const LightingProfile& profile, const bool automatic)
    {
        auto keys = profile.Keyframes();
        if (automatic && keys.empty())
        {
            keys = {{0.0, 0.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 60.0, {1.0, 0.9, 0.8}, 1.0}};
        }
        else if (automatic && keys.size() == 1)
        {
            // An authored profile may retain one key. Enabling it must not erase those art settings.
            auto companion = keys.front();
            companion.DayFraction = std::fmod(companion.DayFraction + 0.5, 1.0);
            keys.push_back(companion);
            std::sort(keys.begin(), keys.end(),
                      [](const auto& left, const auto& right) { return left.DayFraction < right.DayFraction; });
        }
        return LightingProfile(automatic ? LightingOwnership::Environment : LightingOwnership::Authored,
                               std::move(keys), profile.Shadows(), profile.Sky(), profile.DirectionFrame());
    }

    // Insert in the largest cyclic interval, inheriting the preceding key's art settings.
    [[nodiscard]] inline LightingProfile AddLightingKey(const LightingProfile& profile)
    {
        auto keys = profile.Keyframes();
        if (keys.empty() || keys.size() >= LightingProfile::MaximumKeyframes)
        {
            throw std::invalid_argument("Enable daily lighting before adding keys; at most 128 keys are supported.");
        }
        std::size_t selected = 0;
        double largestGap = 0.0;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            const double next = index + 1 < keys.size() ? keys[index + 1].DayFraction : keys.front().DayFraction + 1.0;
            const double gap = next - keys[index].DayFraction;
            if (gap > largestGap)
            {
                selected = index;
                largestGap = gap;
            }
        }
        auto added = keys[selected];
        added.DayFraction = std::fmod(added.DayFraction + largestGap * 0.5, 1.0);
        keys.push_back(added);
        std::sort(keys.begin(), keys.end(),
                  [](const auto& left, const auto& right) { return left.DayFraction < right.DayFraction; });
        return LightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(), profile.Sky(),
                               profile.DirectionFrame());
    }

    [[nodiscard]] inline LightingProfile InsertLightingKeyAtPhase(const LightingProfile& profile, const double phase)
    {
        if (!std::isfinite(phase) || phase < 0.0 || phase >= 1.0)
        {
            throw std::invalid_argument("Key time must be a finite day fraction in [0, 1).");
        }
        auto keys = profile.Keyframes();
        if (keys.size() >= LightingProfile::MaximumKeyframes)
        {
            throw std::invalid_argument("At most 128 lighting keys are supported.");
        }
        const auto insertion = std::lower_bound(keys.begin(), keys.end(), phase, [](const auto& key, const double value)
                                                { return key.DayFraction < value; });
        if (insertion != keys.end() && insertion->DayFraction == phase)
        {
            throw std::invalid_argument("A lighting key already exists at the preview time.");
        }
        const auto sample = profile.Evaluate({0, phase, 86400.0});
        if (!sample)
        {
            throw std::invalid_argument("Enable daily lighting before inserting a preview key.");
        }
        constexpr double degreesPerRadian = 180.0 / std::numbers::pi;
        double azimuth = std::atan2(-sample->Direction[0], -sample->Direction[2]) * degreesPerRadian;
        azimuth = std::fmod(azimuth + 360.0, 360.0);
        const double elevation = std::asin(std::clamp(-sample->Direction[1], -1.0, 1.0)) * degreesPerRadian;
        Keire::Detail::EnvironmentLightingKeyframe added{
            phase, azimuth, elevation, sample->LinearColor, sample->Intensity, sample->Ambient};
        keys.insert(insertion, added);
        return LightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(), profile.Sky(),
                               profile.DirectionFrame());
    }

    [[nodiscard]] inline LightingProfile RemoveLightingKey(const LightingProfile& profile, const std::size_t index)
    {
        if (index >= profile.Keyframes().size())
        {
            throw std::out_of_range("Lighting key no longer exists.");
        }
        auto keys = profile.Keyframes();
        keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
        return LightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(), profile.Sky(),
                               profile.DirectionFrame());
    }
} // namespace KeireEditor::Detail
