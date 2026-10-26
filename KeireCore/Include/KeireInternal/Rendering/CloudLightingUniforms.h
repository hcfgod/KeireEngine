#pragma once

#include "Keire/Math/Math.h"

#include <cstddef>

namespace Keire::RenderBackend
{
    struct CloudLightingUniforms final
    {
        Vector4 Ambient;
        Vector4 Sun;
        Vector4 SunDirection;
    };

    static_assert(sizeof(CloudLightingUniforms) == sizeof(float) * 12);
    static_assert(offsetof(CloudLightingUniforms, Sun) == sizeof(float) * 4);
    static_assert(offsetof(CloudLightingUniforms, SunDirection) == sizeof(float) * 8);

    /// Pack evaluated linear lighting, before exposure. Direction is already normalized by atmosphere packing.
    [[nodiscard]] inline CloudLightingUniforms
    MakeCloudLightingUniforms(const Color ambientColor, const float ambientIntensity, const Color sunColorIntensity,
                              const bool sunEnabled, const Vector4 directionToSun) noexcept
    {
        const float sunIntensity = sunEnabled ? sunColorIntensity.Alpha : 0.0F;
        return {{ambientColor.Red * ambientIntensity, ambientColor.Green * ambientIntensity,
                 ambientColor.Blue * ambientIntensity, 0.0F},
                {sunColorIntensity.Red * sunIntensity, sunColorIntensity.Green * sunIntensity,
                 sunColorIntensity.Blue * sunIntensity, 0.0F},
                {directionToSun.X, directionToSun.Y, directionToSun.Z, 0.0F}};
    }
} // namespace Keire::RenderBackend
