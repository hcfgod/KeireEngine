#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/CloudTemporalRendering.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    static SDL_GPUShader* CreateCloudTemporalShader(SDL_GPUDevice* device, const CloudTemporalPass pass,
                                                    const bool vertex)
    {
        struct Code
        {
            const unsigned char* Bytes;
            std::size_t Size;
        };
        const std::array VertexDxil{
            Code{Detail::BuiltinCloudTraceVertexDxil, Detail::BuiltinCloudTraceVertexDxilSize},
            Code{Detail::BuiltinCloudResolveVertexDxil, Detail::BuiltinCloudResolveVertexDxilSize},
            Code{Detail::BuiltinCloudCompositeVertexDxil, Detail::BuiltinCloudCompositeVertexDxilSize},
        };
        const std::array VertexSpirV{
            Code{Detail::BuiltinCloudTraceVertexSpirV, Detail::BuiltinCloudTraceVertexSpirVSize},
            Code{Detail::BuiltinCloudResolveVertexSpirV, Detail::BuiltinCloudResolveVertexSpirVSize},
            Code{Detail::BuiltinCloudCompositeVertexSpirV, Detail::BuiltinCloudCompositeVertexSpirVSize},
        };
        const std::array VertexMsl{
            Code{Detail::BuiltinCloudTraceVertexMsl, Detail::BuiltinCloudTraceVertexMslSize},
            Code{Detail::BuiltinCloudResolveVertexMsl, Detail::BuiltinCloudResolveVertexMslSize},
            Code{Detail::BuiltinCloudCompositeVertexMsl, Detail::BuiltinCloudCompositeVertexMslSize},
        };
        const std::array FragmentDxil{
            Code{Detail::BuiltinCloudTraceFragmentDxil, Detail::BuiltinCloudTraceFragmentDxilSize},
            Code{Detail::BuiltinCloudResolveFragmentDxil, Detail::BuiltinCloudResolveFragmentDxilSize},
            Code{Detail::BuiltinCloudCompositeFragmentDxil, Detail::BuiltinCloudCompositeFragmentDxilSize},
        };
        const std::array FragmentSpirV{
            Code{Detail::BuiltinCloudTraceFragmentSpirV, Detail::BuiltinCloudTraceFragmentSpirVSize},
            Code{Detail::BuiltinCloudResolveFragmentSpirV, Detail::BuiltinCloudResolveFragmentSpirVSize},
            Code{Detail::BuiltinCloudCompositeFragmentSpirV, Detail::BuiltinCloudCompositeFragmentSpirVSize},
        };
        const std::array FragmentMsl{
            Code{Detail::BuiltinCloudTraceFragmentMsl, Detail::BuiltinCloudTraceFragmentMslSize},
            Code{Detail::BuiltinCloudResolveFragmentMsl, Detail::BuiltinCloudResolveFragmentMslSize},
            Code{Detail::BuiltinCloudCompositeFragmentMsl, Detail::BuiltinCloudCompositeFragmentMslSize},
        };

        const auto index = static_cast<std::size_t>(pass);
        if (index >= 3)
        {
            throw std::invalid_argument("Unsupported cloud temporal pass.");
        }
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        constexpr std::array samplerCounts{3U, 4U, 5U};
        constexpr std::array uniformCounts{4U, 3U, 2U};
        information.num_samplers = vertex ? 0U : samplerCounts[index];
        information.num_uniform_buffers = vertex ? 0U : uniformCounts[index];
        information.num_storage_buffers = vertex || pass == CloudTemporalPass::Resolve ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(device);
        Code code{};
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            code = vertex ? VertexDxil[index] : FragmentDxil[index];
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            code = vertex ? VertexMsl[index] : FragmentMsl[index];
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            code = vertex ? VertexSpirV[index] : FragmentSpirV[index];
        }
        else
        {
            throw std::runtime_error("The active GPU backend exposes no supported cloud temporal shader format.");
        }
        information.code = code.Bytes;
        information.code_size = code.Size;
        auto* shader = SDL_CreateGPUShader(device, &information);
        if (!shader)
        {
            throw std::runtime_error("SDL_CreateGPUShader(cloud temporal) failed: " + LastSdlError());
        }
        return shader;
    }

    SDL_GPUGraphicsPipeline* EnsureCloudTemporalPipeline(RenderSharedState& state, const CloudTemporalPass pass)
    {
        const auto index = static_cast<std::size_t>(pass);
        if (index >= state.CloudTemporalPipelines.size())
        {
            throw std::invalid_argument("Unsupported cloud temporal pass.");
        }
        if (state.CloudTemporalPipelines[index])
        {
            return state.CloudTemporalPipelines[index];
        }
        const auto releaseShader = [&state](SDL_GPUShader* shader) noexcept
        { SDL_ReleaseGPUShader(state.Device, shader); };
        using ShaderOwner = std::unique_ptr<SDL_GPUShader, decltype(releaseShader)>;
        try
        {
            ShaderOwner vertex(CreateCloudTemporalShader(state.Device, pass, true), releaseShader);
            ShaderOwner fragment(CreateCloudTemporalShader(state.Device, pass, false), releaseShader);
            std::array<SDL_GPUColorTargetDescription, 2> colors{};
            colors[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            colors[1].format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex.get();
            information.fragment_shader = fragment.get();
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.target_info.color_target_descriptions = colors.data();
            information.target_info.num_color_targets = pass == CloudTemporalPass::Composite ? 1U : 2U;
            auto* pipeline = SDL_CreateGPUGraphicsPipeline(state.Device, &information);
            if (!pipeline)
            {
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(cloud temporal) failed: " + LastSdlError());
            }
            state.CloudTemporalPipelines[index] = pipeline;
            return pipeline;
        }
        catch (...)
        {
            state.RethrowIfDeviceLost("cloud temporal pipeline creation");
            throw;
        }
    }
} // namespace Keire::RenderBackend
