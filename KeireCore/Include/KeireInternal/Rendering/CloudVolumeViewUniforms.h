#pragma once

#include "Keire/Math/Math.h"
#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Rendering/FogViewUniforms.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <cstddef>
#include <cstdint>

namespace Keire::RenderBackend
{
    struct SceneRenderPacket;

    struct CloudVolumeViewUniforms final
    {
        Matrix4 InverseProjection;
        Matrix4 InverseView;
        VolumetricCloudUniforms Volume;
        Vector4 SunDirection;
        Vector4 SunRadiance;
        /// RGB incident sky radiance; W blends cloud ambient occlusion without changing optical density.
        Vector4 AmbientRadiance;
        /// Orthographic flag, scene exposure, inverse target width and height.
        Vector4 CameraControls;
        /// Detail frequency, variation strength, variation period and density contrast. Appended for ABI compatibility.
        Vector4 ShapeControls;
        /// Regional strength/period; Z selects background attenuation, W stores one plus erosion. ABI 4 prefix.
        Vector4 RegionalControls;
        /// Finite RGB air transport, appended in volume ABI 5.
        AtmosphereUniforms Atmosphere;
        /// Normalized base and crown ramps, appended in volume ABI 6.
        Vector4 HeightProfile;
        /// Independent weather enabled, cloud type, type variation and base-height variation in metres. ABI 7.
        Vector4 WeatherControls;
        /// Height fog and local boxes, appended in volume ABI 8.
        FogViewUniforms Fog;
    };

    static_assert(sizeof(CloudVolumeViewUniforms) == 1520);
    static_assert(offsetof(CloudVolumeViewUniforms, Fog) == 448);
    static_assert(offsetof(CloudVolumeViewUniforms, Volume) == 128);
    static_assert(offsetof(CloudVolumeViewUniforms, CameraControls) == 256);
    static_assert(offsetof(CloudVolumeViewUniforms, ShapeControls) == 272);
    static_assert(offsetof(CloudVolumeViewUniforms, RegionalControls) == 288);
    static_assert(offsetof(CloudVolumeViewUniforms, Atmosphere) == 304);

    [[nodiscard]] constexpr std::uint32_t CloudVolumeUniformByteSize(const std::uint32_t abiVersion) noexcept
    {
        return abiVersion >= 9U   ? 1520U
               : abiVersion >= 8U ? 1008U
               : abiVersion >= 7U ? 448U
               : abiVersion >= 6U ? 432U
               : abiVersion >= 5U ? 416U
               : abiVersion >= 4U ? 304U
               : abiVersion >= 3U ? 288U
                                  : 272U;
    }

    [[nodiscard]] CloudVolumeViewUniforms MakeCloudVolumeViewUniforms(const SceneRenderPacket& packet,
                                                                      std::uint32_t width, std::uint32_t height);
} // namespace Keire::RenderBackend
