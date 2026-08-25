#include "Keire/Log.h"

#include "KeireInternal/LogInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <vector>

#include "spdlog/async.h"
#include "spdlog/details/thread_pool.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Keire
{
    namespace
    {
        std::string MakeLogPath(const std::string& directory, const std::string& file)
        {
            return (std::filesystem::path(directory) / file).string();
        }

        spdlog::level::level_enum ToSpdlogLevel(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warn:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Critical:
                return spdlog::level::critical;
            case LogLevel::Off:
                return spdlog::level::off;
            }
            return spdlog::level::info;
        }

        void ValidateConfig(const LogConfig& config)
        {
            if (config.LogDirectory.empty() || config.CoreLogFile.empty() || config.ClientLogFile.empty())
            {
                throw std::invalid_argument("Log paths and filenames must not be empty.");
            }
            if (config.QueueSize == 0 || config.WorkerThreads == 0)
            {
                throw std::invalid_argument("Log queue size and worker thread count must be greater than zero.");
            }
            if (config.MaxFileSizeBytes == 0 || config.MaxFiles == 0)
            {
                throw std::invalid_argument("Log rotation size and file count must be greater than zero.");
            }

            const auto validateFileName = [](const std::string& fileName)
            {
                const std::filesystem::path path(fileName);
                if (fileName.find('\0') != std::string::npos || fileName.find('/') != std::string::npos ||
                    fileName.find('\\') != std::string::npos || path.is_absolute() || path.has_parent_path() ||
                    fileName == "." || fileName == "..")
                {
                    throw std::invalid_argument("Log filenames must be plain filenames without directory components.");
                }
            };

            validateFileName(config.CoreLogFile);
            validateFileName(config.ClientLogFile);
            if (config.CoreLogFile == config.ClientLogFile)
            {
                throw std::invalid_argument("Core and Client log filenames must be different.");
            }
            if (config.LogDirectory.find('\0') != std::string::npos)
            {
                throw std::invalid_argument("The log directory contains an embedded null character.");
            }

            std::error_code error;
            const std::filesystem::path directory(config.LogDirectory);
            const bool directoryExists = std::filesystem::exists(directory, error);
            if (error)
            {
                throw std::invalid_argument("The configured log directory cannot be inspected.");
            }
            if (directoryExists && !std::filesystem::is_directory(directory, error))
            {
                throw std::invalid_argument("The configured log directory is not a directory.");
            }
            if (error)
            {
                throw std::invalid_argument("The configured log directory cannot be inspected.");
            }
        }

        std::shared_ptr<spdlog::logger>
        CreateAsyncLogger(const std::string& name, const std::vector<spdlog::sink_ptr>& sinks,
                          const spdlog::level::level_enum level,
                          const std::shared_ptr<spdlog::details::thread_pool>& threadPool)
        {
            auto logger = std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), threadPool,
                                                                 spdlog::async_overflow_policy::block);
            logger->set_level(level);
            logger->flush_on(spdlog::level::warn);
            return logger;
        }

        struct FormatSpecification final
        {
            char Fill = ' ';
            char Alignment = 0;
            char Type = 0;
            std::size_t Width = 0;
            std::optional<std::size_t> Precision;
        };

        [[nodiscard]] FormatSpecification ParseFormatSpecification(std::string_view text)
        {
            FormatSpecification result;
            if (text.empty())
                return result;
            if (text.front() != ':')
                throw std::invalid_argument("Kéire log placeholders support only ':' format specifications.");
            text.remove_prefix(1);
            if (text.size() >= 2 && (text[1] == '<' || text[1] == '>' || text[1] == '^'))
            {
                result.Fill = text[0];
                result.Alignment = text[1];
                text.remove_prefix(2);
            }
            else if (!text.empty() && (text.front() == '<' || text.front() == '>' || text.front() == '^'))
            {
                result.Alignment = text.front();
                text.remove_prefix(1);
            }
            if (!text.empty() && text.front() == '0')
            {
                result.Fill = '0';
                result.Alignment = '>';
                text.remove_prefix(1);
            }
            while (!text.empty() && text.front() >= '0' && text.front() <= '9')
            {
                const auto digit = static_cast<std::size_t>(text.front() - '0');
                if (result.Width > (std::numeric_limits<std::size_t>::max() - digit) / 10)
                    throw std::invalid_argument("Kéire log format width is too large.");
                result.Width = result.Width * 10 + digit;
                text.remove_prefix(1);
            }
            if (!text.empty() && text.front() == '.')
            {
                text.remove_prefix(1);
                if (text.empty() || text.front() < '0' || text.front() > '9')
                    throw std::invalid_argument("Kéire log precision requires digits.");
                std::size_t precision = 0;
                while (!text.empty() && text.front() >= '0' && text.front() <= '9')
                {
                    precision = precision * 10 + static_cast<std::size_t>(text.front() - '0');
                    if (precision > 1000)
                        throw std::invalid_argument("Kéire log precision is too large.");
                    text.remove_prefix(1);
                }
                result.Precision = precision;
            }
            if (!text.empty())
            {
                result.Type = text.front();
                text.remove_prefix(1);
            }
            if (!text.empty())
                throw std::invalid_argument("Kéire log format specification contains unsupported characters.");
            return result;
        }

        [[nodiscard]] std::string ApplyWidth(std::string value, const FormatSpecification& specification,
                                             const bool numeric)
        {
            if (value.size() >= specification.Width)
                return value;
            const auto padding = specification.Width - value.size();
            const auto alignment = specification.Alignment != 0 ? specification.Alignment : (numeric ? '>' : '<');
            if (alignment == '<')
                return value + std::string(padding, specification.Fill);
            if (alignment == '^')
            {
                const auto left = padding / 2;
                return std::string(left, specification.Fill) + value + std::string(padding - left, specification.Fill);
            }
            if (specification.Fill == '0' && !value.empty() && (value.front() == '-' || value.front() == '+'))
                return value.substr(0, 1) + std::string(padding, '0') + value.substr(1);
            return std::string(padding, specification.Fill) + value;
        }

        template <typename Integer>
        [[nodiscard]] std::string FormatInteger(const Integer value, const FormatSpecification& specification)
        {
            const bool hexadecimal = specification.Type == 'x' || specification.Type == 'X';
            if (specification.Type != 0 && !hexadecimal && specification.Type != 'd')
                throw std::invalid_argument("This Kéire log format type is not valid for an integer.");
            std::array<char, 128> buffer{};
            const auto [end, error] =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, hexadecimal ? 16 : 10);
            if (error != std::errc{})
                throw std::runtime_error("Kéire could not format an integer log argument.");
            std::string result(buffer.data(), end);
            if (specification.Type == 'X')
                std::ranges::transform(
                    result, result.begin(), [](const char character)
                    { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
            return ApplyWidth(std::move(result), specification, true);
        }

        [[nodiscard]] std::string FormatArgumentValue(const FormatArgument& argument,
                                                      const FormatSpecification& specification)
        {
            return std::visit(
                [&](const auto& value) -> std::string
                {
                    using Value = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<Value, std::int64_t> || std::same_as<Value, std::uint64_t>)
                        return FormatInteger(value, specification);
                    else if constexpr (std::same_as<Value, double>)
                    {
                        if (specification.Type != 0 && specification.Type != 'f' && specification.Type != 'e' &&
                            specification.Type != 'E' && specification.Type != 'g' && specification.Type != 'G')
                            throw std::invalid_argument("This Kéire log format type is not valid for a scalar.");
                        std::ostringstream stream;
                        if (specification.Precision)
                            stream << std::setprecision(static_cast<int>(*specification.Precision));
                        if (specification.Type == 'f')
                            stream << std::fixed;
                        else if (specification.Type == 'e' || specification.Type == 'E')
                            stream << std::scientific;
                        stream << value;
                        auto result = stream.str();
                        if (specification.Type == 'E' || specification.Type == 'G')
                            std::ranges::transform(
                                result, result.begin(), [](const char character)
                                { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
                        return ApplyWidth(std::move(result), specification, true);
                    }
                    else if constexpr (std::same_as<Value, bool>)
                    {
                        if (specification.Type != 0)
                            throw std::invalid_argument("Kéire Boolean log arguments do not accept a format type.");
                        return ApplyWidth(value ? "true" : "false", specification, false);
                    }
                    else
                    {
                        if (specification.Type != 0 && specification.Type != 's')
                            throw std::invalid_argument("This Kéire log format type is not valid for text.");
                        auto result = value;
                        if (specification.Precision && result.size() > *specification.Precision)
                            result.resize(*specification.Precision);
                        return ApplyWidth(std::move(result), specification, false);
                    }
                },
                argument.Value);
        }
    } // namespace

    std::string FormatLogMessage(const std::string_view format, const std::span<const FormatArgument> arguments)
    {
        std::string result;
        result.reserve(format.size() + arguments.size() * 8);
        std::size_t argumentIndex = 0;
        for (std::size_t index = 0; index < format.size();)
        {
            if (format[index] == '{')
            {
                if (index + 1 < format.size() && format[index + 1] == '{')
                {
                    result.push_back('{');
                    index += 2;
                    continue;
                }
                const auto close = format.find('}', index + 1);
                if (close == std::string_view::npos)
                    throw std::invalid_argument("Kéire log format contains an unmatched '{'.");
                if (argumentIndex >= arguments.size())
                    throw std::invalid_argument("Kéire log format has more placeholders than arguments.");
                result += FormatArgumentValue(arguments[argumentIndex++],
                                              ParseFormatSpecification(format.substr(index + 1, close - index - 1)));
                index = close + 1;
                continue;
            }
            if (format[index] == '}')
            {
                if (index + 1 < format.size() && format[index + 1] == '}')
                {
                    result.push_back('}');
                    index += 2;
                    continue;
                }
                throw std::invalid_argument("Kéire log format contains an unmatched '}'.");
            }
            result.push_back(format[index++]);
        }
        if (argumentIndex != arguments.size())
            throw std::invalid_argument("Kéire log format has fewer placeholders than arguments.");
        return result;
    }

    namespace Detail
    {
        class LogState final : public RefCounted
        {
          public:
            explicit LogState(const LogConfig& config) : m_Config(config)
            {
                ValidateConfig(config);
                std::filesystem::create_directories(config.LogDirectory);

                auto threadPool =
                    std::make_shared<spdlog::details::thread_pool>(config.QueueSize, config.WorkerThreads);
                std::vector<spdlog::sink_ptr> coreSinks;
                std::vector<spdlog::sink_ptr> clientSinks;

                if (config.EnableConsole)
                {
                    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    consoleSink->set_pattern("[%T] [%n] [%^%l%$] [thread %t] %v");
                    coreSinks.push_back(consoleSink);
                    clientSinks.push_back(consoleSink);
                }

                auto coreFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    MakeLogPath(config.LogDirectory, config.CoreLogFile), config.MaxFileSizeBytes, config.MaxFiles);
                auto clientFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    MakeLogPath(config.LogDirectory, config.ClientLogFile), config.MaxFileSizeBytes, config.MaxFiles);

                coreFileSink->set_pattern("[%Y-%m-%d %T.%e] [%n] [%l] [thread %t] %v");
                clientFileSink->set_pattern("[%Y-%m-%d %T.%e] [%n] [%l] [thread %t] %v");
                coreSinks.push_back(coreFileSink);
                clientSinks.push_back(clientFileSink);

                const auto level = ToSpdlogLevel(config.Level);
                auto coreLogger = CreateAsyncLogger("KeireCore", coreSinks, level, threadPool);
                auto clientLogger = CreateAsyncLogger("KeireClient", clientSinks, level, threadPool);
                coreLogger->info("Core logger initialized");

                m_CoreLogger = std::move(coreLogger);
                m_ClientLogger = std::move(clientLogger);
                m_ThreadPool = std::move(threadPool);
                m_Open.store(true, std::memory_order_release);
                Retain(LogChannel::Core, LogLevel::Info, "Core logger initialized");
            }

            ~LogState() override { Close(); }

            [[nodiscard]] bool IsOpen() const noexcept { return m_Open.load(std::memory_order_acquire); }
            [[nodiscard]] bool Matches(const LogConfig& config) const noexcept { return m_Config == config; }

            [[nodiscard]] std::size_t SinkCount(const LogChannel channel) const noexcept
            {
                try
                {
                    std::shared_lock lock(m_OperationMutex);
                    const auto& logger = SelectLogger(channel);
                    return m_Open.load(std::memory_order_relaxed) && logger ? logger->sinks().size() : 0;
                }
                catch (...)
                {
                    return 0;
                }
            }

            void Flush(const LogChannel channel) const
            {
                std::shared_lock lock(m_OperationMutex);
                const auto& logger = SelectLogger(channel);
                if (m_Open.load(std::memory_order_relaxed) && logger)
                {
                    logger->flush();
                }
            }

            void SetLevel(const LogChannel channel, const LogLevel level) const
            {
                std::shared_lock lock(m_OperationMutex);
                const auto& logger = SelectLogger(channel);
                if (m_Open.load(std::memory_order_relaxed) && logger)
                {
                    logger->set_level(ToSpdlogLevel(level));
                }
            }

            void SetLevel(const LogLevel level) const
            {
                std::shared_lock lock(m_OperationMutex);
                if (!m_Open.load(std::memory_order_relaxed))
                {
                    return;
                }
                const auto spdlogLevel = ToSpdlogLevel(level);
                m_CoreLogger->set_level(spdlogLevel);
                m_ClientLogger->set_level(spdlogLevel);
            }

            void Flush() const
            {
                std::shared_lock lock(m_OperationMutex);
                if (!m_Open.load(std::memory_order_relaxed))
                {
                    return;
                }
                m_CoreLogger->flush();
                m_ClientLogger->flush();
            }

            void Write(const LogChannel channel, const LogLevel level, const std::string_view message,
                       const std::source_location location) const
            {
                std::shared_lock lock(m_OperationMutex);
                const auto& logger = SelectLogger(channel);
                if (!m_Open.load(std::memory_order_relaxed) || !logger)
                {
                    return;
                }
                const auto spdlogLevel = ToSpdlogLevel(level);
                if (!logger->should_log(spdlogLevel))
                {
                    return;
                }
                const spdlog::source_loc source{location.file_name(), static_cast<int>(location.line()),
                                                location.function_name()};
                logger->log(source, spdlogLevel, spdlog::string_view_t(message.data(), message.size()));
                Retain(channel, level, message);
            }

            [[nodiscard]] std::vector<RetainedLogRecord> ReadRecordsSince(const std::uint64_t sequence) const
            {
                std::shared_lock operationLock(m_OperationMutex);
                if (!m_Open.load(std::memory_order_relaxed))
                {
                    return {};
                }
                std::lock_guard recordLock(m_RecordMutex);
                const auto first = std::upper_bound(m_Records.begin(), m_Records.end(), sequence,
                                                    [](const std::uint64_t value, const RetainedLogRecord& record)
                                                    { return value < record.Sequence; });
                return {first, m_Records.end()};
            }

            void Close() noexcept
            {
                try
                {
                    std::unique_lock lock(m_OperationMutex);
                    if (!m_Open.exchange(false, std::memory_order_acq_rel))
                    {
                        return;
                    }
                    try
                    {
                        m_CoreLogger->flush();
                        m_ClientLogger->flush();
                    }
                    catch (const std::exception& error)
                    {
                        std::fprintf(stderr, "Logger flush during shutdown failed: %s\n", error.what());
                    }
                    m_CoreLogger.reset();
                    m_ClientLogger.reset();
                    m_ThreadPool.reset();
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr, "Logger shutdown failed: %s\n", error.what());
                }
            }

          private:
            void Retain(const LogChannel channel, const LogLevel level, const std::string_view message) const noexcept
            {
                try
                {
                    constexpr std::size_t maximumRetainedRecords = 2048;
                    std::lock_guard lock(m_RecordMutex);
                    m_Records.push_back({m_NextRecordSequence++, channel, level, std::string(message)});
                    while (m_Records.size() > maximumRetainedRecords)
                    {
                        m_Records.pop_front();
                    }
                }
                catch (...)
                {
                }
            }

            const std::shared_ptr<spdlog::logger>& SelectLogger(const LogChannel channel) const noexcept
            {
                return channel == LogChannel::Core ? m_CoreLogger : m_ClientLogger;
            }

            LogConfig m_Config;
            mutable std::shared_mutex m_OperationMutex;
            std::atomic<bool> m_Open{false};
            std::shared_ptr<spdlog::logger> m_CoreLogger;
            std::shared_ptr<spdlog::logger> m_ClientLogger;
            std::shared_ptr<spdlog::details::thread_pool> m_ThreadPool;
            mutable std::mutex m_RecordMutex;
            mutable std::deque<RetainedLogRecord> m_Records;
            mutable std::uint64_t m_NextRecordSequence = 1;
        };
    } // namespace Detail

    namespace
    {
        std::mutex s_LogMutex;
        Ref<Detail::LogState> s_LogState;

        Ref<Detail::LogState> GetOrCreateState()
        {
            std::lock_guard lock(s_LogMutex);
            if (!s_LogState || !s_LogState->IsOpen())
            {
                s_LogState = CreateRef<Detail::LogState>(LogConfig{});
            }
            return s_LogState;
        }

        Ref<Detail::LogState> GetCurrentState()
        {
            std::lock_guard lock(s_LogMutex);
            return s_LogState;
        }
    } // namespace

    LoggerHandle::LoggerHandle(Ref<Detail::LogState> state, const Detail::LogChannel channel) noexcept
        : m_State(std::move(state)), m_Channel(channel)
    {
    }

    LoggerHandle::operator bool() const noexcept { return m_State && m_State->IsOpen(); }

    std::size_t LoggerHandle::SinkCount() const noexcept { return m_State ? m_State->SinkCount(m_Channel) : 0; }

    void LoggerHandle::Flush() const
    {
        if (m_State)
        {
            m_State->Flush(m_Channel);
        }
    }

    void LoggerHandle::SetLevel(const LogLevel level) const
    {
        if (m_State)
        {
            m_State->SetLevel(m_Channel, level);
        }
    }

    void LoggerHandle::Write(const LogLevel level, const std::string_view message,
                             const std::source_location location) const
    {
        if (m_State)
        {
            m_State->Write(m_Channel, level, message, location);
        }
    }

    void Log::Initialize(const LogConfig& config)
    {
        ValidateConfig(config);
        std::lock_guard lock(s_LogMutex);
        if (s_LogState && s_LogState->IsOpen())
        {
            if (s_LogState->Matches(config))
            {
                return;
            }
            throw std::logic_error("Logging is already initialized with a different configuration.");
        }
        s_LogState = CreateRef<Detail::LogState>(config);
    }

    void Log::Shutdown() noexcept
    {
        Ref<Detail::LogState> state;
        {
            std::lock_guard lock(s_LogMutex);
            state = std::move(s_LogState);
        }
        if (state)
        {
            state->Close();
        }
    }

    void Log::Flush()
    {
        const auto state = GetCurrentState();
        if (state)
        {
            state->Flush();
        }
    }

    void Log::SetLevel(const LogLevel level)
    {
        const auto state = GetCurrentState();
        if (state)
        {
            state->SetLevel(level);
        }
    }

    LoggerHandle Log::GetCoreLogger() { return LoggerHandle(GetOrCreateState(), Detail::LogChannel::Core); }

    LoggerHandle Log::GetClientLogger() { return LoggerHandle(GetOrCreateState(), Detail::LogChannel::Client); }

    bool Detail::LogInternalAccess::WriteAndFlushIfOpen(const LogChannel channel, const LogLevel level,
                                                        const std::string_view message,
                                                        const std::source_location location) noexcept
    {
        try
        {
            const auto state = GetCurrentState();
            if (!state || !state->IsOpen())
                return false;
            state->Write(channel, level, message, location);
            state->Flush(channel);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::vector<Detail::RetainedLogRecord> Detail::LogInternalAccess::ReadRecordsSince(const std::uint64_t sequence)
    {
        const auto state = GetCurrentState();
        return state ? state->ReadRecordsSince(sequence) : std::vector<RetainedLogRecord>{};
    }
} // namespace Keire
