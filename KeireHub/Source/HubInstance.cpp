#include "KeireHub/HubInstance.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumActivationBytes = 240;

        [[nodiscard]] std::uint64_t ExecutableHash(const std::filesystem::path& executable)
        {
            std::error_code error;
            auto resolved = std::filesystem::weakly_canonical(executable, error);
            if (error)
            {
                error.clear();
                resolved = std::filesystem::absolute(executable, error);
            }
            const auto encoded = (error ? executable : resolved).generic_u8string();
            std::uint64_t result = 14695981039346656037ULL;
            for (const auto character : encoded)
            {
                auto byte = static_cast<std::uint8_t>(character);
#if defined(_WIN32)
                if (byte >= 'A' && byte <= 'Z')
                    byte = static_cast<std::uint8_t>(byte + ('a' - 'A'));
#endif
                result ^= byte;
                result *= 1099511628211ULL;
            }
            return result;
        }

        [[nodiscard]] std::string EncodeActivation(const HubActivationRequest& activation)
        {
            if (!activation.RequestsBuildSupport())
                return "show";
            const auto payload = "build-support|" + *activation.Platform + '|' + *activation.Architecture;
            if (payload.size() >= MaximumActivationBytes)
                throw std::invalid_argument("Hub activation request is too large.");
            return payload;
        }

        [[nodiscard]] HubActivationRequest DecodeActivation(const std::string_view payload)
        {
            constexpr std::string_view prefix = "build-support|";
            if (!payload.starts_with(prefix))
                return {};
            const auto separator = payload.find('|', prefix.size());
            if (separator == std::string_view::npos)
                return {};
            const auto platform = payload.substr(prefix.size(), separator - prefix.size());
            const auto architecture = payload.substr(separator + 1);
            const bool validPlatform = platform == "windows" || platform == "linux" || platform == "macos";
            const bool validArchitecture = architecture == "x86_64" || architecture == "arm64";
            if (!validPlatform || !validArchitecture)
                return {};
            return {.Platform = std::string(platform), .Architecture = std::string(architecture)};
        }

        [[nodiscard]] std::string HexHash(const std::uint64_t value)
        {
            constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
            std::string result(16, '0');
            for (std::size_t index = 0; index < result.size(); ++index)
                result[result.size() - index - 1] = digits[(value >> (index * 4U)) & 0x0fU];
            return result;
        }

#if defined(_WIN32)
        struct ExistingHubWindow final
        {
            std::wstring Executable;
            HWND Window = nullptr;
            DWORD Process = 0;
        };

        BOOL CALLBACK FindExistingHubWindow(HWND window, LPARAM contextValue)
        {
            auto& context = *reinterpret_cast<ExistingHubWindow*>(contextValue);
            DWORD processId = 0;
            (void)GetWindowThreadProcessId(window, &processId);
            if (processId == 0)
                return TRUE;
            const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (!process)
                return TRUE;
            std::array<wchar_t, 32768> path{};
            DWORD pathLength = static_cast<DWORD>(path.size());
            const bool matches = QueryFullProcessImageNameW(process, 0, path.data(), &pathLength) &&
                                 CompareStringOrdinal(context.Executable.c_str(), -1, path.data(),
                                                      static_cast<int>(pathLength), TRUE) == CSTR_EQUAL;
            CloseHandle(process);
            if (!matches)
                return TRUE;
            context.Window = window;
            context.Process = processId;
            return FALSE;
        }

        void ActivateExistingHubWindow(const std::filesystem::path& executable) noexcept
        {
            try
            {
                ExistingHubWindow context{std::filesystem::weakly_canonical(executable).native()};
                (void)EnumWindows(&FindExistingHubWindow, reinterpret_cast<LPARAM>(&context));
                if (!context.Window)
                    return;
                (void)AllowSetForegroundWindow(context.Process);
                (void)ShowWindowAsync(context.Window, SW_RESTORE);
                (void)SetForegroundWindow(context.Window);
            }
            catch (...)
            {
            }
        }
#endif
    } // namespace

    class HubInstanceCoordinator::Impl final
    {
      public:
        Impl(const std::filesystem::path& executable, const HubActivationRequest& activation, const bool coordinate)
        {
            try
            {
                if (!coordinate)
                    return;
                const auto identity = HexHash(ExecutableHash(executable));
#if defined(_WIN32)
                InitializeWindows(identity, executable, activation);
#else
                InitializeUnix(identity, activation);
#endif
            }
            catch (...)
            {
                Cleanup();
                throw;
            }
        }

        ~Impl() { Cleanup(); }

        void Cleanup() noexcept
        {
#if defined(_WIN32)
            if (m_Shared)
            {
                UnmapViewOfFile(m_Shared);
                m_Shared = nullptr;
            }
            if (m_Primary && m_Instance)
                (void)ReleaseMutex(m_Instance);
            if (m_ActivationGuard)
                CloseHandle(std::exchange(m_ActivationGuard, nullptr));
            if (m_Mapping)
                CloseHandle(std::exchange(m_Mapping, nullptr));
            if (m_ActivationEvent)
                CloseHandle(std::exchange(m_ActivationEvent, nullptr));
            if (m_Instance)
                CloseHandle(std::exchange(m_Instance, nullptr));
#else
            if (m_Fifo >= 0)
                close(std::exchange(m_Fifo, -1));
            if (m_Lock >= 0)
                close(std::exchange(m_Lock, -1));
            if (m_Primary)
            {
                std::error_code ignored;
                std::filesystem::remove(m_FifoPath, ignored);
                std::filesystem::remove(m_LockPath, ignored);
            }
#endif
        }

        [[nodiscard]] bool IsPrimary() const noexcept { return m_Primary; }

        [[nodiscard]] std::optional<HubActivationRequest> PollActivation()
        {
            if (!m_Primary)
                return std::nullopt;
#if defined(_WIN32)
            if (!m_ActivationEvent || !m_Shared || WaitForSingleObject(m_ActivationEvent, 0) != WAIT_OBJECT_0)
                return std::nullopt;
            const auto guard = WaitForSingleObject(m_ActivationGuard, 100);
            if (guard != WAIT_OBJECT_0 && guard != WAIT_ABANDONED)
            {
                (void)SetEvent(m_ActivationEvent);
                return std::nullopt;
            }
            const auto sequence = static_cast<std::uint32_t>(InterlockedCompareExchange(&m_Shared->Sequence, 0, 0));
            const auto length = std::char_traits<char>::length(m_Shared->Payload);
            const std::string payload(m_Shared->Payload, std::min(length, MaximumActivationBytes - 1));
            (void)ReleaseMutex(m_ActivationGuard);
            if (sequence == m_LastSequence)
                return std::nullopt;
            m_LastSequence = sequence;
            return DecodeActivation(payload);
#else
            if (m_Fifo < 0)
                return std::nullopt;
            std::array<char, MaximumActivationBytes> payload{};
            const auto bytes = read(m_Fifo, payload.data(), payload.size() - 1);
            if (bytes <= 0)
                return std::nullopt;
            auto message = std::string_view(payload.data(), static_cast<std::size_t>(bytes));
            if (message.ends_with('\n'))
                message.remove_suffix(1);
            const auto separator = message.rfind('\n');
            return DecodeActivation(message.substr(separator == std::string_view::npos ? 0 : separator + 1));
#endif
        }

      private:
#if defined(_WIN32)
        struct SharedActivation final
        {
            volatile LONG Sequence = 0;
            char Payload[MaximumActivationBytes]{};
        };

        static std::wstring ObjectName(const std::string& identity, const std::wstring_view suffix)
        {
            return L"Local\\KeireHub-" + std::wstring(identity.begin(), identity.end()) + std::wstring(suffix);
        }

        void InitializeWindows(const std::string& identity, const std::filesystem::path& executable,
                               const HubActivationRequest& activation)
        {
            const auto instanceName = ObjectName(identity, L"-instance");
            m_Instance = CreateMutexW(nullptr, TRUE, instanceName.c_str());
            if (!m_Instance)
                throw std::runtime_error("Could not create the Hub instance coordinator.");
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                const auto acquired = WaitForSingleObject(m_Instance, 0);
                if (acquired == WAIT_FAILED)
                    throw std::runtime_error("Could not inspect the active Hub instance.");
                m_Primary = acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED;
            }
            else
                m_Primary = true;

            const auto eventName = ObjectName(identity, L"-activation");
            const auto mappingName = ObjectName(identity, L"-request");
            const auto guardName = ObjectName(identity, L"-request-guard");
            if (m_Primary)
            {
                m_ActivationEvent = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
                m_Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                               static_cast<DWORD>(sizeof(SharedActivation)), mappingName.c_str());
                m_ActivationGuard = CreateMutexW(nullptr, FALSE, guardName.c_str());
            }
            else
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                do
                {
                    m_ActivationEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
                    m_Mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, mappingName.c_str());
                    m_ActivationGuard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, guardName.c_str());
                    if (m_ActivationEvent && m_Mapping && m_ActivationGuard)
                        break;
                    if (m_ActivationEvent)
                        CloseHandle(std::exchange(m_ActivationEvent, nullptr));
                    if (m_Mapping)
                        CloseHandle(std::exchange(m_Mapping, nullptr));
                    if (m_ActivationGuard)
                        CloseHandle(std::exchange(m_ActivationGuard, nullptr));
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } while (std::chrono::steady_clock::now() < deadline);
            }
            if (!m_ActivationEvent || !m_Mapping || !m_ActivationGuard)
                throw std::runtime_error("Could not open the Hub activation channel.");
            m_Shared = static_cast<SharedActivation*>(MapViewOfFile(
                m_Mapping, m_Primary ? FILE_MAP_ALL_ACCESS : FILE_MAP_WRITE, 0, 0, sizeof(SharedActivation)));
            if (!m_Shared)
                throw std::runtime_error("Could not map the Hub activation channel.");
            if (m_Primary)
            {
                std::memset(m_Shared, 0, sizeof(SharedActivation));
                return;
            }
            const auto guard = WaitForSingleObject(m_ActivationGuard, 2000);
            if (guard != WAIT_OBJECT_0 && guard != WAIT_ABANDONED)
                throw std::runtime_error("The active Hub did not accept an activation request.");
            const auto payload = EncodeActivation(activation);
            std::memset(m_Shared->Payload, 0, sizeof(m_Shared->Payload));
            std::memcpy(m_Shared->Payload, payload.data(), payload.size());
            (void)InterlockedIncrement(&m_Shared->Sequence);
            (void)ReleaseMutex(m_ActivationGuard);
            if (!SetEvent(m_ActivationEvent))
                throw std::runtime_error("Could not notify the active Hub.");
            ActivateExistingHubWindow(executable);
        }

        HANDLE m_Instance = nullptr;
        HANDLE m_ActivationEvent = nullptr;
        HANDLE m_Mapping = nullptr;
        HANDLE m_ActivationGuard = nullptr;
        SharedActivation* m_Shared = nullptr;
        std::uint32_t m_LastSequence = 0;
#else
        void InitializeUnix(const std::string& identity, const HubActivationRequest& activation)
        {
            const auto root = std::filesystem::temp_directory_path();
            m_LockPath = root / ("keire-hub-" + identity + ".lock");
            m_FifoPath = root / ("keire-hub-" + identity + ".activate");
            m_Lock = open(m_LockPath.c_str(), O_CREAT | O_RDWR, 0600);
            if (m_Lock < 0)
                throw std::runtime_error("Could not create the Hub instance lock.");
            if (flock(m_Lock, LOCK_EX | LOCK_NB) == 0)
                m_Primary = true;
            else if (errno != EWOULDBLOCK && errno != EAGAIN)
                throw std::runtime_error("Could not acquire the Hub instance lock.");
            else
                m_Primary = false;
            if (m_Primary)
            {
                (void)unlink(m_FifoPath.c_str());
                if (mkfifo(m_FifoPath.c_str(), 0600) != 0)
                    throw std::runtime_error("Could not create the Hub activation channel.");
                m_Fifo = open(m_FifoPath.c_str(), O_RDWR | O_NONBLOCK);
                if (m_Fifo < 0)
                    throw std::runtime_error("Could not open the Hub activation channel.");
                return;
            }
            close(std::exchange(m_Lock, -1));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do
            {
                m_Fifo = open(m_FifoPath.c_str(), O_WRONLY | O_NONBLOCK);
                if (m_Fifo >= 0)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            if (m_Fifo < 0)
                throw std::runtime_error("Could not notify the active Hub.");
            const auto payload = EncodeActivation(activation) + '\n';
            if (write(m_Fifo, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size()))
                throw std::runtime_error("Could not send the Hub activation request.");
            close(std::exchange(m_Fifo, -1));
        }

        int m_Lock = -1;
        int m_Fifo = -1;
        std::filesystem::path m_LockPath;
        std::filesystem::path m_FifoPath;
#endif
        bool m_Primary = true;
    };

    HubInstanceCoordinator::HubInstanceCoordinator(const std::filesystem::path& executable,
                                                   const HubActivationRequest& activation, const bool coordinate)
        : m_Impl(std::make_unique<Impl>(executable, activation, coordinate))
    {
    }

    HubInstanceCoordinator::~HubInstanceCoordinator() = default;

    bool HubInstanceCoordinator::IsPrimary() const noexcept { return m_Impl->IsPrimary(); }

    std::optional<HubActivationRequest> HubInstanceCoordinator::PollActivation() { return m_Impl->PollActivation(); }
} // namespace KeireHub
