#include "doctest/doctest.h"

#include "Keire/Core.h"
#include "KeireInternal/LogInternal.h"
#include "KeireTests/TestSupport.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "spdlog/sinks/null_sink.h"
#include "spdlog/spdlog.h"

using KeireTests::CurrentDirectoryGuard;
using KeireTests::LogFixture;
using KeireTests::MakeTestDirectory;
using KeireTests::ReadFile;

TEST_CASE("Logger writes configured Core and Client files")
{
    LogFixture fixture("configured-files");
    Keire::Log::Initialize(fixture.Config);

    KEIRE_CORE_INFO("configured core message");
    KEIRE_CLIENT_WARN("configured client message");
    Keire::Log::Shutdown();

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
    Keire::Log::Initialize(fixture.Config);
    CHECK_NOTHROW(Keire::Log::Initialize(fixture.Config));

    auto conflictingConfig = fixture.Config;
    conflictingConfig.QueueSize *= 2;
    CHECK_THROWS_AS(Keire::Log::Initialize(conflictingConfig), std::logic_error);
}

TEST_CASE("Logger does not replace or shut down external spdlog state")
{
    LogFixture fixture("external-spdlog");
    const auto previousDefault = spdlog::default_logger();
    const auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    const auto externalLogger = std::make_shared<spdlog::logger>("ExternalTestLogger", sink);
    spdlog::set_default_logger(externalLogger);

    Keire::Log::Initialize(fixture.Config);
    KEIRE_CORE_INFO("private logger message");
    Keire::Log::Shutdown();

    CHECK(spdlog::default_logger() == externalLogger);
    CHECK_NOTHROW(externalLogger->info("external logger remains usable"));
    spdlog::set_default_logger(previousDefault);
}

TEST_CASE("Logging macros evaluate enabled arguments once")
{
    LogFixture fixture("macro-evaluation");
    Keire::Log::Initialize(fixture.Config);
    int evaluations = 0;
    KEIRE_CORE_INFO("evaluation {}", ++evaluations);
    CHECK(evaluations == 1);
}

TEST_CASE("Console output can be suppressed without disabling file logging")
{
    LogFixture fixture("console-suppression");
    Keire::Log::Initialize(fixture.Config);

    auto logger = Keire::Log::GetCoreLogger();
    REQUIRE(logger);
    CHECK(logger.SinkCount() == 1);
    KEIRE_CORE_INFO("file-only message");
}

TEST_CASE("Logger retains filtered structured records for the editor console")
{
    LogFixture fixture("editor-console-records");
    Keire::Log::Initialize(fixture.Config);

    const auto initialRecords = Keire::Detail::LogInternalAccess::ReadRecordsSince(0);
    REQUIRE(initialRecords.size() == 1);
    CHECK(initialRecords.front().Channel == Keire::Detail::LogChannel::Core);
    CHECK(initialRecords.front().Level == Keire::LogLevel::Info);
    CHECK(initialRecords.front().Message == "Core logger initialized");

    const auto initialSequence = initialRecords.back().Sequence;
    KEIRE_CORE_WARN("retained core warning");
    KEIRE_CLIENT_ERROR("[Managed] retained client failure");
    const auto retained = Keire::Detail::LogInternalAccess::ReadRecordsSince(initialSequence);
    REQUIRE(retained.size() == 2);
    CHECK(retained[0].Channel == Keire::Detail::LogChannel::Core);
    CHECK(retained[0].Level == Keire::LogLevel::Warn);
    CHECK(retained[0].Message == "retained core warning");
    CHECK(retained[1].Channel == Keire::Detail::LogChannel::Client);
    CHECK(retained[1].Level == Keire::LogLevel::Error);
    CHECK(retained[1].Message == "[Managed] retained client failure");

    Keire::Log::SetLevel(Keire::LogLevel::Error);
    KEIRE_CORE_INFO("filtered editor console message");
    KEIRE_CORE_ERROR("visible editor console message");
    const auto filtered = Keire::Detail::LogInternalAccess::ReadRecordsSince(retained.back().Sequence);
    REQUIRE(filtered.size() == 1);
    CHECK(filtered.front().Message == "visible editor console message");
}

TEST_CASE("Logger handles provide copyable lifecycle-safe operations")
{
    const auto directory = MakeTestDirectory("handle-operations");
    std::filesystem::remove_all(directory);
    {
        CurrentDirectoryGuard currentDirectory(directory);
        Keire::Log::Shutdown();

        auto logger = Keire::Log::GetClientLogger();
        auto copiedLogger = [](const Keire::LoggerHandle& source) { return Keire::LoggerHandle(source); }(logger);
        REQUIRE(copiedLogger);
        copiedLogger.SetLevel(Keire::LogLevel::Error);
        copiedLogger.Write(Keire::LogLevel::Info, "filtered handle message");
        copiedLogger.Write(Keire::LogLevel::Error, "retained handle message");
        copiedLogger.Flush();
        Keire::Log::Shutdown();

        const auto contents = ReadFile(directory / "Logs" / "Client.log");
        CHECK(contents.find("filtered handle message") == std::string::npos);
        CHECK(contents.find("retained handle message") != std::string::npos);
        CHECK_FALSE(logger);
        CHECK_FALSE(copiedLogger);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("Failure reporting does not restart logging after managed shutdown")
{
    const auto directory = MakeTestDirectory("closed-failure-reporting");
    std::filesystem::remove_all(directory);
    {
        CurrentDirectoryGuard currentDirectory(directory);
        Keire::Log::Shutdown();
        CHECK_FALSE(Keire::Detail::LogInternalAccess::WriteAndFlushIfOpen(Keire::Detail::LogChannel::Client,
                                                                          Keire::LogLevel::Critical, "closed failure"));
        CHECK_FALSE(std::filesystem::exists(directory / "Logs"));
    }

    LogFixture fixture("open-failure-reporting");
    Keire::Log::Initialize(fixture.Config);
    CHECK(Keire::Detail::LogInternalAccess::WriteAndFlushIfOpen(Keire::Detail::LogChannel::Client,
                                                                Keire::LogLevel::Critical, "open failure"));
    Keire::Log::Shutdown();
    CHECK(ReadFile(fixture.Directory / fixture.Config.ClientLogFile).find("open failure") != std::string::npos);

    std::filesystem::remove_all(directory);
}

#if KEIRE_COMPILED_LOG_LEVEL > KEIRE_LOG_LEVEL_DEBUG
TEST_CASE("Compiled-out logging macros do not evaluate arguments")
{
    int evaluations = 0;
    KEIRE_CORE_DEBUG("disabled evaluation {}", ++evaluations);
    CHECK(evaluations == 0);
}
#endif

TEST_CASE("Kéire-owned log formatting supports braces, width, hex, and precision")
{
    CHECK(Keire::LogMessage("plain {}", 42) == "plain 42");
    CHECK(Keire::LogMessage("hex {:08x}", 0x2aU) == "hex 0000002a");
    CHECK(Keire::LogMessage("upper {:>6X}", 0xbeefU) == "upper   BEEF");
    CHECK(Keire::LogMessage("scalar {:.2f}", 3.14159) == "scalar 3.14");
    CHECK(Keire::LogMessage("text {:<6}", "ok") == "text ok    ");
    CHECK(Keire::LogMessage("character {}", 'K') == "character K");
    CHECK(Keire::LogMessage("escaped {{}} {}", true) == "escaped {} true");
    CHECK_THROWS_AS((void)Keire::LogMessage("missing {}"), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::LogMessage("extra", 1), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::LogMessage("bad {:q}", 1), std::invalid_argument);
}

TEST_CASE("Logger rejects invalid configurations")
{
    LogFixture fixture("validation");

    auto config = fixture.Config;
    config.QueueSize = 0;
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.WorkerThreads = 0;
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.MaxFileSizeBytes = 0;
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.MaxFiles = 0;
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.CoreLogFile = "../Core.log";
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    config = fixture.Config;
    config.ClientLogFile = config.CoreLogFile;
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);

    const auto regularFile = fixture.Directory.parent_path() / (fixture.Directory.filename().string() + "-file");
    {
        std::ofstream stream(regularFile);
        stream << "not a directory";
    }
    config = fixture.Config;
    config.LogDirectory = regularFile.string();
    CHECK_THROWS_AS(Keire::Log::Initialize(config), std::invalid_argument);
    std::filesystem::remove(regularFile);
}

TEST_CASE("Logger level changes filter lower-severity messages")
{
    LogFixture fixture("levels");
    Keire::Log::Initialize(fixture.Config);
    Keire::Log::SetLevel(Keire::LogLevel::Error);

    KEIRE_CORE_INFO("message that must be filtered");
    KEIRE_CORE_ERROR("message that must be retained");
    Keire::Log::Shutdown();

    const auto contents = ReadFile(fixture.Directory / fixture.Config.CoreLogFile);
    CHECK(contents.find("message that must be filtered") == std::string::npos);
    CHECK(contents.find("message that must be retained") != std::string::npos);
}

TEST_CASE("Logger rotates files at the configured size")
{
    LogFixture fixture("rotation");
    fixture.Config.MaxFileSizeBytes = 512;
    fixture.Config.MaxFiles = 2;
    Keire::Log::Initialize(fixture.Config);

    for (int index = 0; index < 10; ++index)
    {
        KEIRE_CORE_INFO("rotation message {} {}", index, std::string(128, 'x'));
    }
    Keire::Log::Shutdown();

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
    Keire::Log::Initialize(fixture.Config);

    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int worker = 0; worker < 4; ++worker)
    {
        workers.emplace_back(
            [worker]()
            {
                for (int message = 0; message < 10; ++message)
                {
                    KEIRE_CORE_INFO("core worker {} message {}", worker, message);
                    KEIRE_CLIENT_INFO("client worker {} message {}", worker, message);
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    CHECK_NOTHROW(Keire::Log::Flush());
    Keire::Log::Shutdown();
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
                    auto logger = Keire::Log::GetCoreLogger();
                    logger.Write(Keire::LogLevel::Info, "race-safe lazy message");
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
                    Keire::Log::Initialize(fixture.Config);
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
        Keire::Log::Shutdown();
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

TEST_CASE("Shutdown with a live handle is nonblocking and makes the handle inert")
{
    LogFixture fixture("shutdown-live-handle");
    Keire::Log::Initialize(fixture.Config);
    auto handle = Keire::Log::GetCoreLogger();
    REQUIRE(handle);

    CHECK_NOTHROW(Keire::Log::Shutdown());
    CHECK_FALSE(handle);
    CHECK(handle.SinkCount() == 0);
    CHECK_NOTHROW(handle.SetLevel(Keire::LogLevel::Trace));
    CHECK_NOTHROW(handle.Write(Keire::LogLevel::Info, "closed-state message"));
    CHECK_NOTHROW(handle.Flush());
}

TEST_CASE("Concurrent writes and shutdown leave handles safe")
{
    LogFixture fixture("concurrent-shutdown");
    Keire::Log::Initialize(fixture.Config);
    auto handle = Keire::Log::GetCoreLogger();
    std::atomic<bool> start = false;
    std::vector<std::thread> writers;
    writers.reserve(4);
    for (int writer = 0; writer < 4; ++writer)
    {
        writers.emplace_back(
            [handle, &start, writer]()
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                for (int message = 0; message < 100; ++message)
                {
                    handle.Write(Keire::LogLevel::Info, "concurrent shutdown writer " + std::to_string(writer) +
                                                            " message " + std::to_string(message));
                }
            });
    }
    start.store(true, std::memory_order_release);
    Keire::Log::Shutdown();
    for (auto& writer : writers)
    {
        writer.join();
    }
    CHECK_FALSE(handle);
}

TEST_CASE("Lazy initialization uses the process working directory and can reinitialize")
{
    const auto directory = MakeTestDirectory("lazy-initialization");
    std::filesystem::remove_all(directory);

    {
        CurrentDirectoryGuard currentDirectory(directory);
        Keire::Log::Shutdown();
        KEIRE_CORE_INFO("lazy initialization message");
        Keire::Log::Shutdown();
        CHECK(std::filesystem::is_regular_file(directory / "Logs" / "Core.log"));
    }

    LogFixture fixture("reinitialization");
    CHECK_NOTHROW(Keire::Log::Initialize(fixture.Config));
    KEIRE_CORE_INFO("reinitialized message");
    Keire::Log::Shutdown();
    CHECK(ReadFile(fixture.Directory / fixture.Config.CoreLogFile).find("reinitialized message") != std::string::npos);

    std::filesystem::remove_all(directory);
}
