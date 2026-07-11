#include "Core/Log.h"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include "spdlog/async.h"
#include "spdlog/details/thread_pool.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Core
{
namespace
{
std::shared_ptr<spdlog::logger> s_CoreLogger;
std::shared_ptr<spdlog::logger> s_ClientLogger;
std::shared_ptr<spdlog::details::thread_pool> s_ThreadPool;
std::shared_mutex s_LogMutex;
bool s_Initialized = false;
std::optional<LogConfig> s_ActiveConfig;

std::string MakeLogPath(const std::string& directory, const std::string& file)
{
    return (std::filesystem::path(directory) / file).string();
}

std::shared_ptr<spdlog::logger> CreateAsyncLogger(const std::string& name, const std::vector<spdlog::sink_ptr>& sinks,
                                                  spdlog::level::level_enum level,
                                                  const std::shared_ptr<spdlog::details::thread_pool>& threadPool)
{
    auto logger = std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), threadPool,
                                                         spdlog::async_overflow_policy::block);

    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);
    return logger;
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

void InstallLoggersLocked(const LogConfig& config, spdlog::level::level_enum level)
{
    std::filesystem::create_directories(config.LogDirectory);

    auto threadPool = std::make_shared<spdlog::details::thread_pool>(config.QueueSize, config.WorkerThreads);
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

    auto coreLogger = CreateAsyncLogger("Core", coreSinks, level, threadPool);
    auto clientLogger = CreateAsyncLogger("Client", clientSinks, level, threadPool);

    coreLogger->info("Core logger initialized");
    s_CoreLogger = std::move(coreLogger);
    s_ClientLogger = std::move(clientLogger);
    s_ThreadPool = std::move(threadPool);
    s_ActiveConfig = config;
    s_Initialized = true;
}
} // namespace

LoggerHandle::LoggerHandle(std::shared_ptr<spdlog::logger> logger, std::shared_lock<std::shared_mutex>&& lifecycleLock)
    : m_Logger(std::move(logger)), m_LifecycleLock(std::move(lifecycleLock))
{
}

LoggerHandle::operator bool() const noexcept { return static_cast<bool>(m_Logger); }

std::size_t LoggerHandle::SinkCount() const noexcept { return m_Logger ? m_Logger->sinks().size() : 0; }

void LoggerHandle::Flush() const
{
    if (m_Logger)
    {
        m_Logger->flush();
    }
}

void LoggerHandle::SetLevel(LogLevel level) const
{
    if (m_Logger)
    {
        m_Logger->set_level(Log::ToSpdlogLevel(level));
    }
}

void Log::Initialize(const LogConfig& config)
{
    ValidateConfig(config);
    std::unique_lock<std::shared_mutex> lock(s_LogMutex);
    if (s_Initialized)
    {
        if (s_ActiveConfig && *s_ActiveConfig == config)
        {
            return;
        }
        throw std::logic_error("Logging is already initialized with a different configuration.");
    }

    InstallLoggersLocked(config, ToSpdlogLevel(config.Level));
}

void Log::Shutdown() noexcept
{
    try
    {
        std::unique_lock<std::shared_mutex> lock(s_LogMutex);
        if (!s_Initialized)
        {
            return;
        }

        try
        {
            s_CoreLogger->flush();
            s_ClientLogger->flush();
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Logger flush during shutdown failed: %s\n", error.what());
        }

        s_CoreLogger.reset();
        s_ClientLogger.reset();
        s_ThreadPool.reset();
        s_ActiveConfig.reset();
        s_Initialized = false;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Logger shutdown failed: %s\n", error.what());
        s_CoreLogger.reset();
        s_ClientLogger.reset();
        s_ThreadPool.reset();
        s_ActiveConfig.reset();
        s_Initialized = false;
    }
}

void Log::Flush()
{
    std::shared_lock<std::shared_mutex> lock(s_LogMutex);
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
    std::shared_lock<std::shared_mutex> lock(s_LogMutex);
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

LoggerHandle Log::GetCoreLogger()
{
    while (true)
    {
        std::shared_lock<std::shared_mutex> lock(s_LogMutex);
        if (s_CoreLogger)
        {
            return LoggerHandle(s_CoreLogger, std::move(lock));
        }
        lock.unlock();

        const LogConfig config;
        ValidateConfig(config);
        std::unique_lock<std::shared_mutex> initializationLock(s_LogMutex);
        if (!s_CoreLogger)
        {
            InstallLoggersLocked(config, ToSpdlogLevel(config.Level));
        }
    }
}

LoggerHandle Log::GetClientLogger()
{
    while (true)
    {
        std::shared_lock<std::shared_mutex> lock(s_LogMutex);
        if (s_ClientLogger)
        {
            return LoggerHandle(s_ClientLogger, std::move(lock));
        }
        lock.unlock();

        const LogConfig config;
        ValidateConfig(config);
        std::unique_lock<std::shared_mutex> initializationLock(s_LogMutex);
        if (!s_ClientLogger)
        {
            InstallLoggersLocked(config, ToSpdlogLevel(config.Level));
        }
    }
}

spdlog::level::level_enum Log::ToSpdlogLevel(LogLevel level)
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
} // namespace Core
