#ifndef CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_LOG_H
#define CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_LOG_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>

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
    bool EnableConsole = true;
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

    explicit operator bool() const noexcept;
    std::size_t SinkCount() const noexcept;
    void Flush() const;
    void SetLevel(LogLevel level) const;

    template <typename... Args>
    void Write(spdlog::source_loc location, spdlog::level::level_enum level, spdlog::format_string_t<Args...> format,
               Args&&... args) const
    {
        m_Logger->log(location, level, format, std::forward<Args>(args)...);
    }

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
    friend class LoggerHandle;
    static spdlog::level::level_enum ToSpdlogLevel(LogLevel level);
};
} // namespace Core

#define CORE_DETAIL_LOG(getter, logLevel, ...)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        auto coreDetailLoggerHandle = getter;                                                                          \
        coreDetailLoggerHandle.Write(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, logLevel,              \
                                     __VA_ARGS__);                                                                     \
    } while (false)

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define CORE_TRACE(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::trace, __VA_ARGS__)
#define CLIENT_TRACE(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::trace, __VA_ARGS__)
#else
#define CORE_TRACE(...) (void)0
#define CLIENT_TRACE(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define CORE_DEBUG(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::debug, __VA_ARGS__)
#define CLIENT_DEBUG(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::debug, __VA_ARGS__)
#else
#define CORE_DEBUG(...) (void)0
#define CLIENT_DEBUG(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#define CORE_INFO(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::info, __VA_ARGS__)
#define CLIENT_INFO(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::info, __VA_ARGS__)
#else
#define CORE_INFO(...) (void)0
#define CLIENT_INFO(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#define CORE_WARN(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::warn, __VA_ARGS__)
#define CLIENT_WARN(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::warn, __VA_ARGS__)
#else
#define CORE_WARN(...) (void)0
#define CLIENT_WARN(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#define CORE_ERROR(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::err, __VA_ARGS__)
#define CLIENT_ERROR(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::err, __VA_ARGS__)
#else
#define CORE_ERROR(...) (void)0
#define CLIENT_ERROR(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#define CORE_CRITICAL(...) CORE_DETAIL_LOG(::Core::Log::GetCoreLogger(), ::spdlog::level::critical, __VA_ARGS__)
#define CLIENT_CRITICAL(...) CORE_DETAIL_LOG(::Core::Log::GetClientLogger(), ::spdlog::level::critical, __VA_ARGS__)
#else
#define CORE_CRITICAL(...) (void)0
#define CLIENT_CRITICAL(...) (void)0
#endif

#endif
