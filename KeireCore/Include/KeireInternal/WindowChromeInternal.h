#pragma once

#include "Keire/Window.h"

#include <atomic>
#include <cstdint>

struct SDL_Window;

namespace Keire::Detail
{
    [[nodiscard]] WindowChromeRole HitTestWindowChromeLayout(const WindowChromeLayout& layout,
                                                             WindowPosition position) noexcept;

    class WindowChromeHitTestCache final
    {
      public:
        explicit WindowChromeHitTestCache(bool resizable) noexcept : m_Resizable(resizable) {}
        ~WindowChromeHitTestCache();

        void Store(const WindowChromeLayout& layout) noexcept;
        [[nodiscard]] WindowChromeRole RoleAt(WindowPosition position) const noexcept;
        [[nodiscard]] bool BeginCaptionPress(WindowChromeRole role) noexcept;
        [[nodiscard]] bool CompleteCaptionPress(WindowPosition position, WindowChromeRole& activatedRole) noexcept;
        bool CancelCaptionPress() noexcept;
        [[nodiscard]] bool Resizable() const noexcept { return m_Resizable; }
        [[nodiscard]] bool Attach(SDL_Window* window) noexcept;
        void Detach() noexcept;

        void SetPlatformProcedure(std::uintptr_t procedure) noexcept;
        [[nodiscard]] std::uintptr_t PlatformProcedure() const noexcept;

      private:
        void Lock() const noexcept;
        void Unlock() const noexcept;

        mutable std::atomic_flag m_Lock = ATOMIC_FLAG_INIT;
        WindowChromeLayout m_Layout;
        const bool m_Resizable;
        SDL_Window* m_Window = nullptr;
        std::atomic<std::uintptr_t> m_PlatformProcedure{0};
        std::atomic<WindowChromeRole> m_PressedCaptionRole{WindowChromeRole::Client};
    };

    void ValidateWindowSpecification(const WindowSpecification& specification);
    void ValidateWindowSize(LogicalExtent size, const WindowSpecification& specification);
    [[nodiscard]] std::uint64_t NativeWindowFlags(const WindowSpecification& specification) noexcept;
    void ConfigureMinimumWindowSize(SDL_Window* window, const WindowSpecification& specification);
    [[nodiscard]] LogicalExtent ToLogicalExtent(int width, int height) noexcept;
    [[nodiscard]] PixelExtent ToPixelExtent(int width, int height) noexcept;
    [[nodiscard]] LogicalExtent QueryLogicalSize(SDL_Window* window) noexcept;
    [[nodiscard]] PixelExtent QueryPixelSize(SDL_Window* window) noexcept;
    [[nodiscard]] WindowPosition QueryWindowPosition(SDL_Window* window) noexcept;

    [[nodiscard]] bool ConfigurePlatformCustomChrome(SDL_Window* window, WindowChromeHitTestCache& cache) noexcept;
    void ReleasePlatformCustomChrome(SDL_Window* window, WindowChromeHitTestCache& cache) noexcept;
} // namespace Keire::Detail
