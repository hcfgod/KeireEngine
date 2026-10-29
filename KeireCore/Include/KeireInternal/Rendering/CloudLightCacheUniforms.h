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

    static_assert(offsetof(CloudLightCacheUniforms, GridOrigin) == 1616);
    static_assert(offsetof(CloudLightCacheUniforms, CellSize) == 1632);
    static_assert(offsetof(CloudLightCacheUniforms, GridDimensions) == 1648);
    static_assert(sizeof(CloudLightCacheUniforms) == 1664);
} // namespace Keire::RenderBackend
