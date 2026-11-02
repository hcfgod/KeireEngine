// Opt-in numerical GPU check, independent of editor appearance and performance
// acceptance.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void Verify(SDL_GPUDevice* device, SDL_GPUShaderFormat format, const char* path, std::uint32_t records,
                bool cloudNoise)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "Cannot open invariant shader.");
        const std::vector<std::uint8_t> shader{std::istreambuf_iterator<char>(stream), {}};
        SDL_GPUComputePipelineCreateInfo info{};
        info.code = shader.data();
        info.code_size = shader.size();
        info.entrypoint = "main";
        info.format = format;
        info.num_readwrite_storage_buffers = 1;
        info.num_samplers = cloudNoise ? 1U : 0U;
        info.threadcount_x = 1;
        info.threadcount_y = 1;
        info.threadcount_z = 1;
        const auto releasePipeline = [device](SDL_GPUComputePipeline* value)
        { SDL_ReleaseGPUComputePipeline(device, value); };
        std::unique_ptr<SDL_GPUComputePipeline, decltype(releasePipeline)> pipeline(
            SDL_CreateGPUComputePipeline(device, &info), releasePipeline);
        Require(pipeline != nullptr, SDL_GetError());
        std::cout << "pipeline-ready" << std::endl;

        const Uint32 bytes = records * 4U * sizeof(float);
        const SDL_GPUBufferCreateInfo bufferInfo{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, bytes, 0};
        const auto releaseBuffer = [device](SDL_GPUBuffer* value) { SDL_ReleaseGPUBuffer(device, value); };
        std::unique_ptr<SDL_GPUBuffer, decltype(releaseBuffer)> output(SDL_CreateGPUBuffer(device, &bufferInfo),
                                                                       releaseBuffer);
        Require(output != nullptr, SDL_GetError());
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* value)
        { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr, SDL_GetError());

        // Every output must be written by the shader; untouched GPU memory must never
        // pass as zero.
        const SDL_GPUTransferBufferCreateInfo uploadInfo{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> upload(
            SDL_CreateGPUTransferBuffer(device, &uploadInfo), releaseTransfer);
        Require(upload != nullptr, SDL_GetError());
        auto* initial = static_cast<float*>(SDL_MapGPUTransferBuffer(device, upload.get(), false));
        Require(initial != nullptr, SDL_GetError());
        std::fill_n(initial, records * 4U, std::numeric_limits<float>::quiet_NaN());
        SDL_UnmapGPUTransferBuffer(device, upload.get());

        const auto releaseTexture = [device](SDL_GPUTexture* value) { SDL_ReleaseGPUTexture(device, value); };
        const auto releaseSampler = [device](SDL_GPUSampler* value) { SDL_ReleaseGPUSampler(device, value); };
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> noise(nullptr, releaseTexture);
        std::unique_ptr<SDL_GPUSampler, decltype(releaseSampler)> noiseSampler(nullptr, releaseSampler);
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> noiseUpload(nullptr, releaseTransfer);
        if (cloudNoise)
        {
            SDL_GPUTextureCreateInfo textureInfo{};
            textureInfo.type = SDL_GPU_TEXTURETYPE_3D;
            textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            textureInfo.width = 1;
            textureInfo.height = 1;
            textureInfo.layer_count_or_depth = 1;
            textureInfo.num_levels = 1;
            textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
            noise.reset(SDL_CreateGPUTexture(device, &textureInfo));
            Require(noise != nullptr, SDL_GetError());
            SDL_GPUSamplerCreateInfo samplerInfo{};
            samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
            samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
            samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            noiseSampler.reset(SDL_CreateGPUSampler(device, &samplerInfo));
            Require(noiseSampler != nullptr, SDL_GetError());
            const SDL_GPUTransferBufferCreateInfo noiseUploadInfo{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 4, 0};
            noiseUpload.reset(SDL_CreateGPUTransferBuffer(device, &noiseUploadInfo));
            Require(noiseUpload != nullptr, SDL_GetError());
            auto* pixels = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, noiseUpload.get(), false));
            Require(pixels != nullptr, SDL_GetError());
            // Constant data validates the real sampler contract. It does not validate
            // visual cloud noise.
            std::fill_n(pixels, 4, Uint8{128});
            SDL_UnmapGPUTransferBuffer(device, noiseUpload.get());
        }

        const auto cancel = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancel)> commands(SDL_AcquireGPUCommandBuffer(device), cancel);
        Require(commands != nullptr, SDL_GetError());
        auto* initialize = SDL_BeginGPUCopyPass(commands.get());
        Require(initialize != nullptr, SDL_GetError());
        const SDL_GPUTransferBufferLocation initialSource{upload.get(), 0};
        const SDL_GPUBufferRegion initialDestination{output.get(), 0, bytes};
        SDL_UploadToGPUBuffer(initialize, &initialSource, &initialDestination, false);
        if (cloudNoise)
        {
            const SDL_GPUTextureTransferInfo noiseSource{noiseUpload.get(), 0, 1, 1};
            const SDL_GPUTextureRegion noiseDestination{noise.get(), 0, 0, 0, 0, 0, 1, 1, 1};
            SDL_UploadToGPUTexture(initialize, &noiseSource, &noiseDestination, false);
        }
        SDL_EndGPUCopyPass(initialize);
        const SDL_GPUStorageBufferReadWriteBinding binding{output.get(), false, 0, 0, 0};
        auto* compute = SDL_BeginGPUComputePass(commands.get(), nullptr, 0, &binding, 1);
        Require(compute != nullptr, SDL_GetError());
        SDL_BindGPUComputePipeline(compute, pipeline.get());
        if (cloudNoise)
        {
            const SDL_GPUTextureSamplerBinding noiseBinding{noise.get(), noiseSampler.get()};
            SDL_BindGPUComputeSamplers(compute, 0, &noiseBinding, 1);
        }
        SDL_DispatchGPUCompute(compute, 1, 1, 1);
        SDL_EndGPUComputePass(compute);
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr, SDL_GetError());
        const SDL_GPUBufferRegion source{output.get(), 0, bytes};
        const SDL_GPUTransferBufferLocation destination{transfer.get(), 0};
        SDL_DownloadFromGPUBuffer(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        const auto releaseFence = [device](SDL_GPUFence* value) { SDL_ReleaseGPUFence(device, value); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr, SDL_GetError());
        std::cout << "submitted" << std::endl;
        auto* handle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &handle, 1), SDL_GetError());
        std::cout << "gpu-complete" << std::endl;
        const auto* values = static_cast<const float*>(SDL_MapGPUTransferBuffer(device, transfer.get(), false));
        Require(values != nullptr, SDL_GetError());
        float maximumError = 0.0F;
        bool finite = true;
        for (std::uint32_t index = 0; index < records * 4U; ++index)
        {
            finite = finite && std::isfinite(values[index]);
            maximumError = std::max(maximumError, std::abs(values[index]));
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        std::cout << "records=" << records << " finite=" << finite << " maximum_error=" << maximumError << '\n';
        Require(finite && maximumError <= 0.00001F, "GPU invariant failed.");
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4 || (argc == 5 && std::string(argv[4]) == "--cloud-noise"),
                "Usage: shader-invariant-gpu direct3d12|vulkan shader-path "
                "float4-record-count [--cloud-noise]");
        const std::string backend = argv[1];
        Require(backend == "direct3d12" || backend == "vulkan", "Unsupported backend.");
        std::size_t consumed = 0;
        const auto records = std::stoul(argv[3], &consumed);
        Require(consumed == std::string(argv[3]).size() && records > 0 && records <= 4096,
                "Record count must be an integer from 1 to 4096.");
        Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        std::cout << "sdl-ready" << std::endl;
        const auto format = backend == "vulkan" ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
        const auto destroy = [](SDL_GPUDevice* value) { SDL_DestroyGPUDevice(value); };
        std::unique_ptr<SDL_GPUDevice, decltype(destroy)> device(SDL_CreateGPUDevice(format, true, backend.c_str()),
                                                                 destroy);
        Require(device != nullptr, SDL_GetError());
        std::cout << "device-ready" << std::endl;
        Verify(device.get(), format, argv[2], static_cast<std::uint32_t>(records), argc == 5);
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
