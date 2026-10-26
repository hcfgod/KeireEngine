#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Rendering/CloudLightingUniforms.h"

#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::RecordCloudVolume(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                              const SceneRenderPacket& packet)
    {
        if (packet.CloudVolume.Quality[0] == 0U)
        {
            return;
        }
        const auto& workset = surface.ActiveWorkset();
        if (!commands || !workset.HdrColor || !workset.SampledDepth || !surface.SampledDepthValid ||
            workset.MultisampleHdrColor)
        {
            throw std::logic_error(
                "Cloud volume recording requires current sampled depth and a single-sample HDR target.");
        }
        struct Uniforms
        {
            Matrix4 InverseProjection;
            Matrix4 InverseView;
            VolumetricCloudUniforms Volume;
            Vector4 SunDirection;
            Vector4 SunRadiance;
            Vector4 AmbientRadiance;
            Vector4 CameraControls;
        };
        static_assert(sizeof(Uniforms) == 272);
        const auto& camera = packet.Camera;
        const auto observer = Math::TransformPoint(Math::Inverse(camera.View), {});
        const auto atmosphere = MakeAtmosphereUniforms(
            packet.Environment.Atmosphere, observer,
            {packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z});
        const auto light =
            MakeCloudLightingUniforms(packet.Environment.AmbientColor, packet.Environment.AmbientIntensity,
                                      packet.Lighting.ColorAndIntensity, packet.Lighting.Enabled, atmosphere.SunHeight);
        const Uniforms uniforms{Math::Inverse(camera.Projection),
                                Math::Inverse(camera.View),
                                packet.CloudVolume,
                                light.SunDirection,
                                light.Sun,
                                light.Ambient,
                                {camera.Projection.Elements[15] == 1.0F ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F}};
        const SamplerDescription samplerDescription{TextureFilter::Nearest,    TextureFilter::Nearest,
                                                    TextureFilter::Nearest,    TextureAddressMode::Clamp,
                                                    TextureAddressMode::Clamp, TextureAddressMode::Clamp};
        auto* sampler = ResolveSampler(samplerDescription);
        EnsureCloudVolumePipeline();
        SDL_GPUColorTargetInfo target{};
        target.texture = workset.HdrColor;
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(cloud volume) failed: " + LastSdlError());
        }
        const SDL_GPUTextureSamplerBinding binding{workset.SampledDepth, sampler};
        SDL_BindGPUGraphicsPipeline(pass, CloudVolumePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        ++Statistics.DrawCalls;
    }

    SDL_GPUShader* RenderSharedState::CreateCloudVolumeShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0U : 1U;
        information.num_uniform_buffers = vertex ? 0U : 1U;
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

    void RenderSharedState::EnsureCloudVolumePipeline()
    {
        if (CloudVolumePipeline)
        {
            return;
        }
        const auto releaseShader = [this](SDL_GPUShader* shader) noexcept { SDL_ReleaseGPUShader(Device, shader); };
        using ShaderOwner = std::unique_ptr<SDL_GPUShader, decltype(releaseShader)>;
        try
        {
            ShaderOwner vertex(CreateCloudVolumeShader(true), releaseShader);
            ShaderOwner fragment(CreateCloudVolumeShader(false), releaseShader);
            SDL_GPUColorTargetDescription color{};
            color.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            color.blend_state.enable_blend = true;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex.get();
            information.fragment_shader = fragment.get();
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            auto* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
            {
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(cloud volume) failed: " + LastSdlError());
            }
            CloudVolumePipeline = pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("cloud volume pipeline creation");
            throw;
        }
    }
} // namespace Keire::RenderBackend
