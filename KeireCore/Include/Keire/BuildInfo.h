#pragma once

#include "Keire/Api.h"

#include <string>
#include <string_view>

namespace Keire
{
    struct BuildInfo
    {
        std::string_view ProjectName;
        std::string_view Version;
        std::string_view RepositorySlug;
        std::string_view GitCommit;
        std::string_view Configuration;
        std::string_view Compiler;
        std::string_view Platform;
        std::string_view Architecture;
        bool Dirty;
    };

    [[nodiscard]] KEIRE_API const BuildInfo& GetBuildInfo() noexcept;
    [[nodiscard]] KEIRE_API std::string GetVersionString();
} // namespace Keire
