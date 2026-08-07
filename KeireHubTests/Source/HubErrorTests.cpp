#include "KeireHubRuntime/HubError.h"

#include <doctest/doctest.h>

#include <string>

TEST_CASE("Hub process launch errors have a stable serialized code")
{
    constexpr auto code = KeireHub::HubErrorCode::ProcessLaunchFailed;
    CHECK(std::string(KeireHub::ToString(code)) == "hub.process_launch_failed");
    REQUIRE(KeireHub::ParseHubErrorCode("hub.process_launch_failed"));
    CHECK(*KeireHub::ParseHubErrorCode("hub.process_launch_failed") == code);
}
