#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <cstring>
#include <future>
#include <stdexcept>

namespace Keire::RenderBackend
{
    RenderSharedState::RenderSharedState(RenderSpecification specification, Ref<WindowSystem> windows,
                                         Ref<Keire::Window> window, Ref<AssetSystem> assets, Ref<JobSystem> jobs,
                                         Ref<StreamingSystem> streaming)
        : Specification(std::move(specification)), Windows(std::move(windows)), Window(std::move(window)),
          Assets(std::move(assets)), Streaming(std::move(streaming)), OwnerThread(std::this_thread::get_id()),
          Jobs(std::move(jobs))
    {
        if (!ValidColor(Specification.SwapchainClearColor))
            throw std::invalid_argument("Render swapchain clear color must contain finite values in 0..1.");
        SceneFrameGraph = BuildStaticSceneFrameGraph();
        if (Specification.MaximumFramesInFlight < 1 || Specification.MaximumFramesInFlight > 8)
            throw std::invalid_argument("MaximumFramesInFlight must be in the range 1..8.");
        if (Specification.Mode == RenderMode::Automatic)
            throw std::invalid_argument("RenderSystem requires a resolved render mode.");
        if (Specification.Mode != RenderMode::Rendered)
            return;
        if (!Jobs)
            throw std::invalid_argument("Rendered mode requires an application job system.");
        RenderJobs = Jobs->CreateScope("Renderer background work");
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

            Statistics.AllowedFramesInFlight = SdlAllowedFramesInFlight(Specification.MaximumFramesInFlight);
            if (!SDL_SetGPUAllowedFramesInFlight(Device, Statistics.AllowedFramesInFlight))
                throw std::runtime_error("SDL_SetGPUAllowedFramesInFlight failed: " + LastSdlError());

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

            if (!SDL_GPUTextureSupportsFormat(Device, SceneColorFormat, SDL_GPU_TEXTURETYPE_2D, colorUsage))
                throw std::runtime_error("The active GPU does not support RGBA16F sampled color attachments.");

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
            for (const auto candidate : depthCandidates)
            {
                if (SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D_ARRAY,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                                     SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
                    SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                                     SDL_GPU_TEXTUREUSAGE_SAMPLER))
                {
                    ShadowDepthFormat = candidate;
                    break;
                }
            }
            if (DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID || ShadowDepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
                throw std::runtime_error("The active GPU exposes no compatible scene and sampled shadow depth format.");
            KEIRE_CORE_INFO("Selected GPU attachment formats (output={}, scene={}, depth={}, shadowDepth={}).",
                            static_cast<std::uint32_t>(ColorFormat), static_cast<std::uint32_t>(SceneColorFormat),
                            static_cast<std::uint32_t>(DepthFormat), static_cast<std::uint32_t>(ShadowDepthFormat));
            KEIRE_CORE_INFO("Configured {} GPU frame(s) in flight.", Statistics.AllowedFramesInFlight);

            CreateGeometryResources();
            StartRenderThread();
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    RenderSharedState::~RenderSharedState() { Close(); }

    void RenderSharedState::StartRenderThread()
    {
        if (Specification.Mode != RenderMode::Rendered || RenderThread.joinable())
            return;
        RenderThread = std::jthread(
            [this]
            {
                RenderThreadId = std::this_thread::get_id();
                for (;;)
                {
                    std::function<void()> work;
                    {
                        std::unique_lock lock(RenderQueueMutex);
                        RenderQueueReady.wait(lock, [&] { return StopRenderQueue || !RenderQueue.empty(); });
                        if (StopRenderQueue && RenderQueue.empty())
                            break;
                        work = std::move(RenderQueue.front());
                        RenderQueue.pop_front();
                    }
                    RenderQueueSpace.notify_one();
                    work();
                }
            });
    }

    void RenderSharedState::StopRenderThread() noexcept
    {
        if (!RenderThread.joinable())
            return;
        {
            std::scoped_lock lock(RenderQueueMutex);
            StopRenderQueue = true;
        }
        RenderQueueReady.notify_all();
        RenderThread.join();
        RenderThreadId = {};
    }

    void RenderSharedState::DispatchRender(std::function<void()> work)
    {
        if (!RenderThread.joinable())
        {
            work();
            return;
        }
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(work));
        auto completion = task->get_future();
        {
            std::unique_lock lock(RenderQueueMutex);
            constexpr std::size_t capacity = 2;
            RenderQueueSpace.wait(lock, [&] { return StopRenderQueue || RenderQueue.size() < capacity; });
            if (StopRenderQueue)
                throw std::logic_error("Renderer submission queue is closed.");
            RenderQueue.push_back([task] { (*task)(); });
            Statistics.RendererQueueHighWaterMark =
                std::max(Statistics.RendererQueueHighWaterMark, static_cast<std::uint32_t>(RenderQueue.size()));
        }
        RenderQueueReady.notify_one();
        completion.get();
    }

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
        if (resources.LocalShadow)
            SDL_ReleaseGPUTexture(Device, resources.LocalShadow);
        if (resources.DirectionalShadow)
            SDL_ReleaseGPUTexture(Device, resources.DirectionalShadow);
        if (resources.Depth)
            SDL_ReleaseGPUTexture(Device, resources.Depth);
        if (resources.SampledDepth)
            SDL_ReleaseGPUTexture(Device, resources.SampledDepth);
        if (resources.MultisampleHdrColor)
            SDL_ReleaseGPUTexture(Device, resources.MultisampleHdrColor);
        for (auto* texture : resources.TransientTextures)
            if (texture)
                SDL_ReleaseGPUTexture(Device, texture);
        if (resources.SampledColor)
            SDL_ReleaseGPUTexture(Device, resources.SampledColor);
        if (resources.ExchangeColor)
            SDL_ReleaseGPUTexture(Device, resources.ExchangeColor);
        resources = {};
    }

    SDL_GPUShader* RenderSharedState::CreateShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = Detail::BuiltinShaderUniformBufferCount(vertex);
        information.num_samplers = vertex ? 0U : 2U;

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

    SDL_GPUShader* RenderSharedState::CreateSkyShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 0U : 1U;
        information.num_samplers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinSkyVertexDxil : Detail::BuiltinSkyFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinSkyVertexDxilSize : Detail::BuiltinSkyFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinSkyVertexMsl : Detail::BuiltinSkyFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinSkyVertexMslSize : Detail::BuiltinSkyFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinSkyVertexSpirV : Detail::BuiltinSkyFragmentSpirV;
            information.code_size = vertex ? Detail::BuiltinSkyVertexSpirVSize : Detail::BuiltinSkyFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported sky shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(sky) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateShadowShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 1U : 0U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinShadowVertexDxil : Detail::BuiltinShadowFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinShadowVertexDxilSize : Detail::BuiltinShadowFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinShadowVertexMsl : Detail::BuiltinShadowFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinShadowVertexMslSize : Detail::BuiltinShadowFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinShadowVertexSpirV : Detail::BuiltinShadowFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinShadowVertexSpirVSize : Detail::BuiltinShadowFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported shadow shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(shadow) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateToneMapShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinToneMapVertexDxil : Detail::BuiltinToneMapFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexDxilSize : Detail::BuiltinToneMapFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinToneMapVertexMsl : Detail::BuiltinToneMapFragmentMsl;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexMslSize : Detail::BuiltinToneMapFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinToneMapVertexSpirV : Detail::BuiltinToneMapFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexSpirVSize : Detail::BuiltinToneMapFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported tone-map shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(tone map) failed: " + LastSdlError());
        return shader;
    }

    void RenderSharedState::EnsureFrameUploadContext()
    {
        if (FrameUploadPass)
            return;
        if (FrameUploadCommands)
            throw std::logic_error("The frame upload command buffer is active without a copy pass.");

        FrameUploadCommands = SDL_AcquireGPUCommandBuffer(Device);
        if (!FrameUploadCommands)
            throw std::runtime_error("SDL_AcquireGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
        FrameUploadPass = SDL_BeginGPUCopyPass(FrameUploadCommands);
        if (!FrameUploadPass)
        {
            (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
            FrameUploadCommands = nullptr;
            throw std::runtime_error("SDL_BeginGPUCopyPass(frame uploads) failed: " + LastSdlError());
        }
    }

    SDL_GPUBuffer* RenderSharedState::UploadBuffer(const std::span<const std::byte> bytes,
                                                   const SDL_GPUBufferUsageFlags usage)
    {
        return UploadBuffer(nullptr, bytes, usage);
    }

    SDL_GPUBuffer* RenderSharedState::UploadBuffer(SDL_GPUCommandBuffer* commands,
                                                   const std::span<const std::byte> bytes,
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

            SDL_GPUTransferBufferLocation source{transfer, 0};
            SDL_GPUBufferRegion destination{buffer, 0, byteSize};
            if (commands)
            {
                auto* copy = SDL_BeginGPUCopyPass(commands);
                if (!copy)
                    throw std::runtime_error("SDL_BeginGPUCopyPass(frame-local upload) failed: " + LastSdlError());
                SDL_UploadToGPUBuffer(copy, &source, &destination, false);
                SDL_EndGPUCopyPass(copy);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return buffer;
            }
            if (FrameExecutionActive && !FrameUploadPass)
                EnsureFrameUploadContext();
            if (FrameUploadPass)
            {
                SDL_UploadToGPUBuffer(FrameUploadPass, &source, &destination, false);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return buffer;
            }

            SDL_GPUCommandBuffer* uploadCommands = SDL_AcquireGPUCommandBuffer(Device);
            if (!uploadCommands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(uploadCommands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                throw std::runtime_error("SDL_BeginGPUCopyPass failed: " + LastSdlError());
            }
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            transfer = nullptr;
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
        return UploadVertexBuffer(nullptr, vertices);
    }

    SDL_GPUBuffer* RenderSharedState::UploadVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                         const std::span<const RenderVertex> vertices)
    {
        std::vector<GpuRenderVertex> gpuVertices;
        gpuVertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            gpuVertices.push_back({
                {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                {vertex.Color.X, vertex.Color.Y, vertex.Color.Z, 1.0F},
                {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F},
            });
        }
        return UploadBuffer(commands, std::as_bytes(std::span(gpuVertices)), SDL_GPU_BUFFERUSAGE_VERTEX);
    }

    SDL_GPUBuffer* RenderSharedState::UploadMeshVertexBuffer(const std::span<const MeshVertex> vertices)
    {
        return UploadMeshVertexBuffer(nullptr, vertices);
    }

    SDL_GPUBuffer* RenderSharedState::UploadMeshVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                             const std::span<const MeshVertex> vertices)
    {
        std::vector<GpuMeshVertex> gpuVertices;
        gpuVertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            gpuVertices.push_back({
                {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F},
                {vertex.UV0.X, vertex.UV0.Y, 0.0F, 0.0F},
                {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue, vertex.VertexColor.Alpha},
                vertex.Tangent,
                {vertex.UV1.X, vertex.UV1.Y, 0.0F, 0.0F},
            });
        }
        return UploadBuffer(commands, std::as_bytes(std::span(gpuVertices)),
                            SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
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
        result.EstimatedBytes = static_cast<std::uint64_t>(vertices.size()) * sizeof(RenderVertex) +
                                static_cast<std::uint64_t>(mesh.Vertices().size()) * sizeof(MeshVertex) +
                                static_cast<std::uint64_t>(mesh.Indices().size()) * sizeof(std::uint32_t);
        try
        {
            result.Vertices = UploadVertexBuffer(vertices);
            result.AssetVertices = UploadMeshVertexBuffer(mesh.Vertices());
            result.Indices = UploadBuffer(std::as_bytes(mesh.Indices()), SDL_GPU_BUFFERUSAGE_INDEX);
            result.IndexCount = static_cast<std::uint32_t>(mesh.Indices().size());
            result.Submeshes.assign(mesh.Submeshes().begin(), mesh.Submeshes().end());
            result.Lods.assign(mesh.Lods().begin(), mesh.Lods().end());
            result.DefaultMaterials.reserve(mesh.MaterialSlots().size());
            for (const auto& slot : mesh.MaterialSlots())
                result.DefaultMaterials.push_back(slot.DefaultMaterial);
            result.ShapeSamples.reserve(mesh.Indices().size() / 3U);
            double cumulativeArea = 0.0;
            for (std::size_t index = 0; index + 2U < mesh.Indices().size(); index += 3U)
            {
                const auto& a = mesh.Vertices()[mesh.Indices()[index]].Position;
                const auto& b = mesh.Vertices()[mesh.Indices()[index + 1U]].Position;
                const auto& c = mesh.Vertices()[mesh.Indices()[index + 2U]].Position;
                const auto edge0 = Vector3{b.X - a.X, b.Y - a.Y, b.Z - a.Z};
                const auto edge1 = Vector3{c.X - a.X, c.Y - a.Y, c.Z - a.Z};
                const auto cross = Vector3{edge0.Y * edge1.Z - edge0.Z * edge1.Y, edge0.Z * edge1.X - edge0.X * edge1.Z,
                                           edge0.X * edge1.Y - edge0.Y * edge1.X};
                const auto area =
                    0.5 * std::sqrt(static_cast<double>(cross.X) * cross.X + static_cast<double>(cross.Y) * cross.Y +
                                    static_cast<double>(cross.Z) * cross.Z);
                if (!std::isfinite(area) || area <= 0.0)
                    continue;
                cumulativeArea += area;
                if (!std::isfinite(cumulativeArea) || cumulativeArea > std::numeric_limits<float>::max())
                    throw std::runtime_error("Mesh surface area exceeds the VFX sampling range.");
                result.ShapeSamples.push_back({{a.X, a.Y, a.Z, 0.0F},
                                               {b.X, b.Y, b.Z, 0.0F},
                                               {c.X, c.Y, c.Z, static_cast<float>(cumulativeArea)}});
            }
            result.ShapeSampleWeight = static_cast<float>(cumulativeArea);
            result.Revision = 1;
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
