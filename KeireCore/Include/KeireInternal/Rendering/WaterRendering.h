#pragma once

#include <SDL3/SDL.h>

namespace Keire::RenderBackend
{
    // Returned pipeline belongs to the existing per-device/sample-count PipelineSet lifecycle.
    [[nodiscard]] SDL_GPUGraphicsPipeline* CreateWaterPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat,
                                                               SDL_GPUTextureFormat depthFormat,
                                                               SDL_GPUSampleCount samples);
} // namespace Keire::RenderBackend
