#pragma once

#include "Keire/Rendering/EnvironmentLighting.h"

#include <cmath>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateEnvironmentShadows(const EnvironmentShadowSettings& shadows)
    {
        if (shadows.Quality != ShadowQuality::Disabled && shadows.Quality != ShadowQuality::Hard &&
            shadows.Quality != ShadowQuality::Soft)
        {
            throw std::invalid_argument("Unknown environment shadow quality.");
        }
        if (shadows.Resolution != ShadowResolutionHint::Low && shadows.Resolution != ShadowResolutionHint::Medium &&
            shadows.Resolution != ShadowResolutionHint::High && shadows.Resolution != ShadowResolutionHint::VeryHigh)
        {
            throw std::invalid_argument("Unknown environment shadow resolution hint.");
        }
        if (!std::isfinite(shadows.Strength) || shadows.Strength < 0.0F || shadows.Strength > 1.0F ||
            !std::isfinite(shadows.Bias) || shadows.Bias < 0.0F || shadows.Bias > 1.0F)
        {
            throw std::invalid_argument("Environment shadow strength and bias must be finite in [0, 1].");
        }
    }

    inline void ValidateEnvironmentAmbient(const EnvironmentAmbientLight& ambient)
    {
        for (const auto channel : ambient.LinearColor)
        {
            if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F)
            {
                throw std::invalid_argument("Environment ambient color must be finite linear RGB in [0, 1].");
            }
        }
        if (!std::isfinite(ambient.Intensity) || ambient.Intensity < 0.0F || ambient.Intensity > 16.0F)
        {
            throw std::invalid_argument("Environment ambient intensity must be finite in [0, 16].");
        }
    }

    inline void ValidateEnvironmentLighting(const std::optional<EnvironmentLightingSnapshot>& snapshot)
    {
        if (!snapshot)
        {
            return;
        }
        if (!snapshot->Source || snapshot->Revision == 0)
        {
            throw std::invalid_argument("Environment lighting requires a live source and nonzero revision.");
        }
        if (snapshot->Ambient)
        {
            ValidateEnvironmentAmbient(*snapshot->Ambient);
        }
        if (snapshot->Time && (!std::isfinite(snapshot->Time->DayFraction) || snapshot->Time->DayFraction < 0.0 ||
                               snapshot->Time->DayFraction >= 1.0 || !std::isfinite(snapshot->Time->SecondsPerDay) ||
                               snapshot->Time->SecondsPerDay <= 0.0))
        {
            throw std::invalid_argument("Environment time requires a valid fraction and positive finite day length.");
        }
        if (!snapshot->Directional)
        {
            return;
        }
        const auto& light = *snapshot->Directional;
        ValidateEnvironmentShadows(light.Shadows);
        double lengthSquared = 0.0;
        for (const auto component : light.Direction)
        {
            if (!std::isfinite(component))
            {
                throw std::invalid_argument("Environment light direction must be finite.");
            }
            lengthSquared += static_cast<double>(component) * component;
        }
        constexpr double unitLengthTolerance = 0.0001;
        if (std::abs(lengthSquared - 1.0) > unitLengthTolerance)
        {
            throw std::invalid_argument("Environment light direction must be normalized.");
        }
        for (const auto channel : light.LinearColor)
        {
            if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F)
            {
                throw std::invalid_argument("Environment light color must be finite linear RGB in [0, 1].");
            }
        }
        if (!std::isfinite(light.Intensity) || light.Intensity < 0.0F)
        {
            throw std::invalid_argument("Environment light intensity must be finite and nonnegative.");
        }
    }
} // namespace Keire::Detail
