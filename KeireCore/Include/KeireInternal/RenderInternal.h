#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <vector>

struct ImDrawData;
struct SDL_Window;

namespace Keire::Detail
{
    [[nodiscard]] constexpr std::uint32_t BuiltinShaderUniformBufferCount(const bool vertex) noexcept
    {
        return vertex ? 1U : 2U;
    }
} // namespace Keire::Detail

namespace Keire
{
    class RenderSystemInternalAccess final
    {
      public:
        [[nodiscard]] static SDL_GPUDevice* Device(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_Window* NativeWindow(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_GPUPresentMode PresentMode(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_GPUTexture* Texture(const RenderSurface& surface) noexcept;
        [[nodiscard]] static std::vector<std::uint8_t> ReadbackRGBA8(RenderSystem& renderer,
                                                                     const RenderSurface& surface);
        [[nodiscard]] static std::vector<float>
        ReadbackDirectionalShadow(RenderSystem& renderer, const RenderSurface& surface, std::uint32_t layer);
        [[nodiscard]] static std::vector<float> ReadbackLocalShadow(RenderSystem& renderer,
                                                                    const RenderSurface& surface, std::uint32_t layer);
        static void InjectDeviceLoss(RenderSystem& renderer);
        [[nodiscard]] static std::uint32_t SaturateRendererQueue(RenderSystem& renderer);
        static void RequestSurfaceSize(RenderSurface& surface, std::uint32_t width, std::uint32_t height);
        static void SetPresentationSurface(RenderSystem& renderer, const Ref<RenderSurface>& surface);
        [[nodiscard]] static std::size_t RuntimeUiCommandCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static void* SurfaceState(RenderSurface& surface) noexcept;
        static void WaitIdle(RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t MaterialBindingBuildCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t SkinningStaticBuildCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t SkinningOutputBuildCount(const RenderSystem& renderer) noexcept;
        static void BeginFrame(RenderSystem& renderer);
        static void CancelFrame(RenderSystem& renderer) noexcept;
        static void EndFrame(RenderSystem& renderer, ImDrawData* drawData);
    };
} // namespace Keire
