// Opt-in hardware probe; deliberately separate from the headless unit-test suite.
#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    void Require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void CheckRejected(SDL_GPUDevice* device, const Keire::RenderBackend::CloudNoiseVolume& volume)
    {
        try
        {
            auto* texture = Keire::RenderBackend::UploadCloudNoiseVolume(device, volume);
            SDL_ReleaseGPUTexture(device, texture);
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        throw std::runtime_error("Malformed cloud noise upload was accepted.");
    }

    void CheckRoundTrip(SDL_GPUDevice* device, const std::uint32_t dimension)
    {
        const auto volume = Keire::RenderBackend::BuildCloudNoiseVolume(dimension, 1);
        const auto releaseTexture = [device](SDL_GPUTexture* texture) { SDL_ReleaseGPUTexture(device, texture); };
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> texture(
            Keire::RenderBackend::UploadCloudNoiseVolume(device, volume), releaseTexture);
        std::uint32_t totalBytes = 0;
        for (const auto& mip : volume.Mips)
        {
            totalBytes += static_cast<std::uint32_t>(mip.Rgba.size());
        }
        const SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, totalBytes, 0};
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* buffer)
        { SDL_ReleaseGPUTransferBuffer(device, buffer); };
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &info), releaseTransfer);
        Require(transfer != nullptr, SDL_GetError());
        const auto cancelCommands = [](SDL_GPUCommandBuffer* commands) { (void)SDL_CancelGPUCommandBuffer(commands); };
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancelCommands)> commands(SDL_AcquireGPUCommandBuffer(device),
                                                                                 cancelCommands);
        Require(commands != nullptr, SDL_GetError());
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        Require(copy != nullptr, SDL_GetError());
        std::uint32_t offset = 0;
        for (std::uint32_t level = 0; level < volume.Mips.size(); ++level)
        {
            const auto& mip = volume.Mips[level];
            const SDL_GPUTextureRegion source{texture.get(), level,        0, 0, 0, 0, mip.Dimension,
                                              mip.Dimension, mip.Dimension};
            const SDL_GPUTextureTransferInfo destination{transfer.get(), offset, mip.Dimension, mip.Dimension};
            SDL_DownloadFromGPUTexture(copy, &source, &destination);
            offset += static_cast<std::uint32_t>(mip.Rgba.size());
        }
        SDL_EndGPUCopyPass(copy);
        const auto releaseFence = [device](SDL_GPUFence* fence) { SDL_ReleaseGPUFence(device, fence); };
        std::unique_ptr<SDL_GPUFence, decltype(releaseFence)> fence(
            SDL_SubmitGPUCommandBufferAndAcquireFence(commands.release()), releaseFence);
        Require(fence != nullptr, SDL_GetError());
        auto* fenceHandle = fence.get();
        Require(SDL_WaitForGPUFences(device, true, &fenceHandle, 1), SDL_GetError());
        const auto* mapped = static_cast<const std::byte*>(SDL_MapGPUTransferBuffer(device, transfer.get(), false));
        Require(mapped != nullptr, SDL_GetError());
        offset = 0;
        bool identical = true;
        for (const auto& mip : volume.Mips)
        {
            identical = identical && std::memcmp(mapped + offset, mip.Rgba.data(), mip.Rgba.size()) == 0;
            offset += static_cast<std::uint32_t>(mip.Rgba.size());
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        Require(identical, "Cloud noise GPU mip readback differs from CPU data.");
        std::cout << dimension << " cubed: " << totalBytes << " bytes matched across " << volume.Mips.size()
                  << " mip levels\n";
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto destroyDevice = [](SDL_GPUDevice* device) { SDL_DestroyGPUDevice(device); };
        std::unique_ptr<SDL_GPUDevice, decltype(destroyDevice)> device(
            SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                                false, argc > 1 ? argv[1] : nullptr),
            destroyDevice);
        Require(device != nullptr, SDL_GetError());
        std::cout << "Backend: " << SDL_GetGPUDeviceDriver(device.get()) << '\n';
        auto malformed = Keire::RenderBackend::BuildCloudNoiseVolume(4, 1);
        CheckRejected(nullptr, malformed);
        CheckRejected(device.get(), {});
        malformed.Mips.pop_back();
        CheckRejected(device.get(), malformed);
        malformed = Keire::RenderBackend::BuildCloudNoiseVolume(4, 1);
        malformed.Mips[1].Rgba.pop_back();
        CheckRejected(device.get(), malformed);
        malformed = Keire::RenderBackend::BuildCloudNoiseVolume(4, 1);
        malformed.Mips[1].Dimension = 4;
        CheckRejected(device.get(), malformed);
        malformed = Keire::RenderBackend::BuildCloudNoiseVolume(4, 1);
        malformed.Mips.push_back(malformed.Mips.back());
        CheckRejected(device.get(), malformed);
        CheckRoundTrip(device.get(), 4);
        CheckRoundTrip(device.get(), 64);
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
