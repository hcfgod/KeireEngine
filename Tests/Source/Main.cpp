#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Core.h"

#include <string>

TEST_CASE("Core name is stable")
{
    CHECK(std::string(Core::GetName()) == "Core");
}

TEST_CASE("Core logger can initialize, log, flush, and shutdown")
{
    Core::LogConfig config;
    config.LogDirectory = "Logs/Tests";
    config.CoreLogFile = "CoreTests.log";
    config.ClientLogFile = "ClientTests.log";

    CHECK_NOTHROW(Core::Log::Initialize(config));
    CHECK_NOTHROW(CORE_INFO("doctest core logger smoke test"));
    CHECK_NOTHROW(CLIENT_INFO("doctest client logger smoke test"));
    CHECK_NOTHROW(Core::Log::Flush());
    CHECK_NOTHROW(Core::Log::Shutdown());
}
