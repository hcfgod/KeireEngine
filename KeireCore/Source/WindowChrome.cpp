#include "KeireInternal/WindowChromeInternal.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>
#include <thread>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
        }

        SDL_HitTestResult SDLCALL CustomChromeHitTest(SDL_Window*, const SDL_Point* point, void* context) noexcept
        {
            auto* cache = static_cast<WindowChromeHitTestCache*>(context);
            if (!cache || !point)
                return SDL_HITTEST_NORMAL;

            const auto role = cache->RoleAt({point->x, point->y});
            if (!cache->Resizable() && role >= WindowChromeRole::ResizeTop &&
                role <= WindowChromeRole::ResizeBottomRight)
            {
                return SDL_HITTEST_NORMAL;
            }

            switch (role)
            {
            case WindowChromeRole::Drag:
                return SDL_HITTEST_DRAGGABLE;
            case WindowChromeRole::ResizeTop:
                return SDL_HITTEST_RESIZE_TOP;
            case WindowChromeRole::ResizeBottom:
                return SDL_HITTEST_RESIZE_BOTTOM;
            case WindowChromeRole::ResizeLeft:
                return SDL_HITTEST_RESIZE_LEFT;
            case WindowChromeRole::ResizeRight:
                return SDL_HITTEST_RESIZE_RIGHT;
            case WindowChromeRole::ResizeTopLeft:
                return SDL_HITTEST_RESIZE_TOPLEFT;
            case WindowChromeRole::ResizeTopRight:
                return SDL_HITTEST_RESIZE_TOPRIGHT;
            case WindowChromeRole::ResizeBottomLeft:
                return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            case WindowChromeRole::ResizeBottomRight:
                return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            default:
                return SDL_HITTEST_NORMAL;
            }
        }
    } // namespace

    void ValidateWindowSpecification(const WindowSpecification& specification)
    {
        if (specification.Title.empty() || specification.Title.size() > 1024)
            throw std::invalid_argument("Window title must contain 1..1024 UTF-8 bytes.");
        if (specification.Width < 1 || specification.Width > 16384 || specification.Height < 1 ||
            specification.Height > 16384)
            throw std::invalid_argument("Window dimensions must be in the range 1..16384.");
        if (specification.MinimumWidth < 1 || specification.MinimumWidth > 16384 || specification.MinimumHeight < 1 ||
            specification.MinimumHeight > 16384)
            throw std::invalid_argument("Minimum window dimensions must be in the range 1..16384.");
        if (specification.MinimumWidth > specification.Width || specification.MinimumHeight > specification.Height)
            throw std::invalid_argument("Minimum window dimensions cannot exceed the initial window dimensions.");
        if (specification.Decoration > WindowDecoration::Custom)
            throw std::invalid_argument("Window decoration is invalid.");
        if (specification.Maximized && specification.Mode == WindowMode::BorderlessFullscreen)
            throw std::invalid_argument("A borderless fullscreen window cannot also start maximized.");
    }

    void ValidateWindowSize(const LogicalExtent size, const WindowSpecification& specification)
    {
        if (size.Width < 1 || size.Width > 16384 || size.Height < 1 || size.Height > 16384)
            throw std::invalid_argument("Window dimensions must be in the range 1..16384.");
        if (size.Width < specification.MinimumWidth || size.Height < specification.MinimumHeight)
            throw std::invalid_argument("Window dimensions cannot be smaller than the configured minimum.");
    }

    std::uint64_t NativeWindowFlags(const WindowSpecification& specification) noexcept
    {
        SDL_WindowFlags flags = 0;
        if (specification.Resizable)
            flags |= SDL_WINDOW_RESIZABLE;
        if (specification.HighPixelDensity)
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!specification.Visible)
            flags |= SDL_WINDOW_HIDDEN;
        if (specification.Maximized)
            flags |= SDL_WINDOW_MAXIMIZED;
        if (specification.Mode == WindowMode::BorderlessFullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;
#if !defined(__APPLE__)
        if (specification.Decoration == WindowDecoration::Custom)
            flags |= SDL_WINDOW_BORDERLESS;
#endif
        return flags;
    }

    void ConfigureMinimumWindowSize(SDL_Window* window, const WindowSpecification& specification)
    {
        if (!SDL_SetWindowMinimumSize(window, static_cast<int>(specification.MinimumWidth),
                                      static_cast<int>(specification.MinimumHeight)))
        {
            throw WindowError("SDL_SetWindowMinimumSize", LastSdlError());
        }
    }

    LogicalExtent ToLogicalExtent(const int width, const int height) noexcept
    {
        return {static_cast<std::uint32_t>(width > 0 ? width : 0), static_cast<std::uint32_t>(height > 0 ? height : 0)};
    }

    PixelExtent ToPixelExtent(const int width, const int height) noexcept
    {
        return {static_cast<std::uint32_t>(width > 0 ? width : 0), static_cast<std::uint32_t>(height > 0 ? height : 0)};
    }

    LogicalExtent QueryLogicalSize(SDL_Window* window) noexcept
    {
        int width = 0;
        int height = 0;
        return SDL_GetWindowSize(window, &width, &height) ? ToLogicalExtent(width, height) : LogicalExtent{};
    }

    PixelExtent QueryPixelSize(SDL_Window* window) noexcept
    {
        int width = 0;
        int height = 0;
        return SDL_GetWindowSizeInPixels(window, &width, &height) ? ToPixelExtent(width, height) : PixelExtent{};
    }

    WindowPosition QueryWindowPosition(SDL_Window* window) noexcept
    {
        int x = 0;
        int y = 0;
        return SDL_GetWindowPosition(window, &x, &y) ? WindowPosition{x, y} : WindowPosition{};
    }

    WindowChromeRole HitTestWindowChromeLayout(const WindowChromeLayout& layout, const WindowPosition position) noexcept
    {
        const auto regions = layout.Regions();
        for (std::size_t index = regions.size(); index > 0; --index)
        {
            const auto& region = regions[index - 1];
            if (region.Bounds.Contains(position))
                return region.Role;
        }
        return WindowChromeRole::Client;
    }

    WindowChromeHitTestCache::~WindowChromeHitTestCache() { Detach(); }

    void WindowChromeHitTestCache::Store(const WindowChromeLayout& layout) noexcept
    {
        Lock();
        m_Layout = layout;
        Unlock();
    }

    WindowChromeRole WindowChromeHitTestCache::RoleAt(const WindowPosition position) const noexcept
    {
        Lock();
        const auto result = HitTestWindowChromeLayout(m_Layout, position);
        Unlock();
        return result;
    }

    bool WindowChromeHitTestCache::Attach(SDL_Window* window) noexcept
    {
        if (!window || m_Window)
            return false;

        const bool hitTestInstalled = SDL_SetWindowHitTest(window, CustomChromeHitTest, this);
        const bool platformConfigured = hitTestInstalled && ConfigurePlatformCustomChrome(window, *this);
        if (!platformConfigured)
        {
            ReleasePlatformCustomChrome(window, *this);
            (void)SDL_SetWindowHitTest(window, nullptr, nullptr);
#if !defined(__APPLE__)
            (void)SDL_SetWindowBordered(window, true);
#endif
            return false;
        }

        m_Window = window;
        return true;
    }

    void WindowChromeHitTestCache::Detach() noexcept
    {
        if (!m_Window)
            return;
        ReleasePlatformCustomChrome(m_Window, *this);
        (void)SDL_SetWindowHitTest(m_Window, nullptr, nullptr);
        m_Window = nullptr;
    }

    void WindowChromeHitTestCache::SetPlatformProcedure(const std::uintptr_t procedure) noexcept
    {
        m_PlatformProcedure.store(procedure, std::memory_order_release);
    }

    std::uintptr_t WindowChromeHitTestCache::PlatformProcedure() const noexcept
    {
        return m_PlatformProcedure.load(std::memory_order_acquire);
    }

    void WindowChromeHitTestCache::Lock() const noexcept
    {
        while (m_Lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
    }

    void WindowChromeHitTestCache::Unlock() const noexcept { m_Lock.clear(std::memory_order_release); }

#if !defined(_WIN32) && !defined(__APPLE__)
    bool ConfigurePlatformCustomChrome(SDL_Window*, WindowChromeHitTestCache&) noexcept { return true; }

    void ReleasePlatformCustomChrome(SDL_Window*, WindowChromeHitTestCache&) noexcept {}
#endif
} // namespace Keire::Detail
