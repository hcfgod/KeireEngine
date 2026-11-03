#include "KeireInternal/Rendering/WaterRendering.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"
#include "KeireInternal/Rendering/GlobalIlluminationPolicyInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/WaterRenderCapture.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace Keire::RenderBackend
{
    namespace
    {
        struct WaterVertexUniforms final
        {
            Matrix4 ViewProjection;
            Vector4 Origin;
            Vector4 AxisX;
            Vector4 AxisY;
            Vector4 AxisZ;
            Vector4 Grid;
            Vector4 Footprint;
            Vector4 ViewDepthPlane;
            WaterWaveShaderParameters Waves;
        };

        struct WaterFragmentUniforms final
        {
            Vector4 ObserverExposure;
            Vector4 SunDirectionEnabled;
            Color SunColorIntensity;
            Color AmbientColorIntensity;
            Color SurfaceColor;
            Vector4 Appearance;
        };

        struct WaterDetailUniforms final
        {
            Vector4 AxisX;
            Vector4 AxisY;
            Vector4 AxisZ;
            WaterWaveShaderParameters Waves;
        };

        static_assert(sizeof(WaterVertexUniforms) == 704);
        static_assert(sizeof(WaterFragmentUniforms) == 96);
        static_assert(sizeof(WaterDetailUniforms) == 576);

        [[nodiscard]] SDL_GPUShader* CreateWaterShader(SDL_GPUDevice* device, const bool vertex)
        {
            SDL_GPUShaderCreateInfo information{};
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_uniform_buffers = vertex ? 1U : 4U;
            information.num_samplers = vertex ? 0U : 3U;
            information.num_storage_buffers = vertex ? 0U : 1U;
            const auto formats = SDL_GetGPUShaderFormats(device);
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
            {
                information.format = SDL_GPU_SHADERFORMAT_DXIL;
                information.code = vertex ? Detail::BuiltinWaterVertexDxil : Detail::BuiltinWaterFragmentDxil;
                information.code_size =
                    vertex ? Detail::BuiltinWaterVertexDxilSize : Detail::BuiltinWaterFragmentDxilSize;
            }
            else if (formats & SDL_GPU_SHADERFORMAT_MSL)
            {
                information.format = SDL_GPU_SHADERFORMAT_MSL;
                information.code = vertex ? Detail::BuiltinWaterVertexMsl : Detail::BuiltinWaterFragmentMsl;
                information.code_size =
                    vertex ? Detail::BuiltinWaterVertexMslSize : Detail::BuiltinWaterFragmentMslSize;
            }
            else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
            {
                information.format = SDL_GPU_SHADERFORMAT_SPIRV;
                information.entrypoint = vertex ? "VSMain" : "PSMain";
                information.code = vertex ? Detail::BuiltinWaterVertexSpirV : Detail::BuiltinWaterFragmentSpirV;
                information.code_size =
                    vertex ? Detail::BuiltinWaterVertexSpirVSize : Detail::BuiltinWaterFragmentSpirVSize;
            }
            else
            {
                throw std::runtime_error("The active SDL_GPU backend exposes no supported water shader format.");
            }
            auto* shader = SDL_CreateGPUShader(device, &information);
            if (!shader)
            {
                throw std::runtime_error(std::string("SDL_CreateGPUShader(water) failed: ") + SDL_GetError());
            }
            return shader;
        }
    } // namespace

    SDL_GPUGraphicsPipeline* CreateWaterPipeline(SDL_GPUDevice* device, const SDL_GPUTextureFormat colorFormat,
                                                 const SDL_GPUTextureFormat depthFormat,
                                                 const SDL_GPUSampleCount samples)
    {
        const auto releaseShader = [device](SDL_GPUShader* shader) { SDL_ReleaseGPUShader(device, shader); };
        const std::unique_ptr<SDL_GPUShader, decltype(releaseShader)> vertex(CreateWaterShader(device, true),
                                                                             releaseShader);
        const std::unique_ptr<SDL_GPUShader, decltype(releaseShader)> fragment(CreateWaterShader(device, false),
                                                                               releaseShader);

        SDL_GPUColorTargetDescription color{};
        color.format = colorFormat;
        color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        color.blend_state.enable_blend = true;

        SDL_GPUGraphicsPipelineCreateInfo information{};
        information.vertex_shader = vertex.get();
        information.fragment_shader = fragment.get();
        information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        information.rasterizer_state.enable_depth_clip = true;
        information.multisample_state.sample_count = samples;
        information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        information.depth_stencil_state.enable_depth_test = true;
        information.depth_stencil_state.enable_depth_write = false;
        information.target_info.color_target_descriptions = &color;
        information.target_info.num_color_targets = 1;
        information.target_info.depth_stencil_format = depthFormat;
        information.target_info.has_depth_stencil_target = true;
        auto* pipeline = SDL_CreateGPUGraphicsPipeline(device, &information);
        if (!pipeline)
        {
            throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline(water) failed: ") + SDL_GetError());
        }
        return pipeline;
    }

    void RenderSharedState::DrawWater(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                      const ShadowFrameData& shadows, const std::span<const WaterRenderPatch> patches)
    {
        if (patches.empty())
        {
            return;
        }
        const auto& camera = packet.Camera;
        const auto observer = Math::TransformPoint(Math::Inverse(camera.View), {});
        const auto viewProjection = Math::Multiply(camera.Projection, camera.View);
        const auto& noise = EnsureCloudNoiseTexture();
        const auto& requestedEnvironment =
            packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
        const auto& environment = requestedEnvironment.HasDiffuseIrradiance ? requestedEnvironment : DefaultSkyTexture;
        const auto illumination = ResolveGlobalIlluminationPolicy(
            surface.ActiveFeatureSelection.EffectiveGlobalIllumination, packet.Environment.RequestedIrradynQuality);
        AssetEnvironmentUniforms environmentUniforms{};
        environmentUniforms.DiffuseIrradiance = environment.DiffuseIrradiance;
        environmentUniforms.Parameters = {
            packet.Environment.EnvironmentRotationDegrees,
            illumination.EnvironmentDiffuse ? packet.Environment.EnvironmentDiffuseIntensity : 0.0F,
            illumination.EnvironmentSpecular ? packet.Environment.EnvironmentSpecularIntensity : 0.0F,
            static_cast<float>(environment.MipLevels - 1U)};
        environmentUniforms.Encoding = {static_cast<float>(environment.EnvironmentLayout) +
                                            (environment.HdrEncoded ? 16.0F : 0.0F),
                                        0.0F, 0.0F, 0.0F};
        const std::array bindings{SDL_GPUTextureSamplerBinding{noise.Texture, noise.Sampler},
                                  SDL_GPUTextureSamplerBinding{surface.ActiveWorkset().DirectionalShadow
                                                                   ? surface.ActiveWorkset().DirectionalShadow
                                                                   : EmptyShadowTexture,
                                                               ShadowSampler},
                                  SDL_GPUTextureSamplerBinding{environment.Texture, environment.Sampler}};
        SDL_BindGPUGraphicsPipeline(pass, PipelinesFor(ToSdlSampleCount(surface.ActualSamples)).Water);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        auto* volumeBuffer = surface.ActiveWorkset().DynamicUploads.CloudView.Buffer;
        if (!volumeBuffer)
        {
            throw std::logic_error("Water environment transport was not prepared before drawing.");
        }
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &volumeBuffer, 1);
        SDL_PushGPUFragmentUniformData(commands, 1, &shadows.Directional, sizeof(shadows.Directional));
        SDL_PushGPUFragmentUniformData(commands, 2, &environmentUniforms, sizeof(environmentUniforms));
        const auto& view = camera.View.Elements;
        for (const auto& patch : patches)
        {
            const WaterVertexUniforms vertex{
                viewProjection, patch.Origin, patch.AxisX,     patch.AxisY,
                patch.AxisZ,    patch.Grid,   patch.Footprint, {view[2], view[6], view[10], view[14]},
                patch.Waves};
            const WaterFragmentUniforms fragment{
                {observer.X, observer.Y, observer.Z, packet.Environment.Exposure},
                {packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z,
                 packet.Lighting.Enabled ? 1.0F : 0.0F},
                packet.Lighting.ColorAndIntensity,
                {packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                 packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity},
                patch.SurfaceColor,
                patch.Appearance};
            const WaterDetailUniforms detail{patch.AxisX, patch.AxisY, patch.AxisZ, patch.Waves};
            SDL_PushGPUVertexUniformData(commands, 0, &vertex, sizeof(vertex));
            SDL_PushGPUFragmentUniformData(commands, 0, &fragment, sizeof(fragment));
            SDL_PushGPUFragmentUniformData(commands, 3, &detail, sizeof(detail));
            SDL_DrawGPUPrimitives(pass, patch.VertexCount, 1, 0, 0);
        }
    }
} // namespace Keire::RenderBackend
