#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "spdlog/spdlog.h"

namespace Core
{
    enum class LogLevel
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

    struct LogConfig
    {
        std::string LogDirectory = "Logs";
        std::string CoreLogFile = "Core.log";
        std::string ClientLogFile = "Client.log";
        std::size_t QueueSize = 8192;
        std::size_t WorkerThreads = 1;
        std::size_t MaxFileSizeBytes = 1024 * 1024 * 5;
        std::size_t MaxFiles = 3;
        LogLevel Level =
#if defined(CORE_LOG_DEFAULT_TRACE)
            LogLevel::Trace;
#else
            LogLevel::Info;
#endif
    };

    class Log
    {
    public:
        static void Initialize(const LogConfig& config = LogConfig{});
        static void Shutdown();
        static void Flush();
        static void SetLevel(LogLevel level);

        static std::shared_ptr<spdlog::logger>& GetCoreLogger();
        static std::shared_ptr<spdlog::logger>& GetClientLogger();

    private:
        static spdlog::level::level_enum ToSpdlogLevel(LogLevel level);
    };
}

#define CORE_TRACE(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::trace, __VA_ARGS__)
#define CORE_DEBUG(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::debug, __VA_ARGS__)
#define CORE_INFO(...)     SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::info, __VA_ARGS__)
#define CORE_WARN(...)     SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::warn, __VA_ARGS__)
#define CORE_ERROR(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::err, __VA_ARGS__)
#define CORE_CRITICAL(...) SPDLOG_LOGGER_CALL(::Core::Log::GetCoreLogger(), spdlog::level::critical, __VA_ARGS__)

#define CLIENT_TRACE(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::trace, __VA_ARGS__)
#define CLIENT_DEBUG(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::debug, __VA_ARGS__)
#define CLIENT_INFO(...)     SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::info, __VA_ARGS__)
#define CLIENT_WARN(...)     SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::warn, __VA_ARGS__)
#define CLIENT_ERROR(...)    SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::err, __VA_ARGS__)
#define CLIENT_CRITICAL(...) SPDLOG_LOGGER_CALL(::Core::Log::GetClientLogger(), spdlog::level::critical, __VA_ARGS__)
