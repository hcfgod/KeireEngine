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

        [[nodiscard]] WindowChromeRole CaptionRole(const WPARAM hitResult) noexcept
        {
            switch (hitResult)
            {
            case HTMINBUTTON:
                return WindowChromeRole::Minimize;
            case HTMAXBUTTON:
                return WindowChromeRole::MaximizeRestore;
            case HTCLOSE:
                return WindowChromeRole::Close;
            default:
                return WindowChromeRole::Client;
            }
        }

        void PerformCaptionAction(HWND window, const WindowChromeRole role) noexcept
        {
            switch (role)
            {
            case WindowChromeRole::Minimize:
                (void)PostMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                break;
            case WindowChromeRole::MaximizeRestore:
                (void)PostMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
                break;
            case WindowChromeRole::Close:
                (void)PostMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
                break;
            default:
                break;
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
            else if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK)
            {
                // Native caption hit results keep Windows 11 Snap Layouts available, but they also remove the press
                // from SDL's client input stream. Own the complete transaction so the rendered button cannot miss it.
                if (cache->BeginCaptionPress(CaptionRole(wParam)))
                {
                    (void)SetCapture(window);
                    return 0;
                }
            }
            else if (message == WM_LBUTTONUP || message == WM_NCLBUTTONUP)
            {
                POINT point{};
                WindowChromeRole role = WindowChromeRole::Client;
                bool completedCaptionPress = false;
                if (GetCursorPos(&point) && ScreenToClient(window, &point))
                    completedCaptionPress = cache->CompleteCaptionPress({point.x, point.y}, role);
                else
                    completedCaptionPress = cache->CancelCaptionPress();

                if (GetCapture() == window)
                    (void)ReleaseCapture();
                if (completedCaptionPress)
                {
                    PerformCaptionAction(window, role);
                    return 0;
                }
            }
            else if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
            {
                (void)cache->CancelCaptionPress();
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
