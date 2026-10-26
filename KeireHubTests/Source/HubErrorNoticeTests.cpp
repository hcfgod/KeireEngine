#include "KeireHubInternal/HubErrorNotice.h"

#include "KeireHubRuntime/HubController.h"
#include "KeireHubTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

TEST_CASE("Hub errors remain visible when the controller is unavailable")
{
    std::string notice = "Previous status";
    bool noticeError = false;
    std::string persisted = "Previously persisted error";
    KeireHub::Detail::PublishHubErrorNotice(nullptr, "Editor is unavailable.", notice, noticeError, persisted);
    CHECK(notice == "Editor is unavailable.");
    CHECK(noticeError);
    CHECK(persisted == "Previously persisted error");
}

TEST_CASE("Hub error notifications persist once per consecutive message and survive reopening")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto preferences = temporary.Path() / "Hub";
    KeireHub::HubController controller({.PreferenceRoot = preferences});
    REQUIRE(controller.Load(10));
    std::string notice;
    bool noticeError = false;
    std::string persisted;
    KeireHub::Detail::PublishHubErrorNotice(&controller, "First error", notice, noticeError, persisted);
    CHECK(persisted == "First error");
    REQUIRE(controller.Notifications().Snapshot()->size() == 1U);
    const auto first = controller.Notifications().Snapshot()->front();
    CHECK(first.Severity == KeireHub::NotificationSeverity::Error);
    CHECK(first.Title == "Hub action needs attention");
    CHECK(first.Message == "First error");
    CHECK(first.CreatedUnixSeconds > 0U);
    KeireHub::Detail::PublishHubErrorNotice(&controller, "First error", notice, noticeError, persisted);
    CHECK(controller.Notifications().Snapshot()->size() == 1U);
    KeireHub::Detail::PublishHubErrorNotice(&controller, "Second error", notice, noticeError, persisted);
    CHECK(controller.Notifications().Snapshot()->size() == 2U);
    CHECK(persisted == "Second error");

    KeireHub::HubController reopened({.PreferenceRoot = preferences});
    REQUIRE(reopened.Load(20));
    CHECK(reopened.Notifications().Snapshot()->size() == 2U);
}

TEST_CASE("Hub notification storage failures preserve the banner and permit a later persistence retry")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubController controller({.PreferenceRoot = temporary.Path() / "Hub"});
    REQUIRE(controller.Load(10));
    const auto path = controller.Notifications().Path();
    // An ordinary directory at the file destination makes atomic publication fail without changing permissions.
    std::filesystem::create_directories(path);
    std::string notice;
    bool noticeError = false;
    std::string persisted;
    KeireHub::Detail::PublishHubErrorNotice(&controller, "Retryable error", notice, noticeError, persisted);
    CHECK(notice == "Retryable error");
    CHECK(noticeError);
    CHECK(persisted.empty());
    CHECK(controller.Notifications().Snapshot()->empty());
    REQUIRE(std::filesystem::remove(path));
    KeireHub::Detail::PublishHubErrorNotice(&controller, "Retryable error", notice, noticeError, persisted);
    CHECK(persisted == "Retryable error");
    CHECK(controller.Notifications().Snapshot()->size() == 1U);
}
