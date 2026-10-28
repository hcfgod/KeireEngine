// Opt-in native color-attachment check; does not qualify cloud appearance or performance.
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

    void Run(SDL_GPUDevice* device, SDL_GPUShaderFormat format, const char* vertexPath, const char* fragmentPath)
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
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, Bytes, 0};
        auto releaseTransfer = [device](SDL_GPUTransferBuffer* value) { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr);
        auto cancel = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancel)> commands(SDL_AcquireGPUCommandBuffer(device), cancel);
        Require(commands != nullptr);
        SDL_GPUColorTargetInfo color{};
        color.texture = texture.get();
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands.get(), &color, 1, nullptr);
        Require(pass != nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipeline.get());
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
            double expected = std::exp2(-26.575424759 + 53.150849518 * double(index) / 65535.0);
            if (index == 0)
            {
                expected = 0.0;
            }
            if (index == 1)
            {
                expected = std::numeric_limits<float>::max();
            }
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const double encoded = DecodeHalf(mapped[index * 4 + channel]);
                const double actual = encoded <= -65504.0 ? 0.0
                                      : encoded >= 128.0  ? std::numeric_limits<float>::max()
                                                          : std::exp2(encoded);
                const double reference = std::ldexp(expected, -int(channel));
                if (!std::isfinite(actual) || std::abs(actual - reference) > reference * 0.006)
                {
                    ++failures;
                }
            }
            if (DecodeHalf(mapped[index * 4 + 3]) != 0.375)
            {
                ++failures;
            }
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        std::cout << "attachment_pixels=" << Size * Size << " failed_channels=" << failures << '\n';
        if (failures != 0)
        {
            throw std::runtime_error("Native cloud history storage failed.");
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
        Run(device.get(), format, argv[2], argv[3]);
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
