#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::PrepareRuntimeUiFontAtlas(const RenderFramePacket& frame)
    {
        if (frame.RuntimeUiFontLeases.empty())
            return;
        constexpr std::size_t maximumCachedAtlases = 32U;
        Statistics.RuntimeUiRenderer.GlyphAtlasEntries = 0;
        Statistics.RuntimeUiRenderer.GlyphAtlasBytes = 0;

        const auto retireEntry = [this](RuntimeUiFontAtlasCacheEntry& entry) noexcept
        {
            if (entry.Texture)
                Retire({.Texture = entry.Texture});
            entry = {};
        };
        for (const auto& lease : frame.RuntimeUiFontLeases)
        {
            const auto& source = lease.Atlas;
            if (!RuntimeUiFontLeaseOwnershipValid(lease, frame) || !source || source->Width == 0 ||
                source->Height == 0 || source->Pixels.empty() ||
                source->Pixels.size() > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::logic_error(
                    "Runtime UI font atlas lease is invalid or belongs to another frame generation.");
            }
            Statistics.RuntimeUiRenderer.GlyphAtlasEntries +=
                source->ShapedGlyphs.empty() ? source->Glyphs.size() : source->ShapedGlyphs.size();
            Statistics.RuntimeUiRenderer.GlyphAtlasBytes += source->Pixels.size();

            auto cache = std::ranges::find(RuntimeUiFontAtlases, lease.Font, &RuntimeUiFontAtlasCacheEntry::Font);
            if (cache != RuntimeUiFontAtlases.end() &&
                !RuntimeUiFontAtlasCacheValid(*cache, frame.DeviceGeneration, source->Generation))
            {
                retireEntry(*cache);
                cache->Font = lease.Font;
            }
            if (cache == RuntimeUiFontAtlases.end())
            {
                while (RuntimeUiFontAtlases.size() >= maximumCachedAtlases)
                {
                    const auto eviction =
                        std::ranges::min_element(RuntimeUiFontAtlases, [](const auto& first, const auto& second)
                                                 { return first.LastUsedFrame < second.LastUsedFrame; });
                    if (eviction == RuntimeUiFontAtlases.end())
                        throw std::logic_error("Runtime UI font atlas cache could not select an eviction.");
                    retireEntry(*eviction);
                    RuntimeUiFontAtlases.erase(eviction);
                }
                RuntimeUiFontAtlases.push_back({.Font = lease.Font});
                cache = std::prev(RuntimeUiFontAtlases.end());
            }

            if (!RuntimeUiFontAtlasCacheValid(*cache, frame.DeviceGeneration, source->Generation))
            {
                SDL_GPUTexture* candidate = nullptr;
                SDL_GPUTransferBuffer* transfer = nullptr;
                try
                {
                    SDL_GPUTextureCreateInfo information{};
                    information.type = SDL_GPU_TEXTURETYPE_2D;
                    information.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                    information.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    information.width = source->Width;
                    information.height = source->Height;
                    information.layer_count_or_depth = 1;
                    information.num_levels = 1;
                    information.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    candidate = SDL_CreateGPUTexture(Device, &information);
                    if (!candidate)
                        throw std::runtime_error("SDL_CreateGPUTexture(runtime UI font atlas) failed: " +
                                                 LastSdlError());

                    SDL_GPUTransferBufferCreateInfo transferInformation{};
                    transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    transferInformation.size = static_cast<std::uint32_t>(source->Pixels.size());
                    transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
                    if (!transfer)
                    {
                        throw std::runtime_error("SDL_CreateGPUTransferBuffer(runtime UI font atlas) failed: " +
                                                 LastSdlError());
                    }
                    auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
                    if (!mapped)
                        throw std::runtime_error("SDL_MapGPUTransferBuffer(runtime UI font atlas) failed: " +
                                                 LastSdlError());
                    std::memcpy(mapped, source->Pixels.data(), source->Pixels.size());
                    SDL_UnmapGPUTransferBuffer(Device, transfer);
                    EnsureFrameUploadContext();
                    const SDL_GPUTextureTransferInfo upload{transfer, 0, source->Width, source->Height};
                    const SDL_GPUTextureRegion destination{candidate, 0, 0, 0, 0, 0, source->Width, source->Height, 1};
                    SDL_UploadToGPUTexture(FrameUploadPass, &upload, &destination, false);
                    FrameUploadTransfers.push_back(transfer);
                    transfer = nullptr;
                }
                catch (...)
                {
                    if (transfer)
                        SDL_ReleaseGPUTransferBuffer(Device, transfer);
                    if (candidate)
                        SDL_ReleaseGPUTexture(Device, candidate);
                    RethrowIfDeviceLost("runtime UI font atlas upload");
                    throw;
                }
                cache->Texture = candidate;
                cache->DeviceGeneration = frame.DeviceGeneration;
                cache->AtlasGeneration = source->Generation;
                ++cache->BuildCount;
            }
            cache->LastUsedFrame = frame.Id;
            if (std::ranges::find(PreparedRuntimeUiTextures, lease.Font, &PreparedRuntimeUiTextureBinding::Asset) !=
                PreparedRuntimeUiTextures.end())
            {
                continue;
            }
            PreparedRuntimeUiTextures.push_back(
                {lease.Font, {cache->Texture, WhiteTexture.Sampler}, frame.Id, frame.DeviceGeneration});
        }
    }

    void RenderSharedState::ReleaseRuntimeUiFontAtlas(const bool abandon) noexcept
    {
        if (!abandon && Device)
            for (const auto& atlas : RuntimeUiFontAtlases)
                if (atlas.Texture)
                    SDL_ReleaseGPUTexture(Device, atlas.Texture);
        RuntimeUiFontAtlases.clear();
        // Font bytes and rasterized CPU pages intentionally survive a device generation. Recovery destroys only
        // device-owned atlas textures; the accepted immutable frame can then republish those pages without touching
        // project assets or reshaping text on the render thread.
    }
} // namespace Keire::RenderBackend
