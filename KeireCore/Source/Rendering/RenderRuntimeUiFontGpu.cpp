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
        const auto& source = RuntimeUiFallbackGlyphAtlas();
        if (!source || source->Width == 0 || source->Height == 0 || source->Pixels.empty() ||
            source->Pixels.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::logic_error("The deterministic runtime UI fallback glyph atlas is invalid.");
        }
        for (const auto& lease : frame.RuntimeUiFontLeases)
        {
            if (!RuntimeUiFontLeaseOwnershipValid(lease, frame) || lease.Atlas != source)
                throw std::logic_error("Runtime UI font atlas lease does not belong to the active frame generation.");
        }
        Statistics.RuntimeUiRenderer.GlyphAtlasEntries = source->Glyphs.size();
        Statistics.RuntimeUiRenderer.GlyphAtlasBytes = source->Pixels.size();

        if (!RuntimeUiFontAtlasCacheValid(RuntimeUiFontAtlas, frame.DeviceGeneration))
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
                    throw std::runtime_error("SDL_CreateGPUTexture(runtime UI font atlas) failed: " + LastSdlError());

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
            if (RuntimeUiFontAtlas.Texture)
                Retire({.Texture = RuntimeUiFontAtlas.Texture,
                        .EstimatedBytes = static_cast<std::uint64_t>(source->Pixels.size())});
            RuntimeUiFontAtlas.Texture = candidate;
            RuntimeUiFontAtlas.DeviceGeneration = frame.DeviceGeneration;
            ++RuntimeUiFontAtlas.BuildCount;
        }

        for (const auto& lease : frame.RuntimeUiFontLeases)
        {
            if (std::ranges::find(PreparedRuntimeUiTextures, lease.Font, &PreparedRuntimeUiTextureBinding::Asset) !=
                PreparedRuntimeUiTextures.end())
            {
                continue;
            }
            PreparedRuntimeUiTextures.push_back(
                {lease.Font, {RuntimeUiFontAtlas.Texture, WhiteTexture.Sampler}, frame.Id, frame.DeviceGeneration});
        }
    }

    void RenderSharedState::ReleaseRuntimeUiFontAtlas(const bool abandon) noexcept
    {
        if (RuntimeUiFontAtlas.Texture && !abandon && Device)
            SDL_ReleaseGPUTexture(Device, RuntimeUiFontAtlas.Texture);
        RuntimeUiFontAtlas.Texture = nullptr;
        RuntimeUiFontAtlas.DeviceGeneration = 0;
    }
} // namespace Keire::RenderBackend
