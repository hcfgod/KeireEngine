#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "Keire/Api.h"
#include "Keire/Ref.h"

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

    struct FormatArgument
    {
        using Storage = std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;
        Storage Value;
    };

    namespace Detail
    {
        template <typename T>
        concept HasToString = requires(const T& value) {
            { value.ToString() } -> std::convertible_to<std::string>;
        };

        template <typename T> [[nodiscard]] FormatArgument MakeFormatArgument(T&& value)
        {
            using Value = std::remove_cvref_t<T>;
            if constexpr (std::same_as<Value, bool>)
                return {static_cast<bool>(value)};
            else if constexpr (std::same_as<Value, char>)
                return {std::string(1, value)};
            else if constexpr (std::is_enum_v<Value>)
                return MakeFormatArgument(static_cast<std::underlying_type_t<Value>>(value));
            else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>)
                return {static_cast<std::int64_t>(value)};
            else if constexpr (std::is_integral_v<Value>)
                return {static_cast<std::uint64_t>(value)};
            else if constexpr (std::is_floating_point_v<Value>)
                return {static_cast<double>(value)};
            else if constexpr (std::is_convertible_v<T, std::string_view>)
                return {std::string(std::string_view(std::forward<T>(value)))};
            else if constexpr (HasToString<Value>)
                return {std::string(value.ToString())};
            else if constexpr (std::is_pointer_v<Value>)
                return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value))};
            else
            {
                std::ostringstream stream;
                stream << std::forward<T>(value);
                return {stream.str()};
            }
        }
    } // namespace Detail

    [[nodiscard]] KEIRE_API std::string FormatLogMessage(std::string_view format,
                                                         std::span<const FormatArgument> arguments);

    template <typename... Arguments>
    [[nodiscard]] std::string LogMessage(const std::string_view format, Arguments&&... arguments)
    {
        const std::array<FormatArgument, sizeof...(Arguments)> values{
            Detail::MakeFormatArgument(std::forward<Arguments>(arguments))...};
        return FormatLogMessage(format, values);
    }

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
        [[nodiscard]] std::size_t SinkCount() const noexcept;
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

        [[nodiscard]] static LoggerHandle GetCoreLogger();
        [[nodiscard]] static LoggerHandle GetClientLogger();
    };
} // namespace Keire

#define KEIRE_LOG_LEVEL_TRACE 0
#define KEIRE_LOG_LEVEL_DEBUG 1
#define KEIRE_LOG_LEVEL_INFO 2
#define KEIRE_LOG_LEVEL_WARN 3
#define KEIRE_LOG_LEVEL_ERROR 4
#define KEIRE_LOG_LEVEL_CRITICAL 5
#define KEIRE_LOG_LEVEL_OFF 6

#ifndef KEIRE_COMPILED_LOG_LEVEL
#define KEIRE_COMPILED_LOG_LEVEL KEIRE_LOG_LEVEL_INFO
#endif

#define KEIRE_DETAIL_LOG(getter, logLevel, ...)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        auto keireDetailLoggerHandle = getter;                                                                         \
        if (keireDetailLoggerHandle)                                                                                   \
        {                                                                                                              \
            keireDetailLoggerHandle.Write(logLevel, ::Keire::LogMessage(__VA_ARGS__),                                  \
                                          std::source_location::current());                                            \
        }                                                                                                              \
    } while (false)

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_TRACE
#define KEIRE_CORE_TRACE(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Trace, __VA_ARGS__)
#define KEIRE_CLIENT_TRACE(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Trace, __VA_ARGS__)
#else
#define KEIRE_CORE_TRACE(...) (void)0
#define KEIRE_CLIENT_TRACE(...) (void)0
#endif

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_DEBUG
#define KEIRE_CORE_DEBUG(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Debug, __VA_ARGS__)
#define KEIRE_CLIENT_DEBUG(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Debug, __VA_ARGS__)
#else
#define KEIRE_CORE_DEBUG(...) (void)0
#define KEIRE_CLIENT_DEBUG(...) (void)0
#endif

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_INFO
#define KEIRE_CORE_INFO(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Info, __VA_ARGS__)
#define KEIRE_CLIENT_INFO(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Info, __VA_ARGS__)
#else
#define KEIRE_CORE_INFO(...) (void)0
#define KEIRE_CLIENT_INFO(...) (void)0
#endif

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_WARN
#define KEIRE_CORE_WARN(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Warn, __VA_ARGS__)
#define KEIRE_CLIENT_WARN(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Warn, __VA_ARGS__)
#else
#define KEIRE_CORE_WARN(...) (void)0
#define KEIRE_CLIENT_WARN(...) (void)0
#endif

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_ERROR
#define KEIRE_CORE_ERROR(...) KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Error, __VA_ARGS__)
#define KEIRE_CLIENT_ERROR(...) KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Error, __VA_ARGS__)
#else
#define KEIRE_CORE_ERROR(...) (void)0
#define KEIRE_CLIENT_ERROR(...) (void)0
#endif

#if KEIRE_COMPILED_LOG_LEVEL <= KEIRE_LOG_LEVEL_CRITICAL
#define KEIRE_CORE_CRITICAL(...)                                                                                       \
    KEIRE_DETAIL_LOG(::Keire::Log::GetCoreLogger(), ::Keire::LogLevel::Critical, __VA_ARGS__)
#define KEIRE_CLIENT_CRITICAL(...)                                                                                     \
    KEIRE_DETAIL_LOG(::Keire::Log::GetClientLogger(), ::Keire::LogLevel::Critical, __VA_ARGS__)
#else
#define KEIRE_CORE_CRITICAL(...) (void)0
#define KEIRE_CLIENT_CRITICAL(...) (void)0
#endif
