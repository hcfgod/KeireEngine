#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/BloomUniforms.h"

#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::RecordBloom(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                        const BloomSettings& settings, const BloomPass bloomPass)
    {
        const auto& workset = surface.ActiveWorkset();
        SDL_GPUTexture* source = nullptr;
        SDL_GPUTexture* destination = nullptr;
        switch (bloomPass)
        {
        case BloomPass::Extract:
            source = workset.HdrColor;
            destination = workset.BloomExtracted;
            break;
        case BloomPass::Horizontal:
            source = workset.BloomExtracted;
            destination = workset.BloomHorizontal;
            break;
        case BloomPass::Vertical:
            source = workset.BloomHorizontal;
            destination = workset.BloomResult;
            break;
        default:
            throw std::invalid_argument("Unknown bloom recording pass.");
        }
        const auto sourceWidth = bloomPass == BloomPass::Extract ? surface.Width : BloomHalfExtent(surface.Width);
        const auto sourceHeight = bloomPass == BloomPass::Extract ? surface.Height : BloomHalfExtent(surface.Height);
        const auto uniforms = BuildBloomUniforms(settings, sourceWidth, sourceHeight, bloomPass);
        if (!commands || !source || !destination || source == destination || !ToneMapSampler)
        {
            throw std::logic_error("Bloom requires distinct valid source and destination textures and a sampler.");
        }
        EnsureBloomPipeline();
        SDL_GPUColorTargetInfo target{};
        target.texture = destination;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(bloom) failed: " + LastSdlError());
        }
        const SDL_GPUTextureSamplerBinding binding{source, ToneMapSampler};
        SDL_BindGPUGraphicsPipeline(pass, BloomPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    SDL_GPUShader* RenderSharedState::CreateBloomShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0U : 1U;
        information.num_uniform_buffers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinBloomVertexDxil : Detail::BuiltinBloomFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinBloomVertexDxilSize : Detail::BuiltinBloomFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinBloomVertexMsl : Detail::BuiltinBloomFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinBloomVertexMslSize : Detail::BuiltinBloomFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinBloomVertexSpirV : Detail::BuiltinBloomFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinBloomVertexSpirVSize : Detail::BuiltinBloomFragmentSpirVSize;
        }
        else
        {
            throw std::runtime_error("The active SDL_GPU backend exposes no supported bloom shader format.");
        }
        auto* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
        {
            throw std::runtime_error("SDL_CreateGPUShader(bloom) failed: " + LastSdlError());
        }
        return shader;
    }

    void RenderSharedState::EnsureBloomPipeline()
    {
        if (BloomPipeline)
        {
            return;
        }
        const auto releaseShader = [this](SDL_GPUShader* shader) noexcept { SDL_ReleaseGPUShader(Device, shader); };
        using ShaderOwner = std::unique_ptr<SDL_GPUShader, decltype(releaseShader)>;
        try
        {
            ShaderOwner vertex(CreateBloomShader(true), releaseShader);
            ShaderOwner fragment(CreateBloomShader(false), releaseShader);
            SDL_GPUColorTargetDescription color{};
            color.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
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
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(bloom) failed: " + LastSdlError());
            }
            BloomPipeline = pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("bloom pipeline creation");
            throw;
        }
    }
} // namespace Keire::RenderBackend
