// Opt-in visual diagnostic. This does not qualify Editor composition, post effects or performance budgets.
#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint32_t Width = 960;
    constexpr std::uint32_t Height = 400;
    constexpr std::uint32_t ByteCount = Width * Height * 4 * sizeof(float);

    struct Parameters
    {
        std::array<float, 4> Density{1.0F, 0.2F, 0.416F, 0.6F};
        std::array<float, 4> Shape{4.0F, 0.75F, 64.0F, 1.0F};
        std::array<std::uint32_t, 4> Quality{Width, Height, 48, 6};
        std::array<float, 4> Regional{0.0F, 8.0F, 0.0F, 0.0F};
        std::array<float, 4> Weather{0.0F, 1.0F, 0.5F, 0.0F};
        std::array<float, 4> Lighting{};
    };

    static_assert(sizeof(Parameters) == 96);

    void Require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    float ReadScalar(const char* text, const float minimum, const float maximum)
    {
        std::size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        Require(consumed == std::strlen(text) && std::isfinite(value) && value >= minimum && value <= maximum,
                "Probe parameter is outside its finite supported range.");
        return value;
    }

    std::uint32_t ReadSampleCount(const char* text, const std::uint32_t maximum)
    {
        const float value = ReadScalar(text, 1.0F, static_cast<float>(maximum));
        Require(std::floor(value) == value, "Sample count must be an integer.");
        return static_cast<std::uint32_t>(value);
    }

    std::uint8_t DisplayChannel(const float linear)
    {
        // Same fitted ACES curve as BuiltinToneMap, followed by the output surface's sRGB encoding.
        const float color = std::max(linear, 0.0F);
        const float mapped =
            std::clamp(color * (2.51F * color + 0.03F) / (color * (2.43F * color + 0.59F) + 0.14F), 0.0F, 1.0F);
        const float encoded = mapped <= 0.0031308F ? mapped * 12.92F : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
        return static_cast<std::uint8_t>(std::clamp(std::lround(encoded * 255.0F), 0L, 255L));
    }

    void SaveImage(const std::vector<float>& pixels, const char* path)
    {
        std::vector<std::uint8_t> display(Width * Height * 4);
        double meanOpacity = 0.0;
        for (std::size_t pixel = 0; pixel < Width * Height; ++pixel)
        {
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                Require(std::isfinite(pixels[pixel * 4 + channel]), "Non-finite GPU image output.");
            }
            const float opacity = pixels[pixel * 4 + 3];
            Require(opacity >= 0.0F && opacity <= 1.0F, "Invalid cloud opacity.");
            meanOpacity += opacity;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                display[pixel * 4 + channel] = DisplayChannel(pixels[pixel * 4 + channel]);
            }
            display[pixel * 4 + 3] = 255;
        }
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurfaceFrom(Width, Height, SDL_PIXELFORMAT_RGBA32, display.data(), Width * 4),
            SDL_DestroySurface);
        Require(surface != nullptr, SDL_GetError());
        Require(SDL_SaveBMP(surface.get(), path), SDL_GetError());
        std::ofstream linearOutput(std::string(path) + ".rgba32f", std::ios::binary);
        linearOutput.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(ByteCount));
        linearOutput.close();
        Require(linearOutput.good(), "Cannot save linear GPU image output.");
        std::cout << "pixels=" << Width * Height << " mean_opacity=" << meanOpacity / (Width * Height) << '\n';
    }

    void Render(SDL_GPUDevice* device, const char* shaderPath, const SDL_GPUShaderFormat format,
                const Parameters& parameters, const char* outputPath)
    {
        std::ifstream shaderStream(shaderPath, std::ios::binary);
        Require(shaderStream.good(), "Cannot open appearance probe shader.");
        const std::vector<std::uint8_t> shader{std::istreambuf_iterator<char>(shaderStream),
                                               std::istreambuf_iterator<char>()};
        SDL_GPUComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.code = shader.data();
        pipelineInfo.code_size = shader.size();
        pipelineInfo.entrypoint = "CSMain";
        pipelineInfo.format = format;
        pipelineInfo.num_samplers = 1;
        pipelineInfo.num_readwrite_storage_buffers = 1;
        pipelineInfo.num_uniform_buffers = 1;
        pipelineInfo.threadcount_x = 8;
        pipelineInfo.threadcount_y = 8;
        pipelineInfo.threadcount_z = 1;
        const auto releasePipeline = [device](SDL_GPUComputePipeline* value)
        { SDL_ReleaseGPUComputePipeline(device, value); };
        std::unique_ptr<SDL_GPUComputePipeline, decltype(releasePipeline)> pipeline(
            SDL_CreateGPUComputePipeline(device, &pipelineInfo), releasePipeline);
        Require(pipeline != nullptr, SDL_GetError());

        const auto volume = Keire::RenderBackend::BuildCloudNoiseVolume(64, 1);
        const auto releaseTexture = [device](SDL_GPUTexture* value) { SDL_ReleaseGPUTexture(device, value); };
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> texture(
            Keire::RenderBackend::UploadCloudNoiseVolume(device, volume), releaseTexture);
        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.max_lod = 32.0F;
        const auto releaseSampler = [device](SDL_GPUSampler* value) { SDL_ReleaseGPUSampler(device, value); };
        std::unique_ptr<SDL_GPUSampler, decltype(releaseSampler)> sampler(SDL_CreateGPUSampler(device, &samplerInfo),
                                                                          releaseSampler);
        Require(sampler != nullptr, SDL_GetError());
        const SDL_GPUBufferCreateInfo bufferInfo{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, ByteCount, 0};
        const auto releaseBuffer = [device](SDL_GPUBuffer* value) { SDL_ReleaseGPUBuffer(device, value); };
        std::unique_ptr<SDL_GPUBuffer, decltype(releaseBuffer)> output(SDL_CreateGPUBuffer(device, &bufferInfo),
                                                                       releaseBuffer);
        Require(output != nullptr, SDL_GetError());
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, ByteCount, 0};
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* value)
        { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr, SDL_GetError());

        const auto cancelCommands = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancelCommands)> commands(SDL_AcquireGPUCommandBuffer(device),
                                                                                 cancelCommands);
        Require(commands != nullptr, SDL_GetError());
        SDL_PushGPUComputeUniformData(commands.get(), 0, &parameters, sizeof(parameters));
        const SDL_GPUStorageBufferReadWriteBinding storage{output.get(), false, 0, 0, 0};
        auto* compute = SDL_BeginGPUComputePass(commands.get(), nullptr, 0, &storage, 1);
        Require(compute != nullptr, SDL_GetError());
        SDL_BindGPUComputePipeline(compute, pipeline.get());
        const SDL_GPUTextureSamplerBinding binding{texture.get(), sampler.get()};
        SDL_BindGPUComputeSamplers(compute, 0, &binding, 1);
        SDL_DispatchGPUCompute(compute, (Width + 7) / 8, (Height + 7) / 8, 1);
        SDL_EndGPUComputePass(compute);
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr, SDL_GetError());
        const SDL_GPUBufferRegion source{output.get(), 0, ByteCount};
        const SDL_GPUTransferBufferLocation destination{transfer.get(), 0};
        SDL_DownloadFromGPUBuffer(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        const auto releaseFence = [device](SDL_GPUFence* value) { SDL_ReleaseGPUFence(device, value); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr, SDL_GetError());
        auto* fenceHandle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &fenceHandle, 1), SDL_GetError());
        std::vector<float> pixels(Width * Height * 4);
        const auto* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), false);
        Require(mapped != nullptr, SDL_GetError());
        std::memcpy(pixels.data(), mapped, ByteCount);
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        SaveImage(pixels, outputPath);
        if (parameters.Regional[2] >= 2.0F)
        {
            Require(std::all_of(pixels.begin(), pixels.end(), [](const float value) { return value == 0.0F; }),
                    "Cloud transport diagnostic reported a failed ray; inspect the output image.");
        }
        if (parameters.Regional[2] > 1.5F)
        {
            for (std::size_t index = 3; index < pixels.size(); index += 4)
            {
                Require(pixels[index] == 0.0F, "Cloud/atmosphere transport invariant failed.");
            }
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(
            argc >= 4 && argc <= 21,
            "Usage: cloud-appearance-gpu direct3d12|vulkan shader output.bmp [density "
            "[detail [frequency [coverage [ambient [regional-strength [regional-period "
            "[core [view-samples [shadow-samples [cloud|air|air-check|density-check|depth-check|phase-average [erosion "
            "[weather-enabled [cloud-type [type-variation [ambient-occlusion [base-height-variation]]]]]]]]]]]]]]]]]");
        const std::string backend = argv[1];
        Require(backend == "direct3d12" || backend == "vulkan", "Unsupported probe backend.");
        Parameters parameters;
        if (argc > 4)
        {
            parameters.Density[0] = ReadScalar(argv[4], 0.0F, 2500.0F);
        }
        if (argc > 5)
        {
            parameters.Density[1] = ReadScalar(argv[5], 0.0F, 1.0F);
        }
        if (argc > 6)
        {
            parameters.Shape[0] = ReadScalar(argv[6], 1.0F, 16.0F);
            Require(std::floor(parameters.Shape[0]) == parameters.Shape[0], "Frequency must be an integer.");
        }
        if (argc > 7)
        {
            parameters.Density[3] = ReadScalar(argv[7], 0.0F, 1.0F);
        }
        if (argc > 8)
        {
            parameters.Density[2] = ReadScalar(argv[8], 0.0F, 100.0F);
        }
        if (argc > 9)
        {
            parameters.Regional[0] = ReadScalar(argv[9], 0.0F, 1.0F);
        }
        if (argc > 10)
        {
            parameters.Regional[1] = ReadScalar(argv[10], 1.0F, 64.0F);
            const auto period = static_cast<std::uint32_t>(parameters.Regional[1]);
            Require(static_cast<float>(period) == parameters.Regional[1] && (period & (period - 1U)) == 0,
                    "Regional period must be a power of two.");
        }
        if (argc > 11)
        {
            parameters.Shape[3] = ReadScalar(argv[11], 0.25F, 4.0F);
        }
        if (argc > 12)
        {
            parameters.Quality[2] = ReadSampleCount(argv[12], 128);
        }
        if (argc > 13)
        {
            parameters.Quality[3] = ReadSampleCount(argv[13], 16);
        }
        if (argc > 14)
        {
            const std::string mode = argv[14];
            Require(mode == "cloud" || mode == "air" || mode == "air-check" || mode == "density-check" ||
                        mode == "phase-average" || mode == "depth-check",
                    "Unsupported probe mode.");
            if (mode == "air")
            {
                parameters.Regional[2] = 1.0F;
            }
            else if (mode == "air-check")
            {
                parameters.Regional[2] = 2.0F;
            }
            else if (mode == "density-check")
            {
                parameters.Regional[2] = 3.0F;
            }
            else if (mode == "phase-average")
            {
                parameters.Lighting[1] = 1.0F;
            }
            else if (mode == "depth-check")
            {
                parameters.Regional[2] = 4.0F;
            }
        }
        if (argc > 15)
        {
            parameters.Regional[3] = 1.0F + ReadScalar(argv[15], 0.0F, 1.0F);
        }
        if (argc > 16)
        {
            parameters.Weather[0] = ReadScalar(argv[16], 0.0F, 1.0F);
            Require(parameters.Weather[0] == 0.0F || parameters.Weather[0] == 1.0F,
                    "Weather enabled must be zero or one.");
        }
        if (argc > 17)
        {
            parameters.Weather[1] = ReadScalar(argv[17], 0.0F, 1.0F);
        }
        if (argc > 18)
        {
            parameters.Weather[2] = ReadScalar(argv[18], 0.0F, 1.0F);
        }
        if (argc > 19)
        {
            parameters.Lighting[0] = ReadScalar(argv[19], 0.0F, 1.0F);
        }
        if (argc > 20)
        {
            parameters.Weather[3] = ReadScalar(argv[20], 0.0F, 100000.0F);
        }
        Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto format = backend == "vulkan" ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
        const auto destroyDevice = [](SDL_GPUDevice* value) { SDL_DestroyGPUDevice(value); };
        std::unique_ptr<SDL_GPUDevice, decltype(destroyDevice)> device(
            SDL_CreateGPUDevice(format, false, backend.c_str()), destroyDevice);
        Require(device != nullptr, SDL_GetError());
        std::cout << "Backend: " << SDL_GetGPUDeviceDriver(device.get()) << '\n';
        Render(device.get(), argv[2], format, parameters, argv[3]);
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
