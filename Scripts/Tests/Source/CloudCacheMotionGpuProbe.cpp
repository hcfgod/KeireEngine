// Opt-in GPU readback diagnostic; does not measure editor frame time.
#include "KeireInternal/Rendering/CloudLightCacheGrid.h"
#include "KeireInternal/Rendering/CloudLightCacheResources.h"
#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace Keire::RenderBackend;

    void Require(bool condition)
    {
        if (!condition)
        {
            throw std::runtime_error(SDL_GetError());
        }
    }

    struct Resources final
    {
        SDL_GPUDevice* Device;
        CloudLightCacheResources Cache;
        GpuTextureResources Noise;

        ~Resources()
        {
            ReleaseCloudLightCache(Device, Cache);
            SDL_ReleaseGPUSampler(Device, Noise.Sampler);
            SDL_ReleaseGPUTexture(Device, Noise.Texture);
        }
    };

    std::vector<float> ReadCache(Resources& resources, const CloudVolumeViewUniforms& view, bool aligned)
    {
        auto* device = resources.Device;
        constexpr Uint32 count = 129U * 33U * 129U * 4U;
        constexpr Uint32 bytes = count * sizeof(float);
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* value)
        { SDL_ReleaseGPUTransferBuffer(device, value); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        Require(transfer != nullptr);
        const auto cancel = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancel)> commands(SDL_AcquireGPUCommandBuffer(device), cancel);
        Require(commands != nullptr);
        InvalidateCloudLightCache(resources.Cache);
        (void)RecordCloudLightCache(device, commands.get(), resources.Cache, view, resources.Noise);
        if (!aligned)
        {
            const auto grid = MakeCloudLightCacheGrid(view.Volume, view.WeatherControls.W);
            Require(grid.has_value());
            const CloudLightCacheUniforms uniforms{view,
                                                   {grid->Origin[0], grid->Origin[1], grid->Origin[2], 0},
                                                   {grid->CellSize[0], grid->CellSize[1], grid->CellSize[2], 0},
                                                   {129U, 33U, 129U, 0U}};
            SDL_PushGPUComputeUniformData(commands.get(), 0, &uniforms, sizeof(uniforms));
            const SDL_GPUStorageBufferReadWriteBinding output{resources.Cache.Transmission, false, 0, 0, 0};
            auto* pass = SDL_BeginGPUComputePass(commands.get(), nullptr, 0, &output, 1);
            Require(pass != nullptr);
            SDL_BindGPUComputePipeline(pass, resources.Cache.Builder);
            const SDL_GPUTextureSamplerBinding noise{resources.Noise.Texture, resources.Noise.Sampler};
            SDL_BindGPUComputeSamplers(pass, 0, &noise, 1);
            SDL_DispatchGPUCompute(pass, 17, 5, 129);
            SDL_EndGPUComputePass(pass);
        }
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr);
        const SDL_GPUBufferRegion source{resources.Cache.Transmission, 0, bytes};
        const SDL_GPUTransferBufferLocation destination{transfer.get(), 0};
        SDL_DownloadFromGPUBuffer(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        const auto releaseFence = [device](SDL_GPUFence* value) { SDL_ReleaseGPUFence(device, value); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr);
        auto* fenceHandle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &fenceHandle, 1));
        std::vector<float> result(count);
        const auto* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), false);
        Require(mapped != nullptr);
        std::memcpy(result.data(), mapped, bytes);
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        InvalidateCloudLightCache(resources.Cache);
        return result;
    }

    void VerifyCanonicalVerticalReuse(Resources& resources, CloudVolumeViewUniforms view)
    {
        Keire::VolumetricCloudSettings settings;
        settings.Enabled = true;
        settings.BaseHeightMeters = 1500.0;
        settings.ThicknessMeters = 1200.0;
        settings.PatternSizeMeters = 3000.0;
        settings.WindMetersPerSecond = {};
        const Keire::Detail::EnvironmentClockState clock;
        const float basePhase =
            MakeVolumetricCloudUniforms(settings, {0.0, settings.BaseHeightMeters, 0.0}, clock).PatternOrigin[1];
        const auto releaseQuery = [&resources](SDL_GPUTimestampQuery* value)
        { SDL_ReleaseGPUTimestampQuery(resources.Device, value); };
        std::unique_ptr<SDL_GPUTimestampQuery, decltype(releaseQuery)> query(
            SDL_CreateGPUTimestampQuery(resources.Device), releaseQuery);
        Require(query != nullptr);
        double buildMilliseconds = 0.0;
        double reuseMilliseconds = 0.0;
        InvalidateCloudLightCache(resources.Cache);
        for (int step = 0; step < 21; ++step)
        {
            view.Volume = MakeVolumetricCloudUniforms(settings, {0.0, -1.0 + step * 0.1, 0.0}, clock);
            const auto cancel = [](SDL_GPUCommandBuffer* value) { (void)SDL_CancelGPUCommandBuffer(value); };
            std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancel)> commands(
                SDL_AcquireGPUCommandBuffer(resources.Device), cancel);
            Require(commands != nullptr);
            Require(SDL_WriteGPUTimestamp(commands.get(), query.get(), 0));
            (void)RecordCloudLightCache(resources.Device, commands.get(), resources.Cache, view, resources.Noise,
                                        basePhase);
            Require(SDL_WriteGPUTimestamp(commands.get(), query.get(), 1));
            const bool rebuilt = resources.Cache.Validity->RecordedKey.has_value();
            if (rebuilt != (step == 0))
            {
                throw std::runtime_error("Vertical camera movement unexpectedly rebuilt the cloud light cache.");
            }
            const auto releaseFence = [&resources](SDL_GPUFence* value)
            { SDL_ReleaseGPUFence(resources.Device, value); };
            std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
                SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
            Require(fence != nullptr);
            SubmitCloudLightCache(resources.Cache);
            auto* fenceHandle = fence.get();
            Require(SDL_WaitForGPUFences(resources.Device, true, &fenceHandle, 1));
            double milliseconds = 0.0;
            Require(SDL_GetGPUTimestampQueryResult(resources.Device, query.get(), fenceHandle, &milliseconds));
            if (rebuilt)
            {
                buildMilliseconds += milliseconds;
            }
            else
            {
                reuseMilliseconds += milliseconds;
            }
        }
        std::cout << "vertical cache: 1 build, 20 submitted reuses across sea level; build_gpu_ms=" << buildMilliseconds
                  << " mean_reuse_gpu_ms=" << reuseMilliseconds / 20.0 << '\n';
    }

    void Compare(SDL_GPUDevice* device)
    {
        Resources resources{device};
        resources.Noise.Texture = UploadCloudNoiseVolume(device, BuildCloudNoiseVolume(64, 1));
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler.max_lod = 32;
        resources.Noise.Sampler = SDL_CreateGPUSampler(device, &sampler);
        Require(resources.Noise.Sampler != nullptr);
        for (const float phase : {0.012345F, 255.99998F})
        {
            CloudVolumeViewUniforms view{};
            view.Volume.Geometry = {1500, 1200, 3000, 100000};
            view.Volume.Density = {0.6F, 0.004F, 0.2F, 0.65F};
            view.Volume.PatternOrigin = {phase, 0, phase, 0.5F};
            view.Volume.Quality = {1, 1, 48, 6};
            view.SunDirection = {0.6F, 0.8F, 0, 0};
            view.SunRadiance = {1, 1, 1, 0};
            view.ShapeControls = {4, 0, 64, 1};
            view.RegionalControls = {1, 2, 0, 1.2F};
            view.WeatherControls = {1, 1, 0.5F, 500};
            view.HeightProfile = {0, 0.2F, 0.8F, 1};
            VerifyCanonicalVerticalReuse(resources, view);
            const auto original = ReadCache(resources, view, false);
            const auto aligned = ReadCache(resources, view, true);
            double total = 0;
            double maximum = 0;
            for (std::size_t index = 0; index < original.size(); ++index)
            {
                if (!std::isfinite(original[index]) || !std::isfinite(aligned[index]))
                {
                    throw std::runtime_error("Non-finite cache output.");
                }
                const double error = std::abs(static_cast<double>(original[index]) - aligned[index]);
                maximum = std::max(maximum, error);
                total += error;
            }
            std::cout << "phase=" << phase << " values=" << original.size() << " max=" << maximum
                      << " mean=" << total / original.size() << '\n';
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(SDL_Init(SDL_INIT_VIDEO));
        const auto destroy = [](SDL_GPUDevice* value) { SDL_DestroyGPUDevice(value); };
        std::unique_ptr<SDL_GPUDevice, decltype(destroy)> device(
            SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV, false,
                                argc > 1 ? argv[1] : nullptr),
            destroy);
        Require(device != nullptr);
        std::cout << SDL_GetGPUDeviceDriver(device.get()) << '\n';
        std::cout << "adapter="
                  << SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device.get()), SDL_PROP_GPU_DEVICE_NAME_STRING,
                                           "unknown")
                  << '\n';
        Compare(device.get());
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
