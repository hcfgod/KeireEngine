#pragma once

#include "KeireInternal/UiContextAccessInternal.h"

#include <SDL3/SDL_gpu.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_set>
#include <vector>

struct SDL_Window;

namespace Keire
{
    class RenderSystem;

    namespace Detail
    {
        // Device loss invalidates every SDL GPU handle. Detach the Dear ImGui renderer backend without invoking any
        // SDL release path so its CPU layout, fonts, and registered image pixels can be reused by a new device.
        void AbandonLostGpuBackend(ImGuiContext* context) noexcept;

        class UiImageOwner final
        {
          public:
            [[nodiscard]] ImTextureData* Create(std::uint32_t width, std::uint32_t height,
                                                std::span<const std::byte> pixels);
            void Retire(ImTextureData* texture) noexcept;
            void ProcessRetired();
            void Close() noexcept;

            void Bind(const std::shared_ptr<UiContextAccess>& contextAccess, RenderSystem* renderer,
                      SDL_GPUDevice* device) noexcept;
            void SetDevice(SDL_GPUDevice* device) noexcept;

          private:
            std::mutex m_Mutex;
            std::shared_ptr<UiContextAccess> m_ContextAccess;
            SDL_GPUDevice* m_Device = nullptr;
            RenderSystem* m_Renderer = nullptr;
            std::unordered_set<ImTextureData*> m_Active;
            std::vector<ImTextureData*> m_Retired;
            bool m_Open = true;
        };

        class UiRenderBackend final
        {
          public:
            UiRenderBackend() = default;
            ~UiRenderBackend();

            UiRenderBackend(const UiRenderBackend&) = delete;
            UiRenderBackend& operator=(const UiRenderBackend&) = delete;

            void Initialize(RenderSystem& renderer, const std::shared_ptr<UiContextAccess>& contextAccess,
                            const std::shared_ptr<UiImageOwner>& images);
            void NewFrame();
            void Shutdown() noexcept;

          private:
            void BeforeDeviceRecovery();
            void AfterDeviceRecovery(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat,
                                     SDL_GPUPresentMode presentMode);

            std::shared_ptr<UiContextAccess> m_ContextAccess;
            SDL_Window* m_NativeWindow = nullptr;
            SDL_GPUDevice* m_Device = nullptr;
            SDL_GPUPresentMode m_PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
            RenderSystem* m_Renderer = nullptr;
            std::shared_ptr<UiImageOwner> m_Images;
            bool m_Initialized = false;
        };
    } // namespace Detail
} // namespace Keire
