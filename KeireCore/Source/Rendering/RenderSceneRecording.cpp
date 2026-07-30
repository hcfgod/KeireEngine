#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/ForwardPlusInternal.h"
#include "KeireInternal/Rendering/InstanceBatchInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"

#include "Keire/BuiltinSkinningShaders.h"
#include "Keire/BuiltinVfxShaders.h"
#include "Keire/Log.h"

#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{
    enum class BuiltinVfxShaderStage : std::uint8_t
    {
        Initialize,
        Reset,
        Simulate,
        Spawn,
        Finalize,
        Vertex,
        Fragment
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
        return { Keire::Detail::BuiltinVfx##StageName##Msl, sizeof(Keire::Detail::BuiltinVfx##StageName##Msl) }
        switch (stage)
        {
        case BuiltinVfxShaderStage::Initialize:
            KEIRE_SELECT_VFX_SHADER(Initialize);
        case BuiltinVfxShaderStage::Reset:
            KEIRE_SELECT_VFX_SHADER(Reset);
        case BuiltinVfxShaderStage::Simulate:
            KEIRE_SELECT_VFX_SHADER(Simulate);
        case BuiltinVfxShaderStage::Spawn:
            KEIRE_SELECT_VFX_SHADER(Spawn);
        case BuiltinVfxShaderStage::Finalize:
            KEIRE_SELECT_VFX_SHADER(Finalize);
        case BuiltinVfxShaderStage::Vertex:
            KEIRE_SELECT_VFX_SHADER(Vertex);
        case BuiltinVfxShaderStage::Fragment:
            KEIRE_SELECT_VFX_SHADER(Fragment);
        }
#undef KEIRE_SELECT_VFX_SHADER
        return {};
    }

    class CallbackFrameGraphExecutionContext final : public Keire::RenderBackend::FrameGraphExecutionContext
    {
      public:
        using TransitionCallback = std::function<void(const Keire::RenderBackend::CompiledFrameGraph::Transition&)>;
        using PassCallback = std::function<void(Keire::RenderBackend::FrameGraphPass)>;

        CallbackFrameGraphExecutionContext(TransitionCallback transition, PassCallback pass)
            : m_Transition(std::move(transition)), m_Pass(std::move(pass))
        {
        }

        void Transition(const Keire::RenderBackend::CompiledFrameGraph::Transition& transition) override
        {
            m_Transition(transition);
        }

        void Execute(const Keire::RenderBackend::FrameGraphPass pass,
                     const Keire::RenderBackend::FrameGraphPassDescription&) override
        {
            m_Pass(pass);
        }

      private:
        TransitionCallback m_Transition;
        PassCallback m_Pass;
    };

    struct ClipPoint final
    {
        float X;
        float Y;
        float Z;
        float W;
    };

    [[nodiscard]] ClipPoint TransformClip(const Keire::Matrix4& matrix, const Keire::Vector3 point) noexcept
    {
        const auto& value = matrix.Elements;
        return {value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12],
                value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13],
                value[2] * point.X + value[6] * point.Y + value[10] * point.Z + value[14],
                value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15]};
    }

    [[nodiscard]] Keire::Vector3 Add(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    [[nodiscard]] Keire::Vector3 Subtract(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] Keire::Vector3 Scale(const Keire::Vector3 value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale, value.Z * scale};
    }

    [[nodiscard]] float Length(const Keire::Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] bool IntersectsFrustum(const Keire::Matrix4& clipFromLocal, const Keire::MeshBounds bounds) noexcept
    {
        const std::array corners{Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
        std::array<ClipPoint, corners.size()> clip{};
        std::ranges::transform(corners, clip.begin(),
                               [&](const auto corner) { return TransformClip(clipFromLocal, corner); });
        const auto all = [&](const auto predicate) { return std::ranges::all_of(clip, predicate); };
        return !all([](const auto point) { return point.X < -point.W; }) &&
               !all([](const auto point) { return point.X > point.W; }) &&
               !all([](const auto point) { return point.Y < -point.W; }) &&
               !all([](const auto point) { return point.Y > point.W; }) &&
               !all([](const auto point) { return point.Z < 0.0F; }) &&
               !all([](const auto point) { return point.Z > point.W; });
    }

    [[nodiscard]] float ProjectedHeight(const Keire::Matrix4& viewFromLocal, const Keire::Matrix4& projection,
                                        const Keire::MeshBounds bounds) noexcept
    {
        const Keire::Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                    (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                                    (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
        const Keire::Vector3 extent{(bounds.Maximum.X - bounds.Minimum.X) * 0.5F,
                                    (bounds.Maximum.Y - bounds.Minimum.Y) * 0.5F,
                                    (bounds.Maximum.Z - bounds.Minimum.Z) * 0.5F};
        const auto viewCenter = Keire::Math::TransformPoint(viewFromLocal, center);
        const float localRadius = std::sqrt(extent.X * extent.X + extent.Y * extent.Y + extent.Z * extent.Z);
        const auto& matrix = viewFromLocal.Elements;
        const float scaleX = std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
        const float scaleY = std::sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
        const float scaleZ = std::sqrt(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10]);
        const float radius = localRadius * std::max({scaleX, scaleY, scaleZ});
        return viewCenter.Z > 0.0001F ? 2.0F * radius * std::abs(projection.Elements[5]) / viewCenter.Z : 1.0F;
    }
} // namespace

namespace Keire::RenderBackend
{
    SDL_GPUGraphicsPipeline* RenderSharedState::CreateGpuVfxPipeline(const SDL_GPUSampleCount samples)
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            throw std::runtime_error("The active GPU backend cannot compile built-in VFX shaders.");
        const auto createShader = [this, format](const BuiltinVfxShaderStage stage, const bool vertex)
        {
            const auto embedded = SelectVfxShader(stage, format);
            SDL_GPUShaderCreateInfo information{};
            information.code = embedded.Code;
            information.code_size = embedded.Size;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.format = format;
            information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            information.num_storage_buffers = vertex ? 2U : 0U;
            information.num_uniform_buffers = vertex ? 1U : 0U;
            auto* result = SDL_CreateGPUShader(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUShader(GPU VFX) failed: " + LastSdlError());
            return result;
        };

        auto* vertex = createShader(BuiltinVfxShaderStage::Vertex, true);
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
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    bool RenderSharedState::EnsureGpuVfxPipelines()
    {
        if (VfxPipelinesAttempted)
            return VfxInitializePipeline && VfxResetPipeline && VfxSimulatePipeline && VfxSpawnPipeline &&
                   VfxFinalizePipeline;
        VfxPipelinesAttempted = true;

        const auto supported = SDL_GetGPUShaderFormats(Device);
        const auto format = (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
                            : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                            : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                                       : SDL_GPU_SHADERFORMAT_INVALID;
        if (format == SDL_GPU_SHADERFORMAT_INVALID)
            return false;

        const auto create =
            [this, format](const BuiltinVfxShaderStage stage, const char* entrypoint, const std::uint32_t threads)
        {
            const auto shader = SelectVfxShader(stage, format);
            SDL_GPUComputePipelineCreateInfo information{};
            information.code = shader.Code;
            information.code_size = shader.Size;
            information.entrypoint = entrypoint;
            information.format = format;
            information.num_readwrite_storage_buffers = 5;
            information.num_uniform_buffers = 1;
            information.threadcount_x = threads;
            information.threadcount_y = 1;
            information.threadcount_z = 1;
            return SDL_CreateGPUComputePipeline(Device, &information);
        };
        VfxInitializePipeline = create(BuiltinVfxShaderStage::Initialize, "CSInitialize", 256);
        VfxResetPipeline = create(BuiltinVfxShaderStage::Reset, "CSReset", 1);
        VfxSimulatePipeline = create(BuiltinVfxShaderStage::Simulate, "CSSimulate", 256);
        VfxSpawnPipeline = create(BuiltinVfxShaderStage::Spawn, "CSSpawn", 256);
        VfxFinalizePipeline = create(BuiltinVfxShaderStage::Finalize, "CSFinalize", 1);
        return VfxInitializePipeline && VfxResetPipeline && VfxSimulatePipeline && VfxSpawnPipeline &&
               VfxFinalizePipeline;
    }

    void RenderSharedState::ReleaseGpuVfxWorld(GpuVfxWorldResources& resources) noexcept
    {
        if (resources.IndirectArguments)
            SDL_ReleaseGPUBuffer(Device, resources.IndirectArguments);
        if (resources.Counters)
            SDL_ReleaseGPUBuffer(Device, resources.Counters);
        if (resources.AliveIndices)
            SDL_ReleaseGPUBuffer(Device, resources.AliveIndices);
        if (resources.FreeIndices)
            SDL_ReleaseGPUBuffer(Device, resources.FreeIndices);
        if (resources.Particles)
            SDL_ReleaseGPUBuffer(Device, resources.Particles);
        resources = {};
    }

    void RenderSharedState::PrepareGpuVfx(SDL_GPUCommandBuffer* commands, const VfxRenderSnapshot& snapshot)
    {
        if (snapshot.WorldId() == 0 || snapshot.ParticleCapacity() == 0 || !EnsureGpuVfxPipelines())
            return;
        auto& resources = GpuVfxWorlds[snapshot.WorldId()];
        if (resources.Capacity != snapshot.ParticleCapacity())
        {
            ReleaseGpuVfxWorld(resources);
            const auto create = [this](const std::uint64_t size, const SDL_GPUBufferUsageFlags usage)
            {
                if (size == 0 || size > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("GPU VFX buffer size exceeds the backend limit.");
                SDL_GPUBufferCreateInfo information{};
                information.usage = usage;
                information.size = static_cast<std::uint32_t>(size);
                auto* result = SDL_CreateGPUBuffer(Device, &information);
                if (!result)
                    throw std::runtime_error("SDL_CreateGPUBuffer(GPU VFX) failed: " + LastSdlError());
                return result;
            };
            constexpr std::uint64_t particleStride = 128;
            const auto capacity = snapshot.ParticleCapacity();
            try
            {
                resources.Particles = create(particleStride * capacity, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                                                            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.FreeIndices =
                    create(sizeof(std::uint32_t) * capacity, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.AliveIndices =
                    create(sizeof(std::uint32_t) * capacity,
                           SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                resources.Counters = create(5U * sizeof(std::uint32_t), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
                resources.IndirectArguments =
                    create(sizeof(SDL_GPUIndirectDrawCommand),
                           SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT);
                resources.Capacity = capacity;
            }
            catch (...)
            {
                ReleaseGpuVfxWorld(resources);
                GpuVfxWorlds.erase(snapshot.WorldId());
                throw;
            }
        }
        if (resources.LastPreparedFrame == Statistics.Frame)
            return;
        resources.LastPreparedFrame = Statistics.Frame;

        struct alignas(16) Dispatch final
        {
            std::uint32_t Capacity = 0;
            std::uint32_t SpawnCount = 0;
            float DeltaSeconds = 0.0F;
            std::uint32_t Seed = 0;
            std::array<float, 4> Position{};
            std::array<float, 4> Rotation{};
            std::array<float, 4> ShapeExtentRadius{};
            std::array<float, 4> VelocityMinimumLifetime{};
            std::array<float, 4> VelocityMaximumLifetime{};
            std::array<float, 4> AccelerationShape{};
            std::array<float, 4> ColorStart{};
            std::array<float, 4> ColorEnd{};
            std::array<float, 4> Size{};
            std::array<std::uint32_t, 4> Identity{};
        };
        Dispatch dispatch;
        dispatch.Capacity = resources.Capacity;
        dispatch.DeltaSeconds = std::clamp(snapshot.DeltaSeconds(), 0.0F, 0.1F);

        const std::array writeBindings{
            SDL_GPUStorageBufferReadWriteBinding{resources.Particles, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.FreeIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.AliveIndices, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.Counters, false},
            SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments, false},
        };
        auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                             static_cast<std::uint32_t>(writeBindings.size()));
        if (!pass)
            throw std::runtime_error("SDL_BeginGPUComputePass(VFX) failed: " + LastSdlError());

        const auto resetWorld = resources.ResetRevision != snapshot.ResetRevision();
        if (resetWorld)
        {
            resources.SpawnSequences.clear();
            resources.ResetRevision = snapshot.ResetRevision();
            SDL_BindGPUComputePipeline(pass, VfxInitializePipeline);
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (resources.Capacity + 255U) / 256U, 1, 1);
            ++Statistics.VfxComputeDispatches;
        }
        SDL_BindGPUComputePipeline(pass, VfxResetPipeline);
        SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
        SDL_DispatchGPUCompute(pass, 1, 1, 1);
        ++Statistics.VfxComputeDispatches;

        SDL_BindGPUComputePipeline(pass, VfxSimulatePipeline);
        SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
        SDL_DispatchGPUCompute(pass, (resources.Capacity + 255U) / 256U, 1, 1);
        ++Statistics.VfxComputeDispatches;

        for (const auto& emitter : snapshot.GpuEmitters())
        {
            const auto key = (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
            auto& previous = resources.SpawnSequences[key];
            const auto requested =
                emitter.SpawnSequence >= previous ? emitter.SpawnSequence - previous : emitter.SpawnSequence;
            previous = emitter.SpawnSequence;
            if (requested == 0)
                continue;
            dispatch.SpawnCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, resources.Capacity));
            dispatch.Seed = emitter.Seed;
            dispatch.Position = {emitter.Position.X, emitter.Position.Y, emitter.Position.Z, 0.0F};
            dispatch.Rotation = {emitter.Rotation.X, emitter.Rotation.Y, emitter.Rotation.Z, emitter.Rotation.W};
            dispatch.ShapeExtentRadius = {emitter.ShapeExtent.X, emitter.ShapeExtent.Y, emitter.ShapeExtent.Z,
                                          emitter.ShapeRadius};
            dispatch.VelocityMinimumLifetime = {emitter.VelocityMinimum.X, emitter.VelocityMinimum.Y,
                                                emitter.VelocityMinimum.Z, emitter.LifetimeMinimum};
            dispatch.VelocityMaximumLifetime = {emitter.VelocityMaximum.X, emitter.VelocityMaximum.Y,
                                                emitter.VelocityMaximum.Z, emitter.LifetimeMaximum};
            dispatch.AccelerationShape = {emitter.Acceleration.X, emitter.Acceleration.Y, emitter.Acceleration.Z,
                                          std::bit_cast<float>(static_cast<std::uint32_t>(emitter.Shape))};
            dispatch.ColorStart = {emitter.ColorStart.Red, emitter.ColorStart.Green, emitter.ColorStart.Blue,
                                   emitter.ColorStart.Alpha};
            dispatch.ColorEnd = {emitter.ColorEnd.Red, emitter.ColorEnd.Green, emitter.ColorEnd.Blue,
                                 emitter.ColorEnd.Alpha};
            dispatch.Size = {emitter.SizeStart, emitter.SizeEnd, 0.0F, 0.0F};
            dispatch.Identity = {emitter.Handle.Index(), emitter.Handle.Generation(),
                                 static_cast<std::uint32_t>(emitter.SpawnSequence),
                                 static_cast<std::uint32_t>(emitter.Renderer)};
            SDL_BindGPUComputePipeline(pass, VfxSpawnPipeline);
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (dispatch.SpawnCount + 255U) / 256U, 1, 1);
            ++Statistics.VfxComputeDispatches;
        }

        SDL_BindGPUComputePipeline(pass, VfxFinalizePipeline);
        SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
        SDL_DispatchGPUCompute(pass, 1, 1, 1);
        ++Statistics.VfxComputeDispatches;
        SDL_EndGPUComputePass(pass);
        Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());
        Statistics.VfxGpuBufferBytes =
            static_cast<std::uint64_t>(resources.Capacity) * (128U + sizeof(std::uint32_t) * 2U) +
            5U * sizeof(std::uint32_t) + sizeof(SDL_GPUIndirectDrawCommand);
    }

    bool RenderSharedState::EnsureSkinningPipeline()
    {
        if (SkinningPipelineAttempted)
            return SkinningPipeline != nullptr;
        SkinningPipelineAttempted = true;

        const auto formats = SDL_GetGPUShaderFormats(Device);
        const unsigned char* code = nullptr;
        std::size_t codeSize = 0;
        SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
        if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeDxil;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeDxil);
            format = SDL_GPU_SHADERFORMAT_DXIL;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeSpirv;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeSpirv);
            format = SDL_GPU_SHADERFORMAT_SPIRV;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeMsl;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeMsl);
            format = SDL_GPU_SHADERFORMAT_MSL;
        }
        if (!code)
            return false;

        SDL_GPUComputePipelineCreateInfo createInfo{};
        createInfo.code = code;
        createInfo.code_size = codeSize;
        createInfo.entrypoint = "CSMain";
        createInfo.format = format;
        createInfo.num_readonly_storage_buffers = 3;
        createInfo.num_readwrite_storage_buffers = 2;
        createInfo.num_uniform_buffers = 1;
        createInfo.threadcount_x = 64;
        createInfo.threadcount_y = 1;
        createInfo.threadcount_z = 1;
        SkinningPipeline = SDL_CreateGPUComputePipeline(Device, &createInfo);
        return SkinningPipeline != nullptr;
    }

    void RenderSharedState::PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet)
    {
        struct alignas(16) GpuSkinInfluence
        {
            std::array<std::uint32_t, 4> Bones0{};
            std::array<std::uint32_t, 4> Bones1{};
            std::array<float, 4> Weights0{};
            std::array<float, 4> Weights1{};
        };
        struct alignas(16) GpuSkinMatrix
        {
            std::array<float, 4> Column0{};
            std::array<float, 4> Column1{};
            std::array<float, 4> Column2{};
            std::array<float, 4> Column3{};
        };
        struct SkinDispatch
        {
            std::uint32_t VertexCount = 0;
            std::uint32_t InfluenceCount = 4;
            std::uint32_t SkinningMode = 0;
            std::uint32_t Padding = 0;
        };
        static_assert(sizeof(GpuSkinInfluence) == 64);
        static_assert(alignof(GpuSkinInfluence) == 16);
        static_assert(sizeof(GpuSkinMatrix) == 64);
        static_assert(alignof(GpuSkinMatrix) == 16);
        static_assert(sizeof(SkinDispatch) == 16);
        static_assert(sizeof(GpuMeshVertex) == 80);
        static_assert(sizeof(GpuRenderVertex) == 48);

        const auto createOutput = [this](const std::uint32_t vertexCount)
        {
            const auto assetBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuMeshVertex);
            const auto builtinBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuRenderVertex);
            if (assetBytes > std::numeric_limits<std::uint32_t>::max() ||
                builtinBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Skinned mesh output exceeds SDL's 32-bit buffer limit.");
            }

            GpuSkinOutputResources result;
            const auto createBuffer = [this](const std::uint32_t bytes)
            {
                SDL_GPUBufferCreateInfo createInfo{};
                createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
                createInfo.size = bytes;
                auto* buffer = SDL_CreateGPUBuffer(Device, &createInfo);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer(skin cache) failed: " + LastSdlError());
                return buffer;
            };
            try
            {
                result.AssetVertices = createBuffer(static_cast<std::uint32_t>(assetBytes));
                result.BuiltinVertices = createBuffer(static_cast<std::uint32_t>(builtinBytes));
                return result;
            }
            catch (...)
            {
                if (result.BuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.BuiltinVertices);
                if (result.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                throw;
            }
        };

        for (auto& item : packet.DrawItems)
        {
            item.SkinnedAssetVertices = nullptr;
            item.SkinnedBuiltinVertices = nullptr;
            if (!Assets || !item.Skin || item.SkinPalette.empty())
                continue;

            auto [cacheIterator, inserted] = SkinCache.try_emplace(item.Skin);
            (void)inserted;
            auto& cache = cacheIterator->second;
            cache.LastRequestedFrame = Statistics.Frame;

            const auto skinHandle = Assets->Load<SkinnedMeshAsset>(item.Skin, AssetPriority::High);
            const auto skin = skinHandle.TryGetLoaded();
            if (!skin)
                continue;
            const auto meshHandle = Assets->Load<MeshAsset>(skin->Mesh(), AssetPriority::High);
            const auto meshAsset = meshHandle.TryGetLoaded();
            if (!meshAsset || skinHandle.Revision() == 0 || meshHandle.Revision() == 0)
                continue;

            auto dependencyStamp = std::uint64_t{1469598103934665603ULL};
            dependencyStamp = HashDependencyStamp(dependencyStamp, item.Skin);
            dependencyStamp = HashDependencyStamp(dependencyStamp, skinHandle.Revision());
            dependencyStamp = HashDependencyStamp(dependencyStamp, skin->Mesh());
            dependencyStamp = HashDependencyStamp(dependencyStamp, meshHandle.Revision());
            if (dependencyStamp != cache.LastAttemptedDependencyStamp)
            {
                cache.LastAttemptedDependencyStamp = dependencyStamp;
                bool valid = skin->Influences8().size() == meshAsset->Vertices().size() &&
                             !meshAsset->Vertices().empty() &&
                             meshAsset->Vertices().size() <= std::numeric_limits<std::uint32_t>::max();
                std::uint32_t maximumBoneIndex = 0;
                if (valid)
                {
                    for (const auto& influence : skin->Influences8())
                    {
                        if (influence.Count == 0 || influence.Count > influence.Bones.size())
                        {
                            valid = false;
                            break;
                        }
                        for (std::size_t index = 0; index < influence.Count; ++index)
                        {
                            if (!std::isfinite(influence.Weights[index]) || influence.Weights[index] < 0.0F)
                            {
                                valid = false;
                                break;
                            }
                            maximumBoneIndex =
                                std::max(maximumBoneIndex, static_cast<std::uint32_t>(influence.Bones[index]));
                        }
                        if (!valid)
                            break;
                    }
                }

                try
                {
                    GpuSkinResources replacement;
                    replacement.Valid = valid;
                    if (valid)
                    {
                        replacement.VertexCount = static_cast<std::uint32_t>(meshAsset->Vertices().size());
                        replacement.MaximumBoneIndex = maximumBoneIndex;
                        replacement.MaximumInfluences = skin->MaximumInfluences();
                        const auto* driver = SDL_GetGPUDeviceDriver(Device);
                        if (driver && SupportsComputeSkinning(driver, skin->Method()) && EnsureSkinningPipeline())
                        {
                            std::vector<GpuSkinInfluence> influences(skin->Influences8().size());
                            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
                            {
                                const auto& source = skin->Influences8()[vertex];
                                for (std::size_t influence = 0; influence < source.Count; ++influence)
                                {
                                    if (influence < 4)
                                    {
                                        influences[vertex].Bones0[influence] = source.Bones[influence];
                                        influences[vertex].Weights0[influence] = source.Weights[influence];
                                    }
                                    else
                                    {
                                        influences[vertex].Bones1[influence - 4] = source.Bones[influence];
                                        influences[vertex].Weights1[influence - 4] = source.Weights[influence];
                                    }
                                }
                            }
                            replacement.Influences = UploadBuffer(std::as_bytes(std::span(influences)),
                                                                  SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
                        }
                    }
                    Retire(std::exchange(cache.Resources, std::move(replacement)));
                    cache.Skin = skin;
                    cache.Mesh = meshAsset;
                    cache.LoadedDependencyStamp = dependencyStamp;
                    ++SkinningStaticBuilds;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR("Skin GPU cache rebuild failed for id={} dependency={}: {}", item.Skin.ToString(),
                                     dependencyStamp, error.what());
                }
            }

            if (cache.LoadedDependencyStamp != dependencyStamp || !cache.Skin || !cache.Mesh ||
                !cache.Resources.Valid || cache.Skin->Mesh() != item.Mesh ||
                cache.Skin->Skeleton() != item.SkinSkeleton ||
                cache.Resources.VertexCount != cache.Mesh->Vertices().size() ||
                cache.Resources.MaximumBoneIndex >= item.SkinPalette.size() ||
                !std::ranges::all_of(item.SkinPalette, [](const Matrix4& matrix) { return Math::IsFinite(matrix); }))
            {
                continue;
            }

            item.Skinning = cache.Skin->Method();
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty() || !mesh.AssetVertices)
                continue;

            const auto useCompute = cache.Resources.Influences && SkinningPipeline;
            if (!useCompute)
            {
                std::vector<MeshVertex> deformed(cache.Mesh->Vertices().size());
                SkinMeshCpu(cache.Mesh->Vertices(), cache.Skin->Influences8(), item.SkinPalette, item.Skinning,
                            deformed);
                const auto& sourceBounds = cache.Mesh->Bounds();
                const auto sourceMagnitude =
                    std::max({1.0F, std::abs(sourceBounds.Minimum.X), std::abs(sourceBounds.Minimum.Y),
                              std::abs(sourceBounds.Minimum.Z), std::abs(sourceBounds.Maximum.X),
                              std::abs(sourceBounds.Maximum.Y), std::abs(sourceBounds.Maximum.Z)});
                const auto maximumCoordinate = sourceMagnitude * 8.0F;
                const auto validDeformation =
                    std::ranges::all_of(deformed,
                                        [maximumCoordinate](const MeshVertex& vertex)
                                        {
                                            return Math::IsFinite(vertex.Position) && Math::IsFinite(vertex.Normal) &&
                                                   Math::IsFinite(vertex.Tangent) &&
                                                   std::abs(vertex.Position.X) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Y) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Z) <= maximumCoordinate;
                                        });
                if (!validDeformation)
                    continue;
                std::vector<RenderVertex> builtinVertices;
                builtinVertices.reserve(deformed.size());
                for (const auto& vertex : deformed)
                {
                    builtinVertices.push_back(
                        {vertex.Position,
                         {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                         vertex.Normal});
                }
                item.SkinnedAssetVertices = UploadMeshVertexBuffer(deformed);
                item.SkinnedBuiltinVertices = UploadVertexBuffer(builtinVertices);
                FrameTransientBuffers.push_back(item.SkinnedAssetVertices);
                FrameTransientBuffers.push_back(item.SkinnedBuiltinVertices);
                continue;
            }

            std::vector<GpuSkinMatrix> palette;
            palette.reserve(item.SkinPalette.size());
            for (const auto& matrix : item.SkinPalette)
            {
                const auto& elements = matrix.Elements;
                palette.push_back({
                    {elements[0], elements[1], elements[2], elements[3]},
                    {elements[4], elements[5], elements[6], elements[7]},
                    {elements[8], elements[9], elements[10], elements[11]},
                    {elements[12], elements[13], elements[14], elements[15]},
                });
            }
            auto* paletteBuffer =
                UploadBuffer(std::as_bytes(std::span(palette)), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
            FrameTransientBuffers.push_back(paletteBuffer);

            const GpuSkinInstanceKey instanceKey{packet.Scene, item.Entity};
            auto [instanceIterator, instanceInserted] = cache.Resources.Instances.try_emplace(instanceKey);
            auto& instance = instanceIterator->second;
            if (instanceInserted)
                instance.Outputs.resize(Specification.MaximumFramesInFlight);
            instance.LastPreparedFrame = Statistics.Frame;
            auto& output =
                instance.Outputs[SkinningOutputSlot(ActiveGpuSubmissionSerial, Specification.MaximumFramesInFlight)];
            if (output.Empty())
            {
                output = createOutput(cache.Resources.VertexCount);
                ++SkinningOutputBuilds;
            }
            item.SkinnedAssetVertices = output.AssetVertices;
            item.SkinnedBuiltinVertices = output.BuiltinVertices;

            const std::array writeBindings{SDL_GPUStorageBufferReadWriteBinding{item.SkinnedAssetVertices, false},
                                           SDL_GPUStorageBufferReadWriteBinding{item.SkinnedBuiltinVertices, false}};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                 static_cast<std::uint32_t>(writeBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(skin cache) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, SkinningPipeline);
            const std::array readBindings{mesh.AssetVertices, cache.Resources.Influences, paletteBuffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                             static_cast<std::uint32_t>(readBindings.size()));
            const SkinDispatch dispatch{cache.Resources.VertexCount, cache.Resources.MaximumInfluences};
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (dispatch.VertexCount + 63U) / 64U, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    void RenderSharedState::DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                      const ShadowFrameData& shadows, const SceneDrawPhase phase)
    {
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;
        if (surface.ForwardPlus.Empty())
            throw std::logic_error("Forward+ GPU resources were not prepared before scene recording.");
        std::array<SDL_GPUBuffer*, 3> forwardPlusBuffers{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                         surface.ForwardPlus.LightIndices};

        if (phase == SceneDrawPhase::Opaque && packet.Environment.SkyVisible && pipelines.Sky)
        {
            const auto& environment =
                packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
            if (!environment.Empty())
            {
                const SkyUniforms sky{
                    Math::Inverse(camera.Projection),
                    Math::Inverse(camera.View),
                    {packet.Environment.EnvironmentRotationDegrees, packet.Environment.EnvironmentSpecularIntensity,
                     packet.Environment.Exposure,
                     static_cast<float>(environment.EnvironmentLayout) + (environment.HdrEncoded ? 16.0F : 0.0F)}};
                const SDL_GPUTextureSamplerBinding binding{environment.Texture, environment.Sampler};
                SDL_PushGPUFragmentUniformData(commands, 0, &sky, sizeof(sky));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Sky);
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                ++Statistics.DrawCalls;
            }
        }

        if (phase == SceneDrawPhase::Opaque && packet.DrawGrid && GridBuffer && GridVertexCount > 0)
        {
            const ObjectUniforms object =
                MakeObjectUniforms(Math::Multiply(camera.Projection, camera.View), {}, {}, {1.0F, 1.0F, 1.0F, 1.0F},
                                   lighting, packet.Environment, false);
            const AssetShadowUniforms noShadows{};
            const AssetLocalLightUniforms noLocalLights{};
            const std::array shadowBindings{SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler},
                                            SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler}};
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            SDL_PushGPUFragmentUniformData(commands, 0, &noShadows, sizeof(noShadows));
            SDL_PushGPUFragmentUniformData(commands, 1, &noLocalLights, sizeof(noLocalLights));
            const SDL_GPUBufferBinding binding{GridBuffer, 0};
            SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
            SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                        static_cast<std::uint32_t>(shadowBindings.size()));
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, GridVertexCount, 1, 0, 0);
            ++Statistics.DrawCalls;
        }

        struct PreparedDraw final
        {
            const SceneDrawItem* Item = nullptr;
            MeshSubmesh Submesh;
            AssetId Material;
            MaterialSurfaceState Surface;
            float Depth = 0.0F;
            std::uint32_t SubmeshIndex = 0;
        };
        std::vector<PreparedDraw> prepared;
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Submeshes.empty())
                continue;
            const auto viewFromLocal = Math::Multiply(camera.View, item.World);
            const auto clipFromLocal = Math::Multiply(camera.Projection, viewFromLocal);
            std::uint32_t firstSubmesh = 0;
            std::uint32_t submeshCount = static_cast<std::uint32_t>(mesh.Submeshes.size());
            if (!mesh.Lods.empty())
            {
                const auto height = ProjectedHeight(viewFromLocal, camera.Projection, mesh.Lods.front().Bounds);
                const auto selected =
                    std::ranges::find_if(mesh.Lods, [&](const auto& lod) { return height >= lod.MinimumScreenHeight; });
                const auto& lod = selected != mesh.Lods.end() ? *selected : mesh.Lods.back();
                firstSubmesh = lod.FirstSubmesh;
                submeshCount = lod.SubmeshCount;
            }
            for (std::uint32_t offset = 0; offset < submeshCount; ++offset)
            {
                const auto submeshIndex = firstSubmesh + offset;
                const auto& submesh = mesh.Submeshes[submeshIndex];
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                MaterialSurfaceState surfaceState;
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr)
                    surfaceState = material->Surface;
                const bool transparent = surfaceState.AlphaMode == MaterialAlphaMode::Blend;
                if (transparent != (phase == SceneDrawPhase::Transparent))
                    continue;
                if (!IntersectsFrustum(clipFromLocal, submesh.Bounds))
                {
                    ++Statistics.CulledSubmeshes;
                    continue;
                }
                const Vector3 center{(submesh.Bounds.Minimum.X + submesh.Bounds.Maximum.X) * 0.5F,
                                     (submesh.Bounds.Minimum.Y + submesh.Bounds.Maximum.Y) * 0.5F,
                                     (submesh.Bounds.Minimum.Z + submesh.Bounds.Maximum.Z) * 0.5F};
                prepared.push_back({&item, submesh, materialId, surfaceState,
                                    Math::TransformPoint(viewFromLocal, center).Z, submeshIndex});
                ++Statistics.VisibleSubmeshes;
            }
        }
        std::ranges::stable_sort(
            prepared,
            [](const PreparedDraw& left, const PreparedDraw& right)
            {
                const bool leftBlended = left.Surface.AlphaMode == MaterialAlphaMode::Blend;
                const bool rightBlended = right.Surface.AlphaMode == MaterialAlphaMode::Blend;
                if (leftBlended != rightBlended)
                    return !leftBlended;
                if (leftBlended && left.Depth != right.Depth)
                    return Detail::TransparentBackToFront(left.Depth, right.Depth);
                if (!leftBlended)
                {
                    const auto leftKey =
                        std::tie(left.Surface.AlphaMode, left.Material, left.Item->Mesh, left.SubmeshIndex,
                                 left.Item->ReceiveShadows, left.Item->CastShadows, left.Depth);
                    const auto rightKey =
                        std::tie(right.Surface.AlphaMode, right.Material, right.Item->Mesh, right.SubmeshIndex,
                                 right.Item->ReceiveShadows, right.Item->CastShadows, right.Depth);
                    if (leftKey != rightKey)
                        return leftKey < rightKey;
                }
                if (left.Item->Entity != right.Item->Entity)
                    return left.Item->Entity < right.Item->Entity;
                return left.SubmeshIndex < right.SubmeshIndex;
            });

        AssetLocalLightUniforms localLights{};
        const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
        localLights.Counts.X = static_cast<float>(packet.LocalLights.size());
        localLights.Counts.Y = static_cast<float>(surface.ForwardPlus.Columns);
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            auto& uniform = localLights.Lights[lightIndex];
            uniform.PositionRange = {light.Position.X, light.Position.Y, light.Position.Z, light.Range};
            uniform.DirectionOuter = {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine};
            uniform.ColorIntensity = {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green,
                                      light.ColorAndIntensity.Blue, light.ColorAndIntensity.Alpha};
            uniform.Parameters = {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F,
                                  0.0F};
        }
        enum class FragmentSlot2Binding : std::uint8_t
        {
            None,
            LocalLights,
            Shadows
        };
        auto fragmentSlot2Binding = FragmentSlot2Binding::None;
        AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
        AssetShadowUniforms disabledShadowUniforms{};
        for (auto& parameters : disabledShadowUniforms.Local.Parameters)
            parameters.X = -1.0F;
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex], light.ShadowStrength,
                                                           light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                           std::max(light.ShadowBias * 0.01F, 0.0001F)};
        }

        std::vector<InstanceBatchKey> instanceKeys;
        instanceKeys.reserve(prepared.size());
        for (const auto& draw : prepared)
        {
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            instanceKeys.push_back({draw.Item->Mesh, draw.Material, draw.SubmeshIndex, draw.Surface.AlphaMode,
                                    draw.Item->ReceiveShadows, draw.Item->CastShadows,
                                    material && material->UsesInstancing && !draw.Item->SkinnedAssetVertices});
        }
        const auto batches = BuildInstanceBatches(instanceKeys);
        for (const auto batch : batches)
        {
            const auto drawIndex = static_cast<std::size_t>(batch.First);
            const auto& draw = prepared[drawIndex];
            const auto& item = *draw.Item;
            const auto& mesh = ResolveMesh(item.Mesh);
            const auto viewModel = Math::Multiply(camera.View, item.World);
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            const auto instanceCount = batch.Count;
            SDL_GPUBuffer* instanceBuffer = nullptr;
            if (material && material->UsesInstancing)
            {
                std::vector<GpuInstanceUniform> instances;
                instances.reserve(instanceCount);
                for (std::uint32_t instance = 0; instance < instanceCount; ++instance)
                {
                    const auto& instanceDraw = prepared[drawIndex + instance];
                    instances.push_back({instanceDraw.Item->World, Transpose(Math::Inverse(instanceDraw.Item->World)),
                                         instanceDraw.Item->Tint});
                }
                instanceBuffer =
                    UploadBuffer(std::as_bytes(std::span(instances)), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                FrameTransientBuffers.push_back(instanceBuffer);
            }
            if (material)
            {
                SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
                if (material->UsesForwardPlus)
                    SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                                      static_cast<std::uint32_t>(forwardPlusBuffers.size()));
                const AssetObjectUniforms object{item.World, camera.View, camera.Projection,
                                                 Transpose(Math::Inverse(item.World))};
                AssetSceneUniforms scene{};
                scene.AmbientColorIntensity = {
                    packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                    packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity};
                scene.DirectionalColorIntensity = {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green,
                                                   lighting.ColorAndIntensity.Blue, lighting.ColorAndIntensity.Alpha};
                scene.DirectionalDirectionExposure = {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z,
                                                      packet.Environment.Exposure};
                scene.SurfaceParameters = {material->Surface.AlphaCutoff,
                                           static_cast<float>(material->Surface.AlphaMode),
                                           item.ReceiveShadows ? 1.0F : 0.0F, item.CastShadows ? 1.0F : 0.0F};
                scene.LocalLightCounts = localLights.Counts;
                scene.LocalLights = localLights.Lights;
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                std::array<Vector4, 64> numericProperties;
                std::ranges::copy(material->NumericProperties, numericProperties.begin());
                if (material->TintSlot && !material->UsesInstancing)
                {
                    auto& tint = numericProperties[*material->TintSlot];
                    tint.X *= item.Tint.Red;
                    tint.Y *= item.Tint.Green;
                    tint.Z *= item.Tint.Blue;
                    tint.W *= item.Tint.Alpha;
                }
                SDL_PushGPUFragmentUniformData(
                    commands, 1, numericProperties.data(),
                    static_cast<std::uint32_t>(material->NumericProperties.size() * sizeof(Vector4)));
                if (material->ReceivesShadows)
                {
                    if (fragmentSlot2Binding != FragmentSlot2Binding::Shadows)
                    {
                        SDL_PushGPUFragmentUniformData(commands, 2, &shadowUniforms, sizeof(shadowUniforms));
                        fragmentSlot2Binding = FragmentSlot2Binding::Shadows;
                    }
                }
                else if (fragmentSlot2Binding != FragmentSlot2Binding::LocalLights)
                {
                    SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
                    fragmentSlot2Binding = FragmentSlot2Binding::LocalLights;
                }
                if (!material->Textures.empty() || material->ReceivesShadows)
                {
                    std::array<SDL_GPUTextureSamplerBinding, 18> bindings{};
                    std::ranges::copy(material->Textures, bindings.begin());
                    auto bindingCount = material->Textures.size();
                    if (material->ReceivesShadows)
                    {
                        bindings[bindingCount++] = {surface.Resources.DirectionalShadow
                                                        ? surface.Resources.DirectionalShadow
                                                        : EmptyShadowTexture,
                                                    ShadowSampler};
                        bindings[bindingCount++] = {surface.Resources.LocalShadow ? surface.Resources.LocalShadow
                                                                                  : EmptyShadowTexture,
                                                    ShadowSampler};
                    }
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
                }
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                if (material->UsesInstancing)
                    SDL_BindGPUVertexStorageBuffers(pass, 0, &instanceBuffer, 1);
            }
            else
            {
                const Color tint = draw.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                const ObjectUniforms object =
                    MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), item.World, camera.View, tint,
                                       lighting, packet.Environment, item.ReceiveShadows);
                const auto& builtInShadows = item.ReceiveShadows ? shadowUniforms : disabledShadowUniforms;
                const std::array shadowBindings{
                    SDL_GPUTextureSamplerBinding{
                        surface.Resources.DirectionalShadow ? surface.Resources.DirectionalShadow : EmptyShadowTexture,
                        ShadowSampler},
                    SDL_GPUTextureSamplerBinding{surface.Resources.LocalShadow ? surface.Resources.LocalShadow
                                                                               : EmptyShadowTexture,
                                                 ShadowSampler}};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &builtInShadows, sizeof(builtInShadows));
                SDL_PushGPUFragmentUniformData(commands, 1, &localLights, sizeof(localLights));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                            static_cast<std::uint32_t>(shadowBindings.size()));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedBuiltinVertices ? item.SkinnedBuiltinVertices : mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, instanceCount, draw.Submesh.FirstIndex, 0,
                                         batch.GpuFirstInstance());
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3 * instanceCount;
            Statistics.InstanceBatches += instanceCount > 1 ? 1U : 0U;
        }
    }

    void RenderSharedState::DrawVfx(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                    RenderSurfaceState& surface, const SceneRenderPacket& packet)
    {
        auto& pipelines = PipelinesFor(ToSdlSampleCount(surface.ActualSamples));
        if (packet.Vfx.WorldId() != 0 && pipelines.GpuVfx)
        {
            const auto world = GpuVfxWorlds.find(packet.Vfx.WorldId());
            if (world != GpuVfxWorlds.end() && !world->second.Empty())
            {
                struct alignas(16) CameraUniforms final
                {
                    Matrix4 ViewProjection;
                    std::array<float, 4> Right{};
                    std::array<float, 4> Up{};
                };
                const auto cameraWorld = Math::Inverse(packet.Camera.View);
                const auto right = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
                const auto up = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
                const CameraUniforms camera{Math::Multiply(packet.Camera.Projection, packet.Camera.View),
                                            {right.X, right.Y, right.Z, 0.0F},
                                            {up.X, up.Y, up.Z, 0.0F}};
                SDL_BindGPUGraphicsPipeline(pass, pipelines.GpuVfx);
                SDL_PushGPUVertexUniformData(commands, 0, &camera, sizeof(camera));
                const std::array storage{world->second.Particles, world->second.AliveIndices};
                SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<std::uint32_t>(storage.size()));
                SDL_DrawGPUPrimitivesIndirect(pass, world->second.IndirectArguments, 0, 1);
                ++Statistics.DrawCalls;
                ++Statistics.VfxIndirectDraws;
            }
        }

        const auto particles = packet.Vfx.Particles();
        if (particles.empty())
            return;
        if (!pipelines.Vfx)
            return;

        struct PreparedParticle final
        {
            const VfxRenderParticle* Particle = nullptr;
            float Depth = 0.0F;
            std::uint32_t SpriteFirstVertex = 0;
        };
        std::vector<PreparedParticle> prepared;
        prepared.reserve(particles.size());
        for (const auto& particle : particles)
            prepared.push_back(
                {std::addressof(particle), Math::TransformPoint(packet.Camera.View, particle.Position).Z});
        std::ranges::stable_sort(prepared, [](const auto& left, const auto& right)
                                 { return Detail::TransparentBackToFront(left.Depth, right.Depth); });

        const auto cameraWorld = Math::Inverse(packet.Camera.View);
        const auto cameraRight = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
        const auto cameraUp = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
        const auto cameraForward = Math::TransformDirection(cameraWorld, {0.0F, 0.0F, -1.0F});
        std::vector<RenderVertex> spriteVertices;
        spriteVertices.reserve(std::ranges::count_if(prepared, [](const PreparedParticle& value)
                                                     { return value.Particle->Renderer == VfxRendererType::Sprite; }) *
                               6U);
        constexpr float degreesToRadians = 0.01745329251994329577F;
        for (auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            if (particle.Renderer != VfxRendererType::Sprite)
                continue;
            value.SpriteFirstVertex = static_cast<std::uint32_t>(spriteVertices.size());
            const auto angle = particle.Rotation.Z * degreesToRadians;
            const auto cosine = std::cos(angle);
            const auto sine = std::sin(angle);
            const auto right = Scale(Add(Scale(cameraRight, cosine), Scale(cameraUp, sine)), particle.Size * 0.5F);
            const auto up = Scale(Add(Scale(cameraUp, cosine), Scale(cameraRight, -sine)), particle.Size * 0.5F);
            const auto lowerLeft = Subtract(Subtract(particle.Position, right), up);
            const auto lowerRight = Add(Subtract(particle.Position, up), right);
            const auto upperRight = Add(Add(particle.Position, right), up);
            const auto upperLeft = Add(Subtract(particle.Position, right), up);
            constexpr Vector3 white{1.0F, 1.0F, 1.0F};
            spriteVertices.push_back({lowerLeft, white, cameraForward});
            spriteVertices.push_back({lowerRight, white, cameraForward});
            spriteVertices.push_back({upperRight, white, cameraForward});
            spriteVertices.push_back({lowerLeft, white, cameraForward});
            spriteVertices.push_back({upperRight, white, cameraForward});
            spriteVertices.push_back({upperLeft, white, cameraForward});
        }

        SDL_GPUBuffer* spriteBuffer = nullptr;
        if (!spriteVertices.empty())
        {
            spriteBuffer = UploadVertexBuffer(spriteVertices);
            FrameTransientBuffers.push_back(spriteBuffer);
        }

        const AssetShadowUniforms noShadows{};
        const AssetLocalLightUniforms noLocalLights{};
        const std::array shadowBindings{SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler},
                                        SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler}};
        SDL_BindGPUGraphicsPipeline(pass, pipelines.Vfx);
        SDL_PushGPUFragmentUniformData(commands, 0, &noShadows, sizeof(noShadows));
        SDL_PushGPUFragmentUniformData(commands, 1, &noLocalLights, sizeof(noLocalLights));
        SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(), static_cast<std::uint32_t>(shadowBindings.size()));

        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        for (const auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            if (particle.Size <= 0.0F)
                continue;
            if (particle.Renderer == VfxRendererType::Sprite)
            {
                const ObjectUniforms object = MakeObjectUniforms(viewProjection, {}, packet.Camera.View, particle.Tint,
                                                                 packet.Lighting, packet.Environment, false);
                const SDL_GPUBufferBinding vertexBinding{spriteBuffer, 0};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, value.SpriteFirstVertex, 0);
                ++Statistics.DrawCalls;
                Statistics.Triangles += 2;
                continue;
            }

            const auto& mesh = ResolveMesh(particle.Mesh);
            if (mesh.Empty())
                continue;
            const auto model =
                Math::ComposeTransform(particle.Position, Math::EulerDegreesToQuaternion(particle.Rotation),
                                       {particle.Size, particle.Size, particle.Size});
            const ObjectUniforms object =
                MakeObjectUniforms(Math::Multiply(viewProjection, model), model, packet.Camera.View, particle.Tint,
                                   packet.Lighting, packet.Environment, false);
            const SDL_GPUBufferBinding vertexBinding{mesh.Vertices, 0};
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            for (const auto& submesh : mesh.Submeshes)
            {
                SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
                ++Statistics.DrawCalls;
                Statistics.Triangles += submesh.IndexCount / 3U;
            }
        }
    }

    void RenderSharedState::RecordSampledDepth(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                               const SceneRenderPacket& packet)
    {
        if (!surface.Resources.SampledDepth || !SceneDepthPipeline)
            return;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = surface.Resources.SampledDepth;
        depth.clear_depth = 1.0F;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(sampled depth) failed: " + LastSdlError());
        SDL_BindGPUGraphicsPipeline(pass, SceneDepthPipeline);
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty())
                continue;
            const auto object = Math::Multiply(viewProjection, item.World);
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            const SDL_GPUBufferBinding vertexBinding{
                item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            for (const auto& submesh : mesh.Submeshes)
            {
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                    material && material->Surface.AlphaMode == MaterialAlphaMode::Blend)
                {
                    continue;
                }
                SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.SampledResolvedDepthAvailable = true;
    }

    ShadowFrameData RenderSharedState::RecordShadows(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                     const SceneRenderPacket& packet)
    {
        ShadowFrameData result;
        result.LocalLayers.fill(-1.0F);
        if (!ShadowPipeline || !ShadowSampler)
            return result;

        const auto ensureTexture = [&](SDL_GPUTexture*& texture, std::uint32_t& currentResolution,
                                       std::uint32_t& currentLayers, const std::uint32_t resolution,
                                       const std::uint32_t layers)
        {
            if (texture && currentResolution == resolution && currentLayers == layers)
                return;
            if (texture)
            {
                GpuTextureResources retired;
                retired.Texture = texture;
                Retire(std::move(retired));
                texture = nullptr;
            }
            SDL_GPUTextureCreateInfo information{};
            information.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            information.format = ShadowDepthFormat;
            information.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            information.width = resolution;
            information.height = resolution;
            information.layer_count_or_depth = layers;
            information.num_levels = 1;
            information.sample_count = SDL_GPU_SAMPLECOUNT_1;
            texture = SDL_CreateGPUTexture(Device, &information);
            if (!texture)
                throw std::runtime_error("SDL_CreateGPUTexture(shadow array) failed: " + LastSdlError());
            currentResolution = resolution;
            currentLayers = layers;
        };

        const auto drawLayer = [&](SDL_GPUTexture* texture, const std::uint32_t layer, const Matrix4& lightMatrix)
        {
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = texture;
            depth.clear_depth = 1.0F;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.layer = static_cast<std::uint8_t>(layer);
            auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPURenderPass(shadow) failed: " + LastSdlError());
            SDL_BindGPUGraphicsPipeline(pass, ShadowPipeline);
            for (const auto& item : packet.DrawItems)
            {
                if (!item.CastShadows)
                    continue;
                const auto& mesh = ResolveMesh(item.Mesh);
                if (mesh.Empty())
                    continue;
                const auto object = Math::Multiply(lightMatrix, item.World);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                for (const auto& submesh : mesh.Submeshes)
                    SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
            SDL_EndGPURenderPass(pass);
            ++Statistics.Passes;
        };

        if (packet.Lighting.Enabled && packet.Lighting.Shadows != ShadowQuality::Disabled)
        {
            const auto cascadeCount = std::clamp(packet.Environment.DirectionalShadowCascadeCount, 1U, 4U);
            const auto resolution = packet.Environment.DirectionalShadowResolution;
            ensureTexture(surface.Resources.DirectionalShadow, surface.Resources.DirectionalShadowResolution,
                          surface.Resources.DirectionalShadowLayers, resolution, cascadeCount);
            const float nearPlane = std::max(packet.Camera.NearPlane, 0.0001F);
            const float shadowDistance = std::min(
                std::max(packet.Environment.DirectionalShadowDistance, nearPlane + 0.0001F), packet.Camera.FarPlane);
            const auto splits = BuildPracticalCascadeSplits(nearPlane, shadowDistance, cascadeCount,
                                                            packet.Environment.DirectionalShadowSplitLambda);
            const auto inverseViewProjection =
                Math::Inverse(Math::Multiply(packet.Camera.Projection, packet.Camera.View));
            std::array<Vector3, 4> nearCorners{};
            std::array<Vector3, 4> farCorners{};
            constexpr std::array<Vector2, 4> coordinates{Vector2{-1.0F, -1.0F}, Vector2{1.0F, -1.0F},
                                                         Vector2{1.0F, 1.0F}, Vector2{-1.0F, 1.0F}};
            for (std::size_t index = 0; index < coordinates.size(); ++index)
            {
                const auto nearClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 0.0F});
                const auto farClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 1.0F});
                nearCorners[index] = {nearClip.X / nearClip.W, nearClip.Y / nearClip.W, nearClip.Z / nearClip.W};
                farCorners[index] = {farClip.X / farClip.W, farClip.Y / farClip.W, farClip.Z / farClip.W};
            }
            const auto direction = Normalize(
                Vector3{packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z});
            float previousSplit = nearPlane;
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            {
                const float nearRatio = (previousSplit - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                const float farRatio = (splits[cascade] - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                std::array<Vector3, 8> corners{};
                for (std::size_t corner = 0; corner < 4; ++corner)
                {
                    const auto ray = Subtract(farCorners[corner], nearCorners[corner]);
                    corners[corner] = Add(nearCorners[corner], Scale(ray, nearRatio));
                    corners[corner + 4] = Add(nearCorners[corner], Scale(ray, farRatio));
                }
                Vector3 center{};
                for (const auto corner : corners)
                    center = Add(center, corner);
                center = Scale(center, 1.0F / static_cast<float>(corners.size()));
                float radius = 0.0F;
                for (const auto corner : corners)
                    radius = std::max(radius, Length(Subtract(corner, center)));
                radius = std::max(std::ceil(radius * 16.0F) / 16.0F, 0.25F);
                const auto up = std::abs(direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                const auto eye = Subtract(center, Scale(direction, radius * 2.0F));
                const auto view = Math::LookAt(eye, center, up);
                const auto projection = Math::Orthographic(radius * 2.0F, 1.0F, 0.01F, radius * 4.0F);
                result.Directional.DirectionalMatrices[cascade] = Math::Multiply(projection, view);
                drawLayer(surface.Resources.DirectionalShadow, cascade,
                          result.Directional.DirectionalMatrices[cascade]);
                switch (cascade)
                {
                case 0:
                    result.Directional.DirectionalCascadeSplits.X = splits[cascade];
                    break;
                case 1:
                    result.Directional.DirectionalCascadeSplits.Y = splits[cascade];
                    break;
                case 2:
                    result.Directional.DirectionalCascadeSplits.Z = splits[cascade];
                    break;
                default:
                    result.Directional.DirectionalCascadeSplits.W = splits[cascade];
                    break;
                }
                previousSplit = splits[cascade];
            }
            const float encodedCascadeCount = packet.Lighting.Shadows == ShadowQuality::Hard
                                                  ? -static_cast<float>(cascadeCount)
                                                  : static_cast<float>(cascadeCount);
            result.Directional.DirectionalParameters = {encodedCascadeCount, packet.Lighting.ShadowStrength,
                                                        std::max(packet.Lighting.ShadowBias * 0.01F, 0.0001F),
                                                        1.0F / static_cast<float>(resolution)};
            Statistics.DirectionalShadowCascades += cascadeCount;
        }

        const bool hasLocalShadows = std::ranges::any_of(packet.LocalLights, [](const SceneLocalLight& light)
                                                         { return light.Shadows != ShadowQuality::Disabled; });
        if (hasLocalShadows)
        {
            ensureTexture(surface.Resources.LocalShadow, surface.Resources.LocalShadowResolution,
                          surface.Resources.LocalShadowLayers, LocalShadowResolution, LocalShadowLayerCount);
            std::size_t spotCount = 0;
            std::size_t pointCount = 0;
            constexpr float radiansToDegrees = 57.295779513082320876F;
            constexpr std::array<Vector3, 6> pointDirections{Vector3{1.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F},
                                                             Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, -1.0F, 0.0F},
                                                             Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 0.0F, -1.0F}};
            constexpr std::array<Vector3, 6> pointUps{Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F},
                                                      Vector3{0.0F, 0.0F, -1.0F}, Vector3{0.0F, 0.0F, 1.0F},
                                                      Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F}};
            const auto lightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                if (light.Type == SceneLocalLightType::Spot && spotCount < MaximumShadowedSpotLights)
                {
                    const float outerAngle = std::acos(std::clamp(light.OuterConeCosine, -1.0F, 1.0F));
                    const auto up =
                        std::abs(light.Direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                    const auto view = Math::LookAt(light.Position, Add(light.Position, light.Direction), up);
                    const auto projection = Math::Perspective(
                        std::clamp(outerAngle * 2.0F * radiansToDegrees, 1.01F, 178.0F), 1.0F, 0.05F, light.Range);
                    result.Local.Matrices[spotCount] = Math::Multiply(projection, view);
                    result.LocalLayers[lightIndex] = static_cast<float>(spotCount);
                    drawLayer(surface.Resources.LocalShadow, static_cast<std::uint32_t>(spotCount),
                              result.Local.Matrices[spotCount]);
                    ++spotCount;
                }
                else if (light.Type == SceneLocalLightType::Point && pointCount < MaximumShadowedPointLights)
                {
                    const auto baseLayer = static_cast<std::uint32_t>(MaximumShadowedSpotLights + pointCount * 6U);
                    result.LocalLayers[lightIndex] = static_cast<float>(baseLayer);
                    const auto projection = Math::Perspective(90.0F, 1.0F, 0.05F, light.Range);
                    for (std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto view =
                            Math::LookAt(light.Position, Add(light.Position, pointDirections[face]), pointUps[face]);
                        result.Local.Matrices[baseLayer + face] = Math::Multiply(projection, view);
                        drawLayer(surface.Resources.LocalShadow, baseLayer + face,
                                  result.Local.Matrices[baseLayer + face]);
                    }
                    ++pointCount;
                }
            }
        }
        return result;
    }

    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
    {
        if (!surface.Resources.SampledColor || !surface.Resources.HdrColor)
            return;

        const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
        if (request != Requests.end())
        {
            PrepareSkinning(commands, request->Packet);
            PrepareGpuVfx(commands, request->Packet.Vfx);
        }
        ShadowFrameData shadows;
        shadows.LocalLayers.fill(-1.0F);
        CallbackFrameGraphExecutionContext execution(
            [&](const CompiledFrameGraph::Transition&) { ++Statistics.FrameGraphTransitions; },
            [&](const FrameGraphPass frameGraphPass)
            {
                ++Statistics.ExecutedFrameGraphPasses;
                if (frameGraphPass == SceneFrameGraph.DirectionalShadows)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != Requests.end())
                        shadows = RecordShadows(commands, surface, request->Packet);
                    Statistics.ShadowRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ForwardPlusCulling)
                {
                    if (request == Requests.end())
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    auto contentHash = std::uint64_t{1469598103934665603ULL};
                    const auto hashValue = [&](const auto value)
                    {
                        const auto bytes = std::as_bytes(std::span(std::addressof(value), 1));
                        for (const auto byte : bytes)
                        {
                            contentHash ^= std::to_integer<std::uint8_t>(byte);
                            contentHash *= 1099511628211ULL;
                        }
                    };
                    hashValue(surface.Width);
                    hashValue(surface.Height);
                    for (const auto value : request->Packet.Camera.View.Elements)
                        hashValue(value);
                    for (const auto value : request->Packet.Camera.Projection.Elements)
                        hashValue(value);
                    for (const auto& light : request->Packet.LocalLights)
                    {
                        hashValue(light.Position.X);
                        hashValue(light.Position.Y);
                        hashValue(light.Position.Z);
                        hashValue(light.Range);
                        hashValue(light.Direction.X);
                        hashValue(light.Direction.Y);
                        hashValue(light.Direction.Z);
                        hashValue(light.OuterConeCosine);
                        hashValue(light.ColorAndIntensity.Red);
                        hashValue(light.ColorAndIntensity.Green);
                        hashValue(light.ColorAndIntensity.Blue);
                        hashValue(light.ColorAndIntensity.Alpha);
                        hashValue(light.InnerConeCosine);
                        hashValue(light.Type);
                    }
                    Statistics.VisibleLocalLights += static_cast<std::uint32_t>(request->Packet.LocalLights.size());
                    if (surface.ForwardPlusContentValid && surface.ForwardPlusContentHash == contentHash &&
                        !surface.ForwardPlus.Empty())
                    {
                        ++Statistics.ForwardPlusCacheHits;
                        Statistics.ForwardPlusCullingMilliseconds +=
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
                                .count();
                        return;
                    }
                    std::vector<ForwardPlusLightBounds> localLightBounds;
                    localLightBounds.reserve(request->Packet.LocalLights.size());
                    for (const auto& light : request->Packet.LocalLights)
                        localLightBounds.push_back(
                            {Math::TransformPoint(request->Packet.Camera.View, light.Position), light.Range});
                    const auto tiles = BuildForwardPlusCpuTiles(surface.Width, surface.Height,
                                                                request->Packet.Camera.Projection, localLightBounds);
                    Statistics.OverflowedLightTiles += tiles.OverflowedTiles;
                    std::vector<AssetLocalLightUniform> gpuLights(
                        std::max<std::size_t>(1, request->Packet.LocalLights.size()));
                    for (std::size_t lightIndex = 0; lightIndex < request->Packet.LocalLights.size(); ++lightIndex)
                    {
                        const auto& light = request->Packet.LocalLights[lightIndex];
                        gpuLights[lightIndex] = {
                            {light.Position.X, light.Position.Y, light.Position.Z, light.Range},
                            {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine},
                            {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green, light.ColorAndIntensity.Blue,
                             light.ColorAndIntensity.Alpha},
                            {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F, 0.0F}};
                    }
                    std::vector<ForwardPlusTileUniform> gpuTiles(tiles.Offsets.size());
                    for (std::size_t tileIndex = 0; tileIndex < gpuTiles.size(); ++tileIndex)
                        gpuTiles[tileIndex] = {tiles.Offsets[tileIndex], tiles.Counts[tileIndex]};
                    std::vector<ForwardPlusIndexGroup> gpuIndices(
                        std::max<std::size_t>(1, (tiles.LightIndices.size() + 3U) / 4U));
                    for (std::size_t index = 0; index < tiles.LightIndices.size(); ++index)
                        gpuIndices[index / 4U].Indices[index % 4U] = tiles.LightIndices[index];

                    const std::array payloads{std::as_bytes(std::span(gpuLights)), std::as_bytes(std::span(gpuTiles)),
                                              std::as_bytes(std::span(gpuIndices))};
                    const auto capacityFor = [](const std::size_t required)
                    {
                        if (required == 0 || required > std::numeric_limits<std::uint32_t>::max())
                            throw std::invalid_argument("Forward+ buffer payload exceeds SDL's 32-bit limit.");
                        auto capacity = std::uint32_t{256};
                        while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U)
                            capacity *= 2U;
                        if (capacity < required)
                            capacity = static_cast<std::uint32_t>(required);
                        return capacity;
                    };
                    const std::array requiredCapacities{capacityFor(payloads[0].size()),
                                                        capacityFor(payloads[1].size()),
                                                        capacityFor(payloads[2].size())};
                    const bool requiresReplacement =
                        surface.ForwardPlus.Empty() || surface.ForwardPlus.LightCapacityBytes < requiredCapacities[0] ||
                        surface.ForwardPlus.TileCapacityBytes < requiredCapacities[1] ||
                        surface.ForwardPlus.LightIndexCapacityBytes < requiredCapacities[2];
                    if (requiresReplacement)
                    {
                        ForwardPlusGpuResources replacement;
                        const auto createBuffer = [&](const std::uint32_t byteSize)
                        {
                            SDL_GPUBufferCreateInfo information{};
                            information.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                            information.size = byteSize;
                            auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                            if (!buffer)
                                throw std::runtime_error("SDL_CreateGPUBuffer(Forward+) failed: " + LastSdlError());
                            return buffer;
                        };
                        try
                        {
                            replacement.Lights = createBuffer(requiredCapacities[0]);
                            replacement.Tiles = createBuffer(requiredCapacities[1]);
                            replacement.LightIndices = createBuffer(requiredCapacities[2]);
                            replacement.LightCapacityBytes = requiredCapacities[0];
                            replacement.TileCapacityBytes = requiredCapacities[1];
                            replacement.LightIndexCapacityBytes = requiredCapacities[2];
                        }
                        catch (...)
                        {
                            ReleaseForwardPlusResources(replacement);
                            throw;
                        }
                        Retire(std::exchange(surface.ForwardPlus, replacement));
                        ++Statistics.ForwardPlusBufferReallocations;
                    }

                    std::size_t totalBytes = 0;
                    for (const auto payload : payloads)
                    {
                        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                            throw std::invalid_argument("Combined Forward+ upload exceeds SDL's 32-bit limit.");
                        totalBytes += payload.size();
                    }
                    SDL_GPUTransferBuffer* transfer = nullptr;
                    try
                    {
                        SDL_GPUTransferBufferCreateInfo information{};
                        information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                        information.size = static_cast<std::uint32_t>(totalBytes);
                        transfer = SDL_CreateGPUTransferBuffer(Device, &information);
                        if (!transfer)
                            throw std::runtime_error("SDL_CreateGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
                        if (!mapped)
                            throw std::runtime_error("SDL_MapGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        std::size_t offset = 0;
                        for (const auto payload : payloads)
                        {
                            std::memcpy(mapped + offset, payload.data(), payload.size());
                            offset += payload.size();
                        }
                        SDL_UnmapGPUTransferBuffer(Device, transfer);

                        EnsureFrameUploadContext();
                        const std::array destinations{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                      surface.ForwardPlus.LightIndices};
                        offset = 0;
                        for (std::size_t index = 0; index < payloads.size(); ++index)
                        {
                            SDL_GPUTransferBufferLocation source{transfer, static_cast<std::uint32_t>(offset)};
                            SDL_GPUBufferRegion destination{destinations[index], 0,
                                                            static_cast<std::uint32_t>(payloads[index].size())};
                            SDL_UploadToGPUBuffer(FrameUploadPass, &source, &destination, true);
                            offset += payloads[index].size();
                        }
                        FrameUploadTransfers.push_back(transfer);
                        transfer = nullptr;
                    }
                    catch (...)
                    {
                        if (transfer)
                            SDL_ReleaseGPUTransferBuffer(Device, transfer);
                        throw;
                    }
                    surface.ForwardPlus.Columns = tiles.Columns;
                    surface.ForwardPlus.Rows = tiles.Rows;
                    surface.ForwardPlusContentHash = contentHash;
                    surface.ForwardPlusContentValid = true;
                    Statistics.ForwardPlusUploadBytes += totalBytes;
                    Statistics.ForwardPlusCullingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Opaque)
                {
                    const auto started = std::chrono::steady_clock::now();
                    SDL_GPUColorTargetInfo color{};
                    color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                          : surface.Resources.HdrColor;
                    color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                         surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                    color.load_op = SDL_GPU_LOADOP_CLEAR;
                    color.store_op = surface.Resources.MultisampleHdrColor ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                                                           : SDL_GPU_STOREOP_STORE;
                    color.resolve_texture =
                        surface.Resources.MultisampleHdrColor ? surface.Resources.HdrColor : nullptr;
                    SDL_GPUDepthStencilTargetInfo depth{};
                    SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                    if (surface.Resources.Depth)
                    {
                        depth.texture = surface.Resources.Depth;
                        depth.clear_depth = 1.0F;
                        depth.load_op = SDL_GPU_LOADOP_CLEAR;
                        depth.store_op = SDL_GPU_STOREOP_STORE;
                        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depthPointer = &depth;
                    }
                    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                    if (!pass)
                        throw std::runtime_error("SDL_BeginGPURenderPass(HDR scene) failed: " + LastSdlError());
                    if (request != Requests.end())
                        DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Opaque);
                    SDL_EndGPURenderPass(pass);
                    ++Statistics.Passes;
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ResolveDepth)
                {
                    if (request != Requests.end())
                        RecordSampledDepth(commands, surface, request->Packet);
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Transparency)
                {
                    const auto started = std::chrono::steady_clock::now();
                    SDL_GPUColorTargetInfo color{};
                    color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                          : surface.Resources.HdrColor;
                    color.load_op = SDL_GPU_LOADOP_LOAD;
                    color.store_op =
                        surface.Resources.MultisampleHdrColor ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
                    color.resolve_texture =
                        surface.Resources.MultisampleHdrColor ? surface.Resources.HdrColor : nullptr;
                    SDL_GPUDepthStencilTargetInfo depth{};
                    SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                    if (surface.Resources.Depth)
                    {
                        depth.texture = surface.Resources.Depth;
                        depth.load_op = SDL_GPU_LOADOP_LOAD;
                        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depthPointer = &depth;
                    }
                    auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                    if (!pass)
                        throw std::runtime_error("SDL_BeginGPURenderPass(transparency) failed: " + LastSdlError());
                    if (request != Requests.end())
                    {
                        DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Transparent);
                        DrawVfx(commands, pass, surface, request->Packet);
                    }
                    SDL_EndGPURenderPass(pass);
                    ++Statistics.Passes;
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ToneMap)
                {
                    const auto started = std::chrono::steady_clock::now();
                    RecordToneMap(commands, surface);
                    Statistics.ToneMapMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                }
            });
        SceneFrameGraph.Graph.Execute(SceneFrameGraph.Compiled, execution);
        ++Statistics.Surfaces;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface)
    {
        if (!ToneMapPipeline || !ToneMapSampler || !surface.Resources.HdrColor || !surface.Resources.SampledColor)
            throw std::logic_error("Tone-map resources are unavailable for an active render surface.");
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.HasOutput ? surface.Resources.ExchangeColor : surface.Resources.SampledColor;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
        const SDL_GPUTextureSamplerBinding binding{surface.Resources.HdrColor, ToneMapSampler};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::EndFrame(ImDrawData* drawData)
    {
        RequireOwner("EndFrame");
        if (!FrameActive)
            throw std::logic_error("No render frame is active.");
        FrameActive = false;

        if (Specification.Mode == RenderMode::Headless)
        {
            Statistics.Surfaces = static_cast<std::uint32_t>(LiveSurfaces().size());
            Statistics.Passes = Statistics.Surfaces;
            return;
        }

        const auto started = std::chrono::steady_clock::now();
        DispatchRender([this, drawData] { ExecuteFrame(drawData); });
        Statistics.RendererLatencyMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::ExecuteFrame(ImDrawData* drawData)
    {
        Statistics.CommandRecordingMilliseconds = 0.0F;
        Statistics.ShadowRecordingMilliseconds = 0.0F;
        Statistics.ForwardPlusCullingMilliseconds = 0.0F;
        Statistics.ScenePassMilliseconds = 0.0F;
        Statistics.ToneMapMilliseconds = 0.0F;
        Statistics.FrameUploadMilliseconds = 0.0F;
        Statistics.SwapchainWaitMilliseconds = 0.0F;
        Statistics.UiRecordingMilliseconds = 0.0F;
        Statistics.GpuSubmissionMilliseconds = 0.0F;
        Statistics.GpuFrameMilliseconds = 0.0F;
        Statistics.ForwardPlusCacheHits = 0;
        if (InjectDeviceLossAtNextFrame.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("Injected GPU device loss.");
        if (FrameUploadCommands || FrameUploadPass || !FrameUploadTransfers.empty())
            throw std::logic_error("A previous frame left the GPU upload context active.");

        if (GpuSubmissionSerial == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("GPU submission serial exhausted.");
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
        if (!commands)
            throw std::runtime_error("SDL_AcquireGPUCommandBuffer failed: " + LastSdlError());
        ActiveGpuSubmissionSerial = GpuSubmissionSerial + 1U;
        FrameExecutionActive = true;
        bool frameUploadsSubmitted = false;

        try
        {
            const auto recordingStarted = std::chrono::steady_clock::now();
            for (const auto& request : Requests)
                RecordSurface(commands, *request.Surface);
            Statistics.CommandRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordingStarted).count();

            const auto uploadStarted = std::chrono::steady_clock::now();
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                auto* uploadCommands = std::exchange(FrameUploadCommands, nullptr);
                if (FrameUploadTransfers.empty())
                {
                    (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                }
                else if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                {
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
                }
                else
                {
                    frameUploadsSubmitted = true;
                }
            }
            Statistics.FrameUploadMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStarted).count();

            SDL_GPUTexture* swapchain = nullptr;
            const auto swapchainStarted = std::chrono::steady_clock::now();
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, nullptr, nullptr))
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                commands = nullptr;
                throw std::runtime_error("SDL_WaitAndAcquireGPUSwapchainTexture failed: " + LastSdlError());
            }
            Statistics.SwapchainWaitMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - swapchainStarted).count();

            const auto uiRecordingStarted = std::chrono::steady_clock::now();
            if (swapchain)
            {
                const bool renderUi = drawData && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F;
                if (renderUi)
                    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

                SDL_GPUColorTargetInfo target{};
                target.texture = swapchain;
                target.clear_color = {Specification.SwapchainClearColor.Red, Specification.SwapchainClearColor.Green,
                                      Specification.SwapchainClearColor.Blue, Specification.SwapchainClearColor.Alpha};
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPURenderPass(swapchain) failed: " + LastSdlError());
                if (renderUi)
                    ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
            }
            Statistics.UiRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uiRecordingStarted).count();

            const auto submissionStarted = std::chrono::steady_clock::now();
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            commands = nullptr;
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
            Statistics.GpuSubmissionMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - submissionStarted).count();
            InFlight.push_back({fence, std::move(PendingRetired), std::move(PendingRetiredMeshes),
                                std::move(PendingRetiredSkins), std::move(PendingRetiredTextures),
                                std::move(PendingRetiredPipelines), std::move(PendingRetiredForwardPlus),
                                std::move(FrameTransientBuffers), std::move(FrameUploadTransfers)});
            PendingRetired.clear();
            PendingRetiredMeshes.clear();
            PendingRetiredSkins.clear();
            PendingRetiredTextures.clear();
            PendingRetiredPipelines.clear();
            PendingRetiredForwardPlus.clear();
            FrameTransientBuffers.clear();
            FrameUploadTransfers.clear();
            GpuSubmissionSerial = ActiveGpuSubmissionSerial;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            for (const auto& request : Requests)
            {
                auto* surface = request.Surface;
                if (surface->HasOutput)
                    std::swap(surface->Resources.SampledColor, surface->Resources.ExchangeColor);
                else
                    surface->HasOutput = true;
            }
        }
        catch (...)
        {
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
                FrameUploadCommands = nullptr;
            }
            if (frameUploadsSubmitted && Device)
                (void)SDL_WaitForGPUIdle(Device);
            for (auto* transfer : FrameUploadTransfers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            FrameUploadTransfers.clear();
            if (commands)
                (void)SDL_CancelGPUCommandBuffer(commands);
            for (auto* buffer : FrameTransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            FrameTransientBuffers.clear();
            FrameActive = false;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            throw;
        }
    }

    void RenderSharedState::Close() noexcept
    {
        if (!Open)
            return;
        Open = false;
        FrameActive = false;
        StopRenderThread();
        FrameExecutionActive = false;
        ActiveGpuSubmissionSerial = 0;

        if (Device)
            (void)SDL_WaitForGPUIdle(Device);
        for (const auto& surface : LiveSurfaces())
        {
            ReleaseResources(surface->Resources);
            ReleaseForwardPlusResources(surface->ForwardPlus);
            surface->Owner.reset();
            surface->Width = 0;
            surface->Height = 0;
        }
        for (auto& resources : PendingRetired)
            ReleaseResources(resources);
        PendingRetired.clear();
        for (auto& resources : PendingRetiredMeshes)
            ReleaseMeshResources(resources);
        PendingRetiredMeshes.clear();
        for (auto& resources : PendingRetiredSkins)
            ReleaseGpuSkinResources(resources);
        PendingRetiredSkins.clear();
        for (auto& resources : PendingRetiredTextures)
            ReleaseTextureResources(resources);
        PendingRetiredTextures.clear();
        for (auto* pipeline : PendingRetiredPipelines)
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        PendingRetiredPipelines.clear();
        for (auto& resources : PendingRetiredForwardPlus)
            ReleaseForwardPlusResources(resources);
        PendingRetiredForwardPlus.clear();
        for (auto* buffer : FrameTransientBuffers)
            SDL_ReleaseGPUBuffer(Device, buffer);
        FrameTransientBuffers.clear();
        for (auto* transfer : FrameUploadTransfers)
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
        FrameUploadTransfers.clear();
        FrameUploadPass = nullptr;
        FrameUploadCommands = nullptr;
        for (auto& frame : InFlight)
        {
            for (auto& resources : frame.Retired)
                ReleaseResources(resources);
            for (auto& resources : frame.RetiredMeshes)
                ReleaseMeshResources(resources);
            for (auto& resources : frame.RetiredSkins)
                ReleaseGpuSkinResources(resources);
            for (auto& resources : frame.RetiredTextures)
                ReleaseTextureResources(resources);
            for (auto* pipeline : frame.RetiredPipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            for (auto& resources : frame.RetiredForwardPlus)
                ReleaseForwardPlusResources(resources);
            for (auto* buffer : frame.TransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            for (auto* transfer : frame.TransientTransferBuffers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (Device && frame.Fence)
                SDL_ReleaseGPUFence(Device, frame.Fence);
        }
        InFlight.clear();
        Requests.clear();

        for (auto& pipelines : Pipelines)
        {
            if (pipelines.GpuVfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.GpuVfx);
            if (pipelines.Vfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Vfx);
            if (pipelines.Sky)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Sky);
            if (pipelines.Grid)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Grid);
            if (pipelines.Cube)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Cube);
        }
        Pipelines.clear();
        for (auto& [id, entry] : MeshCache)
        {
            (void)id;
            ReleaseMeshResources(entry.Resources);
        }
        MeshCache.clear();
        for (auto& [id, entry] : SkinCache)
        {
            (void)id;
            ReleaseGpuSkinResources(entry.Resources);
        }
        SkinCache.clear();
        for (auto& [id, entry] : TextureCache)
        {
            (void)id;
            ReleaseTextureResources(entry.Resources);
        }
        TextureCache.clear();
        MaterialCache.clear();
        for (auto& [id, entry] : ShaderCache)
        {
            (void)id;
            for (const auto& pipeline : entry.Pipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline.Handle);
        }
        ShaderCache.clear();
        ReleaseTextureResources(CheckerboardTexture);
        ReleaseTextureResources(DefaultSkyTexture);
        ReleaseTextureResources(WhiteTexture);
        ReleaseTextureResources(FlatNormalTexture);
        ReleaseTextureResources(NeutralOrmTexture);
        ReleaseTextureResources(BlackTexture);
        ReleaseTextureResources(BlackDataTexture);
        ReleaseTextureResources(WhiteDataTexture);
        for (const auto& [description, sampler] : SamplerCache)
        {
            (void)description;
            SDL_ReleaseGPUSampler(Device, sampler);
        }
        SamplerCache.clear();
        if (ShadowSampler)
            SDL_ReleaseGPUSampler(Device, ShadowSampler);
        ShadowSampler = nullptr;
        if (ToneMapSampler)
            SDL_ReleaseGPUSampler(Device, ToneMapSampler);
        ToneMapSampler = nullptr;
        if (EmptyShadowTexture)
            SDL_ReleaseGPUTexture(Device, EmptyShadowTexture);
        EmptyShadowTexture = nullptr;
        if (ShadowPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ShadowPipeline);
        ShadowPipeline = nullptr;
        if (SceneDepthPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, SceneDepthPipeline);
        SceneDepthPipeline = nullptr;
        if (SkinningPipeline)
            SDL_ReleaseGPUComputePipeline(Device, SkinningPipeline);
        SkinningPipeline = nullptr;
        SkinningPipelineAttempted = false;
        for (auto& [world, resources] : GpuVfxWorlds)
        {
            (void)world;
            ReleaseGpuVfxWorld(resources);
        }
        GpuVfxWorlds.clear();
        if (VfxFinalizePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxFinalizePipeline);
        if (VfxSpawnPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxSpawnPipeline);
        if (VfxSimulatePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxSimulatePipeline);
        if (VfxResetPipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxResetPipeline);
        if (VfxInitializePipeline)
            SDL_ReleaseGPUComputePipeline(Device, VfxInitializePipeline);
        VfxFinalizePipeline = nullptr;
        VfxSpawnPipeline = nullptr;
        VfxSimulatePipeline = nullptr;
        VfxResetPipeline = nullptr;
        VfxInitializePipeline = nullptr;
        VfxPipelinesAttempted = false;
        if (ToneMapPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ToneMapPipeline);
        ToneMapPipeline = nullptr;
        ReleaseMeshResources(ErrorMesh);
        ReleaseMeshResources(DefaultMesh);
        if (GridBuffer)
            SDL_ReleaseGPUBuffer(Device, GridBuffer);
        GridBuffer = nullptr;
        GridVertexCount = 0;

        if (WindowClaimed && Device && NativeWindow)
            SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
        WindowClaimed = false;
        if (Device)
            SDL_DestroyGPUDevice(Device);
        Device = nullptr;
        NativeWindow = nullptr;
        Window.Reset();
        Windows.Reset();
        Assets.Reset();
    }
} // namespace Keire::RenderBackend
