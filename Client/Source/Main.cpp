#include "Core.h"

int main()
{
    Core::Log::Initialize();

    CORE_INFO("{} initialized", Core::GetName());
    CLIENT_INFO("{} client is running", Core::GetName());

    Core::Log::Shutdown();
    return 0;
}
