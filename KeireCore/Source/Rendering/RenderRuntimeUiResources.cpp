#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::CaptureRuntimeUiImageLeases(RenderFramePacket& frame)
    {
        const auto captureAssetCommands = [this, &frame](const std::span<const RuntimeUiDrawCommand> commands)
        {
            for (const auto& command : commands)
            {
                if (command.Type != RuntimeUiDrawType::Image || command.RenderTexture || !command.Asset ||
                    FindRuntimeUiImageLease(frame, command.Asset))
                {
                    continue;
                }
                if (!Assets)
                    throw std::logic_error("Runtime UI image capture requires an active asset system.");
                frame.RuntimeUiImageLeases.push_back(
                    {.Asset = command.Asset,
                     .Handle = Assets->Load<Texture2DAsset>(command.Asset, AssetPriority::High)});
            }
        };
        captureAssetCommands(frame.RuntimeUiCommands);
        for (const auto& panel : frame.RuntimeUiCameraPanels)
            captureAssetCommands(panel.Commands);
        for (const auto& panel : frame.RuntimeUiWorldPanels)
            captureAssetCommands(panel.Commands);
        for (const auto& target : frame.RuntimeUiRenderTextures)
            captureAssetCommands(target.Commands);

        const auto captureFontCommands = [&frame](const std::span<const RuntimeUiDrawCommand> commands)
        {
            for (const auto& command : commands)
            {
                if (command.Type != RuntimeUiDrawType::Text)
                    continue;
                const auto font = RuntimeUiFontBindingId(command.Asset);
                if (FindRuntimeUiFontLease(frame, font))
                    continue;
                frame.RuntimeUiFontLeases.push_back({font, RuntimeUiFallbackGlyphAtlas()});
            }
        };
        captureFontCommands(frame.RuntimeUiCommands);
        for (const auto& panel : frame.RuntimeUiCameraPanels)
            captureFontCommands(panel.Commands);
        for (const auto& panel : frame.RuntimeUiWorldPanels)
            captureFontCommands(panel.Commands);
        for (const auto& target : frame.RuntimeUiRenderTextures)
            captureFontCommands(target.Commands);
    }

    void RenderSharedState::PrepareRuntimeUiRenderTextures(const RenderFramePacket& frame)
    {
        constexpr std::size_t maximumCachedTargets = 32U;
        PreparedRuntimeUiTextures.clear();
        FrameRuntimeUiRenderTextureTargets = BuildRuntimeUiRenderTextureOrder(frame);
        PreparedRuntimeUiTextures.reserve(FrameRuntimeUiRenderTextureTargets.size() +
                                          frame.RuntimeUiImageLeases.size());

        const auto retireEntry = [this](RuntimeUiRenderTextureCacheEntry& entry) noexcept
        {
            const auto estimatedBytes =
                static_cast<std::uint64_t>(entry.Width) * static_cast<std::uint64_t>(entry.Height) * 8ULL;
            if (entry.Published)
                Retire({.Texture = entry.Published, .EstimatedBytes = estimatedBytes});
            for (auto* texture : entry.Writers)
                if (texture)
                    Retire({.Texture = texture, .EstimatedBytes = estimatedBytes});
            entry = {};
        };
        const auto createEntry =
            [this, &frame](const AssetId asset, const std::uint32_t width, const std::uint32_t height)
        {
            RuntimeUiRenderTextureCacheEntry candidate;
            candidate.Asset = asset;
            candidate.Width = width;
            candidate.Height = height;
            candidate.DeviceGeneration = frame.DeviceGeneration;
            candidate.LastUsedFrame = frame.Id;
            candidate.Writers.resize(Specification.MaximumFramesInFlight);
            SDL_GPUTextureCreateInfo information{};
            information.type = SDL_GPU_TEXTURETYPE_2D;
            information.format = SceneColorFormat;
            information.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            information.width = width;
            information.height = height;
            information.layer_count_or_depth = 1;
            information.num_levels = 1;
            information.sample_count = SDL_GPU_SAMPLECOUNT_1;
            const auto create = [&]
            {
                auto* texture = SDL_CreateGPUTexture(Device, &information);
                if (!texture)
                    throw std::runtime_error("SDL_CreateGPUTexture(runtime UI RenderTexture) failed: " +
                                             LastSdlError());
                return texture;
            };
            try
            {
                candidate.Published = create();
                for (auto*& writer : candidate.Writers)
                    writer = create();
            }
            catch (...)
            {
                if (candidate.Published)
                    SDL_ReleaseGPUTexture(Device, candidate.Published);
                for (auto* writer : candidate.Writers)
                    if (writer)
                        SDL_ReleaseGPUTexture(Device, writer);
                throw;
            }
            return candidate;
        };

        for (const auto target : FrameRuntimeUiRenderTextureTargets)
        {
            const auto panel =
                std::ranges::find(frame.RuntimeUiRenderTextures, target, &CapturedRuntimeUiRenderTexture::Target);
            if (panel == frame.RuntimeUiRenderTextures.end() || !RuntimeUiRenderTextureOwnershipValid(*panel, frame))
            {
                throw std::logic_error(
                    "Runtime UI RenderTexture packet does not belong to the active frame slot and device generation.");
            }
            const auto width = static_cast<std::uint32_t>(std::ceil(panel->ReferenceResolution.X));
            const auto height = static_cast<std::uint32_t>(std::ceil(panel->ReferenceResolution.Y));
            auto entry =
                std::ranges::find(RuntimeUiRenderTextureCache, target, &RuntimeUiRenderTextureCacheEntry::Asset);
            const bool replace = entry != RuntimeUiRenderTextureCache.end() &&
                                 (entry->Width != width || entry->Height != height ||
                                  entry->DeviceGeneration != frame.DeviceGeneration ||
                                  entry->Writers.size() != Specification.MaximumFramesInFlight);
            if (entry == RuntimeUiRenderTextureCache.end() || replace)
            {
                auto candidate = createEntry(target, width, height);
                if (replace)
                {
                    retireEntry(*entry);
                    *entry = std::move(candidate);
                }
                else
                {
                    while (RuntimeUiRenderTextureCache.size() >= maximumCachedTargets)
                    {
                        const auto eviction = std::ranges::min_element(
                            RuntimeUiRenderTextureCache,
                            [this](const auto& first, const auto& second)
                            {
                                const bool firstProtected =
                                    std::ranges::find(FrameRuntimeUiRenderTextureTargets, first.Asset) !=
                                    FrameRuntimeUiRenderTextureTargets.end();
                                const bool secondProtected =
                                    std::ranges::find(FrameRuntimeUiRenderTextureTargets, second.Asset) !=
                                    FrameRuntimeUiRenderTextureTargets.end();
                                if (firstProtected != secondProtected)
                                    return !firstProtected;
                                return first.LastUsedFrame < second.LastUsedFrame;
                            });
                        if (eviction == RuntimeUiRenderTextureCache.end())
                            throw std::logic_error("Runtime UI RenderTexture cache could not select an eviction.");
                        if (std::ranges::find(FrameRuntimeUiRenderTextureTargets, eviction->Asset) !=
                            FrameRuntimeUiRenderTextureTargets.end())
                        {
                            throw std::length_error(
                                "Runtime UI RenderTexture working set exceeds the bounded 32-target GPU cache.");
                        }
                        retireEntry(*eviction);
                        RuntimeUiRenderTextureCache.erase(eviction);
                    }
                    RuntimeUiRenderTextureCache.push_back(std::move(candidate));
                    entry = std::prev(RuntimeUiRenderTextureCache.end());
                }
            }
            entry->LastUsedFrame = frame.Id;
            if (frame.FrameSlot >= entry->Writers.size() || !entry->Writers[frame.FrameSlot])
                throw std::logic_error("Runtime UI RenderTexture cache has no writer for the active frame slot.");
            PreparedRuntimeUiTextures.push_back(
                {target, {entry->Writers[frame.FrameSlot], WhiteTexture.Sampler}, frame.Id, frame.DeviceGeneration});
        }
    }

    void RenderSharedState::PrepareRuntimeUiTextureBindings(const RenderFramePacket& frame)
    {
        PreparedRuntimeUiTextures.reserve(PreparedRuntimeUiTextures.size() + frame.RuntimeUiImageLeases.size() +
                                          frame.RuntimeUiFontLeases.size());
        PrepareRuntimeUiFontAtlas(frame);
        for (const auto& lease : frame.RuntimeUiImageLeases)
        {
            if (!RuntimeUiImageLeaseOwnershipValid(lease, frame))
            {
                throw std::logic_error(
                    "Runtime UI image lease does not belong to the active frame slot and device generation.");
            }
            if (!lease.Handle || lease.Handle.Id() != lease.Asset)
                throw std::logic_error("Runtime UI image packet contains an invalid asset lease.");

            auto [iterator, inserted] = TextureCache.try_emplace(lease.Asset);
            if (inserted || !iterator->second.Handle)
                iterator->second.Handle = lease.Handle;
            const auto& texture = ResolveTexture(lease.Asset);
            PreparedRuntimeUiTextures.push_back(
                {lease.Asset, {texture.Texture, texture.Sampler}, frame.Id, frame.DeviceGeneration});
        }

        const auto prepareCommands = [this, &frame](const std::span<const RuntimeUiDrawCommand> commands)
        {
            for (const auto& command : commands)
            {
                const auto asset = RuntimeUiTextureAsset(command);
                if (!asset ||
                    std::ranges::find(PreparedRuntimeUiTextures, asset, &PreparedRuntimeUiTextureBinding::Asset) !=
                        PreparedRuntimeUiTextures.end())
                {
                    continue;
                }
                if (!command.RenderTexture)
                {
                    throw std::logic_error("Runtime UI image asset was not captured into the immutable frame packet.");
                }
                auto target =
                    std::ranges::find(RuntimeUiRenderTextureCache, asset, &RuntimeUiRenderTextureCacheEntry::Asset);
                if (target == RuntimeUiRenderTextureCache.end() || target->DeviceGeneration != frame.DeviceGeneration ||
                    !target->HasPublished || !target->Published)
                {
                    throw std::logic_error(
                        "Runtime UI Image references a logical RenderTexture with no published output. Submit its "
                        "UIDocument target in the same frame before sampling it.");
                }
                target->LastUsedFrame = frame.Id;
                PreparedRuntimeUiTextures.push_back(
                    {asset, {target->Published, WhiteTexture.Sampler}, frame.Id, frame.DeviceGeneration});
            }
        };
        prepareCommands(frame.RuntimeUiCommands);
        for (const auto& panel : frame.RuntimeUiCameraPanels)
            prepareCommands(panel.Commands);
        for (const auto& panel : frame.RuntimeUiWorldPanels)
            prepareCommands(panel.Commands);
        for (const auto& target : frame.RuntimeUiRenderTextures)
            prepareCommands(target.Commands);
    }

    SDL_GPUTextureSamplerBinding RenderSharedState::RuntimeUiTextureBinding(const AssetId asset) const
    {
        if (!asset)
            return {WhiteTexture.Texture, WhiteTexture.Sampler};
        const auto binding =
            std::ranges::find(PreparedRuntimeUiTextures, asset, &PreparedRuntimeUiTextureBinding::Asset);
        if (binding == PreparedRuntimeUiTextures.end() || !ActiveFrame || binding->Frame != ActiveFrame->Id ||
            binding->DeviceGeneration != ActiveFrame->DeviceGeneration)
        {
            throw std::logic_error(
                "Runtime UI image texture was not prepared for the active frame and device generation.");
        }
        return binding->Binding;
    }

    void RenderSharedState::PublishRuntimeUiRenderTextures(const RenderFramePacket& frame) noexcept
    {
        for (const auto targetId : FrameRuntimeUiRenderTextureTargets)
        {
            auto target =
                std::ranges::find(RuntimeUiRenderTextureCache, targetId, &RuntimeUiRenderTextureCacheEntry::Asset);
            if (target == RuntimeUiRenderTextureCache.end() || target->DeviceGeneration != frame.DeviceGeneration ||
                frame.FrameSlot >= target->Writers.size())
            {
                continue;
            }
            std::swap(target->Published, target->Writers[frame.FrameSlot]);
            target->HasPublished = target->Published != nullptr;
            target->LastUsedFrame = frame.Id;
        }
    }

    void RenderSharedState::ReleaseRuntimeUiRenderTextureCache(const bool abandon) noexcept
    {
        if (!abandon && Device)
        {
            for (auto& target : RuntimeUiRenderTextureCache)
            {
                if (target.Published)
                    SDL_ReleaseGPUTexture(Device, target.Published);
                for (auto* writer : target.Writers)
                    if (writer)
                        SDL_ReleaseGPUTexture(Device, writer);
            }
        }
        RuntimeUiRenderTextureCache.clear();
        FrameRuntimeUiRenderTextureTargets.clear();
        PreparedRuntimeUiTextures.clear();
    }

} // namespace Keire::RenderBackend
