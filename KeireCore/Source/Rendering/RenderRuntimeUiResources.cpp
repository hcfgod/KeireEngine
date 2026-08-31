#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::size_t MaximumRuntimeUiFontAtlasPages = 32U;

        constexpr float RuntimeUiCustomAtlasRasterSize = 48.0F;
        constexpr std::size_t MaximumRuntimeUiFontFamilies = 32U;

        struct PendingRuntimeUiTextPreparation final
        {
            RuntimeUiDrawCommand* Command = nullptr;
            std::vector<AssetId> FaceBindings;
            std::shared_ptr<const Detail::RuntimeUiTextLayout> Layout;
        };

        [[nodiscard]] UiFontStyle FontAssetStyle(const RuntimeUiFontSlant slant) noexcept
        {
            switch (slant)
            {
            case RuntimeUiFontSlant::Italic:
                return UiFontStyle::Italic;
            case RuntimeUiFontSlant::Oblique:
                return UiFontStyle::Oblique;
            case RuntimeUiFontSlant::Normal:
                return UiFontStyle::Normal;
            }
            return UiFontStyle::Normal;
        }

        [[nodiscard]] const UiFontFaceReference&
        SelectFontFace(const UiFontFamilyDefinition& family, const std::uint16_t weight, const RuntimeUiFontSlant slant)
        {
            const auto requestedStyle = FontAssetStyle(slant);
            return *std::ranges::min_element(
                family.Faces,
                [weight, requestedStyle](const auto& first, const auto& second)
                {
                    const auto score = [weight, requestedStyle](const UiFontFaceReference& candidate)
                    {
                        const auto distance = static_cast<std::uint32_t>(
                            std::abs(static_cast<int>(candidate.Weight) - static_cast<int>(weight)));
                        return std::tuple(candidate.Style == requestedStyle ? 0U : 1U, distance, candidate.Weight,
                                          candidate.Face);
                    };
                    return score(first) < score(second);
                });
        }

        [[nodiscard]] AssetId CustomFontBinding(const AssetId family, const AssetId face,
                                                const std::uint16_t collectionIndex, const std::uint16_t weight,
                                                const RuntimeUiFontSlant slant) noexcept
        {
            auto high = family.High() ^ std::rotl(face.High(), 17) ^ 0x4b4549525549464fULL;
            auto low = family.Low() ^ std::rotl(face.Low(), 29) ^ 0x4e5442494e440000ULL;
            high ^= static_cast<std::uint64_t>(collectionIndex) << 32U | static_cast<std::uint64_t>(weight) << 16U;
            low ^= static_cast<std::uint64_t>(slant) << 56U;
            if (high == 0 && low == 0)
                low = 1;
            AssetId result(high, low);
            if (result == RuntimeUiFallbackFontId)
                result = AssetId(high, low ^ 1U);
            return result;
        }

        [[nodiscard]] AssetId CustomFontPageBinding(const AssetId binding, const std::uint16_t pageIndex) noexcept
        {
            if (pageIndex == 0U)
                return binding;
            auto high = binding.High() ^ std::rotl(0x554941544c415350ULL + pageIndex, pageIndex % 61U);
            auto low = binding.Low() ^ std::rotl(0x4147450000000000ULL + pageIndex, (pageIndex * 7U) % 61U);
            if (high == 0U && low == 0U)
                low = pageIndex;
            return AssetId(high, low);
        }

        [[nodiscard]] std::uint64_t CustomFontAtlasGeneration(const RuntimeUiFontCpuCacheEntry& cache) noexcept
        {
            std::uint64_t result = 1469598103934665603ULL;
            const auto combine = [&result](const std::uint64_t value)
            {
                result ^= value;
                result *= 1099511628211ULL;
            };
            combine(cache.Binding.High());
            combine(cache.Binding.Low());
            combine(cache.FamilyRevision);
            combine(cache.FaceRevision);
            for (const auto glyph : cache.Glyphs)
                combine(glyph);
            return result == 0 ? 1U : result;
        }

        void PopulatePreparedText(RuntimeUiDrawCommand& command, const Detail::RuntimeUiTextLayout& layout,
                                  const std::span<RuntimeUiFontCpuCacheEntry* const> faces)
        {
            command.PreparedFontBinding = {};
            command.PreparedTextGlyphs.clear();
            command.PreparedTextLines.clear();
            command.PreparedTextWidth = 0.0F;
            command.PreparedTextHeight = 0.0F;
            command.PreparedTextGlyphs.reserve(layout.Glyphs.size());
            const float scale = command.FontSize / RuntimeUiCustomAtlasRasterSize;
            for (const auto& placement : layout.Glyphs)
            {
                RuntimeUiPreparedTextGlyph prepared;
                prepared.Position = {placement.X, placement.Y};
                if (placement.Codepoint != U'\n' && placement.FaceIndex < faces.size() && faces[placement.FaceIndex])
                {
                    const auto* face = faces[placement.FaceIndex];
                    for (const auto& page : face->AtlasPages)
                    {
                        const auto* glyph = page ? FindRuntimeUiGlyph(*page, placement.Glyph) : nullptr;
                        if (!glyph)
                            continue;
                        prepared.FontBinding = CustomFontPageBinding(face->Binding, page->PageIndex);
                        prepared.UvMinimum = glyph->UvMinimum;
                        prepared.UvMaximum = glyph->UvMaximum;
                        prepared.Offset = {placement.OffsetX + glyph->Offset.X * scale,
                                           placement.OffsetY + layout.Ascender + glyph->Offset.Y * scale};
                        prepared.Size = {glyph->Width * scale, glyph->Height * scale};
                        if (!command.PreparedFontBinding)
                            command.PreparedFontBinding = prepared.FontBinding;
                        break;
                    }
                }
                command.PreparedTextGlyphs.push_back(prepared);
            }
            command.PreparedTextLines.reserve(layout.Lines.size());
            for (const auto& line : layout.Lines)
                command.PreparedTextLines.push_back({line.FirstGlyph, line.GlyphCount, line.Width});
            command.PreparedTextWidth = layout.Width;
            command.PreparedTextHeight = layout.Height;
        }
    } // namespace

    void RenderSharedState::CaptureRuntimeUiImageLeases(RenderFramePacket& frame)
    {
        const auto captureAssetCommands = [this, &frame](const std::span<const RuntimeUiDrawCommand> commands)
        {
            for (const auto& command : commands)
            {
                const auto capture = [this, &frame](const AssetId asset)
                {
                    if (!asset || FindRuntimeUiImageLease(frame, asset))
                        return;
                    if (!Assets)
                        throw std::logic_error("Runtime UI image capture requires an active asset system.");
                    frame.RuntimeUiImageLeases.push_back(
                        {.Asset = asset, .Handle = Assets->Load<Texture2DAsset>(asset, AssetPriority::High)});
                };
                if (command.Type == RuntimeUiDrawType::Image && !command.RenderTexture)
                    capture(command.Asset);
                capture(command.AlphaMask);
            }
        };
        captureAssetCommands(frame.RuntimeUiCommands);
        for (const auto& panel : frame.RuntimeUiCameraPanels)
            captureAssetCommands(panel.Commands);
        for (const auto& panel : frame.RuntimeUiWorldPanels)
            captureAssetCommands(panel.Commands);
        for (const auto& target : frame.RuntimeUiRenderTextures)
            captureAssetCommands(target.Commands);

        std::vector<PendingRuntimeUiTextPreparation> pendingText;
        std::vector<AssetId> touchedCaches;
        const auto resolveFontCache = [this, &frame](RuntimeUiDrawCommand& command,
                                                     const AssetId resolvedFamily) -> RuntimeUiFontCpuCacheEntry*
        {
            if (!command.Asset || !resolvedFamily || !Assets)
                return nullptr;
            auto cache = std::ranges::find_if(RuntimeUiFontCpuCache,
                                              [&command, resolvedFamily](const auto& candidate)
                                              {
                                                  return candidate.Family == command.Asset &&
                                                         candidate.ResolvedFamily == resolvedFamily &&
                                                         candidate.Weight == command.FontWeight &&
                                                         candidate.Slant == command.FontSlant;
                                              });
            if (cache == RuntimeUiFontCpuCache.end())
            {
                while (RuntimeUiFontCpuCache.size() >= MaximumRuntimeUiFontFamilies)
                {
                    const auto eviction =
                        std::ranges::min_element(RuntimeUiFontCpuCache, [](const auto& first, const auto& second)
                                                 { return first.LastUsedFrame < second.LastUsedFrame; });
                    if (eviction == RuntimeUiFontCpuCache.end() || eviction->LastUsedFrame == frame.Id)
                    {
                        throw std::length_error("Runtime UI font working set exceeds the bounded 32-family CPU cache.");
                    }
                    RuntimeUiFontCpuCache.erase(eviction);
                }
                RuntimeUiFontCpuCache.push_back(
                    {.Family = command.Asset,
                     .ResolvedFamily = resolvedFamily,
                     .FamilyHandle = Assets->Load<UiFontFamilyAsset>(resolvedFamily, AssetPriority::High),
                     .LastUsedFrame = frame.Id,
                     .Weight = command.FontWeight,
                     .Slant = command.FontSlant});
                cache = std::prev(RuntimeUiFontCpuCache.end());
            }
            cache->LastUsedFrame = frame.Id;
            if (!cache->FamilyHandle || cache->FamilyHandle.Id() != resolvedFamily)
                cache->FamilyHandle = Assets->Load<UiFontFamilyAsset>(resolvedFamily, AssetPriority::High);
            const auto family = cache->FamilyHandle.TryGetLoaded();
            if (!family)
                return nullptr;
            if (cache->FamilyRevision != cache->FamilyHandle.Revision() || !cache->Face)
            {
                const auto& selected = SelectFontFace(family->Definition(), command.FontWeight, command.FontSlant);
                cache->FamilyRevision = cache->FamilyHandle.Revision();
                cache->Face = selected.Face;
                cache->CollectionIndex = selected.CollectionIndex;
                cache->FaceHandle = Assets->Load<UiFontFaceAsset>(selected.Face, AssetPriority::High);
                cache->FaceRevision = 0;
                cache->Glyphs.clear();
                cache->AtlasPages.clear();
                cache->AtlasGlyphRequestCount = 0;
                cache->Binding = CustomFontBinding(command.Asset, selected.Face, selected.CollectionIndex,
                                                   command.FontWeight, command.FontSlant);
            }
            if (!cache->FaceHandle || cache->FaceHandle.Id() != cache->Face)
                cache->FaceHandle = Assets->Load<UiFontFaceAsset>(cache->Face, AssetPriority::High);
            const auto face = cache->FaceHandle.TryGetLoaded();
            if (!face)
                return nullptr;
            if (cache->FaceRevision != cache->FaceHandle.Revision())
            {
                cache->FaceRevision = cache->FaceHandle.Revision();
                cache->Glyphs.clear();
                cache->AtlasPages.clear();
                cache->AtlasGlyphRequestCount = 0;
            }
            return &*cache;
        };

        const auto resolveTextLayout =
            [this](const std::span<RuntimeUiFontCpuCacheEntry* const> faces, const RuntimeUiDrawCommand& command)
        {
            if (faces.empty() || !faces.front())
                return std::shared_ptr<const Detail::RuntimeUiTextLayout>{};
            const auto primary = faces.front()->FaceHandle.TryGetLoaded();
            if (!primary)
                return std::shared_ptr<const Detail::RuntimeUiTextLayout>{};
            std::vector<Detail::RuntimeUiTextFace> fallbacks;
            fallbacks.reserve(faces.size() - 1U);
            for (std::size_t index = 1; index < faces.size(); ++index)
            {
                const auto face = faces[index] ? faces[index]->FaceHandle.TryGetLoaded() : nullptr;
                if (!face)
                    continue;
                fallbacks.push_back(
                    {.FontBytes = face->Bytes(),
                     .FontGeneration = faces[index]->FamilyRevision ^ std::rotl(faces[index]->FaceRevision, 1),
                     .CollectionIndex = faces[index]->CollectionIndex});
            }
            return RuntimeUiTextLayouts.Resolve(
                {.FontBytes = primary->Bytes(),
                 .FontGeneration = faces.front()->FamilyRevision ^ std::rotl(faces.front()->FaceRevision, 1),
                 .CollectionIndex = faces.front()->CollectionIndex,
                 .FallbackFaces = fallbacks,
                 .Text = command.Text,
                 .Language = command.Language,
                 .Direction = command.TextDirection,
                 .Wrap = command.TextWrap,
                 .Overflow = command.TextOverflow,
                 .FontSize = command.FontSize,
                 .AvailableWidth = command.Rect.Width,
                 .AuthoredLineHeight = command.LineHeight,
                 .LetterSpacing = command.LetterSpacing,
                 .WordSpacing = command.WordSpacing,
                 .MaximumLines = command.MaximumLines,
                 .Weight = command.FontWeight,
                 .Slant = command.FontSlant});
        };

        const auto captureFontCommands = [this, &pendingText, &resolveFontCache, &resolveTextLayout,
                                          &touchedCaches](const std::span<RuntimeUiDrawCommand> commands)
        {
            for (auto& command : commands)
            {
                if (command.Type != RuntimeUiDrawType::Text)
                    continue;
                command.PreparedFontBinding = {};
                command.PreparedTextGlyphs.clear();
                command.PreparedTextLines.clear();
                auto* primaryCache = resolveFontCache(command, command.Asset);
                if (!primaryCache)
                    continue;
                std::vector<AssetId> faceBindings{primaryCache->Binding};
                std::vector<AssetId> visitedFamilies{command.Asset};
                std::vector<AssetId> pendingFamilies;
                if (const auto family = primaryCache->FamilyHandle.TryGetLoaded())
                    pendingFamilies = family->Definition().FallbackFamilies;
                for (std::size_t index = 0; index < pendingFamilies.size() && visitedFamilies.size() < 16U; ++index)
                {
                    const auto familyId = pendingFamilies[index];
                    if (!familyId || std::ranges::find(visitedFamilies, familyId) != visitedFamilies.end())
                        continue;
                    visitedFamilies.push_back(familyId);
                    auto* fallbackCache = resolveFontCache(command, familyId);
                    if (!fallbackCache)
                        continue;
                    faceBindings.push_back(fallbackCache->Binding);
                    if (const auto family = fallbackCache->FamilyHandle.TryGetLoaded())
                        for (const auto fallback : family->Definition().FallbackFamilies)
                            if (pendingFamilies.size() < 16U &&
                                std::ranges::find(visitedFamilies, fallback) == visitedFamilies.end())
                            {
                                pendingFamilies.push_back(fallback);
                            }
                }
                std::vector<RuntimeUiFontCpuCacheEntry*> faces;
                faces.reserve(faceBindings.size());
                for (const auto binding : faceBindings)
                {
                    const auto cache =
                        std::ranges::find(RuntimeUiFontCpuCache, binding, &RuntimeUiFontCpuCacheEntry::Binding);
                    if (cache != RuntimeUiFontCpuCache.end() && cache->FaceHandle.TryGetLoaded())
                        faces.push_back(&*cache);
                }
                faceBindings.clear();
                for (const auto* face : faces)
                    faceBindings.push_back(face->Binding);
                const auto layout = resolveTextLayout(faces, command);
                if (!layout)
                    continue;
                for (const auto& glyph : layout->Glyphs)
                {
                    if (glyph.Codepoint == U'\n' || glyph.FaceIndex >= faceBindings.size())
                        continue;
                    const auto binding = faceBindings[glyph.FaceIndex];
                    const auto cache =
                        std::ranges::find(RuntimeUiFontCpuCache, binding, &RuntimeUiFontCpuCacheEntry::Binding);
                    if (cache == RuntimeUiFontCpuCache.end())
                        throw std::logic_error("A runtime UI fallback face expired during frame capture.");
                    cache->Glyphs.push_back(glyph.Glyph);
                    if (std::ranges::find(touchedCaches, binding) == touchedCaches.end())
                        touchedCaches.push_back(binding);
                }
                pendingText.push_back({&command, std::move(faceBindings), layout});
            }
        };
        captureFontCommands(frame.RuntimeUiCommands);
        for (auto& panel : frame.RuntimeUiCameraPanels)
            captureFontCommands(panel.Commands);
        for (auto& panel : frame.RuntimeUiWorldPanels)
            captureFontCommands(panel.Commands);
        for (auto& target : frame.RuntimeUiRenderTextures)
            captureFontCommands(target.Commands);

        for (const auto binding : touchedCaches)
        {
            auto cache = std::ranges::find(RuntimeUiFontCpuCache, binding, &RuntimeUiFontCpuCacheEntry::Binding);
            if (cache == RuntimeUiFontCpuCache.end())
                throw std::logic_error("A runtime UI font cache entry expired during owner-thread frame capture.");
            std::ranges::sort(cache->Glyphs);
            const auto duplicate = std::ranges::unique(cache->Glyphs);
            cache->Glyphs.erase(duplicate.begin(), duplicate.end());
            if (cache->AtlasPages.empty() || cache->AtlasGlyphRequestCount != cache->Glyphs.size())
            {
                const auto face = cache->FaceHandle.TryGetLoaded();
                if (!face)
                    continue;
                cache->AtlasPages = BuildRuntimeUiGlyphAtlasPages(face->Bytes(), cache->CollectionIndex, cache->Glyphs,
                                                                  CustomFontAtlasGeneration(*cache));
                cache->AtlasGlyphRequestCount = cache->Glyphs.size();
            }
        }
        const auto pageCount = [this]
        {
            std::size_t result = 0U;
            for (const auto& cache : RuntimeUiFontCpuCache)
                result += cache.AtlasPages.size();
            return result;
        };
        while (pageCount() > MaximumRuntimeUiFontAtlasPages)
        {
            RuntimeUiFontCpuCacheEntry* eviction = nullptr;
            for (auto& cache : RuntimeUiFontCpuCache)
            {
                if (cache.AtlasPages.empty() || std::ranges::find(touchedCaches, cache.Binding) != touchedCaches.end())
                {
                    continue;
                }
                if (!eviction || cache.LastUsedFrame < eviction->LastUsedFrame ||
                    (cache.LastUsedFrame == eviction->LastUsedFrame && cache.Binding < eviction->Binding))
                {
                    eviction = &cache;
                }
            }
            if (!eviction)
            {
                throw std::length_error("Runtime UI font working set exceeds the bounded 32-page atlas cache.");
            }
            eviction->AtlasPages.clear();
            eviction->AtlasGlyphRequestCount = 0U;
        }
        for (const auto& pending : pendingText)
        {
            std::vector<RuntimeUiFontCpuCacheEntry*> faces;
            faces.reserve(pending.FaceBindings.size());
            for (const auto binding : pending.FaceBindings)
            {
                const auto cache =
                    std::ranges::find(RuntimeUiFontCpuCache, binding, &RuntimeUiFontCpuCacheEntry::Binding);
                if (cache == RuntimeUiFontCpuCache.end())
                    break;
                faces.push_back(&*cache);
            }
            if (faces.size() != pending.FaceBindings.size())
                continue;
            PopulatePreparedText(*pending.Command, *pending.Layout, faces);
            for (const auto* face : faces)
            {
                for (const auto& page : face->AtlasPages)
                {
                    const auto binding = CustomFontPageBinding(face->Binding, page->PageIndex);
                    if (!FindRuntimeUiFontLease(frame, binding))
                        frame.RuntimeUiFontLeases.push_back({binding, page, face->FamilyHandle, face->FaceHandle});
                }
            }
        }

        const auto captureFallbackFonts = [&frame](const std::span<const RuntimeUiDrawCommand> commands)
        {
            if (std::ranges::none_of(
                    commands, [](const auto& command)
                    { return command.Type == RuntimeUiDrawType::Text && !command.PreparedFontBinding; }))
            {
                return;
            }
            if (!FindRuntimeUiFontLease(frame, RuntimeUiFallbackFontId))
                frame.RuntimeUiFontLeases.push_back({RuntimeUiFallbackFontId, RuntimeUiFallbackGlyphAtlas()});
        };
        captureFallbackFonts(frame.RuntimeUiCommands);
        for (const auto& panel : frame.RuntimeUiCameraPanels)
            captureFallbackFonts(panel.Commands);
        for (const auto& panel : frame.RuntimeUiWorldPanels)
            captureFallbackFonts(panel.Commands);
        for (const auto& target : frame.RuntimeUiRenderTextures)
            captureFallbackFonts(target.Commands);
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
