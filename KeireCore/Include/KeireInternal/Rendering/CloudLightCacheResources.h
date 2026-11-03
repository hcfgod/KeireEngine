#pragma once

#include "KeireInternal/Rendering/CloudLightCachePublication.h"
#include "KeireInternal/Rendering/CloudLightCacheUniforms.h"
#include "KeireInternal/Rendering/GpuTextureResources.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace Keire::RenderBackend
{
    inline constexpr std::size_t CloudLightCacheLevelCount = 3;
    inline constexpr std::uint32_t CloudLightCacheLevelCapacity = 129U * 33U * 129U;

    struct CloudLightCacheResources final
    {
        SDL_GPUBuffer* Transmission = nullptr;
        SDL_GPUComputePipeline* Builder = nullptr;
        std::array<std::shared_ptr<CloudLightCacheValidity>, CloudLightCacheLevelCount> Validity;

        [[nodiscard]] bool Empty() const noexcept { return !Transmission && !Builder; }
    };

    struct CloudLightCacheLevelUniforms final
    {
        Vector4 GridOrigin;
        Vector4 CellSize;
        std::array<std::uint32_t, 4> GridDimensions{};
    };

    struct CloudLightCacheLookupUniforms final
    {
        std::array<CloudLightCacheLevelUniforms, CloudLightCacheLevelCount> Levels{};
    };

    static_assert(sizeof(CloudLightCacheLevelUniforms) == 48);
    static_assert(sizeof(CloudLightCacheLookupUniforms) == 144);

    void ReleaseCloudLightCache(SDL_GPUDevice* device, CloudLightCacheResources& resources) noexcept;

    // Only call after successful submission of the command buffer that recorded this cache.
    void SubmitCloudLightCache(CloudLightCacheResources& resources) noexcept;

    // A failed/abandoned recording may have overwritten the buffer; discard both validity states.
    void InvalidateCloudLightCache(CloudLightCacheResources& resources) noexcept;

    [[nodiscard]] CloudLightCacheLookupUniforms
    RecordCloudLightCache(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands, CloudLightCacheResources& resources,
                          const CloudVolumeViewUniforms& view, const GpuTextureResources& noise,
                          std::optional<float> cloudBasePatternPhase = std::nullopt,
                          const Detail::ResolvedPlanetFrame* planetFrame = nullptr);
} // namespace Keire::RenderBackend
