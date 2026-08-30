#pragma once

#include "Keire/Assets/Asset.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

namespace Keire::RenderBackend
{
    struct PreparedRuntimeUiTextureBinding final
    {
        AssetId Asset;
        SDL_GPUTextureSamplerBinding Binding{};
        std::uint64_t Frame = 0;
        std::uint32_t DeviceGeneration = 0;
    };

    struct RuntimeUiRenderTextureCacheEntry final
    {
        AssetId Asset;
        SDL_GPUTexture* Published = nullptr;
        std::vector<SDL_GPUTexture*> Writers;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint64_t LastUsedFrame = 0;
        bool HasPublished = false;
    };

    struct RuntimeUiFontAtlasCacheEntry final
    {
        SDL_GPUTexture* Texture = nullptr;
        std::uint32_t DeviceGeneration = 0;
        std::uint64_t BuildCount = 0;
    };

    [[nodiscard]] inline bool RuntimeUiFontAtlasCacheValid(const RuntimeUiFontAtlasCacheEntry& cache,
                                                           const std::uint32_t deviceGeneration) noexcept
    {
        return cache.Texture && cache.DeviceGeneration == deviceGeneration;
    }

} // namespace Keire::RenderBackend
