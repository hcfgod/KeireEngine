// Opt-in hardware sampling comparison; does not qualify cloud appearance or frame-time budgets.
#include "CloudNoiseSamplingReference.h"
#include "KeireInternal/Rendering/CloudNoiseBuffer.h"
#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
    void Require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void CheckSamples(SDL_GPUDevice* device, const char* shaderPath, const SDL_GPUShaderFormat format,
                      const bool compareBuffer)
    {
        std::ifstream shaderStream(shaderPath, std::ios::binary);
        Require(shaderStream.good(), "Cannot open sampling probe shader.");
        const std::vector<std::uint8_t> shader{std::istreambuf_iterator<char>(shaderStream),
                                               std::istreambuf_iterator<char>()};
        SDL_GPUComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.code = shader.data();
        pipelineInfo.code_size = shader.size();
        pipelineInfo.entrypoint = "CSMain";
        pipelineInfo.format = format;
        pipelineInfo.num_samplers = 1;
        pipelineInfo.num_readonly_storage_buffers = compareBuffer ? 1U : 0U;
        pipelineInfo.num_readwrite_storage_buffers = 1;
        pipelineInfo.threadcount_x = 64;
        pipelineInfo.threadcount_y = 1;
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

        const std::uint32_t sampleCount = compareBuffer ? 512U : 416U;
        const std::uint32_t byteCount = sampleCount * 4 * sizeof(float);
        const SDL_GPUBufferCreateInfo bufferInfo{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, byteCount, 0};
        const auto releaseBuffer = [device](SDL_GPUBuffer* value) { SDL_ReleaseGPUBuffer(device, value); };
        std::unique_ptr<SDL_GPUBuffer, decltype(releaseBuffer)> output(SDL_CreateGPUBuffer(device, &bufferInfo),
                                                                       releaseBuffer);
        Require(output != nullptr, SDL_GetError());
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, byteCount, 0};
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* value)
        { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr, SDL_GetError());

        std::unique_ptr<SDL_GPUBuffer, decltype(releaseBuffer)> noiseBuffer(nullptr, releaseBuffer);
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> upload(nullptr, releaseTransfer);
        std::uint32_t uploadBytes = 0;
        if (compareBuffer)
        {
            const auto packed = Keire::RenderBackend::PackCloudNoiseBuffer(volume);
            uploadBytes = static_cast<std::uint32_t>(packed.size() * sizeof(std::uint32_t));
            const SDL_GPUBufferCreateInfo noiseInfo{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, uploadBytes, 0};
            noiseBuffer.reset(SDL_CreateGPUBuffer(device, &noiseInfo));
            Require(noiseBuffer != nullptr, SDL_GetError());
            const SDL_GPUTransferBufferCreateInfo uploadInfo{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uploadBytes, 0};
            upload.reset(SDL_CreateGPUTransferBuffer(device, &uploadInfo));
            Require(upload != nullptr, SDL_GetError());
            auto* mapped = SDL_MapGPUTransferBuffer(device, upload.get(), false);
            Require(mapped != nullptr, SDL_GetError());
            std::memcpy(mapped, packed.data(), uploadBytes);
            SDL_UnmapGPUTransferBuffer(device, upload.get());
        }

        const auto cancelCommands = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancelCommands)> commands(SDL_AcquireGPUCommandBuffer(device),
                                                                                 cancelCommands);
        Require(commands != nullptr, SDL_GetError());
        if (compareBuffer)
        {
            auto* uploadPass = SDL_BeginGPUCopyPass(commands.get());
            Require(uploadPass != nullptr, SDL_GetError());
            const SDL_GPUTransferBufferLocation source{upload.get(), 0};
            const SDL_GPUBufferRegion destination{noiseBuffer.get(), 0, uploadBytes};
            SDL_UploadToGPUBuffer(uploadPass, &source, &destination, false);
            SDL_EndGPUCopyPass(uploadPass);
        }
        const SDL_GPUStorageBufferReadWriteBinding storage{output.get(), false, 0, 0, 0};
        auto* compute = SDL_BeginGPUComputePass(commands.get(), nullptr, 0, &storage, 1);
        Require(compute != nullptr, SDL_GetError());
        SDL_BindGPUComputePipeline(compute, pipeline.get());
        const SDL_GPUTextureSamplerBinding binding{texture.get(), sampler.get()};
        SDL_BindGPUComputeSamplers(compute, 0, &binding, 1);
        if (compareBuffer)
        {
            auto* noise = noiseBuffer.get();
            SDL_BindGPUComputeStorageBuffers(compute, 0, &noise, 1);
        }
        SDL_DispatchGPUCompute(compute, (sampleCount + 63) / 64, 1, 1);
        SDL_EndGPUComputePass(compute);
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr, SDL_GetError());
        const SDL_GPUBufferRegion source{output.get(), 0, byteCount};
        const SDL_GPUTransferBufferLocation destination{transfer.get(), 0};
        SDL_DownloadFromGPUBuffer(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        const auto releaseFence = [device](SDL_GPUFence* value) { SDL_ReleaseGPUFence(device, value); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr, SDL_GetError());
        auto* fenceHandle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &fenceHandle, 1), SDL_GetError());
        const auto* values = static_cast<const float*>(SDL_MapGPUTransferBuffer(device, transfer.get(), false));
        Require(values != nullptr, SDL_GetError());
        double maximumError = 0.0;
        bool finite = true;
        for (std::uint32_t index = 0; index < sampleCount; ++index)
        {
            const float sample = static_cast<float>(index % 32);
            const KeireTest::CloudSamplePoint point{sample * 0.137F - 1.3F, sample * 0.071F + 0.23F,
                                                    sample * 0.193F - 0.91F};
            const auto lowerMip = (index / 32) / 2;
            const auto upperMip = std::min(lowerMip + 1, 6U);
            const double blend = (index / 32) % 2 == 0 ? 0.0 : 0.5;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const double expected =
                    compareBuffer
                        ? 0.0
                        : std::lerp(KeireTest::SampleCloudChannel(volume.Mips[lowerMip], point, channel),
                                    KeireTest::SampleCloudChannel(volume.Mips[upperMip], point, channel), blend);
                const double actual = values[index * 4 + channel];
                finite = finite && std::isfinite(actual);
                maximumError = std::max(maximumError, std::abs(actual - expected));
            }
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        std::cout << "samples=" << sampleCount << " channels=4 maximum_error=" << maximumError << '\n';
        Require(finite && maximumError < 0.004, "GPU cloud sampling differs from the trilinear mip reference.");
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3 || (argc == 4 && std::string(argv[3]) == "--compare-buffer"),
                "Usage: cloud-sample-gpu direct3d12|vulkan compiled-shader-path [--compare-buffer]");
        const std::string backend = argv[1];
        Require(backend == "direct3d12" || backend == "vulkan", "Unsupported probe backend.");
        Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto format = backend == "vulkan" ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
        const auto destroyDevice = [](SDL_GPUDevice* value) { SDL_DestroyGPUDevice(value); };
        std::unique_ptr<SDL_GPUDevice, decltype(destroyDevice)> device(
            SDL_CreateGPUDevice(format, false, backend.c_str()), destroyDevice);
        Require(device != nullptr, SDL_GetError());
        std::cout << "Backend: " << SDL_GetGPUDeviceDriver(device.get()) << '\n';
        CheckSamples(device.get(), argv[2], format, argc == 4);
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
