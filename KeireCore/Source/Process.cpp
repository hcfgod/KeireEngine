#include "KeireInternal/Process.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>

#if defined(_MSC_VER)
#pragma comment(lib, "Shell32.lib")
#endif
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Keire::Detail
{
    namespace
    {
#if defined(_WIN32)
        [[nodiscard]] std::string WideToUtf8(const std::wstring_view value)
        {
            if (value.empty())
                return {};
            const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Windows command-line argument is not valid Unicode");
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), size, nullptr, nullptr) != size)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Windows command-line argument could not be converted to UTF-8");
            return result;
        }

        [[nodiscard]] std::wstring Utf8ToWide(const std::string_view value)
        {
            if (value.empty())
                return {};
            const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
                throw std::runtime_error("Process argument is not valid UTF-8.");
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), size) != size)
                throw std::runtime_error("Process argument could not be converted to UTF-16.");
            return result;
        }

        [[nodiscard]] std::wstring QuoteWindowsArgument(const std::wstring_view value)
        {
            if (value.find_first_of(L" \t\"") == std::wstring_view::npos)
                return std::wstring(value);
            std::wstring result(1, L'\"');
            std::size_t slashes = 0;
            for (const wchar_t character : value)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (character == L'\"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                }
                else
                {
                    result.append(slashes, L'\\');
                    result.push_back(character);
                }
                slashes = 0;
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }
#endif
    } // namespace

    Utf8CommandLine::Utf8CommandLine(const int count, char* const* values)
    {
#if defined(_WIN32)
        (void)count;
        (void)values;
        int wideCount = 0;
        auto* wideValues = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (!wideValues)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Failed to read the Windows command line");
        const auto release = [](wchar_t** arguments)
        {
            if (arguments)
                LocalFree(arguments);
        };
        const std::unique_ptr<wchar_t*, decltype(release)> owner(wideValues, release);
        m_Arguments.reserve(wideCount > 0 ? static_cast<std::size_t>(wideCount) : 0);
        for (int index = 0; index < wideCount; ++index)
            m_Arguments.push_back(
                WideToUtf8(wideValues[index] ? std::wstring_view{wideValues[index]} : std::wstring_view{}));
#else
        m_Arguments.reserve(count > 0 && values ? static_cast<std::size_t>(count) : 0);
        for (int index = 0; index < count && values; ++index)
            m_Arguments.emplace_back(values[index] ? values[index] : "");
#endif
        m_Pointers.reserve(m_Arguments.size());
        for (auto& argument : m_Arguments)
            m_Pointers.push_back(argument.data());
    }

    class ChildProcess::Impl final
    {
      public:
        ~Impl() { Terminate(); }

        void DrainOutput()
        {
            constexpr std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
            std::array<char, 4096> buffer{};
#if defined(_WIN32)
            if (!m_ReadPipe)
                return;
            DWORD available = 0;
            while (PeekNamedPipe(m_ReadPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
            {
                DWORD read = 0;
                if (!ReadFile(m_ReadPipe, buffer.data(),
                              static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available)), &read, nullptr))
                    break;
                if (m_Output.size() < maximumOutputBytes)
                    m_Output.append(buffer.data(), std::min<std::size_t>(read, maximumOutputBytes - m_Output.size()));
                available -= read;
            }
#else
            if (m_ReadDescriptor < 0)
                return;
            for (;;)
            {
                const auto count = read(m_ReadDescriptor, buffer.data(), buffer.size());
                if (count <= 0)
                    break;
                if (m_Output.size() < maximumOutputBytes)
                    m_Output.append(buffer.data(), std::min<std::size_t>(static_cast<std::size_t>(count),
                                                                         maximumOutputBytes - m_Output.size()));
            }
#endif
        }

        bool Poll()
        {
            if (!m_Running)
                return true;
            DrainOutput();
#if defined(_WIN32)
            const auto status = WaitForSingleObject(m_Process, 0);
            if (status == WAIT_TIMEOUT)
                return false;
            if (status != WAIT_OBJECT_0)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Child process wait failed");
            DWORD exitCode = 127;
            (void)GetExitCodeProcess(m_Process, &exitCode);
            m_ExitCode = static_cast<int>(exitCode);
#else
            int status = 0;
            const auto waited = waitpid(m_Process, &status, WNOHANG);
            if (waited == 0)
                return false;
            if (waited < 0)
                throw std::system_error(errno, std::generic_category(), "Child process wait failed");
            m_ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
            m_Running = false;
            DrainOutput();
            CloseHandles();
            return true;
        }

        void Terminate() noexcept
        {
            if (!m_Running)
            {
                CloseHandles();
                return;
            }
#if defined(_WIN32)
            (void)TerminateProcess(m_Process, 125);
            (void)WaitForSingleObject(m_Process, 250);
            m_ExitCode = 125;
#else
            (void)kill(m_Process, SIGKILL);
            int status = 0;
            bool reaped = false;
            for (int attempt = 0; attempt < 50; ++attempt)
            {
                const auto waited = waitpid(m_Process, &status, WNOHANG);
                if (waited == m_Process || (waited < 0 && errno == ECHILD))
                {
                    reaped = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!reaped)
            {
                const auto process = m_Process;
                std::thread(
                    [process]
                    {
                        int detachedStatus = 0;
                        (void)waitpid(process, &detachedStatus, 0);
                    })
                    .detach();
            }
            m_ExitCode = reaped && WIFEXITED(status) ? WEXITSTATUS(status) : 128 + SIGKILL;
#endif
            m_Running = false;
            DrainOutput();
            CloseHandles();
        }

        void CloseHandles() noexcept
        {
#if defined(_WIN32)
            if (m_Thread)
                CloseHandle(std::exchange(m_Thread, nullptr));
            if (m_Process)
                CloseHandle(std::exchange(m_Process, nullptr));
            if (m_ReadPipe)
                CloseHandle(std::exchange(m_ReadPipe, nullptr));
#else
            if (m_ReadDescriptor >= 0)
            {
                close(m_ReadDescriptor);
                m_ReadDescriptor = -1;
            }
#endif
        }

#if defined(_WIN32)
        HANDLE m_Process = nullptr;
        HANDLE m_Thread = nullptr;
        HANDLE m_ReadPipe = nullptr;
#else
        pid_t m_Process = -1;
        int m_ReadDescriptor = -1;
#endif
        bool m_Running = true;
        std::optional<int> m_ExitCode;
        std::string m_Output;
    };

    ChildProcess::ChildProcess(std::unique_ptr<Impl> implementation) noexcept : m_Impl(std::move(implementation)) {}
    ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;
    ChildProcess& ChildProcess::operator=(ChildProcess&&) noexcept = default;
    ChildProcess::~ChildProcess() = default;

    ChildProcess ChildProcess::Start(const std::filesystem::path& executable,
                                     const std::span<const std::string> arguments,
                                     const std::filesystem::path& workingDirectory)
    {
        if (!std::filesystem::is_regular_file(executable) || !std::filesystem::is_directory(workingDirectory))
            throw std::invalid_argument("Process executable or working directory does not exist.");
        auto implementation = std::make_unique<Impl>();
#if defined(_WIN32)
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&implementation->m_ReadPipe, &writePipe, &security, 0))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe failed");
        if (!SetHandleInformation(implementation->m_ReadPipe, HANDLE_FLAG_INHERIT, 0))
        {
            const auto error = GetLastError();
            CloseHandle(writePipe);
            CloseHandle(implementation->m_ReadPipe);
            implementation->m_ReadPipe = nullptr;
            throw std::system_error(static_cast<int>(error), std::system_category(), "CreatePipe failed");
        }
        auto command = QuoteWindowsArgument(executable.wstring());
        for (const auto& argument : arguments)
        {
            command.push_back(L' ');
            command += QuoteWindowsArgument(Utf8ToWide(argument));
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe;
        startup.hStdError = writePipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.wstring().c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, workingDirectory.wstring().c_str(), &startup, &process))
        {
            const auto error = GetLastError();
            CloseHandle(writePipe);
            CloseHandle(implementation->m_ReadPipe);
            implementation->m_ReadPipe = nullptr;
            throw std::system_error(static_cast<int>(error), std::system_category(), "CreateProcess failed");
        }
        CloseHandle(writePipe);
        implementation->m_Process = process.hProcess;
        implementation->m_Thread = process.hThread;
#else
        int descriptors[2]{};
        if (pipe(descriptors) != 0)
            throw std::system_error(errno, std::generic_category(), "pipe failed");
        const auto child = fork();
        if (child < 0)
        {
            const auto error = errno;
            close(descriptors[0]);
            close(descriptors[1]);
            throw std::system_error(error, std::generic_category(), "fork failed");
        }
        if (child == 0)
        {
            close(descriptors[0]);
            (void)dup2(descriptors[1], STDOUT_FILENO);
            (void)dup2(descriptors[1], STDERR_FILENO);
            close(descriptors[1]);
            if (chdir(workingDirectory.c_str()) != 0)
                _exit(126);
            std::vector<std::string> storage;
            storage.reserve(arguments.size() + 1);
            storage.push_back(executable.string());
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> values;
            values.reserve(storage.size() + 1);
            for (auto& value : storage)
                values.push_back(value.data());
            values.push_back(nullptr);
            execv(executable.c_str(), values.data());
            _exit(127);
        }
        close(descriptors[1]);
        (void)fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL) | O_NONBLOCK);
        implementation->m_Process = child;
        implementation->m_ReadDescriptor = descriptors[0];
#endif
        return ChildProcess(std::move(implementation));
    }

    bool ChildProcess::Poll() { return m_Impl->Poll(); }
    bool ChildProcess::Running() const noexcept { return m_Impl && m_Impl->m_Running; }
    std::optional<int> ChildProcess::ExitCode() const noexcept { return m_Impl ? m_Impl->m_ExitCode : std::nullopt; }
    std::string ChildProcess::TakeOutput() { return std::exchange(m_Impl->m_Output, {}); }
    bool ChildProcess::WaitFor(const std::chrono::milliseconds timeout)
    {
        if (timeout.count() < 0)
            throw std::invalid_argument("Child process wait timeout cannot be negative.");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do
        {
            if (Poll())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        return Poll();
    }
    void ChildProcess::Terminate() noexcept
    {
        if (m_Impl)
            m_Impl->Terminate();
    }

    ProcessResult RunProcess(const std::filesystem::path& executable, const std::span<const std::string> arguments,
                             const std::filesystem::path& workingDirectory, const std::chrono::milliseconds timeout)
    {
        if (!std::filesystem::is_regular_file(executable) || !std::filesystem::is_directory(workingDirectory))
            throw std::invalid_argument("Process executable or working directory does not exist.");
        if (timeout.count() <= 0)
            throw std::invalid_argument("Process timeout must be positive.");

        constexpr std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
        ProcessResult result;
#if defined(_WIN32)
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &security, 0) || !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe failed");

        auto command = QuoteWindowsArgument(executable.wstring());
        for (const auto& argument : arguments)
        {
            command.push_back(L' ');
            command += QuoteWindowsArgument(Utf8ToWide(argument));
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe;
        startup.hStdError = writePipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.wstring().c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, workingDirectory.wstring().c_str(), &startup, &process))
        {
            const auto error = GetLastError();
            CloseHandle(writePipe);
            CloseHandle(readPipe);
            throw std::system_error(static_cast<int>(error), std::system_category(), "CreateProcess failed");
        }
        CloseHandle(writePipe);
        writePipe = nullptr;

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<char, 4096> buffer{};
        bool running = true;
        while (running)
        {
            DWORD available = 0;
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
            {
                DWORD read = 0;
                if (!ReadFile(readPipe, buffer.data(),
                              static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available)), &read, nullptr))
                    break;
                if (result.Output.size() < maximumOutputBytes)
                    result.Output.append(buffer.data(),
                                         std::min<std::size_t>(read, maximumOutputBytes - result.Output.size()));
                available -= read;
            }

            const auto status = WaitForSingleObject(process.hProcess, 5);
            if (status == WAIT_OBJECT_0)
                running = false;
            else if (status != WAIT_TIMEOUT)
            {
                const auto error = GetLastError();
                TerminateProcess(process.hProcess, 127);
                WaitForSingleObject(process.hProcess, INFINITE);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                CloseHandle(readPipe);
                throw std::system_error(static_cast<int>(error), std::system_category(), "Process wait failed");
            }
            else if (std::chrono::steady_clock::now() >= deadline)
            {
                result.TimedOut = true;
                TerminateProcess(process.hProcess, 124);
                WaitForSingleObject(process.hProcess, INFINITE);
                running = false;
            }
        }

        DWORD read = 0;
        while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0)
        {
            if (result.Output.size() < maximumOutputBytes)
                result.Output.append(buffer.data(),
                                     std::min<std::size_t>(read, maximumOutputBytes - result.Output.size()));
        }
        DWORD exitCode = 127;
        (void)GetExitCodeProcess(process.hProcess, &exitCode);
        result.ExitCode = static_cast<int>(exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(readPipe);
#else
        int descriptors[2]{};
        if (pipe(descriptors) != 0)
            throw std::system_error(errno, std::generic_category(), "pipe failed");
        const auto child = fork();
        if (child < 0)
        {
            const auto error = errno;
            close(descriptors[0]);
            close(descriptors[1]);
            throw std::system_error(error, std::generic_category(), "fork failed");
        }
        if (child == 0)
        {
            close(descriptors[0]);
            (void)dup2(descriptors[1], STDOUT_FILENO);
            (void)dup2(descriptors[1], STDERR_FILENO);
            close(descriptors[1]);
            if (chdir(workingDirectory.c_str()) != 0)
                _exit(126);
            std::vector<std::string> storage;
            storage.reserve(arguments.size() + 1);
            storage.push_back(executable.string());
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> values;
            values.reserve(storage.size() + 1);
            for (auto& value : storage)
                values.push_back(value.data());
            values.push_back(nullptr);
            execv(executable.c_str(), values.data());
            _exit(127);
        }

        close(descriptors[1]);
        (void)fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL) | O_NONBLOCK);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<char, 4096> buffer{};
        int status = 0;
        bool running = true;
        while (running)
        {
            for (;;)
            {
                const auto count = read(descriptors[0], buffer.data(), buffer.size());
                if (count <= 0)
                    break;
                if (result.Output.size() < maximumOutputBytes)
                    result.Output.append(buffer.data(),
                                         std::min<std::size_t>(static_cast<std::size_t>(count),
                                                               maximumOutputBytes - result.Output.size()));
            }
            const auto waited = waitpid(child, &status, WNOHANG);
            if (waited == child)
                running = false;
            else if (waited < 0)
            {
                close(descriptors[0]);
                throw std::system_error(errno, std::generic_category(), "waitpid failed");
            }
            else if (std::chrono::steady_clock::now() >= deadline)
            {
                result.TimedOut = true;
                (void)kill(child, SIGKILL);
                (void)waitpid(child, &status, 0);
                running = false;
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        for (;;)
        {
            const auto count = read(descriptors[0], buffer.data(), buffer.size());
            if (count <= 0)
                break;
            if (result.Output.size() < maximumOutputBytes)
                result.Output.append(buffer.data(), std::min<std::size_t>(static_cast<std::size_t>(count),
                                                                          maximumOutputBytes - result.Output.size()));
        }
        close(descriptors[0]);
        result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
        return result;
    }

    bool LaunchDetachedProcess(const std::filesystem::path& executable, const std::span<const std::string> arguments,
                               const std::filesystem::path& workingDirectory, std::string& diagnostic) noexcept
    {
        try
        {
            if (!std::filesystem::is_regular_file(executable) || !std::filesystem::is_directory(workingDirectory))
            {
                diagnostic = "Executable or working directory does not exist.";
                return false;
            }
#if defined(_WIN32)
            auto command = QuoteWindowsArgument(executable.wstring());
            for (const auto& argument : arguments)
            {
                command.push_back(L' ');
                command += QuoteWindowsArgument(Utf8ToWide(argument));
            }
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.wstring().c_str(), command.data(), nullptr, nullptr, FALSE,
                                CREATE_NEW_PROCESS_GROUP, nullptr, workingDirectory.wstring().c_str(), &startup,
                                &process))
            {
                diagnostic = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
                return false;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
#else
            const auto child = fork();
            if (child < 0)
            {
                diagnostic = std::strerror(errno);
                return false;
            }
            if (child == 0)
            {
                const auto detached = fork();
                if (detached < 0)
                    _exit(126);
                if (detached > 0)
                    _exit(0);
                (void)setsid();
                if (chdir(workingDirectory.c_str()) != 0)
                    _exit(126);
                std::vector<std::string> storage;
                storage.reserve(arguments.size() + 1);
                storage.push_back(executable.string());
                storage.insert(storage.end(), arguments.begin(), arguments.end());
                std::vector<char*> values;
                values.reserve(storage.size() + 1);
                for (auto& value : storage)
                    values.push_back(value.data());
                values.push_back(nullptr);
                execv(executable.c_str(), values.data());
                _exit(127);
            }
            int status = 0;
            if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                diagnostic = "Detached process bootstrap failed.";
                return false;
            }
            return true;
#endif
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    std::filesystem::path ResolveCompanionExecutable(const std::filesystem::path& executable,
                                                     const std::string_view companionTarget)
    {
        if (executable.empty() || companionTarget.empty())
            throw std::invalid_argument("Companion executable resolution requires an executable and target name.");

        const auto owner = std::filesystem::absolute(executable).lexically_normal();
        auto target = PathFromUtf8(companionTarget);
#if defined(_WIN32)
        target += L".exe";
#endif
        const std::array candidates{owner.parent_path() / target,
                                    owner.parent_path().parent_path() / PathFromUtf8(companionTarget) / target};
        if (const auto found = std::ranges::find_if(candidates, [](const auto& path)
                                                    { return std::filesystem::is_regular_file(path); });
            found != candidates.end())
            return *found;

        throw std::runtime_error("Could not find companion executable '" + std::string(companionTarget) +
                                 "'. Checked '" + PathToUtf8(candidates[0]) + "' and '" + PathToUtf8(candidates[1]) +
                                 "'.");
    }

    bool OpenInExternalEditor(const std::filesystem::path& path, const std::filesystem::path& preferredEditor,
                              const std::filesystem::path& workingDirectory, std::string& diagnostic) noexcept
    {
        try
        {
            const auto source = std::filesystem::weakly_canonical(path);
            const auto working = std::filesystem::weakly_canonical(workingDirectory);
            if (!std::filesystem::is_regular_file(source) || !std::filesystem::is_directory(working))
            {
                diagnostic = "Source file or working directory does not exist.";
                return false;
            }
            const auto sourceText = PathToUtf8(source);
            std::vector<std::string> arguments{sourceText};
            auto extension = source.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            std::filesystem::path managedSolution;
            if (extension == ".cs" || extension == ".keireasm")
            {
                auto solutionName = working.filename().string();
                for (char& character : solutionName)
                    if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
                        character = '_';
                const auto candidate = working / (solutionName + ".sln");
                if (std::filesystem::is_regular_file(candidate))
                    managedSolution = candidate;
            }
            if (!preferredEditor.empty())
            {
                auto editorName = preferredEditor.stem().string();
                for (char& character : editorName)
                {
                    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                }
                if (editorName == "devenv" && !managedSolution.empty())
                {
                    arguments = extension == ".cs"
                                    ? std::vector<std::string>{PathToUtf8(managedSolution), "/Edit", sourceText}
                                    : std::vector<std::string>{PathToUtf8(managedSolution)};
                }
                return LaunchDetachedProcess(std::filesystem::weakly_canonical(preferredEditor), arguments, working,
                                             diagnostic);
            }
#if defined(_WIN32)
            const auto target = managedSolution.empty() ? source : managedSolution;
            const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
                nullptr, L"open", target.wstring().c_str(), nullptr, working.wstring().c_str(), SW_SHOWNORMAL));
            if (result <= 32)
            {
                diagnostic = managedSolution.empty()
                                 ? "Windows could not open the source with its associated application."
                                 : "Windows could not open the generated Visual Studio solution.";
                return false;
            }
            return true;
#elif defined(__APPLE__)
            const std::filesystem::path executable = "/usr/bin/open";
#else
            const std::filesystem::path executable = "/usr/bin/xdg-open";
#endif
#if !defined(_WIN32)
            return LaunchDetachedProcess(executable, arguments, working, diagnostic);
#endif
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    bool RevealInFileManager(const std::filesystem::path& path, std::string& diagnostic) noexcept
    {
        try
        {
            const auto resolved = std::filesystem::weakly_canonical(path);
            if (!std::filesystem::exists(resolved))
            {
                diagnostic = "Path does not exist.";
                return false;
            }
            const bool directory = std::filesystem::is_directory(resolved);
            const auto workingDirectory = directory ? resolved : resolved.parent_path();
            const auto pathValue = resolved.generic_u8string();
            const std::string utf8Path(reinterpret_cast<const char*>(pathValue.data()), pathValue.size());
#if defined(_WIN32)
            std::wstring windowsDirectory(MAX_PATH, L'\0');
            const auto length =
                GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
            if (length == 0 || static_cast<std::size_t>(length) >= windowsDirectory.size())
            {
                diagnostic = "Windows directory could not be resolved.";
                return false;
            }
            windowsDirectory.resize(length);
            const auto executable = std::filesystem::path(windowsDirectory) / "explorer.exe";
            const std::vector<std::string> arguments =
                directory ? std::vector<std::string>{utf8Path} : std::vector<std::string>{"/select," + utf8Path};
#elif defined(__APPLE__)
            const std::filesystem::path executable = "/usr/bin/open";
            const std::vector<std::string> arguments =
                directory ? std::vector<std::string>{utf8Path} : std::vector<std::string>{"-R", utf8Path};
#else
            const std::filesystem::path executable = "/usr/bin/xdg-open";
            const std::vector<std::string> arguments{directory ? utf8Path : workingDirectory.generic_string()};
#endif
            return LaunchDetachedProcess(executable, arguments, workingDirectory, diagnostic);
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }
} // namespace Keire::Detail
