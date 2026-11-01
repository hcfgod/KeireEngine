#pragma once

#include <SDL3/SDL_gpu.h>

namespace Keire::RenderBackend
{
    template <typename MaterialCache>
    void RefreshMaterialTextureBindings(MaterialCache& materials, SDL_GPUTexture* previous,
                                        const SDL_GPUTextureSamplerBinding replacement) noexcept
    {
        if (!previous)
        {
            return;
        }

        for (auto& [materialId, material] : materials)
        {
            (void)materialId;
            for (auto& binding : material.Bindings)
            {
                for (auto& samplerBinding : binding.Binding.Textures)
                {
                    if (samplerBinding.texture == previous)
                    {
                        samplerBinding = replacement;
                    }
                }
            }
        }
    }
} // namespace Keire::RenderBackend
