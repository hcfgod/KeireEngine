#include "Core/BuildInfo.h"

#include "Core/BuildInfo.generated.h"

#include <algorithm>
#include <sstream>

#define CORE_STRINGIZE_DETAIL(value) #value
#define CORE_STRINGIZE(value) CORE_STRINGIZE_DETAIL(value)

#if defined(_MSC_VER)
#define CORE_BUILD_COMPILER "MSVC " CORE_STRINGIZE(_MSC_VER)
#elif defined(__clang__)
#define CORE_BUILD_COMPILER                                                                                            \
    "Clang " CORE_STRINGIZE(__clang_major__) "." CORE_STRINGIZE(__clang_minor__) "." CORE_STRINGIZE(                   \
        __clang_patchlevel__)
#elif defined(__GNUC__)
#define CORE_BUILD_COMPILER                                                                                            \
    "GCC " CORE_STRINGIZE(__GNUC__) "." CORE_STRINGIZE(__GNUC_MINOR__) "." CORE_STRINGIZE(__GNUC_PATCHLEVEL__)
#else
#define CORE_BUILD_COMPILER "Unknown"
#endif

#if defined(_WIN32)
#define CORE_BUILD_PLATFORM "Windows"
#elif defined(__APPLE__)
#define CORE_BUILD_PLATFORM "macOS"
#elif defined(__linux__)
#define CORE_BUILD_PLATFORM "Linux"
#else
#define CORE_BUILD_PLATFORM "Unknown"
#endif

namespace Core
{
    const BuildInfo& GetBuildInfo() noexcept
    {
        static constexpr BuildInfo Info{CORE_BUILD_PROJECT_NAME,  CORE_BUILD_PROJECT_VERSION, CORE_BUILD_GIT_COMMIT,
                                        CORE_BUILD_CONFIGURATION, CORE_BUILD_COMPILER,        CORE_BUILD_PLATFORM,
                                        CORE_BUILD_ARCHITECTURE,  CORE_BUILD_GIT_DIRTY};
        return Info;
    }

    std::string GetVersionString()
    {
        const auto& info = GetBuildInfo();
        const auto commitLength = std::min<std::size_t>(12, info.GitCommit.size());
        std::ostringstream stream;
        stream << info.Version << " (" << info.GitCommit.substr(0, commitLength);
        if (info.Dirty)
        {
            stream << "-dirty";
        }
        stream << ", " << info.Configuration << ", " << info.Compiler << ", " << info.Platform << ' ' << info.Architecture
               << ')';
        return stream.str();
    }
} // namespace Core
