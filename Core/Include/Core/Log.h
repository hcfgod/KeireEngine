#ifndef CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_LOG_H
#define CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_LOG_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>

#include "spdlog/spdlog.h"

namespace Core
{
enum class LogLevel : std::uint8_t
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
    std::size_t MaxFileSizeBytes = std::size_t{5} * 1024 * 1024;
    std::size_t MaxFiles = 3;
    LogLevel Level =
#if defined(CORE_LOG_DEFAULT_TRACE)
        LogLevel::Trace;
#else
        LogLevel::Info;
#endif

    bool operator==(const LogConfig&) const = default;
};

class LoggerHandle
{
  public:
    LoggerHandle(LoggerHandle&&) noexcept = default;
    LoggerHandle& operator=(LoggerHandle&&) noexcept = default;
    LoggerHandle(const LoggerHandle&) = delete;
    LoggerHandle& operator=(const LoggerHandle&) = delete;

    spdlog::logger* operator->() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class Log;

    LoggerHandle(std::shared_ptr<spdlog::logger> logger, std::shared_lock<std::shared_mutex>&& lifecycleLock);

    std::shared_ptr<spdlog::logger> m_Logger;
    std::shared_lock<std::shared_mutex> m_LifecycleLock;
};

class Log
{
  public:
    static void Initialize(const LogConfig& config = LogConfig{});
    static void Shutdown() noexcept;
    static void Flush();
    static void SetLevel(LogLevel level);

    static LoggerHandle GetCoreLogger();
    static LoggerHandle GetClientLogger();

  private:
    static spdlog::level::level_enum ToSpdlogLevel(LogLevel level);
};
} // namespace Core

#define CORE_TRACE(...) SPDLOG_LOGGER_TRACE(::Core::Log::GetCoreLogger(), __VA_ARGS__)
#define CORE_DEBUG(...) SPDLOG_LOGGER_DEBUG(::Core::Log::GetCoreLogger(), __VA_ARGS__)
#define CORE_INFO(...) SPDLOG_LOGGER_INFO(::Core::Log::GetCoreLogger(), __VA_ARGS__)
#define CORE_WARN(...) SPDLOG_LOGGER_WARN(::Core::Log::GetCoreLogger(), __VA_ARGS__)
#define CORE_ERROR(...) SPDLOG_LOGGER_ERROR(::Core::Log::GetCoreLogger(), __VA_ARGS__)
#define CORE_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::Core::Log::GetCoreLogger(), __VA_ARGS__)

#define CLIENT_TRACE(...) SPDLOG_LOGGER_TRACE(::Core::Log::GetClientLogger(), __VA_ARGS__)
#define CLIENT_DEBUG(...) SPDLOG_LOGGER_DEBUG(::Core::Log::GetClientLogger(), __VA_ARGS__)
#define CLIENT_INFO(...) SPDLOG_LOGGER_INFO(::Core::Log::GetClientLogger(), __VA_ARGS__)
#define CLIENT_WARN(...) SPDLOG_LOGGER_WARN(::Core::Log::GetClientLogger(), __VA_ARGS__)
#define CLIENT_ERROR(...) SPDLOG_LOGGER_ERROR(::Core::Log::GetClientLogger(), __VA_ARGS__)
#define CLIENT_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::Core::Log::GetClientLogger(), __VA_ARGS__)

#endif
