#include "KeireInternal/Process.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
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
