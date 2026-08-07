#include "KeireInternal/WindowChromeInternal.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>

#include <SDL3/SDL.h>

namespace Keire::Detail
{
    namespace
    {
        constexpr wchar_t ChromeCacheProperty[] = L"Keire.WindowChromeHitTestCache";

        [[nodiscard]] bool IsResizeRole(const WindowChromeRole role) noexcept
        {
            return role >= WindowChromeRole::ResizeTop && role <= WindowChromeRole::ResizeBottomRight;
        }

        [[nodiscard]] LRESULT CaptionHitResult(const WindowChromeRole role) noexcept
        {
            switch (role)
            {
            case WindowChromeRole::SystemMenu:
                return HTSYSMENU;
            case WindowChromeRole::Minimize:
                return HTMINBUTTON;
            case WindowChromeRole::MaximizeRestore:
                return HTMAXBUTTON;
            case WindowChromeRole::Close:
                return HTCLOSE;
            default:
                return HTCLIENT;
            }
        }

        LRESULT CALLBACK ChromeWindowProcedure(HWND window, const UINT message, const WPARAM wParam,
                                               const LPARAM lParam)
        {
            auto* cache = static_cast<WindowChromeHitTestCache*>(GetPropW(window, ChromeCacheProperty));
            if (!cache)
                return DefWindowProcW(window, message, wParam, lParam);

            const auto previous = reinterpret_cast<WNDPROC>(cache->PlatformProcedure());
            const auto callPrevious = [&]()
            {
                return previous ? CallWindowProcW(previous, window, message, wParam, lParam)
                                : DefWindowProcW(window, message, wParam, lParam);
            };
            if (message == WM_NCHITTEST)
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (ScreenToClient(window, &point))
                {
                    const auto role = cache->RoleAt({point.x, point.y});
                    if (role >= WindowChromeRole::SystemMenu || (IsResizeRole(role) && !cache->Resizable()))
                        return CaptionHitResult(role);
                }
            }
            else if (message == WM_GETMINMAXINFO && lParam != 0)
            {
                // Let SDL retain ownership of minimum tracking dimensions, then constrain borderless maximize to the
                // nearest monitor's work area. Monitor-relative coordinates also handle displays positioned left or
                // above the primary monitor without assuming a non-negative virtual desktop origin.
                const auto result = callPrevious();
                MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};
                const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
                if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
                {
                    auto* sizing = reinterpret_cast<MINMAXINFO*>(lParam);
                    sizing->ptMaxPosition.x = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
                    sizing->ptMaxPosition.y = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
                    sizing->ptMaxSize.x = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
                    sizing->ptMaxSize.y = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
                }
                return result;
            }

            return callPrevious();
        }
    } // namespace

    bool ConfigurePlatformCustomChrome(SDL_Window* window, WindowChromeHitTestCache& cache) noexcept
    {
        if (!window)
            return false;

        const auto properties = SDL_GetWindowProperties(window);
        auto* native =
            static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!native || GetPropW(native, ChromeCacheProperty))
            return false;

        SetLastError(ERROR_SUCCESS);
        const auto previous = GetWindowLongPtrW(native, GWLP_WNDPROC);
        if (previous == 0 && GetLastError() != ERROR_SUCCESS)
            return false;

        cache.SetPlatformProcedure(static_cast<std::uintptr_t>(previous));
        if (!SetPropW(native, ChromeCacheProperty, &cache))
        {
            cache.SetPlatformProcedure(0);
            return false;
        }

        SetLastError(ERROR_SUCCESS);
        const auto replaced =
            SetWindowLongPtrW(native, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ChromeWindowProcedure));
        if (replaced == 0 && GetLastError() != ERROR_SUCCESS)
        {
            RemovePropW(native, ChromeCacheProperty);
            cache.SetPlatformProcedure(0);
            return false;
        }
        return true;
    }

    void ReleasePlatformCustomChrome(SDL_Window* window, WindowChromeHitTestCache& cache) noexcept
    {
        if (!window)
            return;

        const auto properties = SDL_GetWindowProperties(window);
        auto* native =
            static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!native)
            return;

        const auto previous = cache.PlatformProcedure();
        if (previous != 0 &&
            GetWindowLongPtrW(native, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&ChromeWindowProcedure))
            (void)SetWindowLongPtrW(native, GWLP_WNDPROC, static_cast<LONG_PTR>(previous));
        if (GetPropW(native, ChromeCacheProperty) == &cache)
            RemovePropW(native, ChromeCacheProperty);
        cache.SetPlatformProcedure(0);
    }
} // namespace Keire::Detail

#endif
