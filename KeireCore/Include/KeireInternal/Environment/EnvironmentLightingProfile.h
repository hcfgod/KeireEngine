#pragma once

#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Environment/EnvironmentSkyProfile.h"
#include "KeireInternal/Rendering/EnvironmentLightingValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    enum class EnvironmentDirectionalOwnership
    {
        Authored,
        Environment
    };

    struct EnvironmentLightingKeyframe
    {
        double DayFraction = 0.0;
        double AzimuthDegrees = 0.0;
        double ElevationDegrees = 0.0;
        std::array<double, 3> LinearColor{1.0, 1.0, 1.0};
        double Intensity = 1.0;
        std::optional<EnvironmentAmbientLight> Ambient;

        bool operator==(const EnvironmentLightingKeyframe&) const noexcept = default;
    };

    struct EnvironmentDirectionalSample
    {
        // Incoming light-ray direction in the profile's explicit XYZ basis (legacy east/up/north by default).
        std::array<double, 3> Direction{};
        std::array<double, 3> LinearColor{};
        double Intensity = 0.0;
        std::optional<EnvironmentAmbientLight> Ambient;
    };

    // Immutable art-directed daily cycle. Planetary ephemerides and frame conversion are separate evaluators.
    // This is not yet a public asset format or a renderer submission contract.
    class EnvironmentLightingProfile final
    {
      public:
        static constexpr std::size_t MaximumKeyframes = 128;

        explicit EnvironmentLightingProfile(
            const EnvironmentDirectionalOwnership ownership = EnvironmentDirectionalOwnership::Authored,
            std::vector<EnvironmentLightingKeyframe> keyframes = {}, EnvironmentShadowSettings shadows = {},
            std::optional<EnvironmentSkyProfile> sky = {},
            const EnvironmentDirectionalFrame directionFrame = EnvironmentDirectionalFrame::LegacyWorld)
            : m_Ownership(ownership), m_Keyframes(std::move(keyframes)), m_Shadows(shadows), m_Sky(std::move(sky)),
              m_DirectionFrame(directionFrame)
        {
            Validate();
        }

        [[nodiscard]] EnvironmentDirectionalOwnership Ownership() const noexcept { return m_Ownership; }

        EnvironmentLightingProfile(const EnvironmentLightingProfile&) = default;

        EnvironmentLightingProfile(EnvironmentLightingProfile&& other) noexcept
            : m_Ownership(std::exchange(other.m_Ownership, EnvironmentDirectionalOwnership::Authored)),
              m_Keyframes(std::move(other.m_Keyframes)), m_Shadows(std::exchange(other.m_Shadows, {})),
              m_Sky(std::move(other.m_Sky)),
              m_DirectionFrame(std::exchange(other.m_DirectionFrame, EnvironmentDirectionalFrame::LegacyWorld))
        {
            other.m_Keyframes.clear();
            other.m_Sky.reset();
        }

        EnvironmentLightingProfile& operator=(EnvironmentLightingProfile other) noexcept
        {
            std::swap(m_Ownership, other.m_Ownership);
            m_Keyframes.swap(other.m_Keyframes);
            std::swap(m_Shadows, other.m_Shadows);
            m_Sky.swap(other.m_Sky);
            std::swap(m_DirectionFrame, other.m_DirectionFrame);
            return *this;
        }

        bool operator==(const EnvironmentLightingProfile&) const noexcept = default;

        [[nodiscard]] const std::vector<EnvironmentLightingKeyframe>& Keyframes() const noexcept { return m_Keyframes; }

        [[nodiscard]] EnvironmentShadowSettings Shadows() const noexcept { return m_Shadows; }

        [[nodiscard]] const std::optional<EnvironmentSkyProfile>& Sky() const noexcept { return m_Sky; }

        [[nodiscard]] EnvironmentDirectionalFrame DirectionFrame() const noexcept { return m_DirectionFrame; }

        [[nodiscard]] std::optional<EnvironmentDirectionalSample> Evaluate(const EnvironmentClockState clock) const
        {
            (void)EnvironmentClock(clock);
            if (m_Ownership == EnvironmentDirectionalOwnership::Authored)
            {
                return std::nullopt;
            }
            const auto next = std::upper_bound(m_Keyframes.begin(), m_Keyframes.end(), clock.DayFraction,
                                               [](const double phase, const EnvironmentLightingKeyframe& key)
                                               { return phase < key.DayFraction; });
            const auto& right = next == m_Keyframes.end() ? m_Keyframes.front() : *next;
            const auto& left = next == m_Keyframes.begin() ? m_Keyframes.back() : *std::prev(next);
            double interval = right.DayFraction - left.DayFraction;
            if (interval <= 0.0)
            {
                interval += 1.0;
            }
            double elapsed = clock.DayFraction - left.DayFraction;
            if (elapsed < 0.0)
            {
                elapsed += 1.0;
            }
            const double blend = std::clamp(elapsed / interval, 0.0, 1.0);
            double azimuthDelta = std::remainder(right.AzimuthDegrees - left.AzimuthDegrees, 360.0);
            if (azimuthDelta == -180.0)
            {
                azimuthDelta = 180.0;
            }
            constexpr double radiansPerDegree = std::numbers::pi / 180.0;
            const double azimuth = (left.AzimuthDegrees + blend * azimuthDelta) * radiansPerDegree;
            const double elevation = std::lerp(left.ElevationDegrees, right.ElevationDegrees, blend) * radiansPerDegree;
            const double horizontal = std::cos(elevation);
            EnvironmentDirectionalSample result;
            result.Direction = {-horizontal * std::sin(azimuth), -std::sin(elevation), -horizontal * std::cos(azimuth)};
            for (std::size_t channel = 0; channel < result.LinearColor.size(); ++channel)
            {
                result.LinearColor[channel] = std::lerp(left.LinearColor[channel], right.LinearColor[channel], blend);
            }
            result.Intensity = std::lerp(left.Intensity, right.Intensity, blend);
            if (left.Ambient)
            {
                EnvironmentAmbientLight ambient;
                for (std::size_t channel = 0; channel < ambient.LinearColor.size(); ++channel)
                {
                    ambient.LinearColor[channel] =
                        static_cast<float>(std::lerp(static_cast<double>(left.Ambient->LinearColor[channel]),
                                                     static_cast<double>(right.Ambient->LinearColor[channel]), blend));
                }
                ambient.Intensity = static_cast<float>(std::lerp(static_cast<double>(left.Ambient->Intensity),
                                                                 static_cast<double>(right.Ambient->Intensity), blend));
                result.Ambient = ambient;
            }
            return result;
        }

      private:
        void Validate() const
        {
            if (m_DirectionFrame != EnvironmentDirectionalFrame::LegacyWorld &&
                m_DirectionFrame != EnvironmentDirectionalFrame::PlanetFixed &&
                m_DirectionFrame != EnvironmentDirectionalFrame::InertialRoot)
            {
                throw std::invalid_argument("Unknown daily-light reference frame.");
            }
            if (m_Sky)
            {
                m_Sky->Validate();
            }
            ValidateEnvironmentShadows(m_Shadows);
            if (m_Ownership != EnvironmentDirectionalOwnership::Authored &&
                m_Ownership != EnvironmentDirectionalOwnership::Environment)
            {
                throw std::invalid_argument("Unknown environment directional-light ownership.");
            }
            if (m_Keyframes.size() > MaximumKeyframes ||
                (m_Ownership == EnvironmentDirectionalOwnership::Environment && m_Keyframes.size() < 2))
            {
                throw std::invalid_argument("Environment lighting requires 2 to 128 daily keyframes.");
            }
            double previousPhase = -1.0;
            for (const auto& key : m_Keyframes)
            {
                if (key.Ambient.has_value() != m_Keyframes.front().Ambient.has_value())
                {
                    throw std::invalid_argument("Ambient animation must be enabled on all daily keys or none.");
                }
                if (key.Ambient)
                {
                    ValidateEnvironmentAmbient(*key.Ambient);
                }
                if (!std::isfinite(key.DayFraction) || key.DayFraction < 0.0 || key.DayFraction >= 1.0 ||
                    key.DayFraction <= previousPhase || !std::isfinite(key.AzimuthDegrees) ||
                    key.AzimuthDegrees < 0.0 || key.AzimuthDegrees >= 360.0 || !std::isfinite(key.ElevationDegrees) ||
                    key.ElevationDegrees < -90.0 || key.ElevationDegrees > 90.0 || !std::isfinite(key.Intensity) ||
                    key.Intensity < 0.0 || key.Intensity > std::numeric_limits<float>::max())
                {
                    throw std::invalid_argument("Invalid or unordered environment lighting keyframe.");
                }
                for (const double channel : key.LinearColor)
                {
                    if (!std::isfinite(channel) || channel < 0.0 || channel > 1.0)
                    {
                        throw std::invalid_argument("Environment light color must be linear RGB in [0, 1].");
                    }
                }
                previousPhase = key.DayFraction;
            }
        }

        EnvironmentDirectionalOwnership m_Ownership;
        std::vector<EnvironmentLightingKeyframe> m_Keyframes;
        EnvironmentShadowSettings m_Shadows;
        std::optional<EnvironmentSkyProfile> m_Sky;
        EnvironmentDirectionalFrame m_DirectionFrame = EnvironmentDirectionalFrame::LegacyWorld;
    };
} // namespace Keire::Detail
