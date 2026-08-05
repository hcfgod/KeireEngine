#include "Keire/BuildInfo.h"

#include "Keire/BuildInfo.generated.h"

#include <algorithm>
#include <sstream>

#define KEIRE_STRINGIZE_DETAIL(value) #value
#define KEIRE_STRINGIZE(value) KEIRE_STRINGIZE_DETAIL(value)

#if defined(_MSC_VER)
#define KEIRE_BUILD_COMPILER "MSVC " KEIRE_STRINGIZE(_MSC_VER)
#elif defined(__clang__)
#define KEIRE_BUILD_COMPILER                                                                                           \
    "Clang " KEIRE_STRINGIZE(__clang_major__) "." KEIRE_STRINGIZE(__clang_minor__) "." KEIRE_STRINGIZE(                \
        __clang_patchlevel__)
#elif defined(__GNUC__)
#define KEIRE_BUILD_COMPILER                                                                                           \
    "GCC " KEIRE_STRINGIZE(__GNUC__) "." KEIRE_STRINGIZE(__GNUC_MINOR__) "." KEIRE_STRINGIZE(__GNUC_PATCHLEVEL__)
#else
#define KEIRE_BUILD_COMPILER "Unknown"
#endif

#if defined(_WIN32)
#define KEIRE_BUILD_PLATFORM "Windows"
#elif defined(__APPLE__)
#define KEIRE_BUILD_PLATFORM "macOS"
#elif defined(__linux__)
#define KEIRE_BUILD_PLATFORM "Linux"
#else
#define KEIRE_BUILD_PLATFORM "Unknown"
#endif

namespace Keire
{
    const BuildInfo& GetBuildInfo() noexcept
    {
        static constexpr BuildInfo Info{
            KEIRE_BUILD_PROJECT_NAME, KEIRE_BUILD_PROJECT_VERSION, KEIRE_BUILD_REPOSITORY_SLUG,
            KEIRE_BUILD_GIT_COMMIT,   KEIRE_BUILD_CONFIGURATION,   KEIRE_BUILD_COMPILER,
            KEIRE_BUILD_PLATFORM,     KEIRE_BUILD_ARCHITECTURE,    KEIRE_BUILD_GIT_DIRTY};
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
        stream << ", " << info.Configuration << ", " << info.Compiler << ", " << info.Platform << ' '
               << info.Architecture << ')';
        return stream.str();
    }
} // namespace Keire
