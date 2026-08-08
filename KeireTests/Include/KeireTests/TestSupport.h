#pragma once

#include "Keire/Core.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace KeireTests
{
    inline std::filesystem::path TestExecutable;
    inline std::atomic<unsigned int> TestDirectoryCounter = 0;

    [[nodiscard]] inline bool RunningInCi()
    {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        const int result = ::_dupenv_s(&value, &length, "CI");
        std::free(value);
        return result == 0 && length != 0;
#else
        return std::getenv("CI") != nullptr;
#endif
    }

    inline std::filesystem::path MakeTestDirectory(const std::string& name)
    {
        const auto counter = TestDirectoryCounter.fetch_add(1);
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               ("Keire-" + name + "-" + std::to_string(timestamp) + "-" + std::to_string(counter));
    }

    inline std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        std::ostringstream contents;
        contents << stream.rdbuf();
        return contents.str();
    }

    struct LogFixture
    {
        explicit LogFixture(const std::string& name) : Directory(MakeTestDirectory(name))
        {
            Keire::Log::Shutdown();
            std::filesystem::remove_all(Directory);
            Config.LogDirectory = Directory.string();
            Config.CoreLogFile = "CoreTests.log";
            Config.ClientLogFile = "ClientTests.log";
            Config.EnableConsole = false;
        }

        ~LogFixture() noexcept
        {
            try
            {
                Keire::Log::Shutdown();
                std::error_code error;
                std::filesystem::remove_all(Directory, error);
            }
            catch (...) // NOLINT(bugprone-empty-catch)
            {
                // Test cleanup must not terminate the process during stack unwinding.
            }
        }

        std::filesystem::path Directory;
        Keire::LogConfig Config;
    };

    struct CurrentDirectoryGuard
    {
        explicit CurrentDirectoryGuard(const std::filesystem::path& directory)
            : Original(std::filesystem::current_path())
        {
            std::filesystem::create_directories(directory);
            std::filesystem::current_path(directory);
        }

        ~CurrentDirectoryGuard() noexcept
        {
            std::error_code error;
            std::filesystem::current_path(Original, error);
        }

        std::filesystem::path Original;
    };
} // namespace KeireTests
