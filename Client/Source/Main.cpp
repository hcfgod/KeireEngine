#include "Core/Core.h"

#include <cstdio>
#include <exception>

int main()
{
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
