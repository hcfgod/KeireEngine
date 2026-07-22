#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <cstring>
#include <stdexcept>

namespace Keire::RenderBackend
{
    RenderSharedState::RenderSharedState(RenderSpecification specification, Ref<WindowSystem> windows,
                                         Ref<Keire::Window> window, Ref<AssetSystem> assets)
        : Specification(std::move(specification)), Windows(std::move(windows)), Window(std::move(window)),
          Assets(std::move(assets)), OwnerThread(std::this_thread::get_id())
    {
        if (!ValidColor(Specification.SwapchainClearColor))
            throw std::invalid_argument("Render swapchain clear color must contain finite values in 0..1.");
        if (Specification.MaximumFramesInFlight < 1 || Specification.MaximumFramesInFlight > 8)
            throw std::invalid_argument("MaximumFramesInFlight must be in the range 1..8.");
        if (Specification.Mode == RenderMode::Automatic)
            throw std::invalid_argument("RenderSystem requires a resolved render mode.");
        if (Specification.Mode != RenderMode::Rendered)
            return;
        if (!Windows || !Window)
            throw std::invalid_argument("Rendered mode requires an open window system and primary window.");

        NativeWindow = WindowSystemInternalAccess::NativeWindow(*Windows, Window->Id());
        if (!NativeWindow)
            throw std::runtime_error("The renderer could not resolve the primary native window.");

        constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB);
        Device = SDL_CreateGPUDevice(formats, Specification.EnableGpuValidation, nullptr);
        if (!Device)
            throw std::runtime_error("SDL_CreateGPUDevice failed: " + LastSdlError());
        KEIRE_CORE_INFO("Created SDL_GPU device (driver={}, shader formats=0x{:x}).", SDL_GetGPUDeviceDriver(Device),
                        static_cast<std::uint32_t>(SDL_GetGPUShaderFormats(Device)));

        try
        {
            if (!SDL_ClaimWindowForGPUDevice(Device, NativeWindow))
                throw std::runtime_error("SDL_ClaimWindowForGPUDevice failed: " + LastSdlError());
            WindowClaimed = true;

            PresentMode = ToSdlPresentMode(Specification.PresentMode);
            if (!SDL_WindowSupportsGPUPresentMode(Device, NativeWindow, PresentMode))
            {
                KEIRE_CORE_WARN("Requested render present mode is unavailable; falling back to VSync.");
                PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
            }
            if (!SDL_SetGPUSwapchainParameters(Device, NativeWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode))
            {
                throw std::runtime_error("SDL_SetGPUSwapchainParameters failed: " + LastSdlError());
            }

            ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
            const SDL_GPUTextureUsageFlags colorUsage =
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            if (!SDL_GPUTextureSupportsFormat(Device, ColorFormat, SDL_GPU_TEXTURETYPE_2D, colorUsage))
                ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

            constexpr SDL_GPUTextureFormat depthCandidates[] = {
                SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTUREFORMAT_D16_UNORM};
            for (const auto candidate : depthCandidates)
            {
                if (SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
                {
                    DepthFormat = candidate;
                    break;
                }
            }
            KEIRE_CORE_INFO("Selected GPU attachment formats (color={}, depth={}).",
                            static_cast<std::uint32_t>(ColorFormat), static_cast<std::uint32_t>(DepthFormat));

            CreateGeometryResources();
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    RenderSharedState::~RenderSharedState() { Close(); }

    void RenderSharedState::RequireOwner(const char* operation) const
    {
        if (std::this_thread::get_id() != OwnerThread)
            throw std::logic_error(std::string("RenderSystem::") + operation +
                                   " must be called on the application owner thread.");
        if (!Open)
            throw std::logic_error(std::string("RenderSystem::") + operation + " called after shutdown.");
    }

    std::vector<std::shared_ptr<RenderSurfaceState>> RenderSharedState::LiveSurfaces()
    {
        std::vector<std::shared_ptr<RenderSurfaceState>> result;
        result.reserve(Surfaces.size());
        std::erase_if(Surfaces,
                      [&result](const std::weak_ptr<RenderSurfaceState>& weak)
                      {
                          if (auto surface = weak.lock())
                          {
                              result.push_back(std::move(surface));
                              return false;
                          }
                          return true;
                      });
        std::ranges::sort(result, {}, &RenderSurfaceState::Id);
        return result;
    }

    void RenderSharedState::ReleaseResources(SurfaceResources& resources) noexcept
    {
        if (!Device)
        {
            resources = {};
            return;
        }
        if (resources.Depth)
            SDL_ReleaseGPUTexture(Device, resources.Depth);
        if (resources.MultisampleColor)
            SDL_ReleaseGPUTexture(Device, resources.MultisampleColor);
        if (resources.SampledColor)
            SDL_ReleaseGPUTexture(Device, resources.SampledColor);
        resources = {};
    }

    SDL_GPUShader* RenderSharedState::CreateShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = Detail::BuiltinShaderUniformBufferCount(vertex);

        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinUnlitVertexDxil : Detail::BuiltinUnlitFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinUnlitVertexDxilSize : Detail::BuiltinUnlitFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinUnlitVertexMsl : Detail::BuiltinUnlitFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinUnlitVertexMslSize : Detail::BuiltinUnlitFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinUnlitVertexSpirV : Detail::BuiltinUnlitFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinUnlitVertexSpirVSize : Detail::BuiltinUnlitFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported built-in shader format.");

        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUBuffer* RenderSharedState::UploadBuffer(const std::span<const std::byte> bytes,
                                                   const SDL_GPUBufferUsageFlags usage)
    {
        if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("GPU buffer payload is empty or exceeds the 32-bit SDL limit.");
        const auto byteSize = static_cast<std::uint32_t>(bytes.size());
        SDL_GPUBufferCreateInfo bufferInformation{};
        bufferInformation.usage = usage;
        bufferInformation.size = byteSize;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(Device, &bufferInformation);
        if (!buffer)
            throw std::runtime_error("SDL_CreateGPUBuffer failed: " + LastSdlError());

        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = byteSize;
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer failed: " + LastSdlError());

            void* mapped = SDL_MapGPUTransferBuffer(Device, transfer, false);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer failed: " + LastSdlError());
            std::memcpy(mapped, bytes.data(), bytes.size());
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass failed: " + LastSdlError());
            }
            SDL_GPUTransferBufferLocation source{transfer, 0};
            SDL_GPUBufferRegion destination{buffer, 0, byteSize};
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            return buffer;
        }
        catch (...)
        {
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            SDL_ReleaseGPUBuffer(Device, buffer);
            throw;
        }
    }

    SDL_GPUBuffer* RenderSharedState::UploadVertexBuffer(const std::span<const RenderVertex> vertices)
    {
        return UploadBuffer(std::as_bytes(vertices), SDL_GPU_BUFFERUSAGE_VERTEX);
    }

    SDL_GPUSampler* RenderSharedState::ResolveSampler(const SamplerDescription& description)
    {
        const auto found = std::ranges::find(SamplerCache, description, &decltype(SamplerCache)::value_type::first);
        if (found != SamplerCache.end())
            return found->second;
        const auto filter = [](const TextureFilter value)
        { return value == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR; };
        const auto address = [](const TextureAddressMode value)
        {
            switch (value)
            {
            case TextureAddressMode::Clamp:
                return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            case TextureAddressMode::Mirror:
                return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
            case TextureAddressMode::Repeat:
            default:
                return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            }
        };
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = filter(description.Minimum);
        sampler.mag_filter = filter(description.Magnification);
        sampler.mipmap_mode = description.Mip == TextureFilter::Nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
                                                                        : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u = address(description.AddressU);
        sampler.address_mode_v = address(description.AddressV);
        sampler.address_mode_w = address(description.AddressW);
        sampler.max_anisotropy = static_cast<float>(description.Anisotropy);
        sampler.max_lod = 32.0F;
        sampler.enable_anisotropy = description.Anisotropy > 1;
        SDL_GPUSampler* created = SDL_CreateGPUSampler(Device, &sampler);
        if (!created)
            throw std::runtime_error("SDL_CreateGPUSampler failed: " + LastSdlError());
        SamplerCache.emplace_back(description, created);
        return created;
    }

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
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (result.Texture)
                SDL_ReleaseGPUTexture(Device, result.Texture);
            throw;
        }
    }

    GpuMeshResources RenderSharedState::CreateMeshResources(const MeshAsset& mesh)
    {
        std::vector<RenderVertex> vertices;
        vertices.reserve(mesh.Vertices().size());
        for (const auto& vertex : mesh.Vertices())
        {
            vertices.push_back({vertex.Position,
                                {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                                vertex.Normal});
        }
        GpuMeshResources result;
        try
        {
            result.Vertices = UploadVertexBuffer(vertices);
            result.AssetVertices = UploadBuffer(std::as_bytes(mesh.Vertices()), SDL_GPU_BUFFERUSAGE_VERTEX);
            result.Indices = UploadBuffer(std::as_bytes(mesh.Indices()), SDL_GPU_BUFFERUSAGE_INDEX);
            result.IndexCount = static_cast<std::uint32_t>(mesh.Indices().size());
            return result;
        }
        catch (...)
        {
            if (result.Indices)
                SDL_ReleaseGPUBuffer(Device, result.Indices);
            if (result.Vertices)
                SDL_ReleaseGPUBuffer(Device, result.Vertices);
            if (result.AssetVertices)
                SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
            throw;
        }
    }
} // namespace Keire::RenderBackend
