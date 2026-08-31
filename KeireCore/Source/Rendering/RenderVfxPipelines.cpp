#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinVfxShaders.h"
#include "Keire/Log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace
{
    enum class BuiltinVfxShaderStage : std::uint8_t
    {
        Initialize,
        Reset,
        Kill,
        Transform,
        Simulate,
        SimulateOutput,
        Spawn,
        SpawnInitialize,
        SpawnOutput,
        MapStrips,
        LinkStrips,
        Finalize,
        ResetRender,
        FilterRender,
        BuildVisibilityCandidates,
        CompactVisibility,
        Vertex,
        RibbonVertex,
        Fragment,
        MeshVertex,
        MeshFragment,
        CpuVertex,
        CpuFragment
    };
    struct EmbeddedShader final
    {
        const unsigned char* Code = nullptr;
        std::size_t Size = 0;
    };

    [[nodiscard]] EmbeddedShader SelectVfxShader(const BuiltinVfxShaderStage stage,
                                                 const SDL_GPUShaderFormat format) noexcept
    {
#define KEIRE_SELECT_VFX_SHADER(StageName)                                                                             \
    if (format == SDL_GPU_SHADERFORMAT_DXIL)                                                                           \
        return {Keire::Detail::BuiltinVfx##StageName##Dxil, sizeof(Keire::Detail::BuiltinVfx##StageName##Dxil)};       \
    if (format == SDL_GPU_SHADERFORMAT_SPIRV)                                                                          \
        return {Keire::Detail::BuiltinVfx##StageName##Spirv, sizeof(Keire::Detail::BuiltinVfx##StageName##Spirv)};     \
    if (format == SDL_GPU_SHADERFORMAT_MSL)                                                                            \
        return {Keire::Detail::BuiltinVfx##StageName##Msl, sizeof(Keire::Detail::BuiltinVfx##StageName##Msl)};         \
    break
        // Every generated branch selects different embedded shader symbols despite sharing the same control flow.
        // NOLINTNEXTLINE(bugprone-branch-clone)
        switch (stage)
        {
        case BuiltinVfxShaderStage::Initialize:
            KEIRE_SELECT_VFX_SHADER(Initialize);
        case BuiltinVfxShaderStage::Reset:
            KEIRE_SELECT_VFX_SHADER(Reset);
        case BuiltinVfxShaderStage::Kill:
            KEIRE_SELECT_VFX_SHADER(Kill);
        case BuiltinVfxShaderStage::Transform:
            KEIRE_SELECT_VFX_SHADER(Transform);
        case BuiltinVfxShaderStage::Simulate:
            KEIRE_SELECT_VFX_SHADER(Simulate);
        case BuiltinVfxShaderStage::SimulateOutput:
            KEIRE_SELECT_VFX_SHADER(SimulateOutput);
        case BuiltinVfxShaderStage::Spawn:
            KEIRE_SELECT_VFX_SHADER(Spawn);
        case BuiltinVfxShaderStage::SpawnInitialize:
            KEIRE_SELECT_VFX_SHADER(SpawnInitialize);
        case BuiltinVfxShaderStage::SpawnOutput:
            KEIRE_SELECT_VFX_SHADER(SpawnOutput);
        case BuiltinVfxShaderStage::MapStrips:
            KEIRE_SELECT_VFX_SHADER(MapStrips);
        case BuiltinVfxShaderStage::LinkStrips:
            KEIRE_SELECT_VFX_SHADER(LinkStrips);
        case BuiltinVfxShaderStage::Finalize:
            KEIRE_SELECT_VFX_SHADER(Finalize);
        case BuiltinVfxShaderStage::ResetRender:
            KEIRE_SELECT_VFX_SHADER(ResetRender);
        case BuiltinVfxShaderStage::FilterRender:
            KEIRE_SELECT_VFX_SHADER(FilterRender);
        case BuiltinVfxShaderStage::BuildVisibilityCandidates:
            KEIRE_SELECT_VFX_SHADER(BuildVisibilityCandidates);
        case BuiltinVfxShaderStage::CompactVisibility:
            KEIRE_SELECT_VFX_SHADER(CompactVisibility);
        case BuiltinVfxShaderStage::Vertex:
            KEIRE_SELECT_VFX_SHADER(Vertex);
        case BuiltinVfxShaderStage::RibbonVertex:
            KEIRE_SELECT_VFX_SHADER(RibbonVertex);
        case BuiltinVfxShaderStage::Fragment:
            KEIRE_SELECT_VFX_SHADER(Fragment);
        case BuiltinVfxShaderStage::MeshVertex:
            KEIRE_SELECT_VFX_SHADER(MeshVertex);
        case BuiltinVfxShaderStage::MeshFragment:
            KEIRE_SELECT_VFX_SHADER(MeshFragment);
        case BuiltinVfxShaderStage::CpuVertex:
            KEIRE_SELECT_VFX_SHADER(CpuVertex);
        case BuiltinVfxShaderStage::CpuFragment:
            KEIRE_SELECT_VFX_SHADER(CpuFragment);
        }
#undef KEIRE_SELECT_VFX_SHADER
        return {};
    }

} // namespace

namespace Keire::RenderBackend
{
    SDL_GPUGraphicsPipeline* RenderSharedState::CreateCpuVfxPipeline(const SDL_GPUSampleCount samples)
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("The active GPU backend cannot create the CPU VFX output shaders.");
        const auto createShader =
            [this, format](const BuiltinVfxShaderStage stage, const char* entrypoint, const bool vertex)
        {
            const auto embedded = SelectVfxShader(stage, format);
            SDL_GPUShaderCreateInfo information{};
            information.code = embedded.Code;
            information.code_size = embedded.Size;
            information.entrypoint = entrypoint;
            information.format = format;
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_samplers = vertex ? 0U : 1U;
            information.num_uniform_buffers = 1U;
            auto* result = SDL_CreateGPUShader(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUShader(CPU VFX output) failed: " + LastSdlError());
            return result;
        };

        auto* vertex = createShader(BuiltinVfxShaderStage::CpuVertex, "VSCpu", true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = createShader(BuiltinVfxShaderStage::CpuFragment, "PSCpu", false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = true;

            SDL_GPUVertexBufferDescription buffer{};
            buffer.slot = 0;
            buffer.pitch = sizeof(GpuRenderVertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const std::array attributes{
                SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuRenderVertex, Position)},
                SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuRenderVertex, Color)},
                SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuRenderVertex, Normal)}};

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &buffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = attributes.data();
            information.vertex_input_state.num_vertex_attributes = static_cast<std::uint32_t>(attributes.size());
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
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(CPU VFX output) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("CPU VFX output-pipeline creation", error.what());
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateGpuVfxPipeline(const SDL_GPUSampleCount samples,
                                                                     const bool ribbon)
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("The active GPU backend cannot compile built-in VFX shaders.");
        const auto createShader = [this, format, ribbon](const BuiltinVfxShaderStage stage, const bool vertex)
        {
            const auto embedded = SelectVfxShader(stage, format);
            SDL_GPUShaderCreateInfo information{};
            information.code = embedded.Code;
            information.code_size = embedded.Size;
            information.entrypoint = vertex ? (ribbon ? "VSRibbon" : "VSMain") : "PSMain";
            information.format = format;
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_samplers = vertex ? 0U : 1U;
            information.num_storage_buffers = vertex ? 2U : 0U;
            information.num_uniform_buffers = 1U;
            auto* result = SDL_CreateGPUShader(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUShader(GPU VFX) failed: " + LastSdlError());
            return result;
        };

        auto* vertex = createShader(ribbon ? BuiltinVfxShaderStage::RibbonVertex : BuiltinVfxShaderStage::Vertex, true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = createShader(BuiltinVfxShaderStage::Fragment, false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = true;

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
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
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU VFX) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("GPU VFX output-pipeline creation", error.what());
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateGpuVfxMeshPipeline(const SDL_GPUSampleCount samples)
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("The active GPU backend cannot create the VFX Mesh output shaders.");
        const auto createShader =
            [this, format](const BuiltinVfxShaderStage stage, const char* entrypoint, const bool vertex)
        {
            const auto embedded = SelectVfxShader(stage, format);
            SDL_GPUShaderCreateInfo information{};
            information.code = embedded.Code;
            information.code_size = embedded.Size;
            information.entrypoint = entrypoint;
            information.format = format;
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_storage_buffers = vertex ? 2U : 0U;
            information.num_uniform_buffers = 1U;
            auto* result = SDL_CreateGPUShader(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUShader(GPU VFX Mesh) failed: " + LastSdlError());
            return result;
        };

        auto* vertex = createShader(BuiltinVfxShaderStage::MeshVertex, "VSMesh", true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = createShader(BuiltinVfxShaderStage::MeshFragment, "PSMesh", false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = true;

            SDL_GPUVertexBufferDescription buffer{};
            buffer.slot = 0;
            buffer.pitch = sizeof(GpuMeshVertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const std::array attributes{
                SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)},
                SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Normal)},
                SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMeshVertex, UV0)},
                SDL_GPUVertexAttribute{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, VertexColor)},
                SDL_GPUVertexAttribute{4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, Tangent)}};
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &buffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = attributes.data();
            information.vertex_input_state.num_vertex_attributes = static_cast<std::uint32_t>(attributes.size());
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = samples;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = false;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(GPU VFX Mesh) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("GPU VFX mesh-pipeline creation", error.what());
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    void RenderSharedState::StartGpuVfxPipelineWarmup()
    {
        if (!Device)
            return;
        auto expected = GpuVfxPipelineWarmupState::NotStarted;
        if (!VfxPipelineWarmupState.compare_exchange_strong(expected, GpuVfxPipelineWarmupState::Compiling,
                                                            std::memory_order_acq_rel))
        {
            return;
        }

        const auto state = shared_from_this();
        try
        {
            std::scoped_lock lock(RenderQueueMutex);
            if (StopRenderQueue)
                throw std::logic_error("Renderer submission queue is closed.");
            RenderQueue.push_back(
                {[state]
                 {
                     const auto started = std::chrono::steady_clock::now();
                     bool retriedAfterDeviceLoss = false;
                     for (;;)
                     {
                         try
                         {
                             state->CompileGpuVfxPipelines();
                             const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started);
                             state->VfxPipelineWarmupMicroseconds.store(static_cast<std::uint64_t>(elapsed.count()),
                                                                        std::memory_order_relaxed);
                             state->VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Ready,
                                                                 std::memory_order_release);
                             KEIRE_CORE_INFO("GPU VFX pipelines warmed on the render thread in {:.2f} ms.",
                                             static_cast<double>(elapsed.count()) / 1000.0);
                             return;
                         }
                         catch (...)
                         {
                             const auto failure = std::current_exception();
                             bool deviceLost = false;
                             std::string detail = "Unknown GPU backend failure.";
                             try
                             {
                                 std::rethrow_exception(failure);
                             }
                             catch (const GpuDeviceLostError&)
                             {
                                 deviceLost = true;
                             }
                             catch (const std::exception& error)
                             {
                                 detail = error.what();
                                 deviceLost =
                                     state->ClassifyDeviceFailure("GPU VFX pipeline warmup", detail).has_value();
                             }
                             catch (...)
                             {
                             }
                             if (deviceLost)
                             {
                                 state->HandleRenderThreadFailure(failure);
                                 if (!retriedAfterDeviceLoss &&
                                     state->DeviceLifecycle.load(std::memory_order_acquire) ==
                                         RenderDeviceState::Running)
                                 {
                                     retriedAfterDeviceLoss = true;
                                     continue;
                                 }
                                 state->VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Failed,
                                                                     std::memory_order_release);
                                 return;
                             }

                             state->ReleaseGpuVfxPipelines();
                             state->VfxPipelineWarmupFailure = std::move(detail);
                             const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started);
                             state->VfxPipelineWarmupMicroseconds.store(static_cast<std::uint64_t>(elapsed.count()),
                                                                        std::memory_order_relaxed);
                             state->VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Failed,
                                                                 std::memory_order_release);
                             KEIRE_CORE_ERROR("GPU VFX pipeline warmup failed after {:.2f} ms: {}",
                                              static_cast<double>(elapsed.count()) / 1000.0,
                                              state->VfxPipelineWarmupFailure);
                             return;
                         }
                     }
                 },
                 0U,
                 {}});
            const auto queueDepth = static_cast<std::uint32_t>(RenderQueue.size());
            auto highWater = RenderQueueHighWaterMark.load(std::memory_order_relaxed);
            while (highWater < queueDepth &&
                   !RenderQueueHighWaterMark.compare_exchange_weak(highWater, queueDepth, std::memory_order_relaxed))
            {
            }
        }
        catch (...)
        {
            VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Failed, std::memory_order_release);
            throw;
        }
        RenderQueueReady.notify_one();
    }

    void RenderSharedState::CompileGpuVfxPipelines()
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("GPU VFX requires a DXIL, SPIR-V, or MSL compute-shader backend.");

        const auto create =
            [this, format](const BuiltinVfxShaderStage stage, const char* entrypoint, const std::uint32_t threads,
                           const bool usesExecutionTables = false, const std::uint32_t writeBufferCount = 5U,
                           const std::uint32_t uniformBufferCount = 1U, const std::uint32_t readBufferCount = 0U)
        {
            const auto shader = SelectVfxShader(stage, format);
            SDL_GPUComputePipelineCreateInfo information{};
            information.code = shader.Code;
            information.code_size = shader.Size;
            information.entrypoint = entrypoint;
            information.format = format;
            information.num_readonly_storage_buffers = usesExecutionTables ? 8U : readBufferCount;
            information.num_readwrite_storage_buffers = writeBufferCount;
            information.num_samplers = usesExecutionTables ? 1U : 0U;
            information.num_uniform_buffers = uniformBufferCount;
            information.threadcount_x = threads;
            information.threadcount_y = 1;
            information.threadcount_z = 1;
            auto* pipeline = SDL_CreateGPUComputePipeline(Device, &information);
            if (!pipeline)
            {
                throw std::runtime_error("SDL_CreateGPUComputePipeline(GPU VFX " + std::string(entrypoint) +
                                         ") failed: " + LastSdlError());
            }
            return pipeline;
        };
        VfxInitializePipeline = create(BuiltinVfxShaderStage::Initialize, "CSInitialize", 256);
        VfxResetPipeline = create(BuiltinVfxShaderStage::Reset, "CSReset", 1);
        VfxKillPipeline = create(BuiltinVfxShaderStage::Kill, "CSKill", 256);
        VfxTransformPipeline = create(BuiltinVfxShaderStage::Transform, "CSTransform", 256);
        VfxSimulatePipeline = create(BuiltinVfxShaderStage::Simulate, "CSSimulate", 256, true, 7);
        VfxSimulateOutputPipeline = create(BuiltinVfxShaderStage::SimulateOutput, "CSSimulateOutput", 256, true, 7);
        VfxSpawnPipeline = create(BuiltinVfxShaderStage::Spawn, "CSSpawn", 256, true, 7);
        VfxSpawnInitializePipeline = create(BuiltinVfxShaderStage::SpawnInitialize, "CSSpawnInitialize", 256, true, 7);
        VfxSpawnOutputPipeline = create(BuiltinVfxShaderStage::SpawnOutput, "CSSpawnOutput", 256, true, 7);
        VfxMapStripsPipeline = create(BuiltinVfxShaderStage::MapStrips, "CSMapStrips", 256, false, 8);
        VfxLinkStripsPipeline = create(BuiltinVfxShaderStage::LinkStrips, "CSLinkStrips", 256, false, 8);
        VfxFinalizePipeline = create(BuiltinVfxShaderStage::Finalize, "CSFinalize", 1);
        VfxResetRenderPipeline = create(BuiltinVfxShaderStage::ResetRender, "CSResetRender", 1, false, 8);
        VfxFilterRenderPipeline = create(BuiltinVfxShaderStage::FilterRender, "CSFilterRender", 256, false, 8);
        VfxBuildVisibilityPipeline = create(BuiltinVfxShaderStage::BuildVisibilityCandidates,
                                            "CSBuildVisibilityCandidates", 256, false, 5, 1, 5);
        VfxCompactVisibilityPipeline =
            create(BuiltinVfxShaderStage::CompactVisibility, "CSCompactVisibility", 1, false, 5, 1, 5);
    }

    void RenderSharedState::ReleaseGpuVfxPipelines() noexcept
    {
        const auto release = [this](SDL_GPUComputePipeline*& pipeline) noexcept
        {
            if (pipeline)
                SDL_ReleaseGPUComputePipeline(Device, pipeline);
            pipeline = nullptr;
        };
        release(VfxInitializePipeline);
        release(VfxResetPipeline);
        release(VfxKillPipeline);
        release(VfxTransformPipeline);
        release(VfxSimulatePipeline);
        release(VfxSimulateOutputPipeline);
        release(VfxSpawnPipeline);
        release(VfxSpawnInitializePipeline);
        release(VfxSpawnOutputPipeline);
        release(VfxMapStripsPipeline);
        release(VfxLinkStripsPipeline);
        release(VfxFinalizePipeline);
        release(VfxResetRenderPipeline);
        release(VfxFilterRenderPipeline);
        release(VfxBuildVisibilityPipeline);
        release(VfxCompactVisibilityPipeline);
    }

    bool RenderSharedState::EnsureGpuVfxPipelines(const bool requireStripPipelines)
    {
        auto state = VfxPipelineWarmupState.load(std::memory_order_acquire);
        if (state == GpuVfxPipelineWarmupState::NotStarted)
        {
            if (RenderThread.joinable())
            {
                // The first submitted VFX frame is allowed to finish without effects, then the render thread warms
                // the pipelines from its queue. This guarantees that a cold driver cache cannot hold the first
                // visible application frame hostage for tens of seconds.
                StartGpuVfxPipelineWarmup();
                return false;
            }
            auto expected = GpuVfxPipelineWarmupState::NotStarted;
            if (VfxPipelineWarmupState.compare_exchange_strong(expected, GpuVfxPipelineWarmupState::Compiling,
                                                               std::memory_order_acq_rel))
            {
                const auto started = std::chrono::steady_clock::now();
                try
                {
                    CompileGpuVfxPipelines();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started);
                    VfxPipelineWarmupMicroseconds.store(static_cast<std::uint64_t>(elapsed.count()),
                                                        std::memory_order_relaxed);
                    VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Ready, std::memory_order_release);
                    KEIRE_CORE_WARN("GPU VFX pipelines were compiled on first use in {:.2f} ms. Call "
                                    "RenderSystem::RequestGpuVfxPipelineWarmup during loading to avoid this stall.",
                                    static_cast<double>(elapsed.count()) / 1000.0);
                    state = GpuVfxPipelineWarmupState::Ready;
                }
                catch (const std::exception& error)
                {
                    RethrowIfDeviceLost("GPU VFX first-use pipeline compilation");
                    ReleaseGpuVfxPipelines();
                    VfxPipelineWarmupFailure = error.what();
                    VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Failed, std::memory_order_release);
                    throw;
                }
                catch (...)
                {
                    RethrowIfDeviceLost("GPU VFX first-use pipeline compilation");
                    ReleaseGpuVfxPipelines();
                    VfxPipelineWarmupFailure = "Unknown GPU backend failure.";
                    VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::Failed, std::memory_order_release);
                    throw;
                }
            }
            else
                state = expected;
        }
        if (state == GpuVfxPipelineWarmupState::Compiling)
            return false;
        if (state == GpuVfxPipelineWarmupState::Failed)
            throw std::runtime_error("GPU VFX pipeline warmup failed: " + VfxPipelineWarmupFailure);
        const bool baseReady = VfxInitializePipeline && VfxResetPipeline && VfxKillPipeline && VfxTransformPipeline &&
                               VfxSimulatePipeline && VfxSimulateOutputPipeline && VfxSpawnPipeline &&
                               VfxSpawnInitializePipeline && VfxSpawnOutputPipeline && VfxFinalizePipeline &&
                               VfxResetRenderPipeline && VfxFilterRenderPipeline;
        return baseReady && (!requireStripPipelines || (VfxMapStripsPipeline && VfxLinkStripsPipeline));
    }

} // namespace Keire::RenderBackend
