#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    GpuTextureResources RenderSharedState::CreateTextureResources(const Texture2DAsset& asset)
    {
        const auto mips = asset.Mips();
        if (mips.empty() || mips.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Texture GPU upload requires a bounded mip chain.");
        std::size_t totalBytes = 0;
        for (const auto& mip : mips)
        {
            if (mip.Pixels.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                throw std::invalid_argument("Texture GPU upload exceeds SDL's 32-bit transfer limit.");
            totalBytes += mip.Pixels.size();
        }

        GpuTextureResources result;
        result.EstimatedBytes = totalBytes;
        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTextureCreateInfo texture{};
            texture.type = SDL_GPU_TEXTURETYPE_2D;
            texture.format = asset.Settings().ColorSpace == TextureColorSpace::Srgb
                                 ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                                 : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture.width = asset.Width();
            texture.height = asset.Height();
            texture.layer_count_or_depth = 1;
            texture.num_levels = static_cast<std::uint32_t>(mips.size());
            texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
            result.Texture = SDL_CreateGPUTexture(Device, &texture);
            if (!result.Texture)
                throw std::runtime_error("SDL_CreateGPUTexture(asset) failed: " + LastSdlError());

            result.Sampler = ResolveSampler(asset.Settings().Sampler);
            result.HdrEncoded = asset.Settings().HighDynamicRange;
            result.EnvironmentLayout = asset.Settings().EnvironmentLayout;
            result.MipLevels = static_cast<std::uint32_t>(mips.size());
            if (asset.Settings().Semantic == TextureSemantic::Environment)
            {
                const auto irradiance = BakeDiffuseIrradiance(asset);
                for (std::size_t index = 0; index < irradiance.Coefficients.size(); ++index)
                {
                    const auto& coefficient = irradiance.Coefficients[index];
                    result.DiffuseIrradiance[index] = {coefficient.X, coefficient.Y, coefficient.Z, 0.0F};
                }
                result.HasDiffuseIrradiance = true;
            }

            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = static_cast<std::uint32_t>(totalBytes);
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(texture) failed: " + LastSdlError());
            auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(texture) failed: " + LastSdlError());
            std::size_t offset = 0;
            for (const auto& mip : mips)
            {
                std::memcpy(mapped + offset, mip.Pixels.data(), mip.Pixels.size());
                offset += mip.Pixels.size();
            }
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            if (FrameActive)
            {
                EnsureFrameUploadContext();
                offset = 0;
                for (std::size_t index = 0; index < mips.size(); ++index)
                {
                    const auto& mip = mips[index];
                    SDL_GPUTextureTransferInfo source{transfer, static_cast<std::uint32_t>(offset), mip.Width,
                                                      mip.Height};
                    SDL_GPUTextureRegion destination{
                        result.Texture, static_cast<std::uint32_t>(index), 0, 0, 0, 0, mip.Width, mip.Height, 1};
                    SDL_UploadToGPUTexture(FrameUploadPass, &source, &destination, false);
                    offset += mip.Pixels.size();
                }
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return result;
            }

            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(texture) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(texture) failed: " + LastSdlError());
            }
            offset = 0;
            for (std::size_t index = 0; index < mips.size(); ++index)
            {
                const auto& mip = mips[index];
                SDL_GPUTextureTransferInfo source{transfer, static_cast<std::uint32_t>(offset), mip.Width, mip.Height};
                SDL_GPUTextureRegion destination{
                    result.Texture, static_cast<std::uint32_t>(index), 0, 0, 0, 0, mip.Width, mip.Height, 1};
                SDL_UploadToGPUTexture(copy, &source, &destination, false);
                offset += mip.Pixels.size();
            }
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(texture) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU texture upload");
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (result.Texture)
                SDL_ReleaseGPUTexture(Device, result.Texture);
            throw;
        }
    }

    GpuTextureResources RenderSharedState::CreateLightingTextureResources(const LightingTextureArrayAsset& asset)
    {
        const auto& definition = asset.Definition();
        if (definition.Mips.empty() || definition.Mips.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Baked-lighting GPU upload requires a bounded mip chain.");
        std::size_t totalBytes = 0;
        for (const auto& mip : definition.Mips)
        {
            if (mip.Pixels.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                throw std::invalid_argument("Baked-lighting GPU upload exceeds SDL's transfer limit.");
            totalBytes += mip.Pixels.size();
        }

        GpuTextureResources result;
        result.EstimatedBytes = totalBytes;
        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTextureCreateInfo texture{};
            texture.type = definition.Target == LightingTextureTarget::CubeArray ? SDL_GPU_TEXTURETYPE_CUBE_ARRAY
                                                                                 : SDL_GPU_TEXTURETYPE_2D_ARRAY;
            texture.format = definition.Encoding == LightingTextureEncoding::Rgba16Float
                                 ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
                                 : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture.width = definition.Mips.front().Width;
            texture.height = definition.Mips.front().Height;
            texture.layer_count_or_depth = definition.Mips.front().Layers;
            texture.num_levels = static_cast<std::uint32_t>(definition.Mips.size());
            texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
            result.Texture = SDL_CreateGPUTexture(Device, &texture);
            if (!result.Texture)
                throw std::runtime_error("SDL_CreateGPUTexture(baked lighting) failed: " + LastSdlError());
            SamplerDescription sampler;
            sampler.AddressU = TextureAddressMode::Clamp;
            sampler.AddressV = TextureAddressMode::Clamp;
            sampler.AddressW = TextureAddressMode::Clamp;
            result.Sampler = ResolveSampler(sampler);
            result.HdrEncoded = definition.Encoding == LightingTextureEncoding::Rgbe8;
            result.MipLevels = static_cast<std::uint32_t>(definition.Mips.size());

            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = static_cast<std::uint32_t>(totalBytes);
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(baked lighting) failed: " + LastSdlError());
            auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(baked lighting) failed: " + LastSdlError());
            std::size_t offset = 0;
            for (const auto& mip : definition.Mips)
            {
                std::memcpy(mapped + offset, mip.Pixels.data(), mip.Pixels.size());
                offset += mip.Pixels.size();
            }
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            const auto upload = [&](SDL_GPUCopyPass* copy)
            {
                std::size_t sourceOffset = 0;
                for (std::size_t index = 0; index < definition.Mips.size(); ++index)
                {
                    const auto& mip = definition.Mips[index];
                    const auto layerBytes = mip.Pixels.size() / mip.Layers;
                    for (std::uint32_t layer = 0; layer < mip.Layers; ++layer)
                    {
                        SDL_GPUTextureTransferInfo source{transfer,
                                                          static_cast<std::uint32_t>(sourceOffset + layerBytes * layer),
                                                          mip.Width, mip.Height};
                        SDL_GPUTextureRegion destination{result.Texture,
                                                         static_cast<std::uint32_t>(index),
                                                         layer,
                                                         0,
                                                         0,
                                                         0,
                                                         mip.Width,
                                                         mip.Height,
                                                         1};
                        SDL_UploadToGPUTexture(copy, &source, &destination, false);
                    }
                    sourceOffset += mip.Pixels.size();
                }
            };
            if (FrameActive)
            {
                EnsureFrameUploadContext();
                upload(FrameUploadPass);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return result;
            }
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(baked lighting) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(baked lighting) failed: " + LastSdlError());
            }
            upload(copy);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(baked lighting) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("baked-lighting texture upload");
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (result.Texture)
                SDL_ReleaseGPUTexture(Device, result.Texture);
            throw;
        }
    }

    const GpuTextureResources& RenderSharedState::DefaultTexture(const ShaderTextureSemantic semantic) const noexcept
    {
        switch (semantic)
        {
        case ShaderTextureSemantic::BaseColor:
            return WhiteTexture;
        case ShaderTextureSemantic::Normal:
            return FlatNormalTexture;
        case ShaderTextureSemantic::MetallicRoughness:
        case ShaderTextureSemantic::Occlusion:
            return NeutralOrmTexture;
        case ShaderTextureSemantic::Emissive:
            return BlackTexture;
        case ShaderTextureSemantic::Metallic:
            return BlackDataTexture;
        case ShaderTextureSemantic::Roughness:
            return WhiteDataTexture;
        case ShaderTextureSemantic::Specular:
            return WhiteDataTexture;
        case ShaderTextureSemantic::Generic:
        default:
            return CheckerboardTexture;
        }
    }

} // namespace Keire::RenderBackend
