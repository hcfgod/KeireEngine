#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace Keire::RenderBackend
{
    SDL_GPUTexture* UploadCloudNoiseVolume(SDL_GPUDevice* device, const CloudNoiseVolume& volume)
    {
        if (!device || volume.Mips.empty())
        {
            throw std::invalid_argument("Cloud noise upload requires a device and a complete mip chain.");
        }
        const auto dimension = volume.Mips.front().Dimension;
        if (dimension < 4 || dimension > 64 || (dimension & (dimension - 1U)) != 0)
        {
            throw std::invalid_argument("Cloud noise upload dimensions must be powers of two from 4 through 64.");
        }
        std::uint32_t expectedDimension = dimension;
        std::uint32_t totalBytes = 0;
        for (const auto& mip : volume.Mips)
        {
            const auto expectedBytes =
                static_cast<std::size_t>(expectedDimension) * expectedDimension * expectedDimension * 4U;
            if (expectedDimension == 0 || mip.Dimension != expectedDimension || mip.Rgba.size() != expectedBytes)
            {
                throw std::invalid_argument("Cloud noise upload contains an invalid mip level.");
            }
            totalBytes += static_cast<std::uint32_t>(expectedBytes);
            expectedDimension /= 2U;
        }
        if (expectedDimension != 0)
        {
            throw std::invalid_argument("Cloud noise upload requires every mip down to one texel.");
        }
        if (!SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_3D,
                                          SDL_GPU_TEXTUREUSAGE_SAMPLER))
        {
            throw std::runtime_error("The GPU backend does not support sampled RGBA8 cloud noise volumes.");
        }
        const auto failure = [](const char* operation)
        { return std::runtime_error(std::string(operation) + ": " + SDL_GetError()); };
        const auto releaseTexture = [device](SDL_GPUTexture* texture) noexcept
        { SDL_ReleaseGPUTexture(device, texture); };
        const auto releaseTransfer = [device](SDL_GPUTransferBuffer* transfer) noexcept
        { SDL_ReleaseGPUTransferBuffer(device, transfer); };
        const auto cancelCommands = [](SDL_GPUCommandBuffer* commands) noexcept
        { (void)SDL_CancelGPUCommandBuffer(commands); };
        SDL_GPUTextureCreateInfo textureInfo{};
        textureInfo.type = SDL_GPU_TEXTURETYPE_3D;
        textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        textureInfo.width = dimension;
        textureInfo.height = dimension;
        textureInfo.layer_count_or_depth = dimension;
        textureInfo.num_levels = static_cast<std::uint32_t>(volume.Mips.size());
        textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        std::unique_ptr<SDL_GPUTexture, decltype(releaseTexture)> texture(SDL_CreateGPUTexture(device, &textureInfo),
                                                                          releaseTexture);
        if (!texture)
        {
            throw failure("SDL_CreateGPUTexture(cloud noise) failed");
        }
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = totalBytes;
        std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
            SDL_CreateGPUTransferBuffer(device, &transferInfo), releaseTransfer);
        if (!transfer)
        {
            throw failure("SDL_CreateGPUTransferBuffer(cloud noise) failed");
        }
        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(device, transfer.get(), false));
        if (!mapped)
        {
            throw failure("SDL_MapGPUTransferBuffer(cloud noise) failed");
        }
        std::uint32_t offset = 0;
        for (const auto& mip : volume.Mips)
        {
            std::memcpy(mapped + offset, mip.Rgba.data(), mip.Rgba.size());
            offset += static_cast<std::uint32_t>(mip.Rgba.size());
        }
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
        std::unique_ptr<SDL_GPUCommandBuffer, decltype(cancelCommands)> commands(SDL_AcquireGPUCommandBuffer(device),
                                                                                 cancelCommands);
        if (!commands)
        {
            throw failure("SDL_AcquireGPUCommandBuffer(cloud noise) failed");
        }
        auto* copy = SDL_BeginGPUCopyPass(commands.get());
        if (!copy)
        {
            throw failure("SDL_BeginGPUCopyPass(cloud noise) failed");
        }
        offset = 0;
        for (std::uint32_t level = 0; level < volume.Mips.size(); ++level)
        {
            const auto& mip = volume.Mips[level];
            const SDL_GPUTextureTransferInfo source{transfer.get(), offset, mip.Dimension, mip.Dimension};
            const SDL_GPUTextureRegion destination{texture.get(), level,        0, 0, 0, 0, mip.Dimension,
                                                   mip.Dimension, mip.Dimension};
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            offset += static_cast<std::uint32_t>(mip.Rgba.size());
        }
        SDL_EndGPUCopyPass(copy);
        // Submission consumes the command buffer, including on failure.
        if (!SDL_SubmitGPUCommandBuffer(commands.release()))
        {
            throw failure("SDL_SubmitGPUCommandBuffer(cloud noise) failed");
        }
        return texture.release();
    }
} // namespace Keire::RenderBackend
