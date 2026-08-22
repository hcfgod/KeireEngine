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
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
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
    void RenderSharedState::CreateGeometryResources()
    {
        ShadowPipeline = CreateDepthPipeline(true);
        SceneDepthPipeline = CreateDepthPipeline(false);
        ToneMapPipeline = CreateToneMapPipeline();
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
        Retire(std::exchange(surface.Resources, {}));
        Retire(std::exchange(surface.ForwardPlus, {}));
        surface.Owner.reset();
        surface.Width = 0;
        surface.Height = 0;
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
            result.SampledColor = SDL_CreateGPUTexture(Device, &sampled);
            if (!result.SampledColor)
                throw std::runtime_error("SDL_CreateGPUTexture(color) failed: " + LastSdlError());
            result.ExchangeColor = SDL_CreateGPUTexture(Device, &sampled);
            if (!result.ExchangeColor)
                throw std::runtime_error("SDL_CreateGPUTexture(exchange color) failed: " + LastSdlError());

            auto hdr = sampled;
            hdr.format = SceneColorFormat;
            result.TransientTextures.resize(SceneFrameGraph.Compiled.TransientAllocations.size());
            for (std::size_t allocationIndex = 0;
                 allocationIndex < SceneFrameGraph.Compiled.TransientAllocations.size(); ++allocationIndex)
            {
                const auto& allocation = SceneFrameGraph.Compiled.TransientAllocations[allocationIndex];
                if (allocation.Kind != FrameGraphResourceKind::Texture || allocation.CompatibilityKey != 4)
                    throw std::logic_error("The scene frame graph contains an unsupported transient allocation.");
                result.TransientTextures[allocationIndex] = SDL_CreateGPUTexture(Device, &hdr);
                if (!result.TransientTextures[allocationIndex])
                    throw std::runtime_error("SDL_CreateGPUTexture(graph transient) failed: " + LastSdlError());
            }
            const auto hdrAllocation = SceneFrameGraph.Compiled.PhysicalResources[SceneFrameGraph.HdrScene.Value];
            if (hdrAllocation >= result.TransientTextures.size())
                throw std::logic_error("The scene frame graph did not allocate HDR scene color.");
            result.HdrColor = result.TransientTextures[hdrAllocation];

            if (samples != SDL_GPU_SAMPLECOUNT_1)
            {
                auto multisample = hdr;
                multisample.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                multisample.sample_count = samples;
                result.MultisampleHdrColor = SDL_CreateGPUTexture(Device, &multisample);
                if (!result.MultisampleHdrColor)
                    throw std::runtime_error("SDL_CreateGPUTexture(MSAA HDR color) failed: " + LastSdlError());
            }

            if (surface.Specification.Depth && DepthFormat)
            {
                SDL_GPUTextureCreateInfo depth{};
                depth.type = SDL_GPU_TEXTURETYPE_2D;
                depth.format = DepthFormat;
                depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                depth.width = surface.RequestedWidth;
                depth.height = surface.RequestedHeight;
                depth.layer_count_or_depth = 1;
                depth.num_levels = 1;
                depth.sample_count = samples;
                result.Depth = SDL_CreateGPUTexture(Device, &depth);
                if (!result.Depth)
                    throw std::runtime_error("SDL_CreateGPUTexture(depth) failed: " + LastSdlError());

                depth.format = ShadowDepthFormat;
                depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                depth.sample_count = SDL_GPU_SAMPLECOUNT_1;
                result.SampledDepth = SDL_CreateGPUTexture(Device, &depth);
                if (!result.SampledDepth)
                    throw std::runtime_error("SDL_CreateGPUTexture(sampled depth) failed: " + LastSdlError());
            }
            return result;
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
            if (surface.Width != 0 || surface.Height != 0 || !surface.Resources.Empty())
            {
                Retire(std::exchange(surface.Resources, {}));
                surface.Width = 0;
                surface.Height = 0;
                surface.FailedWidth = 0;
                surface.FailedHeight = 0;
                surface.HasOutput = false;
                surface.SampledDepthValid = false;
                ++surface.Generation;
            }
            return;
        }
        if (Specification.Mode == RenderMode::Headless)
        {
            if (surface.Width != surface.RequestedWidth || surface.Height != surface.RequestedHeight)
            {
                surface.Width = surface.RequestedWidth;
                surface.Height = surface.RequestedHeight;
                ++surface.Generation;
            }
            return;
        }
        if (surface.Width == surface.RequestedWidth && surface.Height == surface.RequestedHeight &&
            surface.Resources.SampledColor)
            return;
        if (surface.FailedWidth == surface.RequestedWidth && surface.FailedHeight == surface.RequestedHeight)
            return;

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
            surface.SampledDepthValid = false;
        }
        catch (const std::exception& error)
        {
            surface.FailedWidth = surface.RequestedWidth;
            surface.FailedHeight = surface.RequestedHeight;
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
            vertexBuffer.pitch = sizeof(GpuRenderVertex);
            vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            std::array<SDL_GPUVertexAttribute, 3> attributes{};
            attributes[0].location = 0;
            attributes[0].buffer_slot = 0;
            attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attributes[0].offset = offsetof(GpuRenderVertex, Position);
            attributes[1].location = 1;
            attributes[1].buffer_slot = 0;
            attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attributes[1].offset = offsetof(GpuRenderVertex, Color);
            attributes[2].location = 2;
            attributes[2].buffer_slot = 0;
            attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attributes[2].offset = offsetof(GpuRenderVertex, Normal);

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

            KEIRE_CORE_INFO("Creating built-in pipeline (primitive={}, samples={}, color={}, depth={}, attributes=3).",
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

    SDL_GPUShader* RenderSharedState::CreateAssetShader(const ShaderAssetDefinition& definition,
                                                        const bool vertex) const
    {
        const auto supported = SDL_GetGPUShaderFormats(Device);
        const ShaderVariant* variant = nullptr;
        SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
        const auto findVariant = [&definition](const ShaderBinaryFormat requested)
        {
            const auto found = std::ranges::find(definition.Variants, requested, &ShaderVariant::Format);
            return found == definition.Variants.end() ? nullptr : &*found;
        };
        if (supported & SDL_GPU_SHADERFORMAT_DXIL)
        {
            variant = findVariant(ShaderBinaryFormat::Dxil);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_DXIL;
        }
        if (!variant && (supported & SDL_GPU_SHADERFORMAT_MSL))
        {
            variant = findVariant(ShaderBinaryFormat::Msl);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_MSL;
        }
        if (!variant && (supported & SDL_GPU_SHADERFORMAT_SPIRV))
        {
            variant = findVariant(ShaderBinaryFormat::SpirV);
            if (variant)
                format = SDL_GPU_SHADERFORMAT_SPIRV;
        }
        if (!variant)
            throw std::runtime_error("Shader asset lacks a variant for the active GPU backend.");

        const auto& code = vertex ? variant->Vertex : variant->Fragment;
        const auto textureCount = static_cast<std::uint32_t>(
            std::ranges::count(definition.Properties, ShaderPropertyType::Texture2D, &ShaderPropertyDefinition::Type));
        SDL_GPUShaderCreateInfo information{};
        information.code_size = code.size();
        information.code = reinterpret_cast<const std::uint8_t*>(code.data());
        information.entrypoint = format == SDL_GPU_SHADERFORMAT_SPIRV
                                     ? (vertex ? definition.VertexEntry.c_str() : definition.FragmentEntry.c_str())
                                     : nullptr;
        information.format = format;
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0
                                          : textureCount + definition.UserResourceSlots +
                                                (definition.ReceivesShadows ? 2U : 0U) +
                                                (definition.UsesImageBasedLighting ? 2U : 0U) +
                                                (definition.SpatialLightingAbiVersion == 2U ? 5U : 0U);
        information.num_storage_buffers =
            !vertex ? (definition.UsesForwardPlus ? 3U : 0U) + definition.UserReadOnlyBuffers : 0U;
        if (vertex && definition.UsesInstancing)
            information.num_storage_buffers = 1U;
        information.num_uniform_buffers = vertex ? (definition.UsesVertexMaterialParameters ? 2U : 1U)
                                                 : 2U + (definition.UsesImageBasedLighting ? 2U
                                                         : definition.ReceivesShadows      ? 1U
                                                                                           : 0U);
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(asset) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                    const SDL_GPUSampleCount samples,
                                                                    const MaterialSurfaceState surface)
    {
        SDL_GPUShader* vertex = CreateAssetShader(definition, true);
        SDL_GPUShader* fragment = nullptr;
        try
        {
            fragment = CreateAssetShader(definition, false);
            SDL_GPUColorTargetDescription color{};
            color.format = SceneColorFormat;
            const auto blending = MaterialBlending(surface.AlphaMode);
            const auto blendFactor = [](const MaterialBlendFactor factor) noexcept
            {
                switch (factor)
                {
                case MaterialBlendFactor::Zero:
                    return SDL_GPU_BLENDFACTOR_ZERO;
                case MaterialBlendFactor::SourceAlpha:
                    return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                case MaterialBlendFactor::OneMinusSourceAlpha:
                    return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                case MaterialBlendFactor::DestinationColor:
                    return SDL_GPU_BLENDFACTOR_DST_COLOR;
                case MaterialBlendFactor::One:
                default:
                    return SDL_GPU_BLENDFACTOR_ONE;
                }
            };
            color.blend_state.src_color_blendfactor = blendFactor(blending.SourceColor);
            color.blend_state.dst_color_blendfactor = blendFactor(blending.DestinationColor);
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = blendFactor(blending.SourceAlpha);
            color.blend_state.dst_alpha_blendfactor = blendFactor(blending.DestinationAlpha);
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.enable_blend = blending.Enabled;

            SDL_GPUVertexBufferDescription buffer{};
            buffer.slot = 0;
            buffer.pitch = sizeof(GpuMeshVertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const std::array attributes{
                SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Position)},
                SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMeshVertex, Normal)},
                SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMeshVertex, UV0)},
                SDL_GPUVertexAttribute{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, VertexColor)},
                SDL_GPUVertexAttribute{4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuMeshVertex, Tangent)},
                SDL_GPUVertexAttribute{5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMeshVertex, UV1)}};
            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state.vertex_buffer_descriptions = &buffer;
            information.vertex_input_state.num_vertex_buffers = 1;
            information.vertex_input_state.vertex_attributes = attributes.data();
            information.vertex_input_state.num_vertex_attributes = definition.VertexLayoutVersion == 3
                                                                       ? static_cast<std::uint32_t>(attributes.size())
                                                                   : definition.VertexLayoutVersion == 2 ? 5U
                                                                                                         : 4U;
            information.primitive_type =
                definition.Topology == ShaderPrimitiveTopology::PointList  ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                : definition.Topology == ShaderPrimitiveTopology::LineList ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                                           : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode =
                surface.DoubleSided                           ? SDL_GPU_CULLMODE_NONE
                : definition.Culling == ShaderCullMode::Front ? SDL_GPU_CULLMODE_FRONT
                : definition.Culling == ShaderCullMode::Back  ? SDL_GPU_CULLMODE_BACK
                                                              : SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
            information.rasterizer_state.enable_depth_clip = true;
            information.multisample_state.sample_count = samples;
            information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            information.depth_stencil_state.enable_depth_test = definition.DepthTest;
            information.depth_stencil_state.enable_depth_write = definition.DepthWrite && blending.WritesDepth;
            information.target_info.color_target_descriptions = &color;
            information.target_info.num_color_targets = 1;
            information.target_info.depth_stencil_format = DepthFormat;
            information.target_info.has_depth_stencil_target = true;
            KEIRE_CORE_INFO("Creating asset pipeline (layout={}, topology={}, samples={}, color={}, depth={}, "
                            "attributes={}).",
                            definition.VertexLayoutVersion, static_cast<std::uint32_t>(definition.Topology),
                            static_cast<std::uint32_t>(samples), static_cast<std::uint32_t>(SceneColorFormat),
                            static_cast<std::uint32_t>(DepthFormat),
                            information.vertex_input_state.num_vertex_attributes);
            SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!result)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(asset) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return result;
        }
        catch (...)
        {
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }
} // namespace Keire::RenderBackend
