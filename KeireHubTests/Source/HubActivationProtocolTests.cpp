#include "KeireHubRuntime/HubActivationProtocol.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

TEST_CASE("Hub browser callback registration follows the exact running executable")
{
    const auto executable = std::filesystem::absolute("Build Output/KeireHub.exe").lexically_normal();
    const auto planned = KeireHub::PlanHubActivationProtocolRegistration(executable);
    REQUIRE(planned);
    CHECK(planned.Value().Executable == executable);
    CHECK(planned.Value().Description == "URL:Kéire Hub Protocol");
    CHECK(planned.Value().Icon.find("Build Output/KeireHub.exe\",0") != std::string::npos);
    CHECK(planned.Value().Command.find("Build Output/KeireHub.exe\" \"%1\"") != std::string::npos);
    CHECK(planned.Value().Command.starts_with('"'));
}

TEST_CASE("Hub browser callback registration rejects ambiguous executable paths")
{
    CHECK_FALSE(KeireHub::PlanHubActivationProtocolRegistration("KeireHub.exe"));
    CHECK_FALSE(
        KeireHub::PlanHubActivationProtocolRegistration(std::filesystem::absolute("invalid\"handler/KeireHub.exe")));
}
