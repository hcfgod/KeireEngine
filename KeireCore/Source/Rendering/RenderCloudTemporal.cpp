#include "KeireInternal/Rendering/CloudTemporalRendering.h"
#include "KeireInternal/Rendering/CloudTemporalReprojection.h"
#include "KeireInternal/Rendering/CloudTemporalShaderBindings.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/VolumeShadowUniforms.h"

#include <array>
#include <cstddef>
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
                             SDL_GPUSampler* sampler, const VolumeShadowUniforms& shadows)
    {
        auto& resources = surface.Resources.CloudHistory;
        auto& workset = surface.ActiveWorkset();
        EnsureCloudTemporalResources(state.Device, resources, surface.Width, surface.Height,
                                     state.Specification.MaximumFramesInFlight);
        auto& writer = resources.Writers.at(surface.ActiveWorksetSlot);
        auto* tracePipeline = EnsureCloudTemporalPipeline(state, CloudTemporalPass::Trace);
        auto* resolvePipeline = EnsureCloudTemporalPipeline(state, CloudTemporalPass::Resolve);
        const bool multisample = workset.MultisampleHdrColor != nullptr;
        auto* compositePipeline = EnsureCloudTemporalPipeline(
            state, CloudTemporalPass::Composite, multisample ? surface.ActualSamples : RenderSampleCount::One,
            multisample ? CloudHistoryCompositeMode::Radiance : CloudHistoryCompositeMode::Composite);
        auto* attenuationPipeline =
            multisample ? EnsureCloudTemporalPipeline(state, CloudTemporalPass::Composite, surface.ActualSamples,
                                                      CloudHistoryCompositeMode::Attenuation)
                        : nullptr;
        // Keep binding1 valid without resolving/copying the per-sample background on the MSAA path.
        auto* background = multisample ? workset.SampledDepth : state.EnsureFullscreenInput(surface, true);

        CloudTemporalView current;
        current.Volume = uniforms;
        current.FrameId = packet.AcceptedFrameId;
        current.WorldOrigin = packet.Camera.WorldOrigin;
        current.PlanetFrame = packet.PlanetFrame;
        for (std::size_t index = 0; index < packet.PlanetWeather.size(); ++index)
        {
            current.PlanetFrames[index] = packet.PlanetWeather[index].Frame;
        }
        current.UnjitteredProjection = packet.UnjitteredProjection;
        auto rotationView = packet.Camera.View;
        rotationView.Elements[12] = 0.0F;
        rotationView.Elements[13] = 0.0F;
        rotationView.Elements[14] = 0.0F;
        current.RotationViewProjection = Math::Multiply(packet.Camera.Projection, rotationView);
        // Moving casters change volume lighting without changing environment uniforms. Until shadow
        // content revisions participate in history validation, do not reuse shadowed fog radiance.
        const bool shadowedFog = FogReceivesTerrainSunlight(uniforms.Fog, uniforms.SunRadiance, shadows);
        const auto& historyView = resources.PublishedView ? resources.PublishedView : resources.PreviousExtentView;
        const auto& historyTextures = resources.PublishedView ? resources.Published : resources.PreviousExtent;
        const auto reprojection =
            historyView && historyTextures.Complete() && !shadowedFog
                ? MakeCloudTemporalReprojection(current, *historyView, packet.TemporalHistoryContinuous)
                : std::nullopt;
        const auto& previous = reprojection ? *historyView : current;
        // An uninitialized history is not sampled, but all shader bindings must remain valid.
        const auto& previousTextures = reprojection ? historyTextures : resources.Published;
        const auto resolveUniforms = reprojection.value_or(CloudTemporalResolveUniforms{});
        const std::array<std::uint32_t, 4> traceControls{static_cast<std::uint32_t>(packet.AcceptedFrameId & 1023U),
                                                         reprojection ? 1U : 0U, 0U, 0U};
        const std::array storageBindings{workset.CloudLightCache.Transmission, workset.DynamicUploads.CloudView.Buffer};

        auto* pass = BeginCloudPair(commands, writer.Current);
        const std::array traceBindings{
            SDL_GPUTextureSamplerBinding{workset.SampledDepth, sampler},
            SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler},
            SDL_GPUTextureSamplerBinding{
                workset.DirectionalShadow ? workset.DirectionalShadow : state.EmptyShadowTexture, state.ShadowSampler}};
        SDL_BindGPUGraphicsPipeline(pass, tracePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, traceBindings.data(), static_cast<std::uint32_t>(traceBindings.size()));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, storageBindings.data(),
                                          static_cast<std::uint32_t>(storageBindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, traceControls.data(), sizeof(traceControls));
        SDL_PushGPUFragmentUniformData(commands, 1, &cacheUniforms, sizeof(cacheUniforms));
        SDL_PushGPUFragmentUniformData(commands, 2, &shadows, sizeof(shadows));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);

        pass = BeginCloudPair(commands, writer.History);
        const std::array resolveBindings{SDL_GPUTextureSamplerBinding{writer.Current.RadianceOpacity, sampler},
                                         SDL_GPUTextureSamplerBinding{writer.Current.CloudAndSceneDepth, sampler},
                                         SDL_GPUTextureSamplerBinding{previousTextures.RadianceOpacity, sampler},
                                         SDL_GPUTextureSamplerBinding{previousTextures.CloudAndSceneDepth, sampler}};
        SDL_BindGPUGraphicsPipeline(pass, resolvePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, resolveBindings.data(),
                                    static_cast<std::uint32_t>(resolveBindings.size()));
        constexpr auto resolvedViewBytes =
            static_cast<std::uint32_t>(offsetof(CloudVolumeViewUniforms, PlanetAtmospheres));
        static_assert(resolvedViewBytes <= 4096U);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, resolvedViewBytes);
        SDL_PushGPUFragmentUniformData(commands, 1, &previous.Volume, resolvedViewBytes);
        SDL_PushGPUFragmentUniformData(commands, 2, &resolveUniforms, sizeof(resolveUniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);

        if (!multisample)
        {
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                throw std::runtime_error("SDL_BeginGPUCopyPass(cloud temporal background) failed: " + LastSdlError());
            }
            const SDL_GPUTextureLocation source{workset.HdrColor, 0, 0, 0, 0, 0};
            const SDL_GPUTextureLocation destination{background, 0, 0, 0, 0, 0};
            SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
            SDL_EndGPUCopyPass(copy);
        }
        auto target = CloudTarget(multisample ? workset.MultisampleHdrColor : workset.HdrColor);
        target.load_op = multisample ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
        // STORE retains the original sample coverage for the later transparency pass/final resolve.
        pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(cloud temporal composition) failed: " + LastSdlError());
        }
        const std::array compositeBindings{
            SDL_GPUTextureSamplerBinding{workset.SampledDepth, sampler},
            SDL_GPUTextureSamplerBinding{background, sampler},
            SDL_GPUTextureSamplerBinding{writer.History.RadianceOpacity, sampler},
            SDL_GPUTextureSamplerBinding{writer.History.CloudAndSceneDepth, sampler},
            SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler},
            SDL_GPUTextureSamplerBinding{
                workset.DirectionalShadow ? workset.DirectionalShadow : state.EmptyShadowTexture, state.ShadowSampler}};
        static_assert(compositeBindings.size() == CloudTemporalSamplerCounts[2]);
        static_assert(CloudCompositeShadowSampler + 1U == compositeBindings.size());
        SDL_BindGPUGraphicsPipeline(pass, attenuationPipeline ? attenuationPipeline : compositePipeline);

        SDL_BindGPUFragmentSamplers(pass, 0, compositeBindings.data(),
                                    static_cast<std::uint32_t>(compositeBindings.size()));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, storageBindings.data(),
                                          static_cast<std::uint32_t>(storageBindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 1, &cacheUniforms, sizeof(cacheUniforms));
        SDL_PushGPUFragmentUniformData(commands, CloudCompositeShadowUniform, &shadows, sizeof(shadows));
        if (attenuationPipeline)
        {
            const Vector4 passControls{static_cast<float>(CloudHistoryCompositeMode::Attenuation), 0, 0, 0};
            SDL_BindGPUGraphicsPipeline(pass, attenuationPipeline);
            SDL_PushGPUFragmentUniformData(commands, 0, &passControls, sizeof(passControls));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        const Vector4 passControls{static_cast<float>(multisample ? CloudHistoryCompositeMode::Radiance
                                                                  : CloudHistoryCompositeMode::Composite),
                                   0, 0, 0};
        SDL_BindGPUGraphicsPipeline(pass, compositePipeline);
        SDL_PushGPUFragmentUniformData(commands, 0, &passControls, sizeof(passControls));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        writer.RecordedView = current;
        state.Statistics.Passes += 3U;
        state.Statistics.DrawCalls += multisample ? 4U : 3U;
    }
} // namespace Keire::RenderBackend
