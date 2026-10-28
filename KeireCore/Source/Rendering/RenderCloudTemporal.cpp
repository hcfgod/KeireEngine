#include "KeireInternal/Rendering/CloudTemporalRendering.h"
#include "KeireInternal/Rendering/CloudTemporalReprojection.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>
#include <stdexcept>

namespace Keire::RenderBackend
{
    namespace
    {
        SDL_GPUColorTargetInfo CloudTarget(SDL_GPUTexture* texture)
        {
            SDL_GPUColorTargetInfo target{};
            target.texture = texture;
            target.load_op = SDL_GPU_LOADOP_DONT_CARE;
            target.store_op = SDL_GPU_STOREOP_STORE;
            return target;
        }

        SDL_GPURenderPass* BeginCloudPair(SDL_GPUCommandBuffer* commands, const CloudTemporalTextures& textures)
        {
            const std::array targets{CloudTarget(textures.RadianceOpacity), CloudTarget(textures.CloudAndSceneDepth)};
            auto* pass =
                SDL_BeginGPURenderPass(commands, targets.data(), static_cast<std::uint32_t>(targets.size()), nullptr);
            if (!pass)
            {
                throw std::runtime_error("SDL_BeginGPURenderPass(cloud history) failed: " + LastSdlError());
            }
            return pass;
        }
    } // namespace

    void RecordCloudTemporal(RenderSharedState& state, SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                             const SceneRenderPacket& packet, const CloudVolumeViewUniforms& uniforms,
                             const CloudLightCacheLookupUniforms& cacheUniforms, const GpuTextureResources& noise,
                             SDL_GPUSampler* sampler)
    {
        auto& resources = surface.Resources.CloudHistory;
        auto& workset = surface.ActiveWorkset();
        EnsureCloudTemporalResources(state.Device, resources, surface.Width, surface.Height,
                                     state.Specification.MaximumFramesInFlight);
        auto& writer = resources.Writers.at(surface.ActiveWorksetSlot);
        auto* tracePipeline = EnsureCloudTemporalPipeline(state, CloudTemporalPass::Trace);
        auto* resolvePipeline = EnsureCloudTemporalPipeline(state, CloudTemporalPass::Resolve);
        auto* compositePipeline = EnsureCloudTemporalPipeline(state, CloudTemporalPass::Composite);
        auto* background = state.EnsureFullscreenInput(surface, true);

        CloudTemporalView current;
        current.Volume = uniforms;
        current.FrameId = packet.AcceptedFrameId;
        current.UnjitteredProjection = packet.UnjitteredProjection;
        auto rotationView = packet.Camera.View;
        rotationView.Elements[12] = 0.0F;
        rotationView.Elements[13] = 0.0F;
        rotationView.Elements[14] = 0.0F;
        current.RotationViewProjection = Math::Multiply(packet.Camera.Projection, rotationView);
        const auto reprojection =
            resources.PublishedView
                ? MakeCloudTemporalReprojection(current, *resources.PublishedView, packet.TemporalHistoryContinuous)
                : std::nullopt;
        const auto& previous = reprojection ? *resources.PublishedView : current;
        const auto resolveUniforms = reprojection.value_or(CloudTemporalResolveUniforms{});
        const std::array<std::uint32_t, 4> traceControls{static_cast<std::uint32_t>(packet.AcceptedFrameId & 1023U),
                                                         reprojection ? 1U : 0U, 0U, 0U};

        auto* pass = BeginCloudPair(commands, writer.Current);
        const std::array traceBindings{SDL_GPUTextureSamplerBinding{workset.SampledDepth, sampler},
                                       SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler}};
        SDL_BindGPUGraphicsPipeline(pass, tracePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, traceBindings.data(), static_cast<std::uint32_t>(traceBindings.size()));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &workset.CloudLightCache.Transmission, 1);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 1, &cacheUniforms, sizeof(cacheUniforms));
        SDL_PushGPUFragmentUniformData(commands, 2, traceControls.data(), sizeof(traceControls));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);

        pass = BeginCloudPair(commands, writer.History);
        const std::array resolveBindings{SDL_GPUTextureSamplerBinding{writer.Current.RadianceOpacity, sampler},
                                         SDL_GPUTextureSamplerBinding{writer.Current.CloudAndSceneDepth, sampler},
                                         SDL_GPUTextureSamplerBinding{resources.Published.RadianceOpacity, sampler},
                                         SDL_GPUTextureSamplerBinding{resources.Published.CloudAndSceneDepth, sampler}};
        SDL_BindGPUGraphicsPipeline(pass, resolvePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, resolveBindings.data(),
                                    static_cast<std::uint32_t>(resolveBindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 1, &previous.Volume, sizeof(previous.Volume));
        SDL_PushGPUFragmentUniformData(commands, 2, &resolveUniforms, sizeof(resolveUniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);

        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
        {
            throw std::runtime_error("SDL_BeginGPUCopyPass(cloud temporal background) failed: " + LastSdlError());
        }
        const SDL_GPUTextureLocation source{workset.HdrColor, 0, 0, 0, 0, 0};
        const SDL_GPUTextureLocation destination{background, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
        SDL_EndGPUCopyPass(copy);

        const auto target = CloudTarget(workset.HdrColor);
        pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(cloud temporal composition) failed: " + LastSdlError());
        }
        const std::array compositeBindings{SDL_GPUTextureSamplerBinding{workset.SampledDepth, sampler},
                                           SDL_GPUTextureSamplerBinding{background, sampler},
                                           SDL_GPUTextureSamplerBinding{writer.History.RadianceOpacity, sampler},
                                           SDL_GPUTextureSamplerBinding{writer.History.CloudAndSceneDepth, sampler},
                                           SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler}};
        SDL_BindGPUGraphicsPipeline(pass, compositePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, compositeBindings.data(),
                                    static_cast<std::uint32_t>(compositeBindings.size()));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &workset.CloudLightCache.Transmission, 1);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 1, &cacheUniforms, sizeof(cacheUniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        writer.RecordedView = current;
        state.Statistics.Passes += 3U;
        state.Statistics.DrawCalls += 3U;
    }
} // namespace Keire::RenderBackend
