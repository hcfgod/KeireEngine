#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

TEST_CASE("filesystem rename retries only transient failures with bounded backoff")
{
    std::size_t attempts = 0;
    std::vector<std::chrono::milliseconds> delays;
    std::error_code error;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& result)
    {
        ++attempts;
        result = attempts < 3 ? std::make_error_code(std::errc::permission_denied) : std::error_code{};
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds value) { delays.push_back(value); };

    CHECK(Keire::Detail::TryRenamePathWithRetry("source", "destination", error, operation, delay));
    CHECK(attempts == 3);
    CHECK(delays == std::vector{std::chrono::milliseconds(10), std::chrono::milliseconds(20)});
    CHECK_FALSE(error);
}

TEST_CASE("filesystem rename fails nontransient errors immediately and reports resolved paths")
{
    std::size_t attempts = 0;
    std::size_t delays = 0;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error)
    {
        ++attempts;
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds) { ++delays; };

    std::error_code error;
    CHECK_FALSE(Keire::Detail::TryRenamePathWithRetry("missing-source", "destination", error, operation, delay));
    CHECK(attempts == 1);
    CHECK(delays == 0);
    CHECK(error == std::errc::no_such_file_or_directory);

    const auto source = std::filesystem::absolute("missing-source").lexically_normal().string();
    const auto destination = std::filesystem::absolute("destination").lexically_normal().string();
    try
    {
        Keire::Detail::RenamePathWithRetry("missing-source", "destination", operation, delay);
        FAIL("Expected a resolved-path rename diagnostic.");
    }
    catch (const std::runtime_error& exception)
    {
        CHECK(std::string(exception.what()).find(source) != std::string::npos);
        CHECK(std::string(exception.what()).find(destination) != std::string::npos);
    }
}

TEST_CASE("filesystem rename bounds persistent transient failures")
{
    std::size_t attempts = 0;
    std::vector<std::chrono::milliseconds> delays;
    const auto operation = [&](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error)
    {
        ++attempts;
        error = std::make_error_code(std::errc::permission_denied);
    };
    const auto delay = [&](const std::size_t, const std::chrono::milliseconds value) { delays.push_back(value); };

    std::error_code error;
    CHECK_FALSE(Keire::Detail::TryRenamePathWithRetry("source", "destination", error, operation, delay));
    CHECK(attempts == 5);
    CHECK(delays == std::vector{std::chrono::milliseconds(10), std::chrono::milliseconds(20),
                                std::chrono::milliseconds(40), std::chrono::milliseconds(80),
                                std::chrono::milliseconds(160)});
}
