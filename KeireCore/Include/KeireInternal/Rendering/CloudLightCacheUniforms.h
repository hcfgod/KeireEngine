#pragma once

#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Keire::RenderBackend
{
    struct CloudLightCacheUniforms final
    {
        CloudVolumeViewUniforms View;
        Vector4 GridOrigin;
        Vector4 CellSize;
        std::array<std::uint32_t, 4> GridDimensions{};
    };

    static_assert(offsetof(CloudLightCacheUniforms, GridOrigin) == 13296);
    static_assert(offsetof(CloudLightCacheUniforms, CellSize) == 13312);
    static_assert(offsetof(CloudLightCacheUniforms, GridDimensions) == 13328);
    static_assert(sizeof(CloudLightCacheUniforms) == 13344);
} // namespace Keire::RenderBackend
