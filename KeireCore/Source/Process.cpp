#include "KeireInternal/Process.h"

#include <array>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <cstring>
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
            const auto workingDirectory = std::filesystem::is_directory(resolved) ? resolved : resolved.parent_path();
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
#elif defined(__APPLE__)
            const std::filesystem::path executable = "/usr/bin/open";
#else
            const std::filesystem::path executable = "/usr/bin/xdg-open";
#endif
            const std::array arguments{utf8Path};
            return LaunchDetachedProcess(executable, arguments, workingDirectory, diagnostic);
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }
} // namespace Keire::Detail
