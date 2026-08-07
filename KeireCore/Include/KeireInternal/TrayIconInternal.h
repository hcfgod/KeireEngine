#pragma once

#include <SDL3/SDL_surface.h>

#include <filesystem>
#include <memory>

namespace Keire::Detail
{
    using TrayIcon = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    [[nodiscard]] TrayIcon LoadTrayIcon(const std::filesystem::path& path);
} // namespace Keire::Detail
