#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    SDL_GPUGraphicsPipeline* RenderSharedState::CreateDepthVelocityPipeline()
    {
        const auto createShader = [this](const bool vertex)
        {
            SDL_GPUShaderCreateInfo information{};
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_uniform_buffers = vertex ? 1U : 0U;
            information.num_samplers = 0U;
            const auto formats = SDL_GetGPUShaderFormats(Device);
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
            {
                information.format = SDL_GPU_SHADERFORMAT_DXIL;
                information.code =
                    vertex ? Detail::BuiltinDepthVelocityVertexDxil : Detail::BuiltinDepthVelocityFragmentDxil;
                information.code_size =
                    vertex ? Detail::BuiltinDepthVelocityVertexDxilSize : Detail::BuiltinDepthVelocityFragmentDxilSize;
            }
            else if (formats & SDL_GPU_SHADERFORMAT_MSL)
            {
                information.format = SDL_GPU_SHADERFORMAT_MSL;
                information.code =
                    vertex ? Detail::BuiltinDepthVelocityVertexMsl : Detail::BuiltinDepthVelocityFragmentMsl;
                information.code_size =
                    vertex ? Detail::BuiltinDepthVelocityVertexMslSize : Detail::BuiltinDepthVelocityFragmentMslSize;
            }
            else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
            {
                information.format = SDL_GPU_SHADERFORMAT_SPIRV;
                information.entrypoint = vertex ? "VSMain" : "PSMain";
                information.code =
                    vertex ? Detail::BuiltinDepthVelocityVertexSpirV : Detail::BuiltinDepthVelocityFragmentSpirV;
                information.code_size = vertex ? Detail::BuiltinDepthVelocityVertexSpirVSize
                                               : Detail::BuiltinDepthVelocityFragmentSpirVSize;
            }
            else
                throw std::runtime_error(
                    "The active SDL_GPU backend exposes no supported depth velocity shader format.");
            SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
            if (!shader)
                throw std::runtime_error("SDL_CreateGPUShader(depth velocity) failed: " + LastSdlError());
            return shader;
        };
        const auto releaseShader = [this](SDL_GPUShader* shader) { SDL_ReleaseGPUShader(Device, shader); };
        const std::unique_ptr<SDL_GPUShader, decltype(releaseShader)> vertex(createShader(true), releaseShader);
        const std::unique_ptr<SDL_GPUShader, decltype(releaseShader)> fragment(createShader(false), releaseShader);
        const std::array buffers{
            SDL_GPUVertexBufferDescription{0, sizeof(GpuMeshVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
            SDL_GPUVertexBufferDescription{1, sizeof(GpuMeshVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
        const std::array attributes{
            SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)},
            SDL_GPUVertexAttribute{1, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)}};
        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
        SDL_GPUGraphicsPipelineCreateInfo information{};
        information.vertex_shader = vertex.get();
        information.fragment_shader = fragment.get();
        information.vertex_input_state.vertex_buffer_descriptions = buffers.data();
        information.vertex_input_state.num_vertex_buffers = static_cast<std::uint32_t>(buffers.size());
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
        information.target_info.color_target_descriptions = &color;
        information.target_info.num_color_targets = 1;
        information.target_info.depth_stencil_format = DepthFormat;
        information.target_info.has_depth_stencil_target = true;
        auto* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
        if (!pipeline)
            throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(depth velocity) failed: " + LastSdlError());
        return pipeline;
    }
} // namespace Keire::RenderBackend
