#pragma once

#include "KeireInternal/Rendering/PlanetAtmosphereSetUniforms.h"
#include "KeireInternal/Rendering/PlanetFogViewUniforms.h"
#include "KeireInternal/Rendering/PlanetWeatherFogComposition.h"
#include "KeireInternal/Rendering/PlanetaryCloudUniforms.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Keire::RenderBackend
{
    struct PlanetVolumeWeatherBodyUniforms
    {
        VolumetricCloudUniforms Volume;
        Vector4 ShapeControls;
        Vector4 RegionalControls;
        Vector4 HeightProfile;
        Vector4 WeatherControls;
        Vector4 LightingControls;
        PlanetaryCloudUniforms Geometry;
        FogViewUniforms Fog;
    };

    struct PlanetVolumeWeatherUniforms
    {
        std::array<std::uint32_t, 4> Controls{};
        std::array<PlanetVolumeWeatherBodyUniforms, MaximumPlanetAtmospheres> Bodies{};
    };

    static_assert(sizeof(PlanetVolumeWeatherBodyUniforms) == 1328);
    static_assert(offsetof(PlanetVolumeWeatherBodyUniforms, Geometry) == 160);
    static_assert(offsetof(PlanetVolumeWeatherBodyUniforms, Fog) == 256);
    static_assert(sizeof(PlanetVolumeWeatherUniforms) == 10640);

    [[nodiscard]] inline PlanetVolumeWeatherUniforms
    MakePlanetVolumeWeatherUniforms(const std::span<const Detail::CapturedPlanetWeather> weather,
                                    const std::span<const FogSettings> fog,
                                    const std::array<double, 3>& observerFromAnchor)
    {
        if (weather.size() > MaximumPlanetAtmospheres || fog.size() != weather.size())
        {
            throw std::invalid_argument("Planet volume weather needs matching bounded weather and fog profiles.");
        }
        PlanetVolumeWeatherUniforms result;
        result.Controls[0] = static_cast<std::uint32_t>(weather.size());
        for (std::size_t index = 0; index < weather.size(); ++index)
        {
            const auto& source = weather[index];
            const auto& volume = source.Clouds.Volume;
            auto& body = result.Bodies[index];
            // Pack authored values without narrowing an astronomical observer to a flat local coordinate.
            body.Volume = MakeVolumetricCloudUniforms(volume, {}, source.Frame.Clock);
            const auto phase =
                MakePlanetaryCloudPhase(source.Frame, volume.PatternSizeMeters, volume.WindMetersPerSecond);
            for (std::size_t axis = 0; axis < phase.size(); ++axis)
            {
                body.Volume.PatternOrigin[axis] = phase[axis];
            }
            body.ShapeControls = {
                static_cast<float>(volume.DetailFrequency), static_cast<float>(volume.VariationStrength),
                static_cast<float>(volume.VariationPeriod), static_cast<float>(volume.DensityContrast)};
            body.RegionalControls = {static_cast<float>(volume.RegionalCoverageStrength),
                                     static_cast<float>(volume.RegionalCoveragePeriod), 0.0F,
                                     1.0F + static_cast<float>(volume.ErosionAmount)};
            body.HeightProfile = {
                static_cast<float>(volume.HeightProfile[0]), static_cast<float>(volume.HeightProfile[1]),
                static_cast<float>(volume.HeightProfile[2]), static_cast<float>(volume.HeightProfile[3])};
            body.WeatherControls = {volume.IndependentWeather ? 1.0F : 0.0F, static_cast<float>(volume.CloudType),
                                    static_cast<float>(volume.TypeVariation),
                                    static_cast<float>(volume.BaseHeightVariationMeters)};
            body.LightingControls = {static_cast<float>(volume.AmbientOcclusionStrength), 0.0F, 0.0F, 0.0F};
            body.Geometry = MakePlanetaryCloudUniforms(source.Frame, volume.BaseHeightMeters);
            body.Geometry.RadiusGeometry.Z = static_cast<float>(volume.MinimumTypeThickness);
            body.Fog = MakePlanetFogViewUniforms(fog[index], observerFromAnchor, source.Frame);
        }
        return result;
    }
} // namespace Keire::RenderBackend
