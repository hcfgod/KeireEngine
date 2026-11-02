#include "KeireInternal/Rendering/PostProcessUploadRetry.h"

#include <doctest/doctest.h>

#include <limits>

TEST_CASE("post process failed uploads retry at bounded intervals across multiple cameras")
{
    Keire::Detail::PostProcessUploadRetry retry;
    CHECK(retry.ShouldAttempt(100, false, false));
    retry.RecordAttempt(100);
    CHECK_FALSE(retry.ShouldAttempt(100, false, false));
    CHECK_FALSE(retry.ShouldAttempt(101, false, false));
    CHECK_FALSE(retry.ShouldAttempt(159, false, false));
    REQUIRE(retry.ShouldAttempt(160, false, false));
    retry.RecordAttempt(160);
    CHECK_FALSE(retry.ShouldAttempt(160, false, false));
    CHECK_FALSE(retry.ShouldAttempt(219, false, false));
    CHECK(retry.ShouldAttempt(220, false, false));
}

TEST_CASE("post process uploaded sources remain cached and replacements attempt immediately")
{
    Keire::Detail::PostProcessUploadRetry retry;
    retry.RecordAttempt(100);
    CHECK_FALSE(retry.ShouldAttempt(100, false, true));
    CHECK_FALSE(retry.ShouldAttempt(10000, false, true));
    CHECK(retry.ShouldAttempt(100, true, true));
    retry.RecordAttempt(100);
    CHECK_FALSE(retry.ShouldAttempt(101, false, false));
    CHECK(retry.ShouldAttempt(101, true, false));
    retry.RecordAttempt(101);
    CHECK_FALSE(retry.ShouldAttempt(101, false, false));
    CHECK_FALSE(retry.ShouldAttempt(161, false, true));
}

TEST_CASE("post process upload retries recover from frame reset without overflowing deadlines")
{
    Keire::Detail::PostProcessUploadRetry retry;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    retry.RecordAttempt(maximum - 59);
    CHECK_FALSE(retry.ShouldAttempt(maximum, false, false));
    CHECK(retry.ShouldAttempt(0, false, false));
    retry.RecordAttempt(0);
    CHECK_FALSE(retry.ShouldAttempt(0, false, false));
    CHECK_FALSE(retry.ShouldAttempt(59, false, false));
    CHECK(retry.ShouldAttempt(60, false, false));
    retry.RecordAttempt(maximum - 60);
    CHECK(retry.ShouldAttempt(maximum, false, false));
    CHECK_FALSE(retry.ShouldAttempt(0, false, true));
}
