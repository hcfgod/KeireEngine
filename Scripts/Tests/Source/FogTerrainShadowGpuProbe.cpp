// Offscreen fog cascade sampling check; does not qualify editor composition or performance.
#include "KeireInternal/Rendering/VolumeShadowUniforms.h"
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr std::uint32_t Size = 256;
    constexpr std::uint32_t Bytes = Size * Size * 8;

    void Require(bool condition)
    {
        if (!condition)
        {
            throw std::runtime_error(SDL_GetError());
        }
    }

    auto LoadShader(SDL_GPUDevice* device, const char* path, SDL_GPUShaderFormat format, SDL_GPUShaderStage stage)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.good())
        {
            throw std::runtime_error("Cannot open attachment probe shader.");
        }
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
        SDL_GPUShaderCreateInfo info{};
        info.code = bytes.data();
        info.code_size = bytes.size();
        info.format = format;
        info.stage = stage;
        info.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1U : 0U;
        info.num_uniform_buffers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1U : 0U;
        info.entrypoint = stage == SDL_GPU_SHADERSTAGE_VERTEX ? "VSMain" : "PSMain";
        auto release = [device](SDL_GPUShader* value) { SDL_ReleaseGPUShader(device, value); };
        std::unique_ptr<SDL_GPUShader, decltype(release)> shader(SDL_CreateGPUShader(device, &info), release);
        Require(shader != nullptr);
        return shader;
    }

    double DecodeHalf(std::uint16_t bits)
    {
        const unsigned exponent = (bits >> 10U) & 31U;
        const unsigned mantissa = bits & 1023U;
        const double magnitude = exponent == 0 ? std::ldexp(double(mantissa), -24)
                                               : std::ldexp(1.0 + double(mantissa) / 1024.0, int(exponent) - 15);
        return (bits & 32768U) != 0 ? -magnitude : magnitude;
    }

    void Run(SDL_GPUDevice* device, SDL_GPUShaderFormat format, const char* vertexPath, const char* fragmentPath,
             bool soft)
    {
        auto vertex = LoadShader(device, vertexPath, format, SDL_GPU_SHADERSTAGE_VERTEX);
        auto fragment = LoadShader(device, fragmentPath, format, SDL_GPU_SHADERSTAGE_FRAGMENT);
        SDL_GPUColorTargetDescription target{};
        target.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = vertex.get();
        pipelineInfo.fragment_shader = fragment.get();
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.target_info.color_target_descriptions = &target;
        pipelineInfo.target_info.num_color_targets = 1;
        auto releasePipeline = [device](SDL_GPUGraphicsPipeline* value)
        { SDL_ReleaseGPUGraphicsPipeline(device, value); };
        std::unique_ptr<SDL_GPUGraphicsPipeline, decltype(releasePipeline)> pipeline(
            SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo), releasePipeline);
        Require(pipeline != nullptr);
        SDL_GPUTextureCreateInfo textureInfo{};
        textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        textureInfo.format = target.format;
        textureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        textureInfo.width = Size;
        textureInfo.height = Size;
        textureInfo.layer_count_or_depth = 1;
        textureInfo.num_levels = 1;
        auto releaseTexture = [device](SDL_GPUTexture* value) { SDL_ReleaseGPUTexture(device, value); };
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> texture(SDL_CreateGPUTexture(device, &textureInfo),
                                                                          releaseTexture);
        Require(texture != nullptr);
        SDL_GPUTextureCreateInfo shadowInfo{};
        shadowInfo.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        shadowInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        shadowInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        shadowInfo.width = 16;
        shadowInfo.height = 16;
        shadowInfo.layer_count_or_depth = 4;
        shadowInfo.num_levels = 1;
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> shadow(SDL_CreateGPUTexture(device, &shadowInfo),
                                                                         releaseTexture);
        Require(shadow != nullptr);
        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        auto releaseSampler = [device](SDL_GPUSampler* value) { SDL_ReleaseGPUSampler(device, value); };
        std::unique_ptr<SDL_GPUSampler, decltype(releaseSampler)> sampler(SDL_CreateGPUSampler(device, &samplerInfo),
                                                                          releaseSampler);
        Require(sampler != nullptr);
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, Bytes, 0};
        auto releaseTransfer = [device](SDL_GPUTransferBuffer* value) { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr);
        auto cancel = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancel)> commands(SDL_AcquireGPUCommandBuffer(device), cancel);
        Require(commands != nullptr);
        for (std::uint8_t layer = 0; layer < 4; ++layer)
        {
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = shadow.get();
            depth.layer = layer;
            depth.clear_depth = 0.15F + 0.2F * layer;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto* clearPass = SDL_BeginGPURenderPass(commands.get(), nullptr, 0, &depth);
            Require(clearPass != nullptr);
            SDL_EndGPURenderPass(clearPass);
        }
        Keire::RenderBackend::AssetDirectionalShadowUniforms directional;
        directional.DirectionalParameters = {soft ? 4.0F : -4.0F, 0.75F, 0.0F, 1.0F / 16.0F};
        directional.DirectionalCascadeSplits = {0.2F, 0.4F, 0.6F, 1.0F};
        const auto uniforms = Keire::RenderBackend::MakeVolumeShadowUniforms(directional, {}, {});
        SDL_PushGPUFragmentUniformData(commands.get(), 0, &uniforms, sizeof(uniforms));
        SDL_GPUColorTargetInfo color{};
        color.texture = texture.get();
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands.get(), &color, 1, nullptr);
        Require(pass != nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipeline.get());
        const SDL_GPUTextureSamplerBinding binding{shadow.get(), sampler.get()};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr);
        const SDL_GPUTextureRegion source{texture.get(), 0, 0, 0, 0, 0, Size, Size, 1};
        const SDL_GPUTextureTransferInfo destination{transfer.get(), 0, Size, Size};
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        auto releaseFence = [device](SDL_GPUFence* value) { SDL_ReleaseGPUFence(device, value); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr);
        auto* fenceHandle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &fenceHandle, 1));
        auto* mapped = static_cast<const std::uint16_t*>(SDL_MapGPUTransferBuffer(device, transfer.get(), false));
        Require(mapped != nullptr);
        unsigned failures = 0;
        for (unsigned index = 0; index < Size * Size; ++index)
        {
            const double actual = DecodeHalf(mapped[index * 4]);
            const double expected = DecodeHalf(mapped[index * 4 + 1]);
            if (!std::isfinite(actual) || std::abs(actual - expected) > 0.001)
            {
                ++failures;
            }
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        std::cout << "shadow_pixels=" << Size * Size << " failed_pixels=" << failures << '\n';
        if (failures != 0)
        {
            throw std::runtime_error("Fog shadow cascade sampling failed.");
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
        {
            throw std::runtime_error("Expected backend, vertex shader and fragment shader.");
        }
        Require(SDL_Init(SDL_INIT_VIDEO));
        const auto format = std::string(argv[1]) == "vulkan" ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
        std::unique_ptr<SDL_GPUDevice, decltype(&SDL_DestroyGPUDevice)> device(
            SDL_CreateGPUDevice(format, true, argv[1]), SDL_DestroyGPUDevice);
        Require(device != nullptr);
        Run(device.get(), format, argv[2], argv[3], false);
        Run(device.get(), format, argv[2], argv[3], true);
        device.reset();
        SDL_Quit();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
