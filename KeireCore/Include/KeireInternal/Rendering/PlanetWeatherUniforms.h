#pragma once

#include "KeireInternal/Rendering/CloudUniforms.h"
#include "KeireInternal/Rendering/PlanetAtmosphereSetUniforms.h"
#include "KeireInternal/Rendering/PlanetWeatherCapture.h"

namespace Keire::RenderBackend
{
    struct PlanetWeatherBodyUniforms
    {
        CloudUniforms Clouds;
        PlanetaryCloudUniforms Geometry;
        std::array<Vector4, MaximumCloudLayers> Phase;
    };

    struct PlanetWeatherUniforms
    {
        std::array<std::uint32_t, 4> Controls{};
        std::array<PlanetWeatherBodyUniforms, MaximumPlanetAtmospheres> Bodies{};
    };

    static_assert(sizeof(PlanetWeatherBodyUniforms) == 256);
    static_assert(sizeof(PlanetWeatherUniforms) == 2064);

    [[nodiscard]] inline PlanetWeatherUniforms
    MakePlanetWeatherUniforms(const std::span<const Detail::CapturedPlanetWeather> weather)
    {
        if (weather.size() > MaximumPlanetAtmospheres)
        {
            throw std::length_error("Planet weather uniforms exceed the supported candidate count.");
        }
        PlanetWeatherUniforms result;
        result.Controls[0] = static_cast<std::uint32_t>(weather.size());
        for (std::size_t index = 0; index < weather.size(); ++index)
        {
            const auto& planet = weather[index];
            result.Bodies[index] = {BuildCloudUniforms(planet.Clouds, {}, 0.0),
                                    MakePlanetaryCloudUniforms(planet.Frame, 0.0),
                                    MakePlanetaryCloudLayerPhases(planet.Clouds, planet.Frame)};
        }
        return result;
    }
} // namespace Keire::RenderBackend
