#include "Core/Core.h"

#include <cstdio>
#include <cstring>
#include <exception>

namespace
{
    void PrintHelp(const char* executable)
    {
        std::printf("Usage: %s [--help | --version]\n\nOptions:\n  -h, --help     Show this help.\n"
                    "  -v, --version  Show build and version information.\n",
                    executable);
    }

    void PrintVersion()
    {
        const auto& info = Core::GetBuildInfo();
        std::printf("%.*s %s\n", static_cast<int>(info.ProjectName.size()), info.ProjectName.data(),
                    Core::GetVersionString().c_str());
    }
} // namespace

int main(const int argc, char* argv[])
{
    if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0))
    {
        PrintHelp(argv[0]);
        return 0;
    }
    if (argc == 2 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0))
    {
        PrintVersion();
        return 0;
    }
    if (argc != 1)
    {
        std::fprintf(stderr, "Unknown or unexpected argument '%s'. Run '%s --help' for usage.\n", argv[1], argv[0]);
        return 2;
    }

    try
    {
        Core::Log::Initialize();

        CORE_INFO("{} initialized", Core::GetName());
        CLIENT_INFO("{} client is running", Core::GetName());

        Core::Log::Shutdown();
        return 0;
    }
    catch (const std::exception& exception)
    {
        Core::Log::Shutdown();
        std::fprintf(stderr, "Client failed: %s\n", exception.what());
    }
    catch (...)
    {
        Core::Log::Shutdown();
        std::fputs("Client failed with an unknown exception.\n", stderr);
    }

    return 1;
}
