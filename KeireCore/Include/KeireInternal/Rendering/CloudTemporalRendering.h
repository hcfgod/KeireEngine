#pragma once

#include "KeireInternal/Rendering/CloudTemporalResources.h"

struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;

namespace Keire::RenderBackend
{
    struct RenderSharedState;
    struct RenderSurfaceState;
    struct SceneRenderPacket;
    struct CloudLightCacheLookupUniforms;
    struct GpuTextureResources;

    [[nodiscard]] SDL_GPUGraphicsPipeline* EnsureCloudTemporalPipeline(RenderSharedState& state,
                                                                       CloudTemporalPass pass);

    void RecordCloudTemporal(RenderSharedState& state, SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                             const SceneRenderPacket& packet, const CloudVolumeViewUniforms& uniforms,
                             const CloudLightCacheLookupUniforms& cacheUniforms, const GpuTextureResources& noise,
                             SDL_GPUSampler* sampler);
} // namespace Keire::RenderBackend
