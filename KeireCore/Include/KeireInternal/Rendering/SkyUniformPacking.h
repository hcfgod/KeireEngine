#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/EnvironmentViewCoordinates.h"
#include "KeireInternal/Rendering/PlanetAtmosphereUniforms.h"
#include "KeireInternal/Rendering/RenderSceneRecordingTypesInternal.h"
#include "KeireInternal/Rendering/RenderShaderDataInternal.h"

#include <array>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline bool HasProceduralSky(const RenderEnvironmentSettings& environment) noexcept
    {
        return environment.Atmosphere.Enabled || environment.Stars.Enabled;
    }

    [[nodiscard]] inline SkyUniforms
    MakeSkyUniforms(const RenderCamera& camera, const RenderEnvironmentSettings& environment,
                    const SceneLighting& lighting, const Vector3 observer,
                    const std::array<std::array<double, 2>, MaximumCloudLayers>& cloudPhase, const bool cloudsEnabled,
                    const float textureLayout, const double starPhase, const SkyColorUniforms& skyColors,
                    const Vector3d renderOrigin = {}, const Vector3d environmentAnchor = {},
                    const Detail::ResolvedPlanetFrame* planetFrame = nullptr)
    {
        const auto atmosphere =
            planetFrame
                ? MakePlanetAtmosphereUniforms(environment.Atmosphere,
                                               {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z},
                                               *planetFrame)
                : MakeAtmosphereUniforms(environment.Atmosphere, observer,
                                         {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z},
                                         renderOrigin, environmentAnchor);
        const auto observerFromAnchor = EnvironmentRelativeObserver(observer, renderOrigin, environmentAnchor);
        return {Math::Inverse(camera.Projection),
                Math::Inverse(camera.View),
                {environment.EnvironmentRotationDegrees, environment.EnvironmentSpecularIntensity, environment.Exposure,
                 textureLayout},
                atmosphere,
                cloudsEnabled ? (planetFrame ? BuildCloudUniforms(environment.Clouds, {}, 0.0)
                                             : BuildCloudUniforms(environment.Clouds, observerFromAnchor, cloudPhase))
                              : CloudUniforms{},
                MakeCloudLightingUniforms(environment.AmbientColor, environment.AmbientIntensity,
                                          lighting.ColorAndIntensity, lighting.Enabled, atmosphere.SunHeight),
                MakeStarFieldUniforms(environment.Stars, starPhase),
                skyColors,
                {static_cast<float>(environment.Atmosphere.GroundReflectance[0]),
                 static_cast<float>(environment.Atmosphere.GroundReflectance[1]),
                 static_cast<float>(environment.Atmosphere.GroundReflectance[2]),
                 static_cast<float>(environment.Atmosphere.GroundIntensity)},
                planetFrame ? MakePlanetaryCloudUniforms(*planetFrame, 0.0) : PlanetaryCloudUniforms{},
                planetFrame ? MakePlanetaryCloudLayerPhases(environment.Clouds, *planetFrame)
                            : std::array<Vector4, MaximumCloudLayers>{},
                MakeStarFieldOrientation(planetFrame ? planetFrame->RootToObserver : Detail::FrameRotation{})};
    }
} // namespace Keire::RenderBackend
