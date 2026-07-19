#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <SDL3/SDL_gpu.h>

struct ImDrawData;
struct SDL_Window;

namespace Keire
{
    class RenderSystemInternalAccess final
    {
      public:
        [[nodiscard]] static SDL_GPUDevice* Device(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_Window* NativeWindow(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_GPUPresentMode PresentMode(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_GPUTexture* Texture(const RenderSurface& surface) noexcept;
        [[nodiscard]] static void* SurfaceState(RenderSurface& surface) noexcept;
        static void WaitIdle(RenderSystem& renderer) noexcept;
        static void BeginFrame(RenderSystem& renderer);
        static void EndFrame(RenderSystem& renderer, ImDrawData* drawData);
    };
} // namespace Keire
