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

    static_assert(offsetof(CloudLightCacheUniforms, GridOrigin) == 1520);
    static_assert(offsetof(CloudLightCacheUniforms, CellSize) == 1536);
    static_assert(offsetof(CloudLightCacheUniforms, GridDimensions) == 1552);
    static_assert(sizeof(CloudLightCacheUniforms) == 1568);
} // namespace Keire::RenderBackend
