#include "KeireHubInternal/HubNoticeUi.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>

TEST_CASE("Hub notices persist until dismissal or replacement")
{
    using namespace std::chrono_literals;
    const auto initial = std::chrono::steady_clock::time_point{} + 1s;
    std::string notice = "Located editor 0.4.5. Verify it before use.";
    std::string observed;
    auto started = std::chrono::steady_clock::time_point{};

    KeireHub::Detail::AdvanceHubNotice(notice, observed, started, initial);
    CHECK(observed == notice);
    CHECK(started == initial);
    KeireHub::Detail::AdvanceHubNotice(notice, observed, started, initial + 1h);
    CHECK(notice == "Located editor 0.4.5. Verify it before use.");
    CHECK(started == initial);

    notice = "Editor installation verified and ready.";
    KeireHub::Detail::AdvanceHubNotice(notice, observed, started, initial + 2h);
    CHECK(observed == notice);
    CHECK(started == initial + 2h);
    notice.clear();
    KeireHub::Detail::AdvanceHubNotice(notice, observed, started, initial + 3h);
    CHECK(observed.empty());
    CHECK(KeireHub::Detail::HubNoticeRailHeight == doctest::Approx(64.0F));
}
