#pragma once

#include "KeireInternal/Rendering/CloudHistoryCompositePolicy.h"

#include <SDL3/SDL_gpu.h>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline SDL_GPUColorTargetBlendState CloudHistoryCompositeBlend(const CloudHistoryCompositeMode mode)
    {
        SDL_GPUColorTargetBlendState blend{};
        if (mode == CloudHistoryCompositeMode::Composite)
        {
            return blend;
        }
        if (mode != CloudHistoryCompositeMode::Attenuation && mode != CloudHistoryCompositeMode::Radiance)
        {
            throw std::invalid_argument("Invalid cloud history blend mode.");
        }
        const bool attenuation = mode == CloudHistoryCompositeMode::Attenuation;
        blend.enable_blend = true;
        blend.src_color_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_SRC_COLOR : SDL_GPU_BLENDFACTOR_ONE;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        return blend;
    }
} // namespace Keire::RenderBackend
