#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinOcclusionShaders.h"
#include "Keire/Log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    enum class OcclusionShader : std::uint8_t
    {
        DepthVertex,
        DepthFragment,
        PyramidBase,
        PyramidReduce,
        Classify,
        ScanBlocks,
        ScanBatches,
        Scatter,
        DebugPyramidVertex,
        DebugPyramidFragment,
        DebugBoundsVertex,
        DebugBoundsFragment
    };

    struct EmbeddedShader final
    {
        const unsigned char* Code = nullptr;
        std::size_t Size = 0;
    };

    [[nodiscard]] EmbeddedShader SelectOcclusionShader(const OcclusionShader shader,
                                                       const SDL_GPUShaderFormat format) noexcept
    {
#define KEIRE_SELECT_OCCLUSION_SHADER(Symbol)                                                                          \
    if (format == SDL_GPU_SHADERFORMAT_DXIL)                                                                           \
        return {Keire::Detail::Symbol##Dxil, sizeof(Keire::Detail::Symbol##Dxil)};                                     \
    if (format == SDL_GPU_SHADERFORMAT_SPIRV)                                                                          \
        return {Keire::Detail::Symbol##Spirv, sizeof(Keire::Detail::Symbol##Spirv)};                                   \
    if (format == SDL_GPU_SHADERFORMAT_MSL)                                                                            \
        return {Keire::Detail::Symbol##Msl, sizeof(Keire::Detail::Symbol##Msl)};                                       \
    break
        switch (shader)
        {
        case OcclusionShader::DepthVertex:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDepthVertex);
        case OcclusionShader::DepthFragment:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDepthFragment);
        case OcclusionShader::PyramidBase:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionPyramidBase);
        case OcclusionShader::PyramidReduce:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionPyramidReduce);
        case OcclusionShader::Classify:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionClassify);
        case OcclusionShader::ScanBlocks:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionScanBlocks);
        case OcclusionShader::ScanBatches:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionScanBatches);
        case OcclusionShader::Scatter:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionScatter);
        case OcclusionShader::DebugPyramidVertex:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDebugPyramidVertex);
        case OcclusionShader::DebugPyramidFragment:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDebugPyramidFragment);
        case OcclusionShader::DebugBoundsVertex:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDebugBoundsVertex);
        case OcclusionShader::DebugBoundsFragment:
            KEIRE_SELECT_OCCLUSION_SHADER(BuiltinOcclusionDebugBoundsFragment);
        }
#undef KEIRE_SELECT_OCCLUSION_SHADER
        return {};
    }

    [[nodiscard]] SDL_GPUShaderFormat SelectShaderFormat(SDL_GPUDevice* device) noexcept
    {
        const auto supported = SDL_GetGPUShaderFormats(device);
        return (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
               : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
               : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                          : SDL_GPU_SHADERFORMAT_INVALID;
    }
} // namespace

namespace Keire::RenderBackend
{
    bool RenderSharedState::EnsureGpuOcclusionPipelines()
    {
        if (GpuOcclusionPipelinesAttempted)
            return GpuOcclusionClassifyPipeline != nullptr;
        GpuOcclusionPipelinesAttempted = true;
        if (!Device || !GpuOcclusionCapability)
            return false;

        try
        {
            const auto format = SelectShaderFormat(Device);
            if (format == SDL_GPU_SHADERFORMAT_INVALID)
                throw std::runtime_error("GPU occlusion requires a DXIL, SPIR-V, or MSL shader backend.");

            const auto createGraphicsShader = [this, format](const OcclusionShader shader, const char* entrypoint,
                                                             const bool vertex, const std::uint32_t storageBuffers,
                                                             const std::uint32_t uniformBuffers,
                                                             const std::uint32_t samplers)
            {
                const auto embedded = SelectOcclusionShader(shader, format);
                SDL_GPUShaderCreateInfo information{};
                information.code = embedded.Code;
                information.code_size = embedded.Size;
                information.entrypoint = entrypoint;
                information.format = format;
                information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
                information.num_storage_buffers = storageBuffers;
                information.num_uniform_buffers = uniformBuffers;
                information.num_samplers = samplers;
                auto* result = SDL_CreateGPUShader(Device, &information);
                if (!result)
                    throw std::runtime_error("SDL_CreateGPUShader(GPU occlusion depth) failed: " + LastSdlError());
                return result;
            };

            auto* vertex = createGraphicsShader(OcclusionShader::DepthVertex, "VSDepth", true, 1U, 1U, 0U);
            SDL_GPUShader* fragment = nullptr;
            try
            {
                fragment = createGraphicsShader(OcclusionShader::DepthFragment, "PSDepth", false, 0U, 0U, 0U);
                SDL_GPUVertexBufferDescription vertexBuffer{};
                vertexBuffer.slot = 0;
                vertexBuffer.pitch = sizeof(GpuMeshVertex);
                vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
                const SDL_GPUVertexAttribute position{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                                      offsetof(GpuMeshVertex, Position)};
                constexpr std::array cullModes{SDL_GPU_CULLMODE_NONE, SDL_GPU_CULLMODE_FRONT, SDL_GPU_CULLMODE_BACK};
                for (std::size_t index = 0; index < cullModes.size(); ++index)
                {
                    SDL_GPUGraphicsPipelineCreateInfo information{};
                    information.vertex_shader = vertex;
                    information.fragment_shader = fragment;
                    information.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
                    information.vertex_input_state.num_vertex_buffers = 1;
                    information.vertex_input_state.vertex_attributes = &position;
                    information.vertex_input_state.num_vertex_attributes = 1;
                    information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
                    information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                    information.rasterizer_state.cull_mode = cullModes[index];
                    information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
                    information.rasterizer_state.enable_depth_clip = true;
                    information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
                    information.depth_stencil_state.enable_depth_test = true;
                    information.depth_stencil_state.enable_depth_write = true;
                    information.target_info.depth_stencil_format = ShadowDepthFormat;
                    information.target_info.has_depth_stencil_target = true;
                    GpuOcclusionDepthPipelines[index] = SDL_CreateGPUGraphicsPipeline(Device, &information);
                    if (!GpuOcclusionDepthPipelines[index])
                    {
                        throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU occlusion depth) failed: " +
                                                 LastSdlError());
                    }
                }
                SDL_ReleaseGPUShader(Device, fragment);
                SDL_ReleaseGPUShader(Device, vertex);
            }
            catch (...)
            {
                if (fragment)
                    SDL_ReleaseGPUShader(Device, fragment);
                SDL_ReleaseGPUShader(Device, vertex);
                throw;
            }

            SDL_GPUShader* debugPyramidVertex = nullptr;
            SDL_GPUShader* debugPyramidFragment = nullptr;
            SDL_GPUShader* debugBoundsVertex = nullptr;
            SDL_GPUShader* debugBoundsFragment = nullptr;
            try
            {
                debugPyramidVertex =
                    createGraphicsShader(OcclusionShader::DebugPyramidVertex, "VSDebugPyramid", true, 0U, 0U, 0U);
                debugPyramidFragment =
                    createGraphicsShader(OcclusionShader::DebugPyramidFragment, "PSDebugPyramid", false, 0U, 0U, 1U);
                debugBoundsVertex =
                    createGraphicsShader(OcclusionShader::DebugBoundsVertex, "VSDebugBounds", true, 3U, 1U, 0U);
                debugBoundsFragment =
                    createGraphicsShader(OcclusionShader::DebugBoundsFragment, "PSDebugBounds", false, 0U, 0U, 0U);

                SDL_GPUColorTargetDescription pyramidColor{};
                pyramidColor.format = ColorFormat;
                SDL_GPUGraphicsPipelineCreateInfo pyramid{};
                pyramid.vertex_shader = debugPyramidVertex;
                pyramid.fragment_shader = debugPyramidFragment;
                pyramid.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
                pyramid.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                pyramid.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
                pyramid.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
                pyramid.target_info.color_target_descriptions = &pyramidColor;
                pyramid.target_info.num_color_targets = 1;
                GpuOcclusionDebugPyramidPipeline = SDL_CreateGPUGraphicsPipeline(Device, &pyramid);
                if (!GpuOcclusionDebugPyramidPipeline)
                {
                    throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU occlusion HZB debug) failed: " +
                                             LastSdlError());
                }

                SDL_GPUColorTargetDescription boundsColor{};
                boundsColor.format = ColorFormat;
                boundsColor.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                boundsColor.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                boundsColor.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                boundsColor.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                boundsColor.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                boundsColor.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                boundsColor.blend_state.enable_blend = true;
                SDL_GPUGraphicsPipelineCreateInfo bounds{};
                bounds.vertex_shader = debugBoundsVertex;
                bounds.fragment_shader = debugBoundsFragment;
                bounds.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
                bounds.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                bounds.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
                bounds.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
                bounds.target_info.color_target_descriptions = &boundsColor;
                bounds.target_info.num_color_targets = 1;
                GpuOcclusionDebugBoundsPipeline = SDL_CreateGPUGraphicsPipeline(Device, &bounds);
                if (!GpuOcclusionDebugBoundsPipeline)
                {
                    throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU occlusion bounds debug) failed: " +
                                             LastSdlError());
                }
            }
            catch (...)
            {
                if (debugBoundsFragment)
                    SDL_ReleaseGPUShader(Device, debugBoundsFragment);
                if (debugBoundsVertex)
                    SDL_ReleaseGPUShader(Device, debugBoundsVertex);
                if (debugPyramidFragment)
                    SDL_ReleaseGPUShader(Device, debugPyramidFragment);
                if (debugPyramidVertex)
                    SDL_ReleaseGPUShader(Device, debugPyramidVertex);
                throw;
            }
            SDL_ReleaseGPUShader(Device, debugBoundsFragment);
            SDL_ReleaseGPUShader(Device, debugBoundsVertex);
            SDL_ReleaseGPUShader(Device, debugPyramidFragment);
            SDL_ReleaseGPUShader(Device, debugPyramidVertex);

            const auto createCompute = [this, format](const OcclusionShader shader, const char* entrypoint,
                                                      const std::uint32_t threadsX, const std::uint32_t threadsY,
                                                      const std::uint32_t samplers, const std::uint32_t readBuffers,
                                                      const std::uint32_t writeTextures,
                                                      const std::uint32_t writeBuffers)
            {
                const auto embedded = SelectOcclusionShader(shader, format);
                SDL_GPUComputePipelineCreateInfo information{};
                information.code = embedded.Code;
                information.code_size = embedded.Size;
                information.entrypoint = entrypoint;
                information.format = format;
                information.num_samplers = samplers;
                information.num_readonly_storage_buffers = readBuffers;
                information.num_readwrite_storage_textures = writeTextures;
                information.num_readwrite_storage_buffers = writeBuffers;
                information.num_uniform_buffers = 1;
                information.threadcount_x = threadsX;
                information.threadcount_y = threadsY;
                information.threadcount_z = 1;
                auto* result = SDL_CreateGPUComputePipeline(Device, &information);
                if (!result)
                {
                    throw std::runtime_error("SDL_CreateGPUComputePipeline(GPU occlusion " + std::string(entrypoint) +
                                             ") failed: " + LastSdlError());
                }
                return result;
            };
            GpuOcclusionBuildBasePipeline =
                createCompute(OcclusionShader::PyramidBase, "CSBuildBase", 8, 8, 1, 0, 1, 0);
            GpuOcclusionReducePipeline = createCompute(OcclusionShader::PyramidReduce, "CSReduce", 8, 8, 1, 0, 1, 0);
            GpuOcclusionClassifyPipeline = createCompute(OcclusionShader::Classify, "CSClassify", 256, 1,
                                                         MaximumGpuOcclusionPyramidLevels, 2, 0, 1);
            GpuOcclusionScanBlocksPipeline =
                createCompute(OcclusionShader::ScanBlocks, "CSScanBlocks", 256, 1, 0, 2, 0, 2);
            GpuOcclusionScanBatchesPipeline =
                createCompute(OcclusionShader::ScanBatches, "CSScanBatches", 256, 1, 0, 2, 0, 3);
            GpuOcclusionScatterPipeline = createCompute(OcclusionShader::Scatter, "CSScatter", 256, 1, 0, 6, 0, 1);

            SDL_GPUSamplerCreateInfo sampler{};
            sampler.min_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            GpuOcclusionSampler = SDL_CreateGPUSampler(Device, &sampler);
            if (!GpuOcclusionSampler)
                throw std::runtime_error("SDL_CreateGPUSampler(GPU occlusion) failed: " + LastSdlError());
            return true;
        }
        catch (const std::exception& error)
        {
            GpuOcclusionPipelineFailure = error.what();
            ReleaseGpuOcclusionPipelines();
            GpuOcclusionCapability = false;
            KEIRE_CORE_WARN("GPU occlusion culling is unavailable; retaining direct scene draws: {}",
                            GpuOcclusionPipelineFailure);
            return false;
        }
    }

    void RenderSharedState::ReleaseGpuOcclusionPipelines() noexcept
    {
        if (Device)
        {
            if (GpuOcclusionSampler)
                SDL_ReleaseGPUSampler(Device, GpuOcclusionSampler);
            if (GpuOcclusionDebugBoundsPipeline)
                SDL_ReleaseGPUGraphicsPipeline(Device, GpuOcclusionDebugBoundsPipeline);
            if (GpuOcclusionDebugPyramidPipeline)
                SDL_ReleaseGPUGraphicsPipeline(Device, GpuOcclusionDebugPyramidPipeline);
            if (GpuOcclusionScatterPipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionScatterPipeline);
            if (GpuOcclusionScanBatchesPipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionScanBatchesPipeline);
            if (GpuOcclusionScanBlocksPipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionScanBlocksPipeline);
            if (GpuOcclusionClassifyPipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionClassifyPipeline);
            if (GpuOcclusionReducePipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionReducePipeline);
            if (GpuOcclusionBuildBasePipeline)
                SDL_ReleaseGPUComputePipeline(Device, GpuOcclusionBuildBasePipeline);
            for (auto* pipeline : GpuOcclusionDepthPipelines)
                if (pipeline)
                    SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        }
        GpuOcclusionSampler = nullptr;
        GpuOcclusionDebugBoundsPipeline = nullptr;
        GpuOcclusionDebugPyramidPipeline = nullptr;
        GpuOcclusionScatterPipeline = nullptr;
        GpuOcclusionScanBatchesPipeline = nullptr;
        GpuOcclusionScanBlocksPipeline = nullptr;
        GpuOcclusionClassifyPipeline = nullptr;
        GpuOcclusionReducePipeline = nullptr;
        GpuOcclusionBuildBasePipeline = nullptr;
        GpuOcclusionDepthPipelines.fill(nullptr);
    }
} // namespace Keire::RenderBackend
