#include "KeireInternal/Rendering/MaterialBlendingInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"
#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::uint32_t DynamicUploadCapacity(const std::size_t required)
    {
        if (required == 0 || required > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Dynamic GPU upload exceeds SDL's 32-bit buffer limit.");
        auto capacity = std::uint32_t{256};
        while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U)
            capacity *= 2U;
        return capacity < required ? static_cast<std::uint32_t>(required) : capacity;
    }

    [[nodiscard]] SDL_GPUTextureFormat ToSdlTextureFormat(const Keire::RenderBackend::FrameGraphTextureFormat format)
    {
        using Keire::RenderBackend::FrameGraphTextureFormat;
        switch (format)
        {
        case FrameGraphTextureFormat::Rgba8Unorm:
            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case FrameGraphTextureFormat::Rgba8Srgb:
            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        case FrameGraphTextureFormat::Rgba16Float:
            return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case FrameGraphTextureFormat::Rgba32Float:
            return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        case FrameGraphTextureFormat::Rgba32Uint:
            return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
        case FrameGraphTextureFormat::Rg16Float:
            return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
        case FrameGraphTextureFormat::R32Float:
            return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        case FrameGraphTextureFormat::D32Float:
            return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case FrameGraphTextureFormat::Undefined:
            break;
        }
        throw std::logic_error("A production frame-graph texture requires an exact GPU format.");
    }

    [[nodiscard]] SDL_GPUTextureUsageFlags
    ToSdlTextureUsage(const Keire::RenderBackend::FrameGraphResourceUsage usage) noexcept
    {
        using Keire::RenderBackend::FrameGraphResourceUsage;
        using Keire::RenderBackend::HasFrameGraphResourceUsage;
        SDL_GPUTextureUsageFlags result = 0;
        if (HasFrameGraphResourceUsage(usage, FrameGraphResourceUsage::Sampled))
            result |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (HasFrameGraphResourceUsage(usage, FrameGraphResourceUsage::ColorAttachment))
            result |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        if (HasFrameGraphResourceUsage(usage, FrameGraphResourceUsage::DepthStencilAttachment))
            result |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        if (HasFrameGraphResourceUsage(usage, FrameGraphResourceUsage::Storage))
        {
            result |= SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
                      SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        }
        if (HasFrameGraphResourceUsage(usage, FrameGraphResourceUsage::UnfilteredRead))
            result |= SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
        return result;
    }

    [[nodiscard]] SDL_GPUSampleCount FrameGraphSampleCount(const std::uint8_t sampleCount)
    {
        switch (sampleCount)
        {
        case 1U:
            return SDL_GPU_SAMPLECOUNT_1;
        case 2U:
            return SDL_GPU_SAMPLECOUNT_2;
        case 4U:
            return SDL_GPU_SAMPLECOUNT_4;
        case 8U:
            return SDL_GPU_SAMPLECOUNT_8;
        default:
            throw std::logic_error("A frame-graph texture contains an invalid sample count.");
        }
    }

    [[nodiscard]] std::uint32_t ScaledExtent(const std::uint32_t extent, const std::uint8_t numerator,
                                             const std::uint8_t denominator)
    {
        const auto scaled = (static_cast<std::uint64_t>(extent) * numerator + denominator - 1U) / denominator;
        if (scaled == 0U || scaled > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("A frame-graph relative texture extent is outside SDL's supported range.");
        return static_cast<std::uint32_t>(scaled);
    }

    void ResetGpuOcclusionSurfaceState(Keire::RenderBackend::RenderSurfaceState& surface) noexcept
    {
        surface.GpuOcclusionSubmissionEpoch =
            surface.GpuOcclusionSubmissionEpoch == std::numeric_limits<std::uint64_t>::max()
                ? 1U
                : surface.GpuOcclusionSubmissionEpoch + 1U;
        surface.GpuOcclusionDiagnostics = {};
        surface.GpuOcclusionDiagnostics.RequestedMode = surface.GpuOcclusionSubmittedMode;
        surface.GpuOcclusionAutomaticQualifyingFrames = 0;
        surface.GpuOcclusionAutomaticMinimumFrames = 0;
        surface.GpuOcclusionAutomaticCooldownFrames = 0;
        surface.GpuOcclusionAutomaticUnprofitableActivations = 0;
        surface.GpuOcclusionValidationCooldown = false;
        surface.GpuOcclusionValidationFallbackEventPending = false;
        surface.GpuOcclusionLatestCandidateTriangles = 0;
        surface.GpuOcclusionLatestVisibleTriangles = 0;
        surface.GpuOcclusionAutomaticActive = false;
        surface.GpuOcclusionDebugMipLevel = 0;
        Keire::RenderBackend::GpuOcclusionPolicy::ResetAllocationRetry(surface.GpuOcclusionAllocationRetry);
    }

    [[nodiscard]] Keire::Ref<Keire::Texture2DAsset> CreateDefaultSky()
    {
        constexpr std::uint32_t width = 256;
        constexpr std::uint32_t height = 128;
        constexpr float pi = 3.14159265358979323846F;
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
        // An identity Directional Light sends rays along +Z, so its visible source is at -Z. Custom skyboxes still
        // need an authored Directional Light because a background image cannot cast geometry shadows by itself.
        constexpr Keire::Vector3 sunDirection{0.0F, 0.0F, -1.0F};
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
            const float latitude = (0.5F - v) * pi;
            const float horizon = std::exp(-std::abs(latitude) * 3.6F);
            const float upper = std::clamp((0.5F - v) * 2.0F, 0.0F, 1.0F);
            const float lower = std::clamp((v - 0.5F) * 2.0F, 0.0F, 1.0F);
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
                const float longitude = (u - 0.5F) * 2.0F * pi;
                const float latitudeCosine = std::cos(latitude);
                const Keire::Vector3 direction{std::sin(longitude) * latitudeCosine, std::sin(latitude),
                                               std::cos(longitude) * latitudeCosine};
                const float sunDot = std::max(
                    direction.X * sunDirection.X + direction.Y * sunDirection.Y + direction.Z * sunDirection.Z, 0.0F);
                const float sun = std::pow(sunDot, 384.0F);
                const float glow = std::pow(sunDot, 20.0F) * 0.18F;

                Keire::Vector3 color{0.34F + (0.07F - 0.34F) * upper, 0.48F + (0.16F - 0.48F) * upper,
                                     0.68F + (0.34F - 0.68F) * upper};
                const float horizonBlend = horizon * 0.45F;
                color = {color.X + (0.62F - color.X) * horizonBlend, color.Y + (0.72F - color.Y) * horizonBlend,
                         color.Z + (0.82F - color.Z) * horizonBlend};
                const float lowerBlend = lower * 0.72F;
                color = {color.X + (0.10F - color.X) * lowerBlend, color.Y + (0.12F - color.Y) * lowerBlend,
                         color.Z + (0.16F - color.Z) * lowerBlend};
                color = {color.X + sun + glow, color.Y + (sun + glow) * 0.78F, color.Z + (sun + glow) * 0.48F};

                const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
                const auto channel = [](const float value)
                { return std::byte{static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F))}; };
                pixels[offset] = channel(color.X);
                pixels[offset + 1U] = channel(color.Y);
                pixels[offset + 2U] = channel(color.Z);
                pixels[offset + 3U] = std::byte{255};
            }
        }

        Keire::TextureImportSettings settings;
        settings.Semantic = Keire::TextureSemantic::Environment;
        settings.ColorSpace = Keire::TextureColorSpace::Linear;
        settings.Mips = Keire::TextureMipPolicy::Generate;
        settings.EnvironmentLayout = Keire::TextureEnvironmentLayout::Equirectangular;
        settings.Sampler.AddressU = Keire::TextureAddressMode::Repeat;
        settings.Sampler.AddressV = Keire::TextureAddressMode::Clamp;
        settings.Sampler.AddressW = Keire::TextureAddressMode::Clamp;
        std::vector<Keire::TextureMipLevel> mips{{width, height, std::move(pixels)}};
        while (mips.back().Width > 1U || mips.back().Height > 1U)
        {
            const auto& source = mips.back();
            Keire::TextureMipLevel mip;
            mip.Width = std::max(source.Width / 2U, 1U);
            mip.Height = std::max(source.Height / 2U, 1U);
            mip.Pixels.resize(static_cast<std::size_t>(mip.Width) * mip.Height * 4U);
            for (std::uint32_t y = 0; y < mip.Height; ++y)
                for (std::uint32_t x = 0; x < mip.Width; ++x)
                    for (std::uint32_t channelIndex = 0; channelIndex < 4U; ++channelIndex)
                    {
                        std::uint32_t total = 0;
                        for (std::uint32_t offsetY = 0; offsetY < 2U; ++offsetY)
                            for (std::uint32_t offsetX = 0; offsetX < 2U; ++offsetX)
                            {
                                const auto sourceX = std::min(x * 2U + offsetX, source.Width - 1U);
                                const auto sourceY = std::min(y * 2U + offsetY, source.Height - 1U);
                                const auto sourceIndex =
                                    (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U + channelIndex;
                                total += std::to_integer<std::uint8_t>(source.Pixels[sourceIndex]);
                            }
                        const auto targetIndex = (static_cast<std::size_t>(y) * mip.Width + x) * 4U + channelIndex;
                        mip.Pixels[targetIndex] = std::byte((total + 2U) / 4U);
                    }
            mips.push_back(std::move(mip));
        }
        return Keire::CreateRef<Keire::Texture2DAsset>(settings, std::move(mips));
    }
} // namespace

namespace Keire::RenderBackend
{
    SDL_GPUTexture* RenderSharedState::CaptureUiSurfaceTexture(const std::shared_ptr<RenderSurfaceState>& surface)
    {
        RequireOwner("CaptureUiSurfaceTexture");
        if (!FrameActive)
            throw std::logic_error("Render-surface UI images may only be captured during an active frame.");
        if (!surface)
            throw std::invalid_argument("Render-surface UI capture requires a valid surface epoch.");

        RenderSurfaceToken token;
        SDL_GPUTexture* texture = nullptr;
        {
            std::scoped_lock lock(SurfaceMutex);
            const auto current = std::ranges::find_if(Surfaces, [&surface](const RenderSurfaceRegistryEntry& entry)
                                                      { return entry.Current && entry.State == surface; });
            if (current == Surfaces.end() || !surface->Lifetime)
                throw std::invalid_argument("Render surface epoch is no longer current for UI capture.");

            auto presentation = surface;
            for (std::size_t remaining = Surfaces.size() + 1U; presentation && remaining != 0U; --remaining)
            {
                texture = presentation->PublishedTexture.load(std::memory_order_acquire);
                if (texture)
                    break;

                const auto fallbackLifetime =
                    presentation->PresentationFallbackLifetime.load(std::memory_order_acquire);
                if (!fallbackLifetime)
                {
                    // Publication and fallback release are serialized with SurfaceMutex. The second load also makes
                    // this robust if a future publication path changes that implementation detail.
                    texture = presentation->PublishedTexture.load(std::memory_order_acquire);
                    break;
                }
                const auto fallback = std::ranges::find_if(
                    Surfaces, [&fallbackLifetime](const RenderSurfaceRegistryEntry& entry)
                    { return fallbackLifetime && entry.State && entry.State->Lifetime == fallbackLifetime; });
                if (fallback == Surfaces.end() || fallback->State == presentation)
                    break;
                presentation = fallback->State;
            }
            if (!texture || !presentation->Lifetime)
                return nullptr;
            token = {presentation->Id, presentation->Epoch, presentation->Lifetime};
        }

        const auto textureIdentity = reinterpret_cast<std::uintptr_t>(texture);
        const auto existing =
            std::ranges::find_if(PendingUiSurfaceTextureBindings,
                                 [&token, textureIdentity](const CapturedSurfaceTextureBinding& binding)
                                 {
                                     return binding.Surface.Id == token.Id && binding.Surface.Epoch == token.Epoch &&
                                            binding.TextureIdentity == textureIdentity;
                                 });
        if (existing == PendingUiSurfaceTextureBindings.end())
            PendingUiSurfaceTextureBindings.push_back({token, textureIdentity});
        return texture;
    }

    void RenderSharedState::CreateGeometryResources(const std::uint32_t resourceGeneration)
    {
        ShadowPipeline = CreateDepthPipeline(true);
        SceneDepthPipeline = CreateDepthPipeline(false);
        ToneMapPipeline = CreateToneMapPipeline();
        (void)EnsureDeferredPipelines();
        Msaa2Capability.store(ResolveSamples(RenderSampleCount::Two) == SDL_GPU_SAMPLECOUNT_2,
                              std::memory_order_release);
        Msaa4Capability.store(ResolveSamples(RenderSampleCount::Four) == SDL_GPU_SAMPLECOUNT_4,
                              std::memory_order_release);
        SDL_GPUSamplerCreateInfo shadowSampler{};
        shadowSampler.min_filter = SDL_GPU_FILTER_NEAREST;
        shadowSampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        shadowSampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        shadowSampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        shadowSampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        shadowSampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ShadowSampler = SDL_CreateGPUSampler(Device, &shadowSampler);
        if (!ShadowSampler)
            throw std::runtime_error("SDL_CreateGPUSampler(shadow) failed: " + LastSdlError());
        SDL_GPUSamplerCreateInfo toneMapSampler{};
        toneMapSampler.min_filter = SDL_GPU_FILTER_LINEAR;
        toneMapSampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        toneMapSampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        toneMapSampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        toneMapSampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        toneMapSampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ToneMapSampler = SDL_CreateGPUSampler(Device, &toneMapSampler);
        if (!ToneMapSampler)
            throw std::runtime_error("SDL_CreateGPUSampler(tone map) failed: " + LastSdlError());
        SDL_GPUTextureCreateInfo emptyShadow{};
        emptyShadow.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        emptyShadow.format = ShadowDepthFormat;
        emptyShadow.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        emptyShadow.width = 1;
        emptyShadow.height = 1;
        emptyShadow.layer_count_or_depth = 1;
        emptyShadow.num_levels = 1;
        emptyShadow.sample_count = SDL_GPU_SAMPLECOUNT_1;
        EmptyShadowTexture = SDL_CreateGPUTexture(Device, &emptyShadow);
        if (!EmptyShadowTexture)
            throw std::runtime_error("SDL_CreateGPUTexture(empty shadow) failed: " + LastSdlError());
        DefaultMesh = CreateMeshResources(*MeshAsset::Cube());
        ErrorMesh = CreateMeshResources(*MeshAsset::Error());
        CheckerboardTexture = CreateTextureResources(*Texture2DAsset::Checkerboard());
        DefaultSkyTexture = CreateTextureResources(*CreateDefaultSky());
        BrdfIntegrationLut = CreateTextureResources(*CreateBrdfIntegrationLut(128U, 256U));
        const auto solidTexture =
            [](const std::array<std::byte, 4> pixel, const TextureSemantic semantic, const TextureColorSpace colorSpace)
        {
            TextureImportSettings settings;
            settings.Semantic = semantic;
            settings.ColorSpace = colorSpace;
            settings.Mips = TextureMipPolicy::None;
            std::vector<std::byte> pixels(pixel.begin(), pixel.end());
            return CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{{1, 1, std::move(pixels)}});
        };
        WhiteTexture =
            CreateTextureResources(*solidTexture({std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
                                                 TextureSemantic::Color, TextureColorSpace::Srgb));
        FlatNormalTexture =
            CreateTextureResources(*solidTexture({std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}},
                                                 TextureSemantic::Normal, TextureColorSpace::Linear));
        NeutralOrmTexture =
            CreateTextureResources(*solidTexture({std::byte{255}, std::byte{255}, std::byte{0}, std::byte{255}},
                                                 TextureSemantic::Data, TextureColorSpace::Linear));
        BlackTexture = CreateTextureResources(*solidTexture({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}},
                                                            TextureSemantic::Color, TextureColorSpace::Srgb));
        BlackDataTexture =
            CreateTextureResources(*solidTexture({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}},
                                                 TextureSemantic::Data, TextureColorSpace::Linear));
        WhiteDataTexture =
            CreateTextureResources(*solidTexture({std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
                                                 TextureSemantic::Data, TextureColorSpace::Linear));
        const auto lightingTexture = [](const LightingTextureTarget target, const std::array<std::byte, 4> pixel)
        {
            LightingTextureArrayDefinition definition;
            definition.Target = target;
            definition.Encoding = LightingTextureEncoding::Rgba8;
            LightingTextureMip mip;
            mip.Width = 1;
            mip.Height = 1;
            mip.Layers = target == LightingTextureTarget::CubeArray ? 6U : 1U;
            mip.Pixels.reserve(static_cast<std::size_t>(mip.Layers) * pixel.size());
            for (std::uint32_t layer = 0; layer < mip.Layers; ++layer)
                mip.Pixels.insert(mip.Pixels.end(), pixel.begin(), pixel.end());
            definition.Mips.push_back(std::move(mip));
            return CreateRef<LightingTextureArrayAsset>(std::move(definition));
        };
        DefaultLightingArray = CreateLightingTextureResources(*lightingTexture(
            LightingTextureTarget::Texture2DArray, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
        DefaultLightingMaskArray = CreateLightingTextureResources(*lightingTexture(
            LightingTextureTarget::Texture2DArray, {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}));
        DefaultReflectionCubeArray = CreateLightingTextureResources(*lightingTexture(
            LightingTextureTarget::CubeArray, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
        const AssetSpatialSelectionRecord spatialFallback{};
        SpatialSelectionFallbackBuffer = UploadBuffer(std::as_bytes(std::span(std::addressof(spatialFallback), 1U)),
                                                      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        SpatialSelectionFallbackDeviceGeneration = resourceGeneration;
    }

    void RenderSharedState::ReleaseMeshResources(GpuMeshResources& resources) noexcept
    {
        if (Device && resources.Indices)
            SDL_ReleaseGPUBuffer(Device, resources.Indices);
        if (Device && resources.Vertices)
            SDL_ReleaseGPUBuffer(Device, resources.Vertices);
        if (Device && resources.AssetVertices)
            SDL_ReleaseGPUBuffer(Device, resources.AssetVertices);
        resources = {};
    }

    void RenderSharedState::ReleaseGpuSkinResources(GpuSkinResources& resources) noexcept
    {
        if (Device)
        {
            for (auto& [key, instance] : resources.Instances)
            {
                (void)key;
                for (auto& output : instance.Outputs)
                {
                    const auto releasePalette = [this](GpuSkinPaletteResources& palette)
                    {
                        if (palette.Transfer)
                            SDL_ReleaseGPUTransferBuffer(Device, palette.Transfer);
                        if (palette.Buffer)
                            SDL_ReleaseGPUBuffer(Device, palette.Buffer);
                        palette = {};
                    };
                    releasePalette(output.PreviousPalette);
                    releasePalette(output.Palette);
                    if (output.PreviousBuiltinVertices)
                        SDL_ReleaseGPUBuffer(Device, output.PreviousBuiltinVertices);
                    if (output.PreviousAssetVertices)
                        SDL_ReleaseGPUBuffer(Device, output.PreviousAssetVertices);
                    if (output.BuiltinVertices)
                        SDL_ReleaseGPUBuffer(Device, output.BuiltinVertices);
                    if (output.AssetVertices)
                        SDL_ReleaseGPUBuffer(Device, output.AssetVertices);
                }
            }
            if (resources.Influences)
                SDL_ReleaseGPUBuffer(Device, resources.Influences);
        }
        resources = {};
    }

    void RenderSharedState::ReleaseTextureResources(GpuTextureResources& resources) noexcept
    {
        if (Device && resources.Texture)
            SDL_ReleaseGPUTexture(Device, resources.Texture);
        resources = {};
    }

    void RenderSharedState::ReleaseForwardPlusResources(ForwardPlusGpuResources& resources) noexcept
    {
        if (Device && resources.LightIndices)
            SDL_ReleaseGPUBuffer(Device, resources.LightIndices);
        if (Device && resources.Tiles)
            SDL_ReleaseGPUBuffer(Device, resources.Tiles);
        if (Device && resources.Lights)
            SDL_ReleaseGPUBuffer(Device, resources.Lights);
        resources = {};
    }

    void RenderSharedState::ReleaseDynamicUploadResources(SurfaceDynamicUploadResources& resources) noexcept
    {
        if (Device)
        {
            for (auto& batch : resources.InstanceBatches)
                if (batch.Buffer)
                    SDL_ReleaseGPUBuffer(Device, batch.Buffer);
            if (resources.InstanceTransfer)
                SDL_ReleaseGPUTransferBuffer(Device, resources.InstanceTransfer);
            if (resources.CpuVfxVertices.Buffer)
                SDL_ReleaseGPUBuffer(Device, resources.CpuVfxVertices.Buffer);
            if (resources.CpuVfxTransfer)
                SDL_ReleaseGPUTransferBuffer(Device, resources.CpuVfxTransfer);
        }
        resources = {};
    }

    void RenderSharedState::ReleaseSpatialSelectionFrameResources(GpuSpatialSelectionFrameResources& resources) noexcept
    {
        if (Device)
        {
            if (resources.OutputRecords.Buffer)
                SDL_ReleaseGPUBuffer(Device, resources.OutputRecords.Buffer);
            if (resources.LightProbeCandidates.Buffer)
                SDL_ReleaseGPUBuffer(Device, resources.LightProbeCandidates.Buffer);
            if (resources.ReflectionCandidates.Buffer)
                SDL_ReleaseGPUBuffer(Device, resources.ReflectionCandidates.Buffer);
            if (resources.Draws.Buffer)
                SDL_ReleaseGPUBuffer(Device, resources.Draws.Buffer);
            if (resources.Upload)
                SDL_ReleaseGPUTransferBuffer(Device, resources.Upload);
        }
        resources = {};
    }

    void RenderSharedState::ReleaseGpuVfxFrameResources(GpuVfxFrameResources& resources) noexcept
    {
        if (Device)
        {
            for (auto& output : resources.Outputs)
            {
                if (output.Instances)
                    SDL_ReleaseGPUBuffer(Device, output.Instances);
                if (output.IndirectArguments)
                    SDL_ReleaseGPUBuffer(Device, output.IndirectArguments);
                if (output.Indices)
                    SDL_ReleaseGPUBuffer(Device, output.Indices);
            }
        }
        resources = {};
    }

    void RenderSharedState::ReleaseGpuOcclusionFrameResources(GpuOcclusionFrameResources& resources) noexcept
    {
        if (Device)
        {
            const auto releaseBuffer = [this](GpuOcclusionBuffer& buffer)
            {
                if (buffer.Buffer)
                    SDL_ReleaseGPUBuffer(Device, buffer.Buffer);
            };
            releaseBuffer(resources.Status);
            releaseBuffer(resources.IndirectArguments);
            releaseBuffer(resources.VisibleInstances);
            releaseBuffer(resources.ChunkOffsets);
            releaseBuffer(resources.Batches);
            releaseBuffer(resources.ChunkCounts);
            releaseBuffer(resources.Chunks);
            releaseBuffer(resources.LocalOffsets);
            releaseBuffer(resources.SpatialVolumeVisibilityMask);
            releaseBuffer(resources.LocalLightVisibilityMask);
            releaseBuffer(resources.VfxVisibilityMask);
            releaseBuffer(resources.GeometryVisibility);
            releaseBuffer(resources.InputInstances);
            releaseBuffer(resources.Candidates);
            if (resources.Readback)
                SDL_ReleaseGPUTransferBuffer(Device, resources.Readback);
            if (resources.Upload)
                SDL_ReleaseGPUTransferBuffer(Device, resources.Upload);
            for (auto* texture : resources.Pyramid)
                if (texture)
                    SDL_ReleaseGPUTexture(Device, texture);
            if (resources.Depth)
                SDL_ReleaseGPUTexture(Device, resources.Depth);
        }
        resources = {};
    }

    SDL_GPUBuffer* RenderSharedState::UploadDynamicBuffer(SDL_GPUCommandBuffer* commands, DynamicGpuBuffer& buffer,
                                                          SDL_GPUTransferBuffer*& transfer,
                                                          const std::span<const std::byte> bytes,
                                                          const SDL_GPUBufferUsageFlags usage, const char* diagnostic)
    {
        if (!commands)
            throw std::invalid_argument("A dynamic GPU upload requires an active command buffer.");
        const auto required = DynamicUploadCapacity(bytes.size());
        if (!buffer.Buffer || !transfer || buffer.CapacityBytes < required)
        {
            SDL_GPUBufferCreateInfo bufferInformation{};
            bufferInformation.usage = usage;
            bufferInformation.size = required;
            auto* replacementBuffer = SDL_CreateGPUBuffer(Device, &bufferInformation);
            if (!replacementBuffer)
                throw std::runtime_error(std::string("SDL_CreateGPUBuffer(") + diagnostic +
                                         ") failed: " + LastSdlError());
            SDL_GPUTransferBuffer* replacementTransfer = nullptr;
            try
            {
                SDL_GPUTransferBufferCreateInfo transferInformation{};
                transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                transferInformation.size = required;
                replacementTransfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
                if (!replacementTransfer)
                {
                    throw std::runtime_error(std::string("SDL_CreateGPUTransferBuffer(") + diagnostic +
                                             ") failed: " + LastSdlError());
                }
            }
            catch (...)
            {
                RethrowIfDeviceLost("dynamic GPU upload-buffer allocation");
                SDL_ReleaseGPUBuffer(Device, replacementBuffer);
                throw;
            }
            Retire(std::exchange(buffer.Buffer, replacementBuffer));
            Retire(std::exchange(transfer, replacementTransfer));
            buffer.CapacityBytes = required;
            ++Statistics.DynamicUploadBufferReallocations;
        }

        auto* mapped = SDL_MapGPUTransferBuffer(Device, transfer, true);
        if (!mapped)
            throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer(") + diagnostic +
                                     ") failed: " + LastSdlError());
        std::memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(Device, transfer);

        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            throw std::runtime_error(std::string("SDL_BeginGPUCopyPass(") + diagnostic + ") failed: " + LastSdlError());
        const SDL_GPUTransferBufferLocation source{transfer, 0};
        const SDL_GPUBufferRegion destination{buffer.Buffer, 0, static_cast<std::uint32_t>(bytes.size())};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
        SDL_EndGPUCopyPass(copy);
        Statistics.DynamicUploadBytes += bytes.size();
        return buffer.Buffer;
    }

    SDL_GPUBuffer* RenderSharedState::UploadRuntimeUiBuffer(SDL_GPUCommandBuffer* commands,
                                                            const std::span<const std::byte> bytes,
                                                            const char* diagnostic)
    {
        if (!ActiveFrame || ActiveFrame->FrameSlot >= Specification.MaximumFramesInFlight)
            throw std::logic_error("Runtime UI uploads require an active frame slot.");

        const auto frameSlot = static_cast<std::size_t>(ActiveFrame->FrameSlot);
        if (RuntimeUiUploadBuffers.size() != Specification.MaximumFramesInFlight)
        {
            RuntimeUiUploadBuffers.resize(Specification.MaximumFramesInFlight);
            RuntimeUiUploadTransfers.resize(Specification.MaximumFramesInFlight);
            RuntimeUiUploadCounts.resize(Specification.MaximumFramesInFlight);
        }
        auto& buffers = RuntimeUiUploadBuffers[frameSlot];
        auto& transfers = RuntimeUiUploadTransfers[frameSlot];
        auto& used = RuntimeUiUploadCounts[frameSlot];
        if (used == buffers.size())
        {
            buffers.emplace_back();
            transfers.push_back(nullptr);
        }
        if (transfers.size() != buffers.size())
            throw std::logic_error("Runtime UI upload pool ownership is inconsistent.");

        auto& buffer = buffers[used];
        auto*& transfer = transfers[used];
        const auto previousCapacity = buffer.CapacityBytes;
        ++used;
        auto* result = UploadDynamicBuffer(commands, buffer, transfer, bytes, SDL_GPU_BUFFERUSAGE_VERTEX, diagnostic);
        Statistics.RuntimeUiRenderer.UploadBufferPoolSize = 0;
        for (const auto& slot : RuntimeUiUploadBuffers)
            Statistics.RuntimeUiRenderer.UploadBufferPoolSize += slot.size();
        if (buffer.CapacityBytes != previousCapacity)
            ++Statistics.RuntimeUiRenderer.UploadBufferReallocations;
        return result;
    }

    void RenderSharedState::UploadSceneInstances(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                 PreparedSceneDrawLists& draws,
                                                 const std::span<const GpuInstanceUniform> instances)
    {
        if (instances.empty())
            return;
        if (!commands)
            throw std::invalid_argument("Scene instance uploads require an active command buffer.");
        const auto bytes = std::as_bytes(instances);
        const auto requiredTransferCapacity = DynamicUploadCapacity(bytes.size());
        auto& resources = surface.ActiveWorkset().DynamicUploads;
        if (!resources.InstanceTransfer || resources.InstanceTransferCapacityBytes < requiredTransferCapacity)
        {
            SDL_GPUTransferBufferCreateInfo information{};
            information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            information.size = requiredTransferCapacity;
            auto* replacement = SDL_CreateGPUTransferBuffer(Device, &information);
            if (!replacement)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(scene instances) failed: " + LastSdlError());
            Retire(std::exchange(resources.InstanceTransfer, replacement));
            resources.InstanceTransferCapacityBytes = requiredTransferCapacity;
            ++Statistics.DynamicUploadBufferReallocations;
        }

        std::vector<PreparedSceneBatch*> uploadBatches;
        uploadBatches.reserve(draws.Opaque.Batches.size() + draws.Transparent.Batches.size() +
                              draws.Decals.Batches.size());
        const auto collect = [&](PreparedSceneDrawList& list)
        {
            for (auto& batch : list.Batches)
                if (batch.InstanceDataCount != 0)
                    uploadBatches.push_back(std::addressof(batch));
        };
        collect(draws.Opaque);
        collect(draws.Transparent);
        collect(draws.Decals);
        resources.InstanceBatches.resize(std::max(resources.InstanceBatches.size(), uploadBatches.size()));

        for (std::size_t index = 0; index < uploadBatches.size(); ++index)
        {
            auto& allocation = resources.InstanceBatches[index];
            const auto requiredBytes =
                static_cast<std::size_t>(uploadBatches[index]->InstanceDataCount) * sizeof(GpuInstanceUniform);
            const auto requiredCapacity = DynamicUploadCapacity(requiredBytes);
            if (!allocation.Buffer || allocation.CapacityBytes < requiredCapacity)
            {
                SDL_GPUBufferCreateInfo information{};
                information.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                information.size = requiredCapacity;
                auto* replacement = SDL_CreateGPUBuffer(Device, &information);
                if (!replacement)
                    throw std::runtime_error("SDL_CreateGPUBuffer(scene instance batch) failed: " + LastSdlError());
                Retire(std::exchange(allocation.Buffer, replacement));
                allocation.CapacityBytes = requiredCapacity;
                ++Statistics.DynamicUploadBufferReallocations;
            }
            uploadBatches[index]->InstanceBuffer = allocation.Buffer;
        }

        auto* mapped = SDL_MapGPUTransferBuffer(Device, resources.InstanceTransfer, true);
        if (!mapped)
            throw std::runtime_error("SDL_MapGPUTransferBuffer(scene instances) failed: " + LastSdlError());
        std::memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(Device, resources.InstanceTransfer);

        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            throw std::runtime_error("SDL_BeginGPUCopyPass(scene instances) failed: " + LastSdlError());
        for (const auto* batch : uploadBatches)
        {
            const auto offset = static_cast<std::uint32_t>(static_cast<std::size_t>(batch->InstanceDataFirst) *
                                                           sizeof(GpuInstanceUniform));
            const auto byteSize = static_cast<std::uint32_t>(static_cast<std::size_t>(batch->InstanceDataCount) *
                                                             sizeof(GpuInstanceUniform));
            const SDL_GPUTransferBufferLocation source{resources.InstanceTransfer, offset};
            const SDL_GPUBufferRegion destination{batch->InstanceBuffer, 0, byteSize};
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
        }
        SDL_EndGPUCopyPass(copy);
        Statistics.DynamicUploadBytes += bytes.size();
    }

    void RenderSharedState::Retire(SDL_GPUBuffer* buffer) noexcept
    {
        if (!buffer)
            return;
        if (Open && Device && FrameActive)
            FrameTransientBuffers.push_back(buffer);
        else if (Open && Device && !InFlight.empty())
            InFlight.back().TransientBuffers.push_back(buffer);
        else if (Device)
            SDL_ReleaseGPUBuffer(Device, buffer);
    }

    void RenderSharedState::Retire(SDL_GPUTransferBuffer* transfer) noexcept
    {
        if (!transfer)
            return;
        if (Open && Device && FrameActive)
            FrameUploadTransfers.push_back(transfer);
        else if (Open && Device && !InFlight.empty())
            InFlight.back().TransientTransferBuffers.push_back(transfer);
        else if (Device)
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
    }

    void RenderSharedState::Retire(ForwardPlusGpuResources resources) noexcept
    {
        if (resources.Empty())
            return;
        if (Open && Device && FrameActive)
            PendingRetiredForwardPlus.push_back(resources);
        else if (Open && Device && !InFlight.empty())
            InFlight.back().RetiredForwardPlus.push_back(resources);
        else
            ReleaseForwardPlusResources(resources);
    }

    void RenderSharedState::Retire(GpuTextureResources resources) noexcept
    {
        if (resources.Empty())
            return;
        if (Open && Device && FrameActive)
        {
            PendingRetiredBytes += resources.EstimatedBytes;
            Statistics.FenceRetiredBytes += resources.EstimatedBytes;
            if (Streaming)
                Streaming->ReportRetired(StreamingClass::Texture, 0, resources.EstimatedBytes);
            PendingRetiredTextures.push_back(resources);
        }
        else if (Open && Device && !InFlight.empty())
        {
            InFlight.back().RetiredBytes += resources.EstimatedBytes;
            Statistics.FenceRetiredBytes += resources.EstimatedBytes;
            if (Streaming)
                Streaming->ReportRetired(StreamingClass::Texture, 0, resources.EstimatedBytes);
            InFlight.back().RetiredTextures.push_back(resources);
        }
        else
            ReleaseTextureResources(resources);
    }

    void RenderSharedState::Retire(SDL_GPUGraphicsPipeline* pipeline) noexcept
    {
        if (!pipeline)
            return;
        if (Open && Device && FrameActive)
            PendingRetiredPipelines.push_back(pipeline);
        else if (Open && Device && !InFlight.empty())
            InFlight.back().RetiredPipelines.push_back(pipeline);
        else
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
    }

    void RenderSharedState::Retire(GpuMeshResources resources) noexcept
    {
        if (resources.Empty())
            return;
        if (!Open || !Device)
        {
            ReleaseMeshResources(resources);
            return;
        }
        if (FrameActive)
        {
            PendingRetiredBytes += resources.EstimatedBytes;
            Statistics.FenceRetiredBytes += resources.EstimatedBytes;
            if (Streaming)
                Streaming->ReportRetired(StreamingClass::Mesh, 0, resources.EstimatedBytes);
            PendingRetiredMeshes.push_back(resources);
        }
        else if (!InFlight.empty())
        {
            InFlight.back().RetiredBytes += resources.EstimatedBytes;
            Statistics.FenceRetiredBytes += resources.EstimatedBytes;
            if (Streaming)
                Streaming->ReportRetired(StreamingClass::Mesh, 0, resources.EstimatedBytes);
            InFlight.back().RetiredMeshes.push_back(resources);
        }
        else
            ReleaseMeshResources(resources);
    }

    void RenderSharedState::Retire(GpuSkinResources resources) noexcept
    {
        if (resources.Empty())
            return;
        if (!Open || !Device)
        {
            ReleaseGpuSkinResources(resources);
            return;
        }
        if (FrameActive || FrameExecutionActive)
            PendingRetiredSkins.push_back(std::move(resources));
        else if (!InFlight.empty())
            InFlight.back().RetiredSkins.push_back(std::move(resources));
        else
            ReleaseGpuSkinResources(resources);
    }

    void RenderSharedState::Retire(SurfaceResources resources) noexcept
    {
        if (resources.Empty())
            return;
        if (!Open || !Device)
        {
            ReleaseResources(resources);
            return;
        }
        if (FrameActive)
            PendingRetired.push_back(resources);
        else if (!InFlight.empty())
            InFlight.back().Retired.push_back(resources);
        else
            ReleaseResources(resources);
    }

    void RenderSharedState::RetireSurface(RenderSurfaceState& surface) noexcept
    {
        surface.ResourcesAvailable.store(false, std::memory_order_release);
        surface.PublishedTexture.store(nullptr, std::memory_order_release);
        surface.PresentationFallbackLifetime.store({}, std::memory_order_release);
        Retire(std::exchange(surface.Resources, {}));
        surface.Owner.reset();
        surface.Width = 0;
        surface.Height = 0;
        surface.TemporalHistoryValid = false;
        surface.TemporalHistoryPath = RenderPath::Automatic;
        surface.IrradynHistoryValid = false;
        surface.IrradynRecordedThisFrame = false;
        surface.IrradynCache.Clear();
        surface.PublishSurfacePropertiesSnapshot();
    }

    SDL_GPUSampleCount RenderSharedState::ResolveSamples(const RenderSampleCount requested) const noexcept
    {
        const auto maximum = static_cast<std::uint8_t>(requested);
        constexpr std::pair<std::uint8_t, SDL_GPUSampleCount> candidates[] = {{std::uint8_t{8}, SDL_GPU_SAMPLECOUNT_8},
                                                                              {std::uint8_t{4}, SDL_GPU_SAMPLECOUNT_4},
                                                                              {std::uint8_t{2}, SDL_GPU_SAMPLECOUNT_2},
                                                                              {std::uint8_t{1}, SDL_GPU_SAMPLECOUNT_1}};
        for (const auto& [value, candidate] : candidates)
        {
            if (value > maximum)
                continue;
            if (SDL_GPUTextureSupportsSampleCount(Device, SceneColorFormat, candidate) &&
                (!DepthFormat || SDL_GPUTextureSupportsSampleCount(Device, DepthFormat, candidate)))
                return candidate;
        }
        return SDL_GPU_SAMPLECOUNT_1;
    }

    SurfaceResources RenderSharedState::CreateResources(const RenderSurfaceState& surface,
                                                        const SDL_GPUSampleCount samples)
    {
        SurfaceResources result;
        try
        {
            SDL_GPUTextureCreateInfo sampled{};
            sampled.type = SDL_GPU_TEXTURETYPE_2D;
            sampled.format = ColorFormat;
            sampled.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            sampled.width = surface.RequestedWidth;
            sampled.height = surface.RequestedHeight;
            sampled.layer_count_or_depth = 1;
            sampled.num_levels = 1;
            sampled.sample_count = SDL_GPU_SAMPLECOUNT_1;
            result.FinalOutputs.resize(static_cast<std::size_t>(Specification.MaximumFramesInFlight) + 1U);
            for (auto*& output : result.FinalOutputs)
            {
                output = SDL_CreateGPUTexture(Device, &sampled);
                if (!output)
                    throw std::runtime_error("SDL_CreateGPUTexture(final output) failed: " + LastSdlError());
            }
            result.TemporalHistories.resize(static_cast<std::size_t>(Specification.MaximumFramesInFlight) + 1U);
            for (auto*& history : result.TemporalHistories)
            {
                history = SDL_CreateGPUTexture(Device, &sampled);
                if (!history)
                    throw std::runtime_error("SDL_CreateGPUTexture(temporal history) failed: " + LastSdlError());
            }
            result.Worksets.resize(Specification.MaximumFramesInFlight);
            const bool deferredResources = DeferredCapability.load(std::memory_order_acquire);
            if (deferredResources)
            {
                auto irradynHistory = sampled;
                irradynHistory.format = SceneColorFormat;
                irradynHistory.width = ScaledExtent(surface.RequestedWidth, 1U, 2U);
                irradynHistory.height = ScaledExtent(surface.RequestedHeight, 1U, 2U);
                result.IrradynHistories.resize(static_cast<std::size_t>(Specification.MaximumFramesInFlight) + 1U);
                for (auto*& history : result.IrradynHistories)
                {
                    history = SDL_CreateGPUTexture(Device, &irradynHistory);
                    if (!history)
                        throw std::runtime_error("SDL_CreateGPUTexture(Irradyn history) failed: " + LastSdlError());
                }
            }
            const auto& resourceGraph = deferredResources ? DeferredSceneFrameGraph : SceneFrameGraph;
            for (auto& workset : result.Worksets)
            {
                workset.TransientTextures.resize(resourceGraph.Compiled.TransientAllocations.size());
                for (std::size_t allocationIndex = 0;
                     allocationIndex < resourceGraph.Compiled.TransientAllocations.size(); ++allocationIndex)
                {
                    const auto& allocation = resourceGraph.Compiled.TransientAllocations[allocationIndex];
                    if (allocation.Kind != FrameGraphResourceKind::Texture)
                        throw std::logic_error("The scene frame graph contains an unsupported transient allocation.");
                    SDL_GPUTextureCreateInfo transient{};
                    transient.type = SDL_GPU_TEXTURETYPE_2D;
                    transient.format = ToSdlTextureFormat(allocation.Texture.Format);
                    transient.usage = ToSdlTextureUsage(allocation.Texture.Usage);
                    transient.width = ScaledExtent(surface.RequestedWidth, allocation.Texture.WidthScaleNumerator,
                                                   allocation.Texture.WidthScaleDenominator);
                    transient.height = ScaledExtent(surface.RequestedHeight, allocation.Texture.HeightScaleNumerator,
                                                    allocation.Texture.HeightScaleDenominator);
                    transient.layer_count_or_depth = 1;
                    transient.num_levels = 1;
                    transient.sample_count = FrameGraphSampleCount(allocation.Texture.SampleCount);
                    if (transient.usage == 0U ||
                        !SDL_GPUTextureSupportsFormat(Device, transient.format, transient.type, transient.usage))
                    {
                        throw std::runtime_error("The active GPU backend cannot allocate frame-graph texture format " +
                                                 std::to_string(static_cast<std::uint32_t>(allocation.Texture.Format)) +
                                                 " with its declared usage set.");
                    }
                    workset.TransientTextures[allocationIndex] = SDL_CreateGPUTexture(Device, &transient);
                    if (!workset.TransientTextures[allocationIndex])
                        throw std::runtime_error("SDL_CreateGPUTexture(graph transient) failed: " + LastSdlError());
                }
                const auto transientTexture = [&](const FrameGraphResource resource, const char* diagnostic)
                {
                    if (!resource || resource.Value >= resourceGraph.Compiled.PhysicalResources.size())
                        throw std::logic_error(std::string("The scene frame graph did not declare ") + diagnostic +
                                               '.');
                    const auto allocation = resourceGraph.Compiled.PhysicalResources[resource.Value];
                    if (allocation >= workset.TransientTextures.size())
                        throw std::logic_error(std::string("The scene frame graph did not allocate ") + diagnostic +
                                               '.');
                    return workset.TransientTextures[allocation];
                };
                workset.HdrColor = transientTexture(resourceGraph.HdrScene, "HDR scene color");
                workset.GBufferVelocity = transientTexture(resourceGraph.GBufferVelocity, "motion vectors");
                if (resourceGraph.Path == RenderPath::DeferredHybrid)
                {
                    workset.GBufferBaseColorMetallic =
                        transientTexture(resourceGraph.GBufferBaseColorMetallic, "GBuffer base color and metallic");
                    workset.GBufferNormalRoughness =
                        transientTexture(resourceGraph.GBufferNormalRoughness, "GBuffer normal and roughness");
                    workset.GBufferMaterial = transientTexture(resourceGraph.GBufferMaterial, "GBuffer material");
                    workset.GBufferLighting =
                        transientTexture(resourceGraph.GBufferLighting, "GBuffer baked and spatial lighting");
                    workset.DBufferBaseColor = transientTexture(resourceGraph.DBufferBaseColor, "DBuffer base color");
                    workset.DBufferNormal = transientTexture(resourceGraph.DBufferNormal, "DBuffer normal");
                    workset.DBufferMaterial = transientTexture(resourceGraph.DBufferMaterial, "DBuffer material");
                    workset.IrradynRadiance = transientTexture(resourceGraph.IrradynRadiance, "Irradyn radiance");
                }

                if (samples != SDL_GPU_SAMPLECOUNT_1)
                {
                    auto multisample = sampled;
                    multisample.format = SceneColorFormat;
                    multisample.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                    multisample.sample_count = samples;
                    workset.MultisampleHdrColor = SDL_CreateGPUTexture(Device, &multisample);
                    if (!workset.MultisampleHdrColor)
                        throw std::runtime_error("SDL_CreateGPUTexture(MSAA HDR color) failed: " + LastSdlError());
                }

                if (surface.Specification.Depth && DepthFormat)
                {
                    SDL_GPUTextureCreateInfo depth{};
                    depth.type = SDL_GPU_TEXTURETYPE_2D;
                    depth.format = DepthFormat;
                    depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                    if (deferredResources)
                        depth.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    depth.width = surface.RequestedWidth;
                    depth.height = surface.RequestedHeight;
                    depth.layer_count_or_depth = 1;
                    depth.num_levels = 1;
                    depth.sample_count = deferredResources ? SDL_GPU_SAMPLECOUNT_1 : samples;
                    workset.Depth = SDL_CreateGPUTexture(Device, &depth);
                    if (!workset.Depth)
                        throw std::runtime_error("SDL_CreateGPUTexture(depth) failed: " + LastSdlError());

                    if (deferredResources && samples != SDL_GPU_SAMPLECOUNT_1)
                    {
                        depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                        depth.sample_count = samples;
                        workset.MultisampleDepth = SDL_CreateGPUTexture(Device, &depth);
                        if (!workset.MultisampleDepth)
                        {
                            throw std::runtime_error("SDL_CreateGPUTexture(MSAA depth) failed: " + LastSdlError());
                        }
                    }

                    depth.format = ShadowDepthFormat;
                    depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    depth.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    workset.SampledDepth = SDL_CreateGPUTexture(Device, &depth);
                    if (!workset.SampledDepth)
                        throw std::runtime_error("SDL_CreateGPUTexture(sampled depth) failed: " + LastSdlError());
                }
            }
            return result;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("render surface resource creation", error.what());
            ReleaseResources(result);
            throw;
        }
        catch (...)
        {
            ReleaseResources(result);
            throw;
        }
    }

    void RenderSharedState::EnsureSurface(RenderSurfaceState& surface)
    {
        if (surface.RequestedWidth == 0 || surface.RequestedHeight == 0)
        {
            surface.ResourcesAvailable.store(false, std::memory_order_release);
            if (surface.Generation == 0U || surface.Width != 0 || surface.Height != 0 || !surface.Resources.Empty())
            {
                Retire(std::exchange(surface.Resources, {}));
                surface.Width = 0;
                surface.Height = 0;
                surface.FailedWidth = 0;
                surface.FailedHeight = 0;
                surface.HasOutput = false;
                surface.TemporalHistoryValid = false;
                surface.TemporalHistoryPath = RenderPath::Automatic;
                surface.IrradynHistoryValid = false;
                surface.IrradynRecordedThisFrame = false;
                surface.IrradynCache.Clear();
                surface.SampledDepthValid = false;
                surface.PublishedTexture.store(nullptr, std::memory_order_release);
                surface.PresentationFallbackLifetime.store({}, std::memory_order_release);
                surface.PublishedDepthAvailable.store(false, std::memory_order_release);
                ++surface.Generation;
                ResetGpuOcclusionSurfaceState(surface);
            }
            surface.PublishSurfacePropertiesSnapshot();
            return;
        }
        if (Specification.Mode == RenderMode::Headless)
        {
            surface.ResourcesAvailable.store(false, std::memory_order_release);
            if (surface.Width != surface.RequestedWidth || surface.Height != surface.RequestedHeight)
            {
                surface.Width = surface.RequestedWidth;
                surface.Height = surface.RequestedHeight;
                ++surface.Generation;
                ResetGpuOcclusionSurfaceState(surface);
            }
            surface.PublishSurfacePropertiesSnapshot();
            return;
        }
        if (surface.Width == surface.RequestedWidth && surface.Height == surface.RequestedHeight &&
            surface.Resources.PublishedColor() && surface.Resources.PublishedTemporalHistory() &&
            (!DeferredCapability.load(std::memory_order_acquire) || surface.Resources.PublishedIrradynHistory()) &&
            surface.Resources.Worksets.size() == Specification.MaximumFramesInFlight)
        {
            surface.PublishSurfacePropertiesSnapshot();
            surface.ResourcesAvailable.store(true, std::memory_order_release);
            return;
        }
        surface.ResourcesAvailable.store(false, std::memory_order_release);
        if (surface.FailedWidth == surface.RequestedWidth && surface.FailedHeight == surface.RequestedHeight)
        {
            surface.PublishSurfacePropertiesSnapshot();
            return;
        }

        const auto samples = ResolveSamples(surface.Specification.SampleCount);
        try
        {
            auto replacement = CreateResources(surface, samples);
            Retire(std::exchange(surface.Resources, replacement));
            surface.Width = surface.RequestedWidth;
            surface.Height = surface.RequestedHeight;
            surface.ActualSamples = FromSdlSampleCount(samples);
            surface.FailedWidth = 0;
            surface.FailedHeight = 0;
            ++surface.Generation;
            surface.HasOutput = false;
            surface.TemporalHistoryValid = false;
            surface.TemporalHistoryPath = RenderPath::Automatic;
            surface.IrradynHistoryValid = false;
            surface.IrradynRecordedThisFrame = false;
            surface.IrradynCache.Clear();
            surface.SampledDepthValid = false;
            surface.PublishedDepthAvailable.store(false, std::memory_order_release);
            ResetGpuOcclusionSurfaceState(surface);
            surface.PublishSurfacePropertiesSnapshot();
            surface.ResourcesAvailable.store(true, std::memory_order_release);
        }
        catch (const GpuDeviceLostError&)
        {
            surface.PublishSurfacePropertiesSnapshot();
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("render surface resize", error.what());
            surface.ResourcesAvailable.store(false, std::memory_order_release);
            surface.FailedWidth = surface.RequestedWidth;
            surface.FailedHeight = surface.RequestedHeight;
            surface.PublishSurfacePropertiesSnapshot();
            KEIRE_CORE_ERROR("Could not resize render surface '{}': {}", surface.Specification.Name, error.what());
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreatePipeline(const SDL_GPUSampleCount samples,
                                                               const SDL_GPUPrimitiveType primitive,
                                                               const bool depthWrite, const bool blend)
    {
        SDL_GPUShader* vertex = CreateShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateShader(false);

            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = blend;

            SDL_GPUVertexBufferDescription vertexBuffer{};
            vertexBuffer.slot = 0;
            vertexBuffer.pitch = sizeof(GpuMeshVertex);
            vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            std::array<SDL_GPUVertexAttribute, 6> attributes{};
            attributes[0].location = 0;
            attributes[0].buffer_slot = 0;
            attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attributes[0].offset = offsetof(GpuMeshVertex, Position);
            attributes[1].location = 1;
            attributes[1].buffer_slot = 0;
            attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attributes[1].offset = offsetof(GpuMeshVertex, Normal);
            attributes[2].location = 2;
            attributes[2].buffer_slot = 0;
            attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attributes[2].offset = offsetof(GpuMeshVertex, UV0);
            attributes[3].location = 3;
            attributes[3].buffer_slot = 0;
            attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            attributes[3].offset = offsetof(GpuMeshVertex, VertexColor);
            attributes[4].location = 4;
            attributes[4].buffer_slot = 0;
            attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            attributes[4].offset = offsetof(GpuMeshVertex, Tangent);
            attributes[5].location = 5;
            attributes[5].buffer_slot = 0;
            attributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attributes[5].offset = offsetof(GpuMeshVertex, UV1);

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = attributes.data();
            information.vertex_input_state.num_vertex_attributes = static_cast<std::uint32_t>(attributes.size());
            information.primitive_type = primitive;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = samples;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = depthWrite;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;

            KEIRE_CORE_INFO("Creating built-in pipeline (primitive={}, samples={}, color={}, depth={}, attributes=6).",
                            static_cast<std::uint32_t>(primitive), static_cast<std::uint32_t>(samples),
                            static_cast<std::uint32_t>(SceneColorFormat), static_cast<std::uint32_t>(DepthFormat));
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
                throw std::runtime_error(
                    "SDL_CreateGPUGraphicsPipeline failed for " +
                    std::string(primitive == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST ? "triangle list" : "line list") +
                    ": " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("render pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateSkyPipeline(const SDL_GPUSampleCount samples)
    {
        SDL_GPUShader* vertex = CreateSkyShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateSkyShader(false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = samples;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(sky) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("sky pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateGridPipeline(const SDL_GPUSampleCount samples)
    {
        SDL_GPUShader* vertex = CreateGridShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateGridShader(false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = true;

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = samples;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = false;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(grid) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("grid pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateDepthPipeline(const bool depthBias)
    {
        SDL_GPUShader* vertex = CreateShadowShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateShadowShader(false);
            SDL_GPUVertexBufferDescription buffer{};
            buffer.slot = 0;
            buffer.pitch = sizeof(GpuMeshVertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const SDL_GPUVertexAttribute position{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                                  offsetof(GpuMeshVertex, Position)};
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &buffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = &position;
            information.vertex_input_state.num_vertex_attributes = 1;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            if (depthBias)
            {
                information.rasterizer_state.depth_bias_constant_factor = DirectionalShadowDepthBiasConstant;
                information.rasterizer_state.depth_bias_clamp = 0.0F;
                information.rasterizer_state.depth_bias_slope_factor = DirectionalShadowDepthBiasSlope;
                information.rasterizer_state.enable_depth_bias = true;
            }
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = true;
            information.depth_stencil_state.enable_depth_write = true;
            information.target_info.num_color_targets = 0;
            information.target_info.depth_stencil_format = ShadowDepthFormat;
            information.target_info.has_depth_stencil_target = true;
            auto* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(depth-only) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("depth pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateToneMapPipeline()
    {
        SDL_GPUShader* vertex = CreateToneMapShader(true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateToneMapShader(false);
            SDL_GPUColorTargetDescription color{};
            color.format = ColorFormat;
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(tone map) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("tone-map pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    RenderPipelineSet& RenderSharedState::PipelinesFor(const SDL_GPUSampleCount samples)
    {
        const auto found = std::ranges::find(Pipelines, samples, &RenderPipelineSet::Samples);
        if (found != Pipelines.end())
            return *found;

        RenderPipelineSet result;
        result.Samples = samples;
        try
        {
            result.Cube = CreatePipeline(samples, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, true);
            result.Grid = CreateGridPipeline(samples);
            result.Sky = CreateSkyPipeline(samples);
            result.Vfx = CreateCpuVfxPipeline(samples);
            result.GpuVfx = CreateGpuVfxPipeline(samples);
            result.GpuVfxRibbon = CreateGpuVfxPipeline(samples, true);
            result.GpuVfxMesh = CreateGpuVfxMeshPipeline(samples);
            return Pipelines.emplace_back(result);
        }
        catch (...)
        {
            RethrowIfDeviceLost("surface pipeline-set creation");
            if (result.GpuVfxMesh)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.GpuVfxMesh);
            if (result.GpuVfxRibbon)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.GpuVfxRibbon);
            if (result.GpuVfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.GpuVfx);
            if (result.Vfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.Vfx);
            if (result.Grid)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.Grid);
            if (result.Sky)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.Sky);
            if (result.Cube)
                SDL_ReleaseGPUGraphicsPipeline(Device, result.Cube);
            throw;
        }
    }

} // namespace Keire::RenderBackend
