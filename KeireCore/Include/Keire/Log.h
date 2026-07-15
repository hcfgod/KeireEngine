#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include "Keire/Api.h"
#include "Keire/Ref.h"
#include "spdlog/common.h"
#include "spdlog/fmt/fmt.h"

namespace Keire
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
#if defined(KEIRE_LOG_DEFAULT_TRACE)
            LogLevel::Trace;
#else
            LogLevel::Info;
#endif

        bool operator==(const LogConfig&) const = default;
    };

    namespace Detail
    {
        class LogState;

        enum class LogChannel : std::uint8_t
        {
            Core,
            Client
        };
    } // namespace Detail

    class KEIRE_API LoggerHandle
    {
      public:
        LoggerHandle(const LoggerHandle&) noexcept = default;
        LoggerHandle& operator=(const LoggerHandle&) noexcept = default;
        LoggerHandle(LoggerHandle&&) noexcept = default;
        LoggerHandle& operator=(LoggerHandle&&) noexcept = default;

        explicit operator bool() const noexcept;
        std::size_t SinkCount() const noexcept;
        void Flush() const;
        void SetLevel(LogLevel level) const;
        void Write(LogLevel level, std::string_view message,
                   std::source_location location = std::source_location::current()) const;

      private:
        friend class Log;

        LoggerHandle(Ref<Detail::LogState> state, Detail::LogChannel channel) noexcept;

        Ref<Detail::LogState> m_State;
        Detail::LogChannel m_Channel;
    };

    class KEIRE_API Log
    {
      public:
        static void Initialize(const LogConfig& config = LogConfig{});
        static void Shutdown() noexcept;
        static void Flush();
        static void SetLevel(LogLevel level);

        static LoggerHandle GetCoreLogger();
        static LoggerHandle GetClientLogger();
    };
} // namespace Keire

#define KEIRE_DETAIL_LOG(getter, logLevel, ...)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        auto keireDetailLoggerHandle = getter;                                                                         \
        if (keireDetailLoggerHandle)                                                                                   \
        {                                                                                                              \
            keireDetailLoggerHandle.Write(logLevel, ::fmt::format(__VA_ARGS__), std::source_location::current());      \
        }                                                                                                              \
    } while (false)

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define KEIRE_CORE_TRACE(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Trace, __VA_ARGS__)
#define KEIRE_CLIENT_TRACE(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Trace, __VA_ARGS__)
#else
#define KEIRE_CORE_TRACE(...) (void)0
#define KEIRE_CLIENT_TRACE(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define KEIRE_CORE_DEBUG(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Debug, __VA_ARGS__)
#define KEIRE_CLIENT_DEBUG(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Debug, __VA_ARGS__)
#else
#define KEIRE_CORE_DEBUG(...) (void)0
#define KEIRE_CLIENT_DEBUG(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#define KEIRE_CORE_INFO(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Info, __VA_ARGS__)
#define KEIRE_CLIENT_INFO(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Info, __VA_ARGS__)
#else
#define KEIRE_CORE_INFO(...) (void)0
#define KEIRE_CLIENT_INFO(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#define KEIRE_CORE_WARN(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Warn, __VA_ARGS__)
#define KEIRE_CLIENT_WARN(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Warn, __VA_ARGS__)
#else
#define KEIRE_CORE_WARN(...) (void)0
#define KEIRE_CLIENT_WARN(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#define KEIRE_CORE_ERROR(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Error, __VA_ARGS__)
#define KEIRE_CLIENT_ERROR(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Error, __VA_ARGS__)
#else
#define KEIRE_CORE_ERROR(...) (void)0
#define KEIRE_CLIENT_ERROR(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#define KEIRE_CORE_CRITICAL(...)                                                                                       \
    KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Critical, __VA_ARGS__)
#define KEIRE_CLIENT_CRITICAL(...)                                                                                     \
    KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Critical, __VA_ARGS__)
#else
#define KEIRE_CORE_CRITICAL(...) (void)0
#define KEIRE_CLIENT_CRITICAL(...) (void)0
#endif
