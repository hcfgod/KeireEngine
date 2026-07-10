#include "Core/Log.h"

#include <filesystem>
#include <mutex>
#include <vector>

#include "spdlog/async.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Core
{
    namespace
    {
        std::shared_ptr<spdlog::logger> s_CoreLogger;
        std::shared_ptr<spdlog::logger> s_ClientLogger;
        std::mutex s_LogMutex;
        bool s_Initialized = false;

        std::string MakeLogPath(const std::string& directory, const std::string& file)
        {
            return (std::filesystem::path(directory) / file).string();
        }

        std::shared_ptr<spdlog::logger> CreateAsyncLogger(
            const std::string& name,
            const std::vector<spdlog::sink_ptr>& sinks,
            spdlog::level::level_enum level)
        {
            auto logger = std::make_shared<spdlog::async_logger>(
                name,
                sinks.begin(),
                sinks.end(),
                spdlog::thread_pool(),
                spdlog::async_overflow_policy::block);

            logger->set_level(level);
            logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(logger);
            return logger;
        }
    }

    void Log::Initialize(const LogConfig& config)
    {
        std::lock_guard<std::mutex> lock(s_LogMutex);
        if (s_Initialized)
        {
            return;
        }

        std::filesystem::create_directories(config.LogDirectory);

        spdlog::init_thread_pool(config.QueueSize, config.WorkerThreads);

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%T] [%n] [%^%l%$] [thread %t] %v");

        auto coreFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            MakeLogPath(config.LogDirectory, config.CoreLogFile),
            config.MaxFileSizeBytes,
            config.MaxFiles);

        auto clientFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            MakeLogPath(config.LogDirectory, config.ClientLogFile),
            config.MaxFileSizeBytes,
            config.MaxFiles);

        coreFileSink->set_pattern("[%Y-%m-%d %T.%e] [%n] [%l] [thread %t] %v");
        clientFileSink->set_pattern("[%Y-%m-%d %T.%e] [%n] [%l] [thread %t] %v");

        const auto level = ToSpdlogLevel(config.Level);
        s_CoreLogger = CreateAsyncLogger("Core", { consoleSink, coreFileSink }, level);
        s_ClientLogger = CreateAsyncLogger("Client", { consoleSink, clientFileSink }, level);

        spdlog::set_default_logger(s_CoreLogger);
        s_Initialized = true;

        CORE_INFO("Core logger initialized");
    }

    void Log::Shutdown()
    {
        std::lock_guard<std::mutex> lock(s_LogMutex);
        if (!s_Initialized)
        {
            return;
        }

        Flush();
        s_CoreLogger.reset();
        s_ClientLogger.reset();
        spdlog::drop_all();
        spdlog::shutdown();
        s_Initialized = false;
    }

    void Log::Flush()
    {
        if (s_CoreLogger)
        {
            s_CoreLogger->flush();
        }

        if (s_ClientLogger)
        {
            s_ClientLogger->flush();
        }
    }

    void Log::SetLevel(LogLevel level)
    {
        const auto spdlogLevel = ToSpdlogLevel(level);

        if (s_CoreLogger)
        {
            s_CoreLogger->set_level(spdlogLevel);
        }

        if (s_ClientLogger)
        {
            s_ClientLogger->set_level(spdlogLevel);
        }
    }

    std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
    {
        if (!s_CoreLogger)
        {
            Initialize();
        }

        return s_CoreLogger;
    }

    std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
    {
        if (!s_ClientLogger)
        {
            Initialize();
        }

        return s_ClientLogger;
    }

    spdlog::level::level_enum Log::ToSpdlogLevel(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace: return spdlog::level::trace;
            case LogLevel::Debug: return spdlog::level::debug;
            case LogLevel::Info: return spdlog::level::info;
            case LogLevel::Warn: return spdlog::level::warn;
            case LogLevel::Error: return spdlog::level::err;
            case LogLevel::Critical: return spdlog::level::critical;
            case LogLevel::Off: return spdlog::level::off;
        }

        return spdlog::level::info;
    }
}
