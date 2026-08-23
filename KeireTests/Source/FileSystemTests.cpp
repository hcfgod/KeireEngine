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
#include <string_view>
#include <system_error>
#include <vector>

TEST_CASE("filesystem UTF-8 conversion preserves non-ASCII project paths")
{
    const std::string encoded = "KéireEngine/Assets/Créature.png";
    const auto path = Keire::Detail::PathFromUtf8(encoded);
    CHECK(Keire::Detail::PathToUtf8(path) == encoded);
    CHECK(path.filename() == Keire::Detail::PathFromUtf8("Créature.png"));
}

TEST_CASE("filesystem UTF-8 conversion composes and creates a non-ASCII Hub project destination")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireHubUtf8-" + suffix);
    const std::string parentText = "Kéire Projects";
    const std::string projectText = "Créature Demo";
    const auto parent = root / Keire::Detail::PathFromUtf8(parentText);

    const auto parentInput = Keire::Detail::PathToUtf8(parent);
    const auto destination = Keire::Detail::PathFromUtf8(parentInput) / Keire::Detail::PathFromUtf8(projectText);
    std::filesystem::create_directories(destination);

    CHECK(std::filesystem::is_directory(destination));
    CHECK(Keire::Detail::PathToUtf8(destination.parent_path().filename()) == parentText);
    CHECK(Keire::Detail::PathToUtf8(destination.filename()) == projectText);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
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

TEST_CASE("atomic file publication supports Windows paths whose temporary name exceeds MAX_PATH")
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("KeireLongAtomic-" + suffix);
    std::filesystem::create_directories(root);
    REQUIRE(root.native().size() < 240);
    const auto filenameLength = std::size_t{245} - root.native().size() - 1;
    REQUIRE(filenameLength > 4);
    REQUIRE(filenameLength < 255);
    const auto destination = root / (std::string(filenameLength - 4, 'a') + ".bin");
    REQUIRE(destination.native().size() == 245);
    REQUIRE(destination.native().size() + std::string_view(".tmp.18446744073709551615").size() > 260);

    CHECK_NOTHROW(Keire::Detail::WriteTextFileAtomically(destination, "long-path atomic publication"));
    CHECK(Keire::Detail::ReadTextFile(destination, 1024) == "long-path atomic publication");

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

TEST_CASE("conditional atomic file publication preserves unchanged files")
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("KeireConditionalPublication-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "workspace.sln";
    REQUIRE(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "solution"));
    const auto preservedTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(path, preservedTime);

    CHECK_FALSE(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "solution"));
    CHECK(std::filesystem::last_write_time(path) == preservedTime);
    CHECK(Keire::Detail::WriteTextFileAtomicallyIfChanged(path, "updated solution"));
    CHECK(Keire::Detail::ReadTextFile(path, 1024) == "updated solution");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}
