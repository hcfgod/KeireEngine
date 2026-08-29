#include "KeireInternal/UiRenderBackendInternal.h"

#include "KeireInternal/RenderInternal.h"

#include "Keire/Ui.h"

#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        namespace
        {
            [[nodiscard]] std::string LastSdlError()
            {
                const char* error = SDL_GetError();
                return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
            }

            class ScopedTextureRequestDetachment final
            {
              public:
                ScopedTextureRequestDetachment() : m_Platform(ImGui::GetPlatformIO())
                {
                    m_SavedTextures.swap(m_Platform.Textures);
                }

                ~ScopedTextureRequestDetachment() { m_SavedTextures.swap(m_Platform.Textures); }

                ScopedTextureRequestDetachment(const ScopedTextureRequestDetachment&) = delete;
                ScopedTextureRequestDetachment& operator=(const ScopedTextureRequestDetachment&) = delete;

              private:
                ImGuiPlatformIO& m_Platform;
                ImVector<ImTextureData*> m_SavedTextures;
            };

            void InitializeGpuBackend(SDL_GPUDevice* device, const SDL_GPUTextureFormat colorFormat,
                                      const SDL_GPUPresentMode presentMode, const char* operation)
            {
                ImGui_ImplSDLGPU3_InitInfo information{};
                information.Device = device;
                information.ColorTargetFormat = colorFormat;
                information.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
                information.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
                information.PresentMode = presentMode;
                if (!ImGui_ImplSDLGPU3_Init(&information))
                    throw UiError(operation, LastSdlError());

                // Device recovery retries the interrupted immutable packet before the owner can begin another UI
                // frame. Build the backend's samplers, pipeline, and shaders on the render thread now so that retry
                // never observes the freshly initialized backend's null lazy resources. The SDL backend begins by
                // invalidating PlatformIO::Textures; those are owner-thread producer records in Kéire's staged
                // renderer, so keep them detached while the new, empty backend initializes its device objects.
                const ScopedTextureRequestDetachment detachedTextureRequests;
                ImGui_ImplSDLGPU3_CreateDeviceObjects();
            }
        } // namespace

        void AbandonLostGpuBackend(ImGuiContext* context) noexcept
        {
            if (!context)
                return;

            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            void* backendData = std::exchange(io.BackendRendererUserData, nullptr);
            if (!backendData)
                return;

            ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
            platform.Renderer_RenderState = nullptr;
            // PlatformIO::Textures contains owner-thread producer records. Immutable packets and the render-thread
            // texture cache own every SDL texture, so device loss must not alter the live records' logical IDs,
            // status, pixels, or dirty rectangles.
            for (ImGuiViewport* viewport : platform.Viewports)
            {
                if (viewport)
                    viewport->RendererUserData = nullptr;
            }

            io.BackendRendererName = nullptr;
            io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures |
                                 ImGuiBackendFlags_RendererHasViewports);
            platform.ClearRendererHandlers();

            // imgui_impl_sdlgpu3 allocates a trivially destructible, private POD block with IM_NEW. Its public
            // shutdown routine is intentionally not used here because it releases handles belonging to the lost
            // device. The pinned backend has no owned C++ objects, so freeing the block directly is the safe abandon
            // operation; the raw SDL handles are invalid and deliberately never touched again.
            ImGui::MemFree(backendData);
        }

        ImTextureData* UiImageOwner::Create(const std::uint32_t width, const std::uint32_t height,
                                            const std::span<const std::byte> pixels)
        {
            std::shared_ptr<UiContextAccess> contextAccess;
            {
                std::scoped_lock lock(m_Mutex);
                contextAccess = m_ContextAccess;
            }
            if (!contextAccess)
                throw std::logic_error("UI image owner is closed.");
            const auto contextLock = contextAccess->Acquire();
            std::scoped_lock lock(m_Mutex);
            if (!m_Open || m_ContextAccess != contextAccess)
                throw std::logic_error("UI image owner is closed.");
            auto texture = std::make_unique<ImTextureData>();
            texture->Create(ImTextureFormat_RGBA32, static_cast<int>(width), static_cast<int>(height));
            std::memcpy(texture->GetPixels(), pixels.data(), pixels.size());
            ImGui::RegisterUserTexture(texture.get());
            ImTextureData* result = texture.release();
            m_Active.insert(result);
            return result;
        }

        void UiImageOwner::Retire(ImTextureData* texture) noexcept
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Open && texture && m_Active.contains(texture) &&
                std::ranges::find(m_Retired, texture) == m_Retired.end())
                m_Retired.push_back(texture);
        }

        void UiImageOwner::ProcessRetired()
        {
            std::shared_ptr<UiContextAccess> contextAccess;
            {
                std::scoped_lock lock(m_Mutex);
                contextAccess = m_ContextAccess;
            }
            if (!contextAccess)
                return;
            const auto contextLock = contextAccess->Acquire();
            std::vector<ImTextureData*> retired;
            {
                std::scoped_lock lock(m_Mutex);
                if (!m_Open || m_ContextAccess != contextAccess)
                    return;
                retired.swap(m_Retired);
            }
            if (retired.empty())
                return;
            std::vector<std::uintptr_t> logicalTextureIds;
            logicalTextureIds.reserve(retired.size());
            for (const auto* texture : retired)
            {
                if (!texture)
                    continue;
                const auto logicalId = texture->GetTexID();
                if (logicalId != ImTextureID_Invalid)
                    logicalTextureIds.push_back(static_cast<std::uintptr_t>(logicalId));
            }
            try
            {
                if (!logicalTextureIds.empty() && m_Renderer && m_Renderer->IsOpen())
                    RenderSystemInternalAccess::QueueUiTextureRetirements(*m_Renderer, logicalTextureIds);
            }
            catch (...)
            {
                std::scoped_lock lock(m_Mutex);
                for (auto* texture : retired)
                {
                    if (texture && m_Active.contains(texture) &&
                        std::ranges::find(m_Retired, texture) == m_Retired.end())
                    {
                        m_Retired.push_back(texture);
                    }
                }
                throw;
            }
            for (auto* texture : retired)
            {
                {
                    std::scoped_lock lock(m_Mutex);
                    if (!m_Active.erase(texture))
                        continue;
                }
                ImGui::UnregisterUserTexture(texture);
                delete texture;
            }
        }

        void UiImageOwner::Close() noexcept
        {
            std::shared_ptr<UiContextAccess> contextAccess;
            {
                std::scoped_lock lock(m_Mutex);
                if (!m_Open)
                    return;
                contextAccess = m_ContextAccess;
            }
            if (!contextAccess)
                return;
            try
            {
                const auto contextLock = contextAccess->Acquire();
                std::scoped_lock lock(m_Mutex);
                if (!m_Open || m_ContextAccess != contextAccess)
                    return;
                m_Open = false;
                for (auto* texture : m_Active)
                {
                    ImGui::UnregisterUserTexture(texture);
                    delete texture;
                }
            }
            catch (...)
            {
            }
            std::scoped_lock lock(m_Mutex);
            m_Open = false;
            m_Active.clear();
            m_Retired.clear();
            m_ContextAccess.reset();
            m_Device = nullptr;
            m_Renderer = nullptr;
        }

        void UiImageOwner::Bind(const std::shared_ptr<UiContextAccess>& contextAccess, RenderSystem* renderer,
                                SDL_GPUDevice* device) noexcept
        {
            std::scoped_lock lock(m_Mutex);
            m_ContextAccess = contextAccess;
            m_Renderer = renderer;
            m_Device = device;
        }

        void UiImageOwner::SetDevice(SDL_GPUDevice* device) noexcept
        {
            std::scoped_lock lock(m_Mutex);
            m_Device = device;
        }

        UiRenderBackend::~UiRenderBackend() { Shutdown(); }

        void UiRenderBackend::Initialize(RenderSystem& renderer, const std::shared_ptr<UiContextAccess>& contextAccess,
                                         const std::shared_ptr<UiImageOwner>& images)
        {
            if (!contextAccess)
                throw std::invalid_argument("The rendered UI backend requires shared context access.");
            m_ContextAccess = contextAccess;
            m_Renderer = &renderer;
            m_Images = images;
            m_NativeWindow = RenderSystemInternalAccess::NativeWindow(renderer);
            if (!m_NativeWindow)
                throw UiError("ResolveNativeWindow", "the primary window is not available");
            m_Device = RenderSystemInternalAccess::Device(renderer);
            if (!m_Device)
                throw UiError("ResolveGpuDevice", "the application renderer is not in rendered mode");
            m_PresentMode = RenderSystemInternalAccess::PresentMode(renderer);
            m_Images->Bind(contextAccess, &renderer, m_Device);
            RenderSystemInternalAccess::SetUiContextAccess(renderer, contextAccess);

            try
            {
                RenderSystemInternalAccess::RunOnRenderThread(
                    renderer,
                    [this]
                    {
                        const auto contextLock = m_ContextAccess->Acquire();
                        InitializeGpuBackend(m_Device, SDL_GetGPUSwapchainTextureFormat(m_Device, m_NativeWindow),
                                             m_PresentMode, "ImGui_ImplSDLGPU3_Init");
                        m_Initialized = true;
                    });
            }
            catch (...)
            {
                RenderSystemInternalAccess::SetUiContextAccess(renderer, {});
                throw;
            }

            RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                renderer, [this] { BeforeDeviceRecovery(); },
                [this](SDL_GPUDevice* device, const SDL_GPUTextureFormat colorFormat,
                       const SDL_GPUPresentMode presentMode)
                { AfterDeviceRecovery(device, colorFormat, presentMode); });
        }

        void UiRenderBackend::NewFrame()
        {
            const auto contextLock = m_ContextAccess->Acquire();
            if (!m_Initialized)
                throw std::logic_error("The rendered UI backend is not initialized.");
            ImGui_ImplSDLGPU3_NewFrame();
        }

        void UiRenderBackend::Shutdown() noexcept
        {
            if (m_Renderer && m_Renderer->IsOpen())
            {
                try
                {
                    RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(*m_Renderer, {}, {});
                }
                catch (...)
                {
                }
            }
            if (m_Initialized)
            {
                bool shutdownOnRenderThread = false;
                try
                {
                    if (m_Renderer && m_Renderer->IsOpen() && m_Renderer->DeviceState() == RenderDeviceState::Running)
                    {
                        RenderSystemInternalAccess::RunOnRenderThread(
                            *m_Renderer,
                            [this]
                            {
                                const auto contextLock = m_ContextAccess->Acquire();
                                const ScopedTextureRequestDetachment detachedTextureRequests;
                                ImGui_ImplSDLGPU3_Shutdown();
                            });
                        shutdownOnRenderThread = true;
                    }
                }
                catch (...)
                {
                }
                if (!shutdownOnRenderThread)
                {
                    // The renderer may already have closed, or the current generation may be lost. Its GPU handles
                    // can no longer be released safely, but the ImGui context still requires its backend registration
                    // to be detached before destruction.
                    try
                    {
                        const auto contextLock = m_ContextAccess->Acquire();
                        AbandonLostGpuBackend(ImGui::GetCurrentContext());
                    }
                    catch (...)
                    {
                    }
                }
                m_Initialized = false;
            }
            if (m_Images)
                m_Images->SetDevice(nullptr);
            if (m_Renderer)
            {
                try
                {
                    RenderSystemInternalAccess::SetUiContextAccess(*m_Renderer, {});
                }
                catch (...)
                {
                }
            }
            m_NativeWindow = nullptr;
            m_Device = nullptr;
            m_Renderer = nullptr;
            m_Images.reset();
            m_ContextAccess.reset();
        }

        void UiRenderBackend::BeforeDeviceRecovery()
        {
            const auto contextLock = m_ContextAccess->Acquire();
            if (m_Initialized)
            {
                AbandonLostGpuBackend(ImGui::GetCurrentContext());
                m_Initialized = false;
            }
            m_Device = nullptr;
            m_Images->SetDevice(nullptr);
        }

        void UiRenderBackend::AfterDeviceRecovery(SDL_GPUDevice* device, const SDL_GPUTextureFormat colorFormat,
                                                  const SDL_GPUPresentMode presentMode)
        {
            const auto contextLock = m_ContextAccess->Acquire();
            InitializeGpuBackend(device, colorFormat, presentMode, "ImGui_ImplSDLGPU3_Init(recovery)");
            ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
            for (int index = 1; index < platform.Viewports.Size; ++index)
            {
                ImGuiViewport* viewport = platform.Viewports[index];
                if (viewport && !viewport->RendererUserData && platform.Renderer_CreateWindow)
                    platform.Renderer_CreateWindow(viewport);
            }
            m_Device = device;
            m_PresentMode = presentMode;
            m_Images->SetDevice(device);
            m_Initialized = true;
        }
    } // namespace Detail
} // namespace Keire
