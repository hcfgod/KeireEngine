#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/CloudNoiseBuffer.h"
#include "KeireInternal/Rendering/CloudNoiseUpload.h"

#include <span>

namespace Keire::RenderBackend
{
    const GpuTextureResources& RenderSharedState::EnsureCloudNoiseTexture()
    {
        if (CloudNoiseTexture.Texture)
        {
            return CloudNoiseTexture;
        }
        // A fixed atlas is shared by all views. Authored seeds will transform sampling coordinates,
        // rather than regenerating a volume while the user edits a seed or switches cameras.
        constexpr std::uint32_t noiseDimension = 64;
        constexpr std::uint32_t atlasSeed = 1;
        const auto volume = BuildCloudNoiseVolume(noiseDimension, atlasSeed);
        GpuTextureResources resources;
        resources.Sampler = ResolveSampler(SamplerDescription{});
        resources.MipLevels = static_cast<std::uint32_t>(volume.Mips.size());
        for (const auto& mip : volume.Mips)
        {
            resources.EstimatedBytes += mip.Rgba.size();
        }
        try
        {
            resources.Texture = UploadCloudNoiseVolume(Device, volume);
        }
        catch (...)
        {
            RethrowIfDeviceLost("cloud noise upload");
            throw;
        }
        CloudNoiseTexture = resources;
        return CloudNoiseTexture;
    }
    SDL_GPUBuffer* RenderSharedState::EnsureCloudNoiseBuffer()
    {
        if (CloudNoiseBuffer)
        {
            return CloudNoiseBuffer;
        }
        constexpr std::uint32_t noiseDimension = 64;
        constexpr std::uint32_t atlasSeed = 1;
        const auto packed = PackCloudNoiseBuffer(BuildCloudNoiseVolume(noiseDimension, atlasSeed));
        CloudNoiseBuffer = UploadBuffer(std::as_bytes(std::span(packed)), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        return CloudNoiseBuffer;
    }
} // namespace Keire::RenderBackend
