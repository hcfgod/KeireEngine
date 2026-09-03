#include "Keire/BuiltinUnlitShaders.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::RenderBackend
{
    namespace
    {
        struct EmbeddedShader final
        {
            const unsigned char* Code = nullptr;
            std::uint32_t Size = 0;
        };

        [[nodiscard]] EmbeddedShader SelectDeferredGBufferShader(const SDL_GPUShaderFormat format, const bool vertex)
        {
            if (format == SDL_GPU_SHADERFORMAT_DXIL)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredGBufferVertexDxil,
                                               Detail::BuiltinDeferredGBufferVertexDxilSize}
                              : EmbeddedShader{Detail::BuiltinDeferredGBufferFragmentDxil,
                                               Detail::BuiltinDeferredGBufferFragmentDxilSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_MSL)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredGBufferVertexMsl,
                                               Detail::BuiltinDeferredGBufferVertexMslSize}
                              : EmbeddedShader{Detail::BuiltinDeferredGBufferFragmentMsl,
                                               Detail::BuiltinDeferredGBufferFragmentMslSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_SPIRV)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredGBufferVertexSpirV,
                                               Detail::BuiltinDeferredGBufferVertexSpirVSize}
                              : EmbeddedShader{Detail::BuiltinDeferredGBufferFragmentSpirV,
                                               Detail::BuiltinDeferredGBufferFragmentSpirVSize};
            }
            throw std::runtime_error("The active SDL_GPU backend exposes no deferred GBuffer shader format.");
        }

        [[nodiscard]] EmbeddedShader SelectDeferredLightingShader(const SDL_GPUShaderFormat format, const bool vertex)
        {
            if (format == SDL_GPU_SHADERFORMAT_DXIL)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredLightingVertexDxil,
                                               Detail::BuiltinDeferredLightingVertexDxilSize}
                              : EmbeddedShader{Detail::BuiltinDeferredLightingFragmentDxil,
                                               Detail::BuiltinDeferredLightingFragmentDxilSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_MSL)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredLightingVertexMsl,
                                               Detail::BuiltinDeferredLightingVertexMslSize}
                              : EmbeddedShader{Detail::BuiltinDeferredLightingFragmentMsl,
                                               Detail::BuiltinDeferredLightingFragmentMslSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_SPIRV)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinDeferredLightingVertexSpirV,
                                               Detail::BuiltinDeferredLightingVertexSpirVSize}
                              : EmbeddedShader{Detail::BuiltinDeferredLightingFragmentSpirV,
                                               Detail::BuiltinDeferredLightingFragmentSpirVSize};
            }
            throw std::runtime_error("The active SDL_GPU backend exposes no deferred-lighting shader format.");
        }

        [[nodiscard]] EmbeddedShader SelectIrradynShader(const SDL_GPUShaderFormat format, const bool vertex)
        {
            if (format == SDL_GPU_SHADERFORMAT_DXIL)
            {
                return vertex
                           ? EmbeddedShader{Detail::BuiltinIrradynVertexDxil, Detail::BuiltinIrradynVertexDxilSize}
                           : EmbeddedShader{Detail::BuiltinIrradynFragmentDxil, Detail::BuiltinIrradynFragmentDxilSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_MSL)
            {
                return vertex
                           ? EmbeddedShader{Detail::BuiltinIrradynVertexMsl, Detail::BuiltinIrradynVertexMslSize}
                           : EmbeddedShader{Detail::BuiltinIrradynFragmentMsl, Detail::BuiltinIrradynFragmentMslSize};
            }
            if (format == SDL_GPU_SHADERFORMAT_SPIRV)
            {
                return vertex ? EmbeddedShader{Detail::BuiltinIrradynVertexSpirV, Detail::BuiltinIrradynVertexSpirVSize}
                              : EmbeddedShader{Detail::BuiltinIrradynFragmentSpirV,
                                               Detail::BuiltinIrradynFragmentSpirVSize};
            }
            throw std::runtime_error("The active SDL_GPU backend exposes no Irradyn shader format.");
        }

        [[nodiscard]] SDL_GPUShaderFormat SelectShaderFormat(SDL_GPUDevice* device)
        {
            const auto formats = SDL_GetGPUShaderFormats(device);
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
                return SDL_GPU_SHADERFORMAT_DXIL;
            if (formats & SDL_GPU_SHADERFORMAT_MSL)
                return SDL_GPU_SHADERFORMAT_MSL;
            if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
                return SDL_GPU_SHADERFORMAT_SPIRV;
            return SDL_GPU_SHADERFORMAT_INVALID;
        }
    } // namespace

    SDL_GPUShader* RenderSharedState::CreateDeferredGBufferShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = Detail::DeferredGBufferShaderUniformBufferCount(vertex);
        information.num_storage_buffers = Detail::DeferredGBufferShaderStorageBufferCount(vertex);
        information.format = SelectShaderFormat(Device);
        const auto embedded = SelectDeferredGBufferShader(information.format, vertex);
        information.code = embedded.Code;
        information.code_size = embedded.Size;
        if (information.format == SDL_GPU_SHADERFORMAT_SPIRV)
            information.entrypoint = vertex ? "VSMain" : "PSMain";
        auto* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(deferred GBuffer) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateDeferredLightingShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.format = SelectShaderFormat(Device);
        information.num_uniform_buffers = Detail::DeferredLightingShaderUniformBufferCount(vertex);
        information.num_samplers = Detail::DeferredLightingShaderSamplerCount(vertex);
        information.num_storage_textures = Detail::DeferredLightingShaderStorageTextureCount(vertex);
        information.num_storage_buffers = Detail::DeferredLightingShaderStorageBufferCount(vertex);
        const auto embedded = SelectDeferredLightingShader(information.format, vertex);
        information.code = embedded.Code;
        information.code_size = embedded.Size;
        if (information.format == SDL_GPU_SHADERFORMAT_SPIRV)
            information.entrypoint = vertex ? "VSMain" : "PSMain";
        auto* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(deferred lighting) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateIrradynShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.format = SelectShaderFormat(Device);
        information.num_uniform_buffers = Detail::IrradynShaderUniformBufferCount(vertex);
        information.num_samplers = Detail::IrradynShaderSamplerCount(vertex);
        const auto embedded = SelectIrradynShader(information.format, vertex);
        information.code = embedded.Code;
        information.code_size = embedded.Size;
        if (information.format == SDL_GPU_SHADERFORMAT_SPIRV)
            information.entrypoint = vertex ? "VSMain" : "PSMain";
        auto* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(Irradyn) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateDeferredGBufferPipeline()
    {
        SDL_GPUShader* vertex = CreateDeferredGBufferShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateDeferredGBufferShader(false);
            SDL_GPUVertexBufferDescription vertexBuffer{};
            vertexBuffer.slot = 0;
            vertexBuffer.pitch = sizeof(GpuRenderVertex);
            vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const std::array attributes{
                SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuRenderVertex, Position)},
                SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuRenderVertex, Color)},
                SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuRenderVertex, Normal)}};
            std::array<SDL_GPUColorTargetDescription, 4> colors{};
            colors[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
            colors[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            colors[2].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            colors[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = attributes.data();
            information.vertex_input_state.num_vertex_attributes = static_cast<std::uint32_t>(attributes.size());
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = true;
            information.target_info.color_target_descriptions = colors.data();
            information.target_info.num_color_targets = static_cast<std::uint32_t>(colors.size());
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(deferred GBuffer) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("deferred GBuffer pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateDeferredLightingPipeline()
    {
        SDL_GPUShader* vertex = CreateDeferredLightingShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateDeferredLightingShader(false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
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
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(deferred lighting) failed: " + LastSdlError());
            }
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("deferred-lighting pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateIrradynPipeline(const bool additive)
    {
        SDL_GPUShader* vertex = CreateIrradynShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateIrradynShader(false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            if (additive)
            {
                color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
                color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                color.blend_state.enable_blend = true;
            }

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            auto* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(Irradyn) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("Irradyn pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    bool RenderSharedState::EnsureDeferredPipelines()
    {
        if (DeferredPipelinesAttempted)
        {
            return DeferredGBufferPipeline && DeferredLightingPipeline && IrradynTracePipeline &&
                   IrradynCompositePipeline && DeferredSampler;
        }
        DeferredPipelinesAttempted = true;
        DeferredCapability.store(false, std::memory_order_release);
        DeferredPipelineFailure.clear();
        constexpr auto sampledGBufferUsage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        constexpr std::array sampledGBufferFormats{
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT};
        constexpr auto depthUsage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        const auto unsupportedSampledFormat = std::ranges::find_if(
            sampledGBufferFormats, [this](const SDL_GPUTextureFormat format)
            { return !SDL_GPUTextureSupportsFormat(Device, format, SDL_GPU_TEXTURETYPE_2D, sampledGBufferUsage); });
        if (DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
            DeferredPipelineFailure = "No sampled depth format is available.";
        else if (!SDL_GPUTextureSupportsFormat(Device, DepthFormat, SDL_GPU_TEXTURETYPE_2D, depthUsage))
            DeferredPipelineFailure =
                "The selected depth format does not support simultaneous depth-target and sampler usage (format=" +
                std::to_string(static_cast<std::uint32_t>(DepthFormat)) + ").";
        else if (unsupportedSampledFormat != sampledGBufferFormats.end())
            DeferredPipelineFailure = "A sampled color GBuffer format is unavailable (format=" +
                                      std::to_string(static_cast<std::uint32_t>(*unsupportedSampledFormat)) + ").";
        if (!DeferredPipelineFailure.empty())
        {
            KEIRE_CORE_WARN("Deferred Hybrid pipeline capability probe failed: {}", DeferredPipelineFailure);
            return false;
        }

        try
        {
            DeferredGBufferPipeline = CreateDeferredGBufferPipeline();
            DeferredLightingPipeline = CreateDeferredLightingPipeline();
            IrradynTracePipeline = CreateIrradynPipeline(false);
            IrradynCompositePipeline = CreateIrradynPipeline(true);
            SDL_GPUSamplerCreateInfo sampler{};
            sampler.min_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            DeferredSampler = SDL_CreateGPUSampler(Device, &sampler);
            if (!DeferredSampler)
                throw std::runtime_error("SDL_CreateGPUSampler(deferred) failed: " + LastSdlError());
            DeferredCapability.store(true, std::memory_order_release);
            return true;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("deferred pipeline capability probe", error.what());
            DeferredPipelineFailure = error.what();
            ReleaseDeferredPipelines();
            DeferredPipelinesAttempted = true;
            KEIRE_CORE_WARN("Deferred Hybrid pipeline capability probe failed: {}", DeferredPipelineFailure);
            return false;
        }
    }

    void RenderSharedState::ReleaseDeferredPipelines() noexcept
    {
        if (DeferredSampler)
            SDL_ReleaseGPUSampler(Device, DeferredSampler);
        if (DeferredLightingPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, DeferredLightingPipeline);
        if (IrradynCompositePipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, IrradynCompositePipeline);
        if (IrradynTracePipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, IrradynTracePipeline);
        if (DeferredGBufferPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, DeferredGBufferPipeline);
        DeferredSampler = nullptr;
        DeferredLightingPipeline = nullptr;
        IrradynCompositePipeline = nullptr;
        IrradynTracePipeline = nullptr;
        DeferredGBufferPipeline = nullptr;
        DeferredPipelinesAttempted = false;
        DeferredCapability.store(false, std::memory_order_release);
    }
} // namespace Keire::RenderBackend
