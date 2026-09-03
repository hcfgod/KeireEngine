#include "KeireInternal/Rendering/MaterialBlendingInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire::RenderBackend
{
    SDL_GPUShader* RenderSharedState::CreateAssetShader(const ShaderAssetDefinition& definition, const bool vertex,
                                                        const std::string_view passRole) const
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
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
        information.num_samplers = vertex ? 0
                                          : textureCount + definition.UserResourceSlots +
                                                (definition.ReceivesShadows ? 2U : 0U) +
                                                (definition.UsesImageBasedLighting ? 2U : 0U) +
                                                (definition.SpatialLightingAbiVersion >= 2U ? 5U : 0U);
        information.num_storage_buffers = !vertex ? (definition.UsesForwardPlus ? 3U : 0U) +
                                                        (definition.SpatialLightingAbiVersion == 3U ? 1U : 0U) +
                                                        definition.UserReadOnlyBuffers
                                                  : 0U;
        if (vertex && definition.UsesInstancing)
            information.num_storage_buffers = 1U;
        information.num_uniform_buffers = vertex ? (definition.InstanceAddressingAbiVersion == 2U
                                                        ? 3U
                                                        : (definition.UsesVertexMaterialParameters ? 2U : 1U))
                                                 : 2U + (definition.UsesImageBasedLighting ? 2U
                                                         : definition.ReceivesShadows      ? 1U
                                                                                           : 0U);
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(asset) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                    const SDL_GPUSampleCount samples,
                                                                    const MaterialSurfaceState surface,
                                                                    const std::string_view passRole)
    {
        SDL_GPUShader* vertex = CreateAssetShader(definition, true, passRole);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateAssetShader(definition, false, passRole);
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
                colors[0].format = SceneColorFormat;
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
            const auto blending = MaterialBlending(surface.AlphaMode);
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
            information.primitive_type =
                definition.Topology == ShaderPrimitiveTopology::PointList  ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                : definition.Topology == ShaderPrimitiveTopology::LineList ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                                           : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode =
                surface.DoubleSided                           ? SDL_GPU_CULLMODE_NONE
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
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            KEIRE_CORE_INFO(
                "Creating asset pipeline (role={}, layout={}, topology={}, samples={}, colors={}, depth={}, "
                "attributes={}).",
                passRole, definition.VertexLayoutVersion, static_cast<std::uint32_t>(definition.Topology),
                static_cast<std::uint32_t>(information.multisample_state.sample_count), colorCount,
                static_cast<std::uint32_t>(DepthFormat), information.vertex_input_state.num_vertex_attributes);
            SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(asset) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("asset shader pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }
} // namespace Keire::RenderBackend
