#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubInstance.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    using namespace std::chrono_literals;

    [[nodiscard]] std::optional<KeireHub::HubActivationRequest>
    WaitForActivation(KeireHub::HubInstanceCoordinator& instance)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        do
        {
            if (auto activation = instance.PollActivation())
                return activation;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        return std::nullopt;
    }

#if defined(_WIN32)
    [[nodiscard]] std::wstring Utf8ToWide(const std::string_view value)
    {
        if (value.empty())
            return {};
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("A Hub instance test argument is too long.");
        const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                static_cast<int>(value.size()), nullptr, 0);
        if (length <= 0)
            throw std::runtime_error("Could not decode a Hub instance test argument.");
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), length) != length)
        {
            throw std::runtime_error("Could not decode a Hub instance test argument.");
        }
        return result;
    }

    [[nodiscard]] std::wstring QuoteArgument(const std::wstring_view value)
    {
        if (value.empty())
            return L"\"\"";
        if (value.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
            return std::wstring(value);
        std::wstring result = L"\"";
        std::size_t slashes = 0;
        for (const auto character : value)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            if (character == L'\"')
            {
                result.append(slashes * 2U + 1U, L'\\');
                result.push_back(L'\"');
            }
            else
            {
                result.append(slashes, L'\\');
                result.push_back(character);
            }
            slashes = 0;
        }
        result.append(slashes * 2U, L'\\');
        result.push_back(L'\"');
        return result;
    }
#else
    void StopUnixChild(const pid_t process) noexcept
    {
        if (process <= 0)
            return;
        (void)kill(process, SIGTERM);
        int status = 0;
        while (waitpid(process, &status, 0) < 0 && errno == EINTR)
        {
        }
    }

    class ScopedUnixRuntimeDirectory final
    {
      public:
        explicit ScopedUnixRuntimeDirectory(const std::filesystem::path& path)
        {
            if (const auto* current = std::getenv("XDG_RUNTIME_DIR"))
                m_Previous = current;
            if (chmod(path.c_str(), 0700) != 0 || setenv("XDG_RUNTIME_DIR", path.c_str(), 1) != 0)
                throw std::runtime_error("Could not configure the Hub instance test runtime directory.");
        }

        ~ScopedUnixRuntimeDirectory()
        {
            if (m_Previous)
                (void)setenv("XDG_RUNTIME_DIR", m_Previous->c_str(), 1);
            else
                (void)unsetenv("XDG_RUNTIME_DIR");
        }

        ScopedUnixRuntimeDirectory(const ScopedUnixRuntimeDirectory&) = delete;
        ScopedUnixRuntimeDirectory& operator=(const ScopedUnixRuntimeDirectory&) = delete;

      private:
        std::optional<std::string> m_Previous;
    };

    class UnixChildProcess final
    {
      public:
        explicit UnixChildProcess(const pid_t process) noexcept : m_Process(process) {}
        ~UnixChildProcess() { StopUnixChild(m_Process); }

        UnixChildProcess(const UnixChildProcess&) = delete;
        UnixChildProcess& operator=(const UnixChildProcess&) = delete;

        UnixChildProcess(UnixChildProcess&& other) noexcept : m_Process(std::exchange(other.m_Process, -1)) {}

        UnixChildProcess& operator=(UnixChildProcess&& other) noexcept
        {
            if (this == &other)
                return *this;
            StopUnixChild(m_Process);
            m_Process = std::exchange(other.m_Process, -1);
            return *this;
        }

      private:
        pid_t m_Process = -1;
    };

    [[nodiscard]] UnixChildProcess SpawnLongLivedUnixChild()
    {
        int synchronization[2]{};
        if (pipe(synchronization) != 0)
            throw std::runtime_error("Could not create the Hub instance exec synchronization pipe.");

        const auto writeFlags = fcntl(synchronization[1], F_GETFD);
        if (writeFlags < 0 || fcntl(synchronization[1], F_SETFD, writeFlags | FD_CLOEXEC) != 0)
        {
            (void)close(synchronization[0]);
            (void)close(synchronization[1]);
            throw std::runtime_error("Could not secure the Hub instance exec synchronization pipe.");
        }

        const auto process = fork();
        if (process < 0)
        {
            (void)close(synchronization[0]);
            (void)close(synchronization[1]);
            throw std::runtime_error("Could not fork the Hub instance inheritance test child.");
        }
        if (process == 0)
        {
            (void)close(synchronization[0]);
            execl("/bin/sleep", "sleep", "30", static_cast<char*>(nullptr));
            const auto error = errno;
            (void)write(synchronization[1], &error, sizeof(error));
            _exit(127);
        }

        (void)close(synchronization[1]);
        pollfd signal{.fd = synchronization[0], .events = POLLIN | POLLHUP, .revents = 0};
        int result = -1;
        do
        {
            result = poll(&signal, 1, 5'000);
        } while (result < 0 && errno == EINTR);
        if (result <= 0)
        {
            (void)close(synchronization[0]);
            StopUnixChild(process);
            throw std::runtime_error("The Hub instance inheritance test child did not exec.");
        }

        int childError = 0;
        ssize_t bytes = -1;
        do
        {
            bytes = read(synchronization[0], &childError, sizeof(childError));
        } while (bytes < 0 && errno == EINTR);
        (void)close(synchronization[0]);
        if (bytes != 0)
        {
            StopUnixChild(process);
            throw std::runtime_error("The Hub instance inheritance test child could not exec.");
        }
        return UnixChildProcess(process);
    }
#endif

    [[nodiscard]] std::optional<int> RunSecondary(const std::vector<std::string>& arguments)
    {
#if defined(_WIN32)
        const auto& executable = KeireHubTests::ExecutablePath;
        auto command = QuoteArgument(executable.wstring());
        for (const auto& argument : arguments)
        {
            command.push_back(L' ');
            command += QuoteArgument(Utf8ToWide(argument));
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const auto workingDirectory = std::filesystem::current_path();
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            workingDirectory.c_str(), &startup, &process))
        {
            return std::nullopt;
        }
        CloseHandle(process.hThread);
        const auto waited = WaitForSingleObject(process.hProcess, 5'000);
        if (waited != WAIT_OBJECT_0)
        {
            (void)TerminateProcess(process.hProcess, 124);
            (void)WaitForSingleObject(process.hProcess, 5'000);
            CloseHandle(process.hProcess);
            return std::nullopt;
        }
        DWORD exitCode = 0;
        const bool read = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hProcess);
        return read ? std::optional<int>(static_cast<int>(exitCode)) : std::nullopt;
#else
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1U);
        storage.push_back(KeireHubTests::ExecutablePath.string());
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> values;
        values.reserve(storage.size() + 1U);
        for (auto& value : storage)
            values.push_back(value.data());
        values.push_back(nullptr);

        const auto process = fork();
        if (process < 0)
            return std::nullopt;
        if (process == 0)
        {
            execv(KeireHubTests::ExecutablePath.c_str(), values.data());
            _exit(127);
        }

        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            int status = 0;
            const auto waited = waitpid(process, &status, WNOHANG);
            if (waited == process)
            {
                if (WIFEXITED(status))
                    return WEXITSTATUS(status);
                if (WIFSIGNALED(status))
                    return 128 + WTERMSIG(status);
                return 124;
            }
            if (waited < 0 && errno != EINTR)
                return std::nullopt;
            std::this_thread::sleep_for(5ms);
        }
        (void)kill(process, SIGKILL);
        int status = 0;
        while (waitpid(process, &status, 0) < 0 && errno == EINTR)
        {
        }
        return std::nullopt;
#endif
    }

    void SendActivation(const std::filesystem::path& identity, const std::vector<std::string>& arguments)
    {
        std::vector<std::string> processArguments{"--hub-instance-secondary", identity.string()};
        processArguments.insert(processArguments.end(), arguments.begin(), arguments.end());
        const auto exitCode = RunSecondary(processArguments);
        REQUIRE(exitCode);
        CHECK(*exitCode == 0);
    }
} // namespace

TEST_CASE("Hub instance coordination activates one primary process and releases ownership on shutdown")
{
    const std::filesystem::path identity =
        "keire-hub-instance-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        KeireHub::HubInstanceCoordinator primary(identity, {}, true);
        REQUIRE(primary.IsPrimary());

        const auto rejected =
            RunSecondary({"--hub-instance-secondary", identity.string(), "--navigate", "unsupported-page"});
        REQUIRE(rejected);
        CHECK(*rejected == 30);
        CHECK_FALSE(primary.PollActivation());

        SendActivation(identity, {"--show"});
        const auto show = WaitForActivation(primary);
        REQUIRE(show);
        CHECK(show->Action == KeireHub::HubActivationAction::Show);

        SendActivation(identity, {"--navigate", "templates"});
        const auto navigation = WaitForActivation(primary);
        REQUIRE(navigation);
        CHECK(navigation->Action == KeireHub::HubActivationAction::Navigate);
        REQUIRE(navigation->Page);
        CHECK(*navigation->Page == KeireHub::HubPage::Templates);

        SendActivation(identity, {"--build-support", "windows", "arm64"});
        const auto support = WaitForActivation(primary);
        REQUIRE(support);
        CHECK(support->RequestsBuildSupport());
        CHECK(support->Platform == "windows");
        CHECK(support->Architecture == "arm64");
    }

    const KeireHub::HubInstanceCoordinator replacement(identity, {}, true);
    CHECK(replacement.IsPrimary());
}

#if !defined(_WIN32)
TEST_CASE("Hub instance ownership is not inherited by a long-lived exec child")
{
    const std::filesystem::path identity =
        "keire-hub-inheritance-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto primary = std::make_unique<KeireHub::HubInstanceCoordinator>(identity, KeireHub::HubActivationRequest{}, true);
    REQUIRE(primary->IsPrimary());

    auto inheritedChild = SpawnLongLivedUnixChild();
    primary.reset();

    KeireHub::HubInstanceCoordinator replacement(identity, {}, true);
    CHECK(replacement.IsPrimary());
}

TEST_CASE("Hub instance coordination rejects a precreated Unix activation symlink")
{
    KeireHubTests::TemporaryDirectory temporary;
    const ScopedUnixRuntimeDirectory runtimeEnvironment(temporary.Path());
    const std::filesystem::path identity =
        "keire-hub-symlink-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        const KeireHub::HubInstanceCoordinator primary(identity, {}, true);
        REQUIRE(primary.IsPrimary());
    }

    const auto runtimeDirectory =
        temporary.Path() / ("keire-hub-" + std::to_string(static_cast<unsigned long long>(geteuid())));
    struct stat runtimeStatus{};
    REQUIRE(stat(runtimeDirectory.c_str(), &runtimeStatus) == 0);
    CHECK(S_ISDIR(runtimeStatus.st_mode));
    CHECK(runtimeStatus.st_uid == geteuid());
    CHECK((runtimeStatus.st_mode & (S_IRWXG | S_IRWXO)) == 0);

    std::filesystem::path lockPath;
    for (const auto& entry : std::filesystem::directory_iterator(runtimeDirectory))
    {
        if (entry.path().extension() == ".lock")
        {
            lockPath = entry.path();
            break;
        }
    }
    REQUIRE_FALSE(lockPath.empty());

    const auto sentinel = temporary.Path() / "sentinel.txt";
    KeireHubTests::WriteText(sentinel, "preserve");
    const auto activationPath = lockPath.parent_path() / (lockPath.stem().string() + ".activate");
    REQUIRE(symlink(sentinel.c_str(), activationPath.c_str()) == 0);

    const KeireHub::HubActivationRequest activation;
    CHECK_THROWS_AS(KeireHub::HubInstanceCoordinator(identity, activation, true), std::runtime_error);
    CHECK(std::filesystem::is_symlink(activationPath));
    CHECK(KeireHubTests::ReadText(sentinel) == "preserve");
}
#endif
