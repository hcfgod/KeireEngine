#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <vector>

TEST_CASE("filesystem UTF-8 conversion preserves non-ASCII project paths")
{
    const std::string encoded = "KéireEngine/Assets/Créature.png";
    const auto path = Keire::Detail::PathFromUtf8(encoded);
    CHECK(Keire::Detail::PathToUtf8(path) == encoded);
    CHECK(path.filename() == Keire::Detail::PathFromUtf8("Créature.png"));
}

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

    const auto source = Keire::Detail::PathToUtf8(std::filesystem::absolute("missing-source").lexically_normal());
    const auto destination = Keire::Detail::PathToUtf8(std::filesystem::absolute("destination").lexically_normal());
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

#if defined(_WIN32)
TEST_CASE("filesystem rename supports extended-length Windows destinations")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireLongRename-" + suffix);
    auto directory = root;
    for (std::size_t index = 0; index < 5; ++index)
        directory /= std::string(30, static_cast<char>('a' + index));
    std::filesystem::create_directories(directory);
    const auto source = directory / "source.bin";
    const auto destination = directory / (std::string(100, 'd') + ".bin");
    REQUIRE(source.native().size() < 260);
    REQUIRE(destination.native().size() >= 260);
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "long-path rename";
        REQUIRE(stream.good());
    }

    CHECK_NOTHROW(Keire::Detail::RenamePathWithRetry(source, destination));
    CHECK_NOTHROW(Keire::Detail::RenamePathWithRetry(destination, source));
    CHECK(Keire::Detail::ReadTextFile(source, 1024) == "long-path rename");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
#endif

TEST_CASE("atomic file publication replaces complete text and binary contents")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("KeireAtomicPublication-" + suffix);
    std::filesystem::create_directories(directory);
    const auto textPath = directory / "settings.json";
    const auto binaryPath = directory / "thumbnail.rgba";

    Keire::Detail::WriteTextFileAtomically(textPath, "old");
    Keire::Detail::WriteTextFileAtomically(textPath, "replacement");
    CHECK(Keire::Detail::ReadTextFile(textPath, 1024) == "replacement");

    const std::array<std::byte, 3> bytes{std::byte{0x01}, std::byte{0x7f}, std::byte{0xff}};
    Keire::Detail::WriteFileAtomically(binaryPath, bytes);
    std::ifstream input(binaryPath, std::ios::binary);
    std::array<std::byte, 3> actual{};
    input.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    REQUIRE(input);
    CHECK(actual == bytes);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}
