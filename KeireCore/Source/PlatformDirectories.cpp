#include "Keire/PlatformDirectories.h"

#include "Keire/BuildInfo.h"

#include <SDL3/SDL_filesystem.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view value)
        {
            const auto* first = reinterpret_cast<const char8_t*>(value.data());
            return std::filesystem::path(std::u8string(first, first + value.size()));
        }

        [[nodiscard]] std::filesystem::path UserFolder(const SDL_Folder folder)
        {
            const char* value = SDL_GetUserFolder(folder);
            if (!value || *value == '\0')
                throw std::runtime_error("Cannot resolve the requested per-user directory.");
            const auto result = PathFromUtf8(value);
            if (!result.is_absolute())
                throw std::runtime_error("The platform returned a non-absolute per-user directory.");
            return result;
        }
    } // namespace

    std::filesystem::path GetPreferenceDirectory()
    {
        const auto productName = std::string(GetBuildInfo().ProjectName);
        char* preference = SDL_GetPrefPath(productName.c_str(), productName.c_str());
        if (!preference)
            throw std::runtime_error("Cannot resolve the per-user preference directory.");
        const auto result = PathFromUtf8(preference);
        SDL_free(preference);
        return result;
    }

    std::filesystem::path GetUserDocumentsDirectory()
    {
        if (const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS); documents && *documents != '\0')
        {
            const auto result = PathFromUtf8(documents);
            if (result.is_absolute())
                return result;
        }
        return UserFolder(SDL_FOLDER_HOME);
    }
} // namespace Keire
