#include "KeireInternal/WindowChromeInternal.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>

namespace Keire::Detail
{
    bool ConfigurePlatformCustomChrome(SDL_Window* window, WindowChromeHitTestCache&) noexcept
    {
        @autoreleasepool
        {
            if (!window)
                return false;

            const auto properties = SDL_GetWindowProperties(window);
            NSWindow* native =
                (__bridge NSWindow*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
            if (!native)
                return false;

            native.titleVisibility = NSWindowTitleHidden;
            native.titlebarAppearsTransparent = YES;
            native.styleMask |= NSWindowStyleMaskFullSizeContentView;
            native.movableByWindowBackground = NO;
            return true;
        }
    }

    void ReleasePlatformCustomChrome(SDL_Window*, WindowChromeHitTestCache&) noexcept {}
} // namespace Keire::Detail

#endif
