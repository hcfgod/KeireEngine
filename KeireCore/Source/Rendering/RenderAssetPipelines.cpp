#include "KeireInternal/Rendering/MaterialBlendingInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire::RenderBackend
{
    void RenderSharedState::StartAssetPipelinePreparation(AssetId id, Ref<const ShaderAsset> shader,
                                                          std::uint64_t revision,
                                                          std::vector<GpuShaderEntry::Pipeline> pipelines)
    {
        if (!RenderJobs || AssetPipelineWork.Job)
        {
            throw std::logic_error("Material pipeline preparation requires an available renderer job scope.");
        }
        auto batch = std::make_shared<GpuAssetPipelineBatch>(Device, pipelines.size());
        AssetPipelineWork.Id = id;
        AssetPipelineWork.Shader = shader;
        AssetPipelineWork.Revision = revision;
        AssetPipelineWork.Generation = DeviceGeneration.load(std::memory_order_acquire);
        AssetPipelineWork.Pipelines = pipelines;
        AssetPipelineWork.Batch = batch;
        auto* const device = Device;
        const auto colorFormat = SceneColorFormat;
        const auto depthFormat = DepthFormat;
        try
        {
            AssetPipelineWork.Job = RenderJobs->Submit(
                {.Name = "Material pipeline preparation",
                 .Priority = JobPriority::Low,
                 .Class = JobClass::Blocking,
                 .Domain = JobDomain::Rendering},
                [batch, shader, pipelines = std::move(pipelines), device, colorFormat, depthFormat](JobContext& context)
                {
                    batch->Prepare(
                        [&](std::size_t index, GpuAssetPipelineBatch::Slot& slot)
                        {
                            const auto& descriptor = pipelines[index];
                            MaterialSurfaceState surface;
                            surface.AlphaMode = descriptor.AlphaMode;
                            surface.DoubleSided = descriptor.DoubleSided;
                            slot.Pipeline = CreateAssetPipelineForDevice(
                                device, colorFormat, depthFormat, shader->Definition(), descriptor.Samples, surface,
                                RuntimeMaterialPassName(descriptor.PassRole), slot.Vertex, slot.Fragment);
                        },
                        [&context] { return context.StopRequested(); });
                });
        }
        catch (...)
        {
            CancelAssetPipelinePreparation(false);
            throw;
        }
    }

    void RenderSharedState::CancelAssetPipelinePreparation(bool abandon) noexcept
    {
        if (AssetPipelineWork.Job)
        {
            AssetPipelineWork.Job.Cancel();
            (void)AssetPipelineWork.Job.Wait();
            if (!abandon)
            {
                try
                {
                    AssetPipelineWork.Job.Result().RethrowIfFailed();
                }
                catch (...)
                {
                    try
                    {
                        RethrowIfDeviceLost("material pipeline preparation cleanup");
                    }
                    catch (...)
                    {
                        abandon = true;
                    }
                }
            }
        }
        if (AssetPipelineWork.Batch && abandon)
        {
            AssetPipelineWork.Batch->Abandon();
        }
        AssetPipelineWork = {};
    }

    void RenderSharedState::CompleteAssetPipelinePreparation()
    {
        if (!AssetPipelineWork.Job || !AssetPipelineWork.Job.IsComplete())
        {
            return;
        }
        try
        {
            const auto result = AssetPipelineWork.Job.Result();
            result.RethrowIfFailed();
            if (AssetPipelineWork.Generation != DeviceGeneration.load(std::memory_order_acquire))
            {
                CancelAssetPipelinePreparation(true);
                return;
            }
            const auto found = ShaderCache.find(AssetPipelineWork.Id);
            if (!result.Succeeded() || found == ShaderCache.end() ||
                found->second.Handle.Revision() != AssetPipelineWork.Revision)
            {
                CancelAssetPipelinePreparation(false);
                return;
            }
            auto& entry = found->second;
            auto& replacements = AssetPipelineWork.Pipelines;
            const bool replaceRevision = entry.LoadedRevision != AssetPipelineWork.Revision;
            // Allocate before transferring ownership so allocation failure leaves the batch intact.
            if (!replaceRevision)
            {
                entry.Pipelines.reserve(entry.Pipelines.size() + replacements.size());
            }
            if (!AssetPipelineWork.Batch->IsComplete())
            {
                throw std::runtime_error("Material pipeline preparation did not finish every pass.");
            }
            if (replaceRevision)
            {
                // Cached bindings must never fall back to handles retired by this publication.
                for (auto& [materialId, material] : MaterialCache)
                {
                    (void)materialId;
                    for (auto& binding : material.Bindings)
                    {
                        const auto usesPreviousPipeline = [&](const GpuShaderEntry::Pipeline& pipeline)
                        { return pipeline.Handle == binding.Binding.Pipeline; };
                        if (std::ranges::any_of(entry.Pipelines, usesPreviousPipeline))
                        {
                            const auto samples = binding.Samples;
                            binding = {};
                            binding.Samples = samples;
                        }
                    }
                }
                for (const auto& pipeline : entry.Pipelines)
                {
                    Retire(pipeline.Handle);
                }
            }
            for (std::size_t index = 0; index < replacements.size(); ++index)
            {
                replacements[index].Handle = AssetPipelineWork.Batch->ReleasePipeline(index);
            }
            if (replaceRevision)
            {
                entry.Pipelines = std::move(replacements);
            }
            else
            {
                entry.Pipelines.insert(entry.Pipelines.end(), replacements.begin(), replacements.end());
            }
            entry.LastGood = AssetPipelineWork.Shader;
            entry.LoadedRevision = AssetPipelineWork.Revision;
        }
        catch (...)
        {
            try
            {
                RethrowIfDeviceLost("background material pipeline preparation");
            }
            catch (...)
            {
                CancelAssetPipelinePreparation(true);
                throw;
            }
            try
            {
                throw;
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_ERROR("Background material pipeline preparation failed for id={} revision={}: {}",
                                 AssetPipelineWork.Id.ToString(), AssetPipelineWork.Revision, error.what());
            }
            catch (...)
            {
                KEIRE_CORE_ERROR(
                    "Background material pipeline preparation failed for id={} revision={} (unknown exception).",
                    AssetPipelineWork.Id.ToString(), AssetPipelineWork.Revision);
            }
        }
        CancelAssetPipelinePreparation(false);
    }

    SDL_GPUShader* RenderSharedState::CreateAssetShader(const ShaderAssetDefinition& definition, const bool vertex,
                                                        const std::string_view passRole) const
    {
        return CreateAssetShaderForDevice(Device, definition, vertex, passRole);
    }

    SDL_GPUShader* RenderSharedState::CreateAssetShaderForDevice(SDL_GPUDevice* device,
                                                                 const ShaderAssetDefinition& definition,
                                                                 const bool vertex, const std::string_view passRole)
    {
        const auto supported = SDL_GetGPUShaderFormats(device);
        const ShaderVariant* variant = nullptr;
        SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
        const auto findVariant = [&definition, passRole](const ShaderBinaryFormat requested)
        {
            const auto found =
                std::ranges::find_if(definition.Variants, [requested, passRole](const ShaderVariant& variant)
                                     { return variant.Format == requested && variant.PassRole == passRole; });
            return found == definition.Variants.end() ? nullptr : &*found;
        };
        if (supported & SDL_GPU_SHADERFORMAT_DXIL)
        {
            variant = findVariant(ShaderBinaryFormat::Dxil);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_DXIL;
        }
        if (!variant && (supported & SDL_GPU_SHADERFORMAT_MSL))
        {
            variant = findVariant(ShaderBinaryFormat::Msl);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_MSL;
        }
        if (!variant && (supported & SDL_GPU_SHADERFORMAT_SPIRV))
        {
            variant = findVariant(ShaderBinaryFormat::SpirV);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_SPIRV;
        }
        if (!variant)
            throw std::runtime_error("Shader asset lacks pass role '" + std::string(passRole) +
                                     "' for the active GPU backend.");

        const auto& code = vertex ? variant->Vertex : variant->Fragment;
        const auto textureCount = static_cast<std::uint32_t>(
            std::ranges::count(definition.Properties, ShaderPropertyType::Texture2D, &ShaderPropertyDefinition::Type));
        SDL_GPUShaderCreateInfo information{};
        information.code_size = code.size();
        information.code = reinterpret_cast<const std::uint8_t*>(code.data());
        information.entrypoint = format == SDL_GPU_SHADERFORMAT_SPIRV
                                     ? (vertex ? definition.VertexEntry.c_str() : definition.FragmentEntry.c_str())
                                     : nullptr;
        information.format = format;
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers =
            vertex ? 0
                   : textureCount + (definition.VertexLayoutVersion == UiShaderVertexLayoutVersion ? 1U : 0U) +
                         definition.UserResourceSlots + (definition.ReceivesShadows ? 2U : 0U) +
                         (definition.UsesImageBasedLighting ? 2U : 0U) +
                         (definition.SpatialLightingAbiVersion >= 2U ? 5U : 0U) +
                         (VolumeLightingUsesNoiseTexture(definition.VolumeLightingAbiVersion) ? 1U : 0U);
        information.num_storage_buffers =
            !vertex ? (definition.UsesForwardPlus ? 3U : 0U) + (definition.SpatialLightingAbiVersion == 3U ? 1U : 0U) +
                          (VolumeLightingUsesNoiseBuffer(definition.VolumeLightingAbiVersion) ? 1U : 0U) +
                          definition.UserReadOnlyBuffers
                    : 0U;
        if (vertex && definition.UsesInstancing)
            information.num_storage_buffers = 1U;
        if (vertex && (passRole == "vfxBillboard" || passRole == "vfxRibbon"))
            information.num_storage_buffers = 2U;
        else if (vertex && passRole == "vfxCpu")
            information.num_storage_buffers = 0U;
        information.num_uniform_buffers = vertex ? (definition.InstanceAddressingAbiVersion == 2U
                                                        ? 3U
                                                        : (definition.UsesVertexMaterialParameters ? 2U : 1U))
                                                 : 2U + (definition.UsesImageBasedLighting           ? 2U
                                                         : definition.VolumeLightingAbiVersion >= 1U ? 1U
                                                         : definition.ReceivesShadows                ? 1U
                                                                                                     : 0U);
        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(asset) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                    const SDL_GPUSampleCount samples,
                                                                    const MaterialSurfaceState surface,
                                                                    const std::string_view passRole)
    {
        SDL_GPUShader* vertex = nullptr;
        SDL_GPUShader* fragment = nullptr;
        try
        {
            auto* pipeline = CreateAssetPipelineForDevice(Device, SceneColorFormat, DepthFormat, definition, samples,
                                                          surface, passRole, vertex, fragment);
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("asset shader pipeline creation");
            if (fragment)
            {
                SDL_ReleaseGPUShader(Device, fragment);
            }
            if (vertex)
            {
                SDL_ReleaseGPUShader(Device, vertex);
            }
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateAssetPipelineForDevice(
        SDL_GPUDevice* device, const SDL_GPUTextureFormat sceneColorFormat, const SDL_GPUTextureFormat depthFormat,
        const ShaderAssetDefinition& definition, const SDL_GPUSampleCount samples, const MaterialSurfaceState surface,
        const std::string_view passRole, SDL_GPUShader*& vertex, SDL_GPUShader*& fragment)
    {
        if (definition.VertexLayoutVersion == UiShaderVertexLayoutVersion)
            throw std::invalid_argument("Screen-space materials require a camera effect or UI consumer, not a mesh.");
        const auto shaderCreationStarted = std::chrono::steady_clock::now();
        vertex = CreateAssetShaderForDevice(device, definition, true, passRole);
        fragment = CreateAssetShaderForDevice(device, definition, false, passRole);
        const auto shaderCreationElapsed = std::chrono::steady_clock::now() - shaderCreationStarted;
        const auto runtimeRole = RuntimeMaterialPassRoleFromName(passRole);
        const auto targetLayout = MaterialPassTargetLayoutForRole(runtimeRole);
        if (targetLayout == MaterialPassTargetLayout::Unsupported)
            throw std::invalid_argument("Shader pass role is not executable by the material runtime: " +
                                        std::string(passRole));
        std::array<SDL_GPUColorTargetDescription, 4> colors{};
        std::uint32_t colorCount = 1U;
        switch (targetLayout)
        {
        case MaterialPassTargetLayout::ForwardColor:
            colors[0].format = sceneColorFormat;
            break;
        case MaterialPassTargetLayout::Velocity:
            colors[0].format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
            break;
        case MaterialPassTargetLayout::GBuffer:
            colors[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
            colors[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            colors[2].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            colors[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
            colorCount = 4U;
            break;
        case MaterialPassTargetLayout::DBuffer:
            colors[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
            colors[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            colors[2].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            colorCount = 3U;
            break;
        case MaterialPassTargetLayout::Unsupported:
            break;
        }
        const auto blending = MaterialBlending(surface.AlphaMode, definition.VolumeLightingAbiVersion >= 1U);
        const auto blendFactor = [](const MaterialBlendFactor factor) noexcept
        {
            switch (factor)
            {
            case MaterialBlendFactor::Zero:
                return SDL_GPU_BLENDFACTOR_ZERO;
            case MaterialBlendFactor::SourceAlpha:
                return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            case MaterialBlendFactor::OneMinusSourceAlpha:
                return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            case MaterialBlendFactor::DestinationColor:
                return SDL_GPU_BLENDFACTOR_DST_COLOR;
            case MaterialBlendFactor::One:
            default:
                return SDL_GPU_BLENDFACTOR_ONE;
            }
        };
        const bool passBlends = (targetLayout == MaterialPassTargetLayout::ForwardColor && blending.Enabled) ||
                                targetLayout == MaterialPassTargetLayout::DBuffer;
        for (std::uint32_t index = 0; index < colorCount; ++index)
        {
            auto& color = colors[index];
            color.blend_state.src_color_blendfactor = targetLayout == MaterialPassTargetLayout::DBuffer
                                                          ? SDL_GPU_BLENDFACTOR_SRC_ALPHA
                                                          : blendFactor(blending.SourceColor);
            color.blend_state.dst_color_blendfactor = targetLayout == MaterialPassTargetLayout::DBuffer
                                                          ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
                                                          : blendFactor(blending.DestinationColor);
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = targetLayout == MaterialPassTargetLayout::DBuffer
                                                          ? SDL_GPU_BLENDFACTOR_ONE
                                                          : blendFactor(blending.SourceAlpha);
            color.blend_state.dst_alpha_blendfactor = targetLayout == MaterialPassTargetLayout::DBuffer
                                                          ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
                                                          : blendFactor(blending.DestinationAlpha);
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = passBlends;
        }

        std::array<SDL_GPUVertexBufferDescription, 2> buffers{};
        buffers[0].slot = 0;
        buffers[0].pitch = sizeof(GpuMeshVertex);
        buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffers[1].slot = 1;
        buffers[1].pitch = sizeof(GpuMeshVertex);
        buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        const std::array attributes{
            SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)},
            SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Normal)},
            SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMeshVertex, UV0)},
            SDL_GPUVertexAttribute{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, VertexColor)},
            SDL_GPUVertexAttribute{4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, Tangent)},
            SDL_GPUVertexAttribute{5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMeshVertex, UV1)},
            SDL_GPUVertexAttribute{6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)}};
        const std::array particleAttributes{
            SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuRenderVertex, Position)},
            SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuRenderVertex, Color)},
            SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuRenderVertex, Normal)}};
        SDL_GPUGraphicsPipelineCreateInfo information{};
        information.vertex_shader = vertex;
        information.fragment_shader = fragment;
        information.vertex_input_state.vertex_buffer_descriptions = buffers.data();
        information.vertex_input_state.num_vertex_buffers =
            targetLayout == MaterialPassTargetLayout::Velocity ? 2U : 1U;
        information.vertex_input_state.vertex_attributes = attributes.data();
        information.vertex_input_state.num_vertex_attributes = targetLayout == MaterialPassTargetLayout::Velocity
                                                                   ? static_cast<std::uint32_t>(attributes.size())
                                                               : definition.VertexLayoutVersion == 3 ? 6U
                                                               : definition.VertexLayoutVersion == 2 ? 5U
                                                                                                     : 4U;
        if (runtimeRole == RuntimeMaterialPassRole::VfxCpu)
        {
            buffers[0].pitch = sizeof(GpuRenderVertex);
            information.vertex_input_state.vertex_attributes = particleAttributes.data();
            information.vertex_input_state.num_vertex_attributes = 3U;
        }
        else if (runtimeRole == RuntimeMaterialPassRole::VfxBillboard ||
                 runtimeRole == RuntimeMaterialPassRole::VfxRibbon)
            information.vertex_input_state = {};
        information.primitive_type =
            definition.Topology == ShaderPrimitiveTopology::PointList  ? SDL_GPU_PRIMITIVETYPE_POINTLIST
            : definition.Topology == ShaderPrimitiveTopology::LineList ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                                       : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        information.rasterizer_state.cull_mode = surface.DoubleSided                           ? SDL_GPU_CULLMODE_NONE
                                                 : definition.Culling == ShaderCullMode::Front ? SDL_GPU_CULLMODE_FRONT
                                                 : definition.Culling == ShaderCullMode::Back  ? SDL_GPU_CULLMODE_BACK
                                                                                               : SDL_GPU_CULLMODE_NONE;
        information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        information.rasterizer_state.enable_depth_clip = true;
        information.multisample_state.sample_count =
            targetLayout == MaterialPassTargetLayout::ForwardColor ? samples : SDL_GPU_SAMPLECOUNT_1;
        information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        information.depth_stencil_state.enable_depth_test = definition.DepthTest;
        information.depth_stencil_state.enable_depth_write =
            targetLayout != MaterialPassTargetLayout::DBuffer && definition.DepthWrite && blending.WritesDepth;
        information.target_info.color_target_descriptions = colors.data();
        information.target_info.num_color_targets = colorCount;
        information.target_info.depth_stencil_format = depthFormat;
        information.target_info.has_depth_stencil_target = true;
        KEIRE_CORE_INFO("Creating asset pipeline (role={}, layout={}, topology={}, samples={}, colors={}, depth={}, "
                        "attributes={}).",
                        passRole, definition.VertexLayoutVersion, static_cast<std::uint32_t>(definition.Topology),
                        static_cast<std::uint32_t>(information.multisample_state.sample_count), colorCount,
                        static_cast<std::uint32_t>(depthFormat), information.vertex_input_state.num_vertex_attributes);
        const auto pipelineCreationStarted = std::chrono::steady_clock::now();
        SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(device, &information);
        const auto pipelineCreationElapsed = std::chrono::steady_clock::now() - pipelineCreationStarted;
        if (!result)
            throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(asset) failed: " + LastSdlError());
        KEIRE_CORE_INFO("Created asset pipeline (role={}, shader_ms={:.3f}, pipeline_ms={:.3f}).", passRole,
                        std::chrono::duration<double, std::milli>(shaderCreationElapsed).count(),
                        std::chrono::duration<double, std::milli>(pipelineCreationElapsed).count());
        return result;
    }
} // namespace Keire::RenderBackend
