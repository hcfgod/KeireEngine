#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "Core/Core.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "spdlog/sinks/null_sink.h"

namespace
{
    std::atomic<unsigned int> s_TestDirectoryCounter = 0;

    std::filesystem::path MakeTestDirectory(const std::string& name)
    {
        const auto counter = s_TestDirectoryCounter.fetch_add(1);
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("CrossPlatformCoreClientTemplate-" + name + "-" +
                                                         std::to_string(timestamp) + "-" + std::to_string(counter));
    }

    std::string ReadFile(const std::filesystem::path& path)
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
            Core::Log::Shutdown();
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
                Core::Log::Shutdown();
                std::error_code error;
                std::filesystem::remove_all(Directory, error);
            }
            catch (...) // NOLINT(bugprone-empty-catch)
            {
                // Test cleanup must not terminate the process during stack unwinding.
            }
        }

        std::filesystem::path Directory;
        Core::LogConfig Config;
    };

    struct CurrentDirectoryGuard
    {
        explicit CurrentDirectoryGuard(const std::filesystem::path& directory) : Original(std::filesystem::current_path())
        {
            std::filesystem::create_directories(directory);
            std::filesystem::current_path(directory);
        }

        ~CurrentDirectoryGuard() noexcept
        {
            try
            {
                std::error_code error;
                std::filesystem::current_path(Original, error);
            }
            catch (...) // NOLINT(bugprone-empty-catch)
            {
                // Restoring the working directory is best-effort in a destructor.
            }
        }

        std::filesystem::path Original;
    };
} // namespace

int main(const int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--core-assert-probe") == 0)
    {
        CORE_ASSERT(false, "assertion probe");
        return 2;
    }
    doctest::Context context(argc, argv);
    return context.run();
}

TEST_CASE("Build information is populated")
{
    const auto& info = Core::GetBuildInfo();
    CHECK(info.ProjectName == "C++ Cross-Platform Core-Client Template");
    CHECK(info.Version == "0.1.0");
    CHECK_FALSE(info.GitCommit.empty());
    CHECK_FALSE(info.Configuration.empty());
    CHECK_FALSE(info.Compiler.empty());
    CHECK_FALSE(info.Platform.empty());
    CHECK_FALSE(info.Architecture.empty());
    CHECK_FALSE(Core::GetVersionString().empty());
}

TEST_CASE("Assertions evaluate successful conditions once when enabled")
{
    int evaluations = 0;
    CORE_ASSERT(++evaluations == 1);
#if defined(CORE_ASSERTIONS_ENABLED)
    CHECK(evaluations == 1);
#else
    CHECK(evaluations == 0);
#endif
}

TEST_CASE("Core name is stable")
{
    LogFixture fixture("core-name");
    Core::Log::Initialize(fixture.Config);

    CHECK(std::string(Core::GetName()) == "Core");
}

TEST_CASE("Logger writes configured Core and Client files")
{
    LogFixture fixture("configured-files");
    Core::Log::Initialize(fixture.Config);

    CORE_INFO("configured core message");
    CLIENT_WARN("configured client message");
    Core::Log::Shutdown();

    const auto coreFile = fixture.Directory / fixture.Config.CoreLogFile;
    const auto clientFile = fixture.Directory / fixture.Config.ClientLogFile;
    REQUIRE(std::filesystem::is_regular_file(coreFile));
    REQUIRE(std::filesystem::is_regular_file(clientFile));
    CHECK(ReadFile(coreFile).find("configured core message") != std::string::npos);
    CHECK(ReadFile(clientFile).find("configured client message") != std::string::npos);
}

TEST_CASE("Logger initialization is idempotent only for the same configuration")
{
    LogFixture fixture("idempotence");
    Core::Log::Initialize(fixture.Config);

    CHECK_NOTHROW(Core::Log::Initialize(fixture.Config));

    auto conflictingConfig = fixture.Config;
    conflictingConfig.QueueSize *= 2;
    CHECK_THROWS_AS(Core::Log::Initialize(conflictingConfig), std::logic_error);
}

TEST_CASE("Logger does not replace or shut down external spdlog state")
{
    LogFixture fixture("external-spdlog");
    const auto previousDefault = spdlog::default_logger();
    const auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    const auto externalLogger = std::make_shared<spdlog::logger>("ExternalTestLogger", sink);
    spdlog::set_default_logger(externalLogger);

    Core::Log::Initialize(fixture.Config);
    CORE_INFO("private logger message");
    Core::Log::Shutdown();

    CHECK(spdlog::default_logger() == externalLogger);
    CHECK_NOTHROW(externalLogger->info("external logger remains usable"));
    spdlog::set_default_logger(previousDefault);
}

TEST_CASE("Logging macros evaluate enabled arguments once")
{
    LogFixture fixture("macro-evaluation");
    Core::Log::Initialize(fixture.Config);
    int evaluations = 0;
    CORE_INFO("evaluation {}", ++evaluations);
    CHECK(evaluations == 1);
}

TEST_CASE("Console output can be suppressed without disabling file logging")
{
    LogFixture fixture("console-suppression");
    Core::Log::Initialize(fixture.Config);

    {
        auto logger = Core::Log::GetCoreLogger();
        REQUIRE(logger);
        CHECK(logger.SinkCount() == 1);
    }
    CORE_INFO("file-only message");
}

TEST_CASE("Logger handles provide lifecycle-safe level and flush operations")
{
    const auto directory = MakeTestDirectory("handle-operations");
    std::filesystem::remove_all(directory);
    {
        CurrentDirectoryGuard currentDirectory(directory);
        Core::Log::Shutdown();

        {
            auto logger = Core::Log::GetClientLogger();
            REQUIRE(logger);
            logger.SetLevel(Core::LogLevel::Error);
            logger.Write(spdlog::source_loc{}, spdlog::level::info, "filtered handle message");
            logger.Write(spdlog::source_loc{}, spdlog::level::err, "retained handle message");
            logger.Flush();
        }
        Core::Log::Shutdown();

        const auto contents = ReadFile(directory / "Logs" / "Client.log");
        CHECK(contents.find("filtered handle message") == std::string::npos);
        CHECK(contents.find("retained handle message") != std::string::npos);
    }
    std::filesystem::remove_all(directory);
}

#if SPDLOG_ACTIVE_LEVEL > SPDLOG_LEVEL_DEBUG
TEST_CASE("Compiled-out logging macros do not evaluate arguments")
{
    int evaluations = 0;
    CORE_DEBUG("disabled evaluation {}", ++evaluations);
    CHECK(evaluations == 0);
}
#endif

TEST_CASE("Logger rejects invalid configurations")
{
    LogFixture fixture("validation");

    auto config = fixture.Config;
    config.QueueSize = 0;
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.WorkerThreads = 0;
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.MaxFileSizeBytes = 0;
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.MaxFiles = 0;
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.CoreLogFile = "../Core.log";
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.ClientLogFile = config.CoreLogFile;
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);

    const auto regularFile = fixture.Directory.parent_path() / (fixture.Directory.filename().string() + "-file");
    {
        std::ofstream stream(regularFile);
        stream << "not a directory";
    }
    config = fixture.Config;
    config.LogDirectory = regularFile.string();
    CHECK_THROWS_AS(Core::Log::Initialize(config), std::invalid_argument);
    std::filesystem::remove(regularFile);
}

TEST_CASE("Logger level changes filter lower-severity messages")
{
    LogFixture fixture("levels");
    Core::Log::Initialize(fixture.Config);
    Core::Log::SetLevel(Core::LogLevel::Error);

    CORE_INFO("message that must be filtered");
    CORE_ERROR("message that must be retained");
    Core::Log::Shutdown();

    const auto contents = ReadFile(fixture.Directory / fixture.Config.CoreLogFile);
    CHECK(contents.find("message that must be filtered") == std::string::npos);
    CHECK(contents.find("message that must be retained") != std::string::npos);
}

TEST_CASE("Logger rotates files at the configured size")
{
    LogFixture fixture("rotation");
    fixture.Config.MaxFileSizeBytes = 512;
    fixture.Config.MaxFiles = 2;
    Core::Log::Initialize(fixture.Config);

    for (int index = 0; index < 10; ++index)
    {
        CORE_INFO("rotation message {} {}", index, std::string(128, 'x'));
    }
    Core::Log::Shutdown();

    std::size_t coreFileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(fixture.Directory))
    {
        if (entry.path().filename().string().starts_with("CoreTests"))
        {
            ++coreFileCount;
        }
    }
    CHECK(coreFileCount > 1);
}

TEST_CASE("Logger supports concurrent Core and Client logging")
{
    LogFixture fixture("concurrency");
    Core::Log::Initialize(fixture.Config);

    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int worker = 0; worker < 4; ++worker)
    {
        workers.emplace_back(
            [worker]()
            {
                for (int message = 0; message < 10; ++message)
                {
                    CORE_INFO("core worker {} message {}", worker, message);
                    CLIENT_INFO("client worker {} message {}", worker, message);
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    CHECK_NOTHROW(Core::Log::Flush());
    Core::Log::Shutdown();
    CHECK(ReadFile(fixture.Directory / fixture.Config.CoreLogFile).find("core worker") != std::string::npos);
    CHECK(ReadFile(fixture.Directory / fixture.Config.ClientLogFile).find("client worker") != std::string::npos);
}

TEST_CASE("Lazy acquisition and explicit initialization serialize safely")
{
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        LogFixture fixture("initialization-race-" + std::to_string(iteration));
        CurrentDirectoryGuard currentDirectory(fixture.Directory);
        std::atomic<bool> start = false;
        bool lazyInitializationWon = false;
        std::exception_ptr getterFailure;
        std::exception_ptr initializerFailure;

        std::thread getter(
            [&]()
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                try
                {
                    auto logger = Core::Log::GetCoreLogger();
                    logger.Write(spdlog::source_loc{}, spdlog::level::info, "race-safe lazy message");
                }
                catch (...)
                {
                    getterFailure = std::current_exception();
                }
            });
        std::thread initializer(
            [&]()
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                try
                {
                    Core::Log::Initialize(fixture.Config);
                }
                catch (const std::logic_error&)
                {
                    lazyInitializationWon = true;
                }
                catch (...)
                {
                    initializerFailure = std::current_exception();
                }
            });

        start.store(true, std::memory_order_release);
        getter.join();
        initializer.join();
        CHECK(getterFailure == nullptr);
        CHECK(initializerFailure == nullptr);
        Core::Log::Shutdown();
        if (lazyInitializationWon)
        {
            CHECK(std::filesystem::is_regular_file(fixture.Directory / "Logs" / "Core.log"));
        }
        else
        {
            CHECK(std::filesystem::is_regular_file(fixture.Directory / fixture.Config.CoreLogFile));
        }
    }
}

TEST_CASE("Shutdown waits for active logger handles")
{
    using namespace std::chrono_literals;

    LogFixture fixture("shutdown-coordination");
    Core::Log::Initialize(fixture.Config);
    std::future<void> shutdown;

    {
        auto handle = Core::Log::GetCoreLogger();
        REQUIRE(handle);
        shutdown = std::async(std::launch::async, []() { Core::Log::Shutdown(); });
        CHECK(shutdown.wait_for(50ms) == std::future_status::timeout);
    }

    CHECK(shutdown.wait_for(2s) == std::future_status::ready);
    CHECK_NOTHROW(shutdown.get());
}

TEST_CASE("Lazy initialization uses the process working directory and can reinitialize")
{
    const auto directory = MakeTestDirectory("lazy-initialization");
    std::filesystem::remove_all(directory);

    {
        CurrentDirectoryGuard currentDirectory(directory);
        Core::Log::Shutdown();
        CORE_INFO("lazy initialization message");
        Core::Log::Shutdown();
        CHECK(std::filesystem::is_regular_file(directory / "Logs" / "Core.log"));
    }

    LogFixture fixture("reinitialization");
    CHECK_NOTHROW(Core::Log::Initialize(fixture.Config));
    CORE_INFO("reinitialized message");
    Core::Log::Shutdown();
    CHECK(ReadFile(fixture.Directory / fixture.Config.CoreLogFile).find("reinitialized message") != std::string::npos);

    std::filesystem::remove_all(directory);
}
