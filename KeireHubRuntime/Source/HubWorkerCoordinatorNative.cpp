#include "KeireHubRuntime/HubWorkerCoordinator.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumWorkerArguments = 32;
        constexpr std::size_t MaximumWorkerArgumentBytes = std::size_t{32} * 1024;

        [[nodiscard]] HubError LaunchError(const std::string_view details)
        {
            return {.Code = HubErrorCode::WorkerInterrupted,
                    .Message = "The Hub could not start the package worker.",
                    .Retryable = true,
                    .AffectedItem = "package-worker",
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] HubStatus ValidateLaunch(const HubWorkerLaunch& launch)
        {
            if (launch.Executable.empty() || launch.WorkingDirectory.empty() ||
                launch.Arguments.size() > MaximumWorkerArguments)
            {
                return HubStatus::Failure(LaunchError("Invalid worker launch paths or argument count."));
            }
            std::size_t totalBytes = 0;
            for (const auto& argument : launch.Arguments)
            {
                if (argument.size() > MaximumWorkerArgumentBytes ||
                    totalBytes > MaximumWorkerArgumentBytes - argument.size())
                {
                    return HubStatus::Failure(LaunchError("Worker arguments exceed the allowed size."));
                }
                totalBytes += argument.size();
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(launch.Executable, error) || error ||
                !std::filesystem::is_directory(launch.WorkingDirectory, error) || error)
            {
                return HubStatus::Failure(LaunchError("Worker executable or working directory does not exist."));
            }
            return HubStatus::Success();
        }

#if defined(_WIN32)
        [[nodiscard]] std::wstring Utf8ToWide(const std::string_view value)
        {
            if (value.empty())
                return {};
            if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Worker argument is too large.");
            const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
                throw std::invalid_argument("Worker argument is not valid UTF-8.");
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), size) != size)
            {
                throw std::invalid_argument("Worker argument could not be converted to UTF-16.");
            }
            return result;
        }

        [[nodiscard]] std::wstring QuoteWindowsArgument(const std::wstring_view value)
        {
            if (value.empty())
                return L"\"\"";
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

        [[nodiscard]] HubStatus LaunchWorkerDetached(const HubWorkerLaunch& launch)
        {
            if (auto status = ValidateLaunch(launch); !status)
                return status;
            try
            {
#if defined(_WIN32)
                auto command = QuoteWindowsArgument(launch.Executable.wstring());
                for (const auto& argument : launch.Arguments)
                {
                    command.push_back(L' ');
                    command += QuoteWindowsArgument(Utf8ToWide(argument));
                }
                if (command.size() >= 32'767)
                    return HubStatus::Failure(LaunchError("Worker command line exceeds the Windows limit."));

                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(launch.Executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                    CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr,
                                    launch.WorkingDirectory.c_str(), &startup, &process))
                {
                    return HubStatus::Failure(LaunchError(
                        std::error_code(static_cast<int>(GetLastError()), std::system_category()).message()));
                }
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                return HubStatus::Success();
#else
                std::vector<std::string> storage;
                storage.reserve(launch.Arguments.size() + 1);
                storage.push_back(launch.Executable.string());
                storage.insert(storage.end(), launch.Arguments.begin(), launch.Arguments.end());
                std::vector<char*> values;
                values.reserve(storage.size() + 1);
                for (auto& value : storage)
                    values.push_back(value.data());
                values.push_back(nullptr);

                const auto child = fork();
                if (child < 0)
                    return HubStatus::Failure(LaunchError(std::strerror(errno)));
                if (child == 0)
                {
                    const auto detached = fork();
                    if (detached < 0)
                        _exit(126);
                    if (detached > 0)
                        _exit(0);
                    if (setsid() < 0 || chdir(launch.WorkingDirectory.c_str()) != 0)
                        _exit(126);
                    const auto nullDescriptor = open("/dev/null", O_RDWR);
                    if (nullDescriptor >= 0)
                    {
                        (void)dup2(nullDescriptor, STDIN_FILENO);
                        (void)dup2(nullDescriptor, STDOUT_FILENO);
                        (void)dup2(nullDescriptor, STDERR_FILENO);
                        if (nullDescriptor > STDERR_FILENO)
                            (void)close(nullDescriptor);
                    }
                    execv(launch.Executable.c_str(), values.data());
                    _exit(127);
                }
                int status = 0;
                pid_t waited = 0;
                do
                {
                    waited = waitpid(child, &status, 0);
                } while (waited < 0 && errno == EINTR);
                if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    return HubStatus::Failure(LaunchError("Detached worker bootstrap failed."));
                return HubStatus::Success();
#endif
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(LaunchError(error.what()));
            }
        }

        class NativeHubWorkerProcessHost final : public HubWorkerProcessHost
        {
          public:
            HubStatus LaunchDetached(const HubWorkerLaunch& launch) override { return LaunchWorkerDetached(launch); }

            bool IsProcessAlive(const std::uint64_t processId) const noexcept override
            {
                if (processId == 0)
                    return false;
#if defined(_WIN32)
                if (processId > std::numeric_limits<DWORD>::max())
                    return false;
                const auto process =
                    OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
                if (!process)
                    return GetLastError() == ERROR_ACCESS_DENIED;
                const auto wait = WaitForSingleObject(process, 0);
                CloseHandle(process);
                return wait == WAIT_TIMEOUT;
#else
                if (processId > static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max()))
                    return false;
                int result = 0;
                do
                {
                    errno = 0;
                    result = kill(static_cast<pid_t>(processId), 0);
                } while (result != 0 && errno == EINTR);
                return result == 0 || errno == EPERM;
#endif
            }
        };
    } // namespace

    std::unique_ptr<HubWorkerProcessHost> CreateNativeHubWorkerProcessHost()
    {
        return std::make_unique<NativeHubWorkerProcessHost>();
    }
} // namespace KeireHub
