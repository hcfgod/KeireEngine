#ifndef CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_BUILD_INFO_H
#define CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_BUILD_INFO_H

#include "Core/Api.h"

#include <string>
#include <string_view>

namespace Core
{
    struct BuildInfo
    {
        std::string_view ProjectName;
        std::string_view Version;
        std::string_view GitCommit;
        std::string_view Configuration;
        std::string_view Compiler;
        std::string_view Platform;
        std::string_view Architecture;
        bool Dirty;
    };

    CORE_API const BuildInfo& GetBuildInfo() noexcept;
    CORE_API std::string GetVersionString();
} // namespace Core

#endif
