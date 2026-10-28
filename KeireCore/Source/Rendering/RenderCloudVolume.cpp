#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Rendering/CloudLightingUniforms.h"
#include "KeireInternal/Rendering/CloudTemporalRendering.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    CloudVolumeViewUniforms MakeCloudVolumeViewUniforms(const SceneRenderPacket& packet, const std::uint32_t width,
                                                        const std::uint32_t height)
    {
        if ((packet.CloudVolume.Quality[0] == 0U && !packet.Environment.Atmosphere.Enabled) || width == 0U ||
            height == 0U)
        {
            return {};
        }
        const auto& camera = packet.Camera;
        const auto observer = Math::TransformPoint(Math::Inverse(camera.View), {});
        const auto atmosphere = MakeAtmosphereUniforms(
            packet.Environment.Atmosphere, observer,
            {packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z});
        const auto light =
            MakeCloudLightingUniforms(packet.Environment.AmbientColor, packet.Environment.AmbientIntensity,
                                      packet.Lighting.ColorAndIntensity, packet.Lighting.Enabled, atmosphere.SunHeight);
        return {Math::Inverse(camera.Projection),
                Math::Inverse(camera.View),
                packet.CloudVolume,
                light.SunDirection,
                light.Sun,
                {light.Ambient.X, light.Ambient.Y, light.Ambient.Z,
                 static_cast<float>(packet.Environment.Clouds.Volume.AmbientOcclusionStrength)},
                {camera.Projection.Elements[15] == 1.0F ? 1.0F : 0.0F, packet.Environment.Exposure,
                 1.0F / static_cast<float>(width), 1.0F / static_cast<float>(height)},
                {static_cast<float>(packet.Environment.Clouds.Volume.DetailFrequency),
                 static_cast<float>(packet.Environment.Clouds.Volume.VariationStrength),
                 static_cast<float>(packet.Environment.Clouds.Volume.VariationPeriod),
                 static_cast<float>(packet.Environment.Clouds.Volume.DensityContrast)},
                {static_cast<float>(packet.Environment.Clouds.Volume.RegionalCoverageStrength),
                 static_cast<float>(packet.Environment.Clouds.Volume.RegionalCoveragePeriod), 0.0F,
                 1.0F + static_cast<float>(packet.Environment.Clouds.Volume.ErosionAmount)},
                atmosphere,
                {static_cast<float>(packet.Environment.Clouds.Volume.HeightProfile[0]),
                 static_cast<float>(packet.Environment.Clouds.Volume.HeightProfile[1]),
                 static_cast<float>(packet.Environment.Clouds.Volume.HeightProfile[2]),
                 static_cast<float>(packet.Environment.Clouds.Volume.HeightProfile[3])},
                {packet.Environment.Clouds.Volume.IndependentWeather ? 1.0F : 0.0F,
                 static_cast<float>(packet.Environment.Clouds.Volume.CloudType),
                 static_cast<float>(packet.Environment.Clouds.Volume.TypeVariation),
                 static_cast<float>(packet.Environment.Clouds.Volume.BaseHeightVariationMeters)}};
    }

    void RenderSharedState::RecordCloudVolume(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                              const SceneRenderPacket& packet)
    {
        if (packet.CloudVolume.Quality[0] == 0U && !packet.Environment.Atmosphere.Enabled)
        {
            return;
        }
        auto& workset = surface.ActiveWorkset();
        if (!commands || !workset.HdrColor || !workset.SampledDepth || !surface.SampledDepthValid)
        {
            throw std::logic_error("Cloud volume recording requires current sampled depth and an HDR target.");
        }
        auto uniforms = MakeCloudVolumeViewUniforms(packet, surface.Width, surface.Height);
        const SamplerDescription samplerDescription{TextureFilter::Nearest,    TextureFilter::Nearest,
                                                    TextureFilter::Nearest,    TextureAddressMode::Clamp,
                                                    TextureAddressMode::Clamp, TextureAddressMode::Clamp};
        auto* sampler = ResolveSampler(samplerDescription);
        const auto& noise = EnsureCloudNoiseTexture();
        const auto cacheUniforms = RecordCloudLightCache(Device, commands, workset.CloudLightCache, uniforms, noise,
                                                         packet.CloudBasePatternPhase);
        if (workset.CloudLightCache.Validity->RecordedKey)
        {
            ++Statistics.CloudLightCacheBuilds;
            Statistics.CloudLightCacheCells += static_cast<std::uint64_t>(cacheUniforms.GridDimensions[0]) *
                                               cacheUniforms.GridDimensions[1] * cacheUniforms.GridDimensions[2];
            PendingCloudLightCaches.Track(workset.CloudLightCache.Validity);
            ++Statistics.Passes;
        }
        else if (packet.CloudVolume.Quality[0] != 0U && cacheUniforms.GridDimensions[0] != 0U)
        {
            ++Statistics.CloudLightCacheReuses;
        }
        const auto samples =
            workset.MultisampleHdrColor ? ToSdlSampleCount(surface.ActualSamples) : SDL_GPU_SAMPLECOUNT_1;
        if (!workset.MultisampleHdrColor && packet.CloudVolume.Quality[0] != 0U)
        {
            RecordCloudTemporal(*this, commands, surface, packet, uniforms, cacheUniforms, noise, sampler);
            return;
        }
        const bool composite = !workset.MultisampleHdrColor;
        // A resolved background copy would discard per-sample edge colors. Keep RGB blending for MSAA targets.
        const auto finalPass = composite ? CloudVolumePass::Composite : CloudVolumePass::Radiance;
        auto* finalPipeline = EnsureCloudVolumePipeline(samples, finalPass);
        auto* attenuationPipeline =
            composite ? nullptr : EnsureCloudVolumePipeline(samples, CloudVolumePass::Attenuation);
        // The unused third binding in the MSAA path must still be a valid 2D texture distinct from its target.
        auto* background = composite ? EnsureFullscreenInput(surface, true) : workset.SampledDepth;
        if (composite)
        {
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                throw std::runtime_error("SDL_BeginGPUCopyPass(cloud background) failed: " + LastSdlError());
            }
            const SDL_GPUTextureLocation source{workset.HdrColor, 0, 0, 0, 0, 0};
            const SDL_GPUTextureLocation destination{background, 0, 0, 0, 0, 0};
            SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
            SDL_EndGPUCopyPass(copy);
        }

        SDL_GPUColorTargetInfo target{};
        target.texture = workset.MultisampleHdrColor ? workset.MultisampleHdrColor : workset.HdrColor;
        target.load_op = composite ? SDL_GPU_LOADOP_DONT_CARE : SDL_GPU_LOADOP_LOAD;
        // Preserve multisample color for the following transparency pass, which owns the final resolve.
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(cloud volume) failed: " + LastSdlError());
        }
        const std::array bindings{SDL_GPUTextureSamplerBinding{workset.SampledDepth, sampler},
                                  SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler},
                                  SDL_GPUTextureSamplerBinding{background, sampler}};
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &workset.CloudLightCache.Transmission, 1);
        SDL_PushGPUFragmentUniformData(commands, 1, &cacheUniforms, sizeof(cacheUniforms));
        if (attenuationPipeline)
        {
            SDL_BindGPUGraphicsPipeline(pass, attenuationPipeline);
            uniforms.RegionalControls.Z = static_cast<float>(CloudVolumePass::Attenuation);
            SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        SDL_BindGPUGraphicsPipeline(pass, finalPipeline);
        uniforms.RegionalControls.Z = static_cast<float>(finalPass);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.DrawCalls += composite ? 1U : 2U;
    }

    SDL_GPUShader* RenderSharedState::CreateCloudVolumeShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0U : 3U;
        information.num_uniform_buffers = vertex ? 0U : 2U;
        information.num_storage_buffers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinCloudVolumeVertexDxil : Detail::BuiltinCloudVolumeFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinCloudVolumeVertexDxilSize : Detail::BuiltinCloudVolumeFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinCloudVolumeVertexMsl : Detail::BuiltinCloudVolumeFragmentMsl;
            information.code_size =
                vertex ? Detail::BuiltinCloudVolumeVertexMslSize : Detail::BuiltinCloudVolumeFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinCloudVolumeVertexSpirV : Detail::BuiltinCloudVolumeFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinCloudVolumeVertexSpirVSize : Detail::BuiltinCloudVolumeFragmentSpirVSize;
        }
        else
        {
            throw std::runtime_error("The active GPU backend exposes no supported cloud-volume shader format.");
        }
        auto* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
        {
            throw std::runtime_error("SDL_CreateGPUShader(cloud volume) failed: " + LastSdlError());
        }
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::EnsureCloudVolumePipeline(const SDL_GPUSampleCount samples,
                                                                          const CloudVolumePass pass)
    {
        const bool composite = pass == CloudVolumePass::Composite;
        const bool attenuation = pass == CloudVolumePass::Attenuation;
        const auto sampleIndex = static_cast<std::size_t>(samples);
        const auto pipelineIndex = CloudVolumePipelineIndex(sampleIndex, pass);
        if (!pipelineIndex)
        {
            throw std::invalid_argument("Unsupported cloud-volume pass or sample count.");
        }
        const auto index = *pipelineIndex;
        if (CloudVolumePipelines[index])
        {
            return CloudVolumePipelines[index];
        }
        const auto releaseShader = [this](SDL_GPUShader* shader) noexcept { SDL_ReleaseGPUShader(Device, shader); };
        using ShaderOwner = std::unique_ptr<SDL_GPUShader, decltype(releaseShader)>;
        try
        {
            ShaderOwner vertex(CreateCloudVolumeShader(true), releaseShader);
            ShaderOwner fragment(CreateCloudVolumeShader(false), releaseShader);
            SDL_GPUColorTargetDescription color{};
            color.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            color.blend_state.enable_blend = !composite;
            color.blend_state.src_color_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_color_blendfactor =
                attenuation ? SDL_GPU_BLENDFACTOR_SRC_COLOR : SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = attenuation ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor =
                attenuation ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex.get();
            information.fragment_shader = fragment.get();
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = samples;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            auto* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
            {
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(cloud volume) failed: " + LastSdlError());
            }
            CloudVolumePipelines[index] = pipeline;
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("cloud volume pipeline creation");
            throw;
        }
    }
} // namespace Keire::RenderBackend
