#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Rendering/Atmosphere.h"

#include <algorithm>
#include <cmath>

namespace Keire::RenderBackend
{
    /// Kilometres keep the shader's optical depths dimensionless without large metre-valued products.
    struct AtmosphereUniforms final
    {
        Vector4 ObserverRadius;
        Vector4 SunHeight;
        Vector4 RayleighScale;
        Vector4 Mie;
        Vector4 AbsorptionPeak;
        Vector4 SolarWidth;
        Vector4 Controls;
    };

    static_assert(sizeof(AtmosphereUniforms) == sizeof(float) * 28);

    [[nodiscard]] inline AtmosphereUniforms MakeAtmosphereUniforms(const AtmosphereSettings& settings,
                                                                   const Vector3 observer,
                                                                   const Vector3 lightDirection) noexcept
    {
        constexpr double MetresPerKilometre = 1000.0;
        const auto kilometres = [](const double value) { return static_cast<float>(value / MetresPerKilometre); };
        const auto perKilometre = [](const double value) { return static_cast<float>(value * MetresPerKilometre); };
        // Subtract before narrowing; this does not restore precision already lost in a float scene transform.
        AtmosphereUniforms result{};
        result.ObserverRadius = {kilometres(static_cast<double>(observer.X) - settings.PlanetCenter[0]),
                                 kilometres(static_cast<double>(observer.Y) - settings.PlanetCenter[1]),
                                 kilometres(static_cast<double>(observer.Z) - settings.PlanetCenter[2]),
                                 kilometres(settings.PlanetRadius)};
        const double directionLength =
            std::hypot(static_cast<double>(lightDirection.X), static_cast<double>(lightDirection.Y),
                       static_cast<double>(lightDirection.Z));
        if (directionLength > 0.0 && std::isfinite(directionLength))
        {
            result.SunHeight = {static_cast<float>(-lightDirection.X / directionLength),
                                static_cast<float>(-lightDirection.Y / directionLength),
                                static_cast<float>(-lightDirection.Z / directionLength), kilometres(settings.Height)};
        }
        else
        {
            result.SunHeight = {0.0F, 1.0F, 0.0F, kilometres(settings.Height)};
        }
        result.RayleighScale = {perKilometre(settings.RayleighScattering[0]),
                                perKilometre(settings.RayleighScattering[1]),
                                perKilometre(settings.RayleighScattering[2]), kilometres(settings.RayleighScaleHeight)};
        result.Mie = {perKilometre(settings.MieScattering), perKilometre(settings.MieExtinction),
                      static_cast<float>(settings.MieAnisotropy), kilometres(settings.MieScaleHeight)};
        result.AbsorptionPeak = {
            perKilometre(settings.AbsorptionExtinction[0]), perKilometre(settings.AbsorptionExtinction[1]),
            perKilometre(settings.AbsorptionExtinction[2]), kilometres(settings.AbsorptionPeakAltitude)};
        result.SolarWidth = {static_cast<float>(settings.SolarIrradiance[0]),
                             static_cast<float>(settings.SolarIrradiance[1]),
                             static_cast<float>(settings.SolarIrradiance[2]), kilometres(settings.AbsorptionHalfWidth)};
        result.Controls = {settings.Enabled ? 1.0F : 0.0F,
                           static_cast<float>(std::clamp(settings.ViewSamples, 4U, 64U)),
                           static_cast<float>(std::clamp(settings.LightSamples, 2U, 32U)),
                           static_cast<float>(settings.SunAngularRadius)};
        return result;
    }
} // namespace Keire::RenderBackend
