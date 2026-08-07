#include "Keire/Window.h"

#include <SDL3/SDL_video.h>

namespace Keire
{
    SystemColorScheme GetSystemColorScheme() noexcept
    {
        switch (SDL_GetSystemTheme())
        {
        case SDL_SYSTEM_THEME_LIGHT:
            return SystemColorScheme::Light;
        case SDL_SYSTEM_THEME_DARK:
            return SystemColorScheme::Dark;
        case SDL_SYSTEM_THEME_UNKNOWN:
            return SystemColorScheme::Unknown;
        }
        return SystemColorScheme::Unknown;
    }
} // namespace Keire
