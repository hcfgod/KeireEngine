#include "Core/Core.h"

#include <string>

int main()
{
    const auto& build = Core::GetBuildInfo();
    Core::LogConfig config;
    config.EnableConsole = false;
    config.LogDirectory = "Logs";
    Core::Log::Initialize(config);
    CORE_INFO("SDK consumer initialized with Core {}", build.Version);
    Core::Log::Shutdown();
    return std::string(Core::GetName()).empty() || build.Version.empty() ? 1 : 0;
}
