#pragma once

#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Keire::RenderBackend
{
    struct CloudLightCacheUniforms final
    {
        // The builder consumes one resolved cloud body, not the multi-planet transport arrays.
        // Keep its uniform block within SDL Vulkan's 4 KiB descriptor range.
        std::array<std::byte, offsetof(CloudVolumeViewUniforms, PlanetAtmospheres)> View{};
        Vector4 GridOrigin;
        Vector4 CellSize;
        std::array<std::uint32_t, 4> GridDimensions{};

        CloudLightCacheUniforms(const CloudVolumeViewUniforms& view, const Vector4& gridOrigin, const Vector4& cellSize,
                                const std::array<std::uint32_t, 4>& gridDimensions)
            : GridOrigin(gridOrigin), CellSize(cellSize), GridDimensions(gridDimensions)
        {
            static_assert(std::is_trivially_copyable_v<CloudVolumeViewUniforms>);
            std::memcpy(View.data(), &view, View.size());
        }
    };

    static_assert(offsetof(CloudLightCacheUniforms, GridOrigin) == 1616);
    static_assert(offsetof(CloudLightCacheUniforms, CellSize) == 1632);
    static_assert(offsetof(CloudLightCacheUniforms, GridDimensions) == 1648);
    static_assert(sizeof(CloudLightCacheUniforms) == 1664);
    static_assert(sizeof(CloudLightCacheUniforms) <= 4096);
} // namespace Keire::RenderBackend
