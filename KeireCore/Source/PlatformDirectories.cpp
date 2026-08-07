#include "Keire/PlatformDirectories.h"

#include "Keire/BuildInfo.h"

#include <SDL3/SDL_filesystem.h>

#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::filesystem::path UserFolder(const SDL_Folder folder)
        {
            const char* value = SDL_GetUserFolder(folder);
            if (!value || *value == '\0')
                throw std::runtime_error("Cannot resolve the requested per-user directory.");
            std::filesystem::path result(value);
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
        const std::filesystem::path result(preference);
        SDL_free(preference);
        return result;
    }

    std::filesystem::path GetUserDocumentsDirectory()
    {
        if (const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS); documents && *documents != '\0')
        {
            std::filesystem::path result(documents);
            if (result.is_absolute())
                return result;
        }
        return UserFolder(SDL_FOLDER_HOME);
    }
} // namespace Keire
