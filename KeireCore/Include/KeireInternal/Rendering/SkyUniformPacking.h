#pragma once

#include "Keire/Rendering/RenderSystem.h"
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
                    const float textureLayout, const double starPhase, const SkyColorUniforms& skyColors)
    {
        const auto atmosphere = MakeAtmosphereUniforms(
            environment.Atmosphere, observer, {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z});
        return {Math::Inverse(camera.Projection),
                Math::Inverse(camera.View),
                {environment.EnvironmentRotationDegrees, environment.EnvironmentSpecularIntensity, environment.Exposure,
                 textureLayout},
                atmosphere,
                cloudsEnabled ? BuildCloudUniforms(environment.Clouds, {observer.X, observer.Y, observer.Z}, cloudPhase)
                              : CloudUniforms{},
                MakeCloudLightingUniforms(environment.AmbientColor, environment.AmbientIntensity,
                                          lighting.ColorAndIntensity, lighting.Enabled, atmosphere.SunHeight),
                MakeStarFieldUniforms(environment.Stars, starPhase),
                skyColors};
    }
} // namespace Keire::RenderBackend
