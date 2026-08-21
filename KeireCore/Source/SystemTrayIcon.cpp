#include "KeireInternal/TrayIconInternal.h"

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] TrayIcon LoadIcon(const std::filesystem::path& path, const int maximumDimension)
        {
            if (path.empty())
                return {nullptr, SDL_DestroySurface};
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                return {nullptr, SDL_DestroySurface};
            const auto size = input.tellg();
            if (size <= 0 || size > std::streamoff{16} * 1024 * 1024)
                return {nullptr, SDL_DestroySurface};
            std::vector<std::byte> encoded(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(encoded.data()), size);
            if (!input)
                return {nullptr, SDL_DestroySurface};
            int width = 0;
            int height = 0;
            int channels = 0;
            auto* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                                  static_cast<int>(encoded.size()), &width, &height, &channels, 4);
            if (!decoded || width <= 0 || height <= 0)
            {
                stbi_image_free(decoded);
                return {nullptr, SDL_DestroySurface};
            }
            TrayIcon result(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (result)
                for (int row = 0; row < height; ++row)
                    std::memcpy(
                        static_cast<std::byte*>(result->pixels) + static_cast<std::ptrdiff_t>(row) * result->pitch,
                        decoded + static_cast<std::ptrdiff_t>(row) * width * 4, static_cast<std::size_t>(width) * 4);
            stbi_image_free(decoded);
            if (!result || maximumDimension <= 0 || (width <= maximumDimension && height <= maximumDimension))
                return result;

            const auto scale = static_cast<double>(maximumDimension) / static_cast<double>(std::max(width, height));
            const int scaledWidth = std::max(1, static_cast<int>(static_cast<double>(width) * scale));
            const int scaledHeight = std::max(1, static_cast<int>(static_cast<double>(height) * scale));
            return {SDL_ScaleSurface(result.get(), scaledWidth, scaledHeight, SDL_SCALEMODE_LINEAR),
                    SDL_DestroySurface};
        }
    } // namespace

    TrayIcon LoadTrayIcon(const std::filesystem::path& path) { return LoadIcon(path, 0); }

    TrayIcon LoadWindowIcon(const std::filesystem::path& path) { return LoadIcon(path, 256); }
} // namespace Keire::Detail
