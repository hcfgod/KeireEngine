#include "KeireHub/HubInstance.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
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
#include <limits.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
#if !defined(_WIN32)
        constexpr std::string_view ActivationStreamMagic = "KHAC";
        constexpr std::size_t ActivationHeaderBytes = 10;
        static_assert(MaximumHubActivationFrameBytes <= PIPE_BUF,
                      "Hub activation frames must fit in one atomic FIFO write.");

        class UnixDescriptor final
        {
          public:
            UnixDescriptor() = default;
            explicit UnixDescriptor(const int descriptor) noexcept : m_Descriptor(descriptor) {}

            ~UnixDescriptor()
            {
                if (m_Descriptor >= 0)
                    (void)close(m_Descriptor);
            }

            UnixDescriptor(const UnixDescriptor&) = delete;
            UnixDescriptor& operator=(const UnixDescriptor&) = delete;

            UnixDescriptor(UnixDescriptor&& other) noexcept : m_Descriptor(std::exchange(other.m_Descriptor, -1)) {}

            UnixDescriptor& operator=(UnixDescriptor&& other) noexcept
            {
                if (this == &other)
                    return *this;
                if (m_Descriptor >= 0)
                    (void)close(m_Descriptor);
                m_Descriptor = std::exchange(other.m_Descriptor, -1);
                return *this;
            }

            [[nodiscard]] int Get() const noexcept { return m_Descriptor; }
            [[nodiscard]] int Release() noexcept { return std::exchange(m_Descriptor, -1); }

          private:
            int m_Descriptor = -1;
        };

        [[nodiscard]] int CloseOnExecFlags(const int flags) noexcept
        {
#if defined(O_CLOEXEC)
            return flags | O_CLOEXEC;
#else
            return flags;
#endif
        }

        [[nodiscard]] int NoFollowFlags(const int flags) noexcept
        {
#if defined(O_NOFOLLOW)
            return flags | O_NOFOLLOW;
#else
            return flags;
#endif
        }

        [[nodiscard]] bool SetCloseOnExec(const int descriptor) noexcept
        {
            int flags = -1;
            do
            {
                flags = fcntl(descriptor, F_GETFD);
            } while (flags < 0 && errno == EINTR);
            if (flags < 0)
                return false;
            if ((flags & FD_CLOEXEC) != 0)
                return true;

            int result = -1;
            do
            {
                result = fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
            } while (result < 0 && errno == EINTR);
            return result == 0;
        }

        [[nodiscard]] int OpenPath(const std::filesystem::path& path, const int flags) noexcept
        {
            int descriptor = -1;
            do
            {
                descriptor = open(path.c_str(), CloseOnExecFlags(NoFollowFlags(flags)));
            } while (descriptor < 0 && errno == EINTR);
            if (descriptor < 0)
                return -1;
            if (SetCloseOnExec(descriptor))
                return descriptor;

            const auto error = errno;
            (void)close(descriptor);
            errno = error;
            return -1;
        }

        [[nodiscard]] int OpenAt(const int directory, const std::string& name, const int flags,
                                 const mode_t mode = 0) noexcept
        {
            int descriptor = -1;
            do
            {
                descriptor = openat(directory, name.c_str(), CloseOnExecFlags(NoFollowFlags(flags)), mode);
            } while (descriptor < 0 && errno == EINTR);
            if (descriptor < 0)
                return -1;
            if (SetCloseOnExec(descriptor))
                return descriptor;

            const auto error = errno;
            (void)close(descriptor);
            errno = error;
            return -1;
        }

        [[nodiscard]] bool IsPrivateOwner(const struct stat& status) noexcept
        {
            return status.st_uid == geteuid() && (status.st_mode & (S_IRWXG | S_IRWXO)) == 0;
        }

        [[nodiscard]] bool IsPrivateDirectory(const struct stat& status) noexcept
        {
            return S_ISDIR(status.st_mode) && IsPrivateOwner(status) && (status.st_mode & S_IRWXU) == S_IRWXU;
        }

        [[nodiscard]] bool IsSafeRuntimeBase(const struct stat& status) noexcept
        {
            if (!S_ISDIR(status.st_mode))
                return false;
            const bool privateOwner = status.st_uid == geteuid() && (status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
            const bool sharedSticky = status.st_uid == 0 && (status.st_mode & S_ISVTX) != 0;
            return privateOwner || sharedSticky;
        }

        [[nodiscard]] bool IsPrivateRegularFile(const struct stat& status) noexcept
        {
            return S_ISREG(status.st_mode) && IsPrivateOwner(status) && status.st_nlink == 1 &&
                   (status.st_mode & S_IRWXU) == (S_IRUSR | S_IWUSR);
        }

        [[nodiscard]] bool IsPrivateFifo(const struct stat& status) noexcept
        {
            return S_ISFIFO(status.st_mode) && IsPrivateOwner(status) && status.st_nlink == 1 &&
                   (status.st_mode & S_IRWXU) == (S_IRUSR | S_IWUSR);
        }

        [[nodiscard]] bool IsOwnedFifo(const struct stat& status) noexcept
        {
            return S_ISFIFO(status.st_mode) && status.st_uid == geteuid() && status.st_nlink == 1 &&
                   (status.st_mode & (S_IRWXG | S_IRWXO)) == 0;
        }

        [[nodiscard]] int OpenValidatedRuntimeBase(const std::filesystem::path& candidate,
                                                   const bool requirePrivate) noexcept
        {
            if (candidate.empty() || !candidate.is_absolute())
                return -1;

            int flags = O_RDONLY;
#if defined(O_DIRECTORY)
            flags |= O_DIRECTORY;
#endif
            UnixDescriptor descriptor(OpenPath(candidate, flags));
            if (descriptor.Get() < 0)
                return -1;

            struct stat status{};
            if (fstat(descriptor.Get(), &status) != 0)
                return -1;
            if (requirePrivate ? !IsPrivateDirectory(status) : !IsSafeRuntimeBase(status))
                return -1;
            return descriptor.Release();
        }

        struct UnixRuntimeBase final
        {
            UnixDescriptor Descriptor;
        };

        [[nodiscard]] UnixRuntimeBase SelectUnixRuntimeBase()
        {
            if (const auto* configured = std::getenv("XDG_RUNTIME_DIR"); configured && *configured != '\0')
            {
                std::error_code error;
                const auto resolved = std::filesystem::canonical(configured, error);
                if (!error)
                {
                    const auto descriptor = OpenValidatedRuntimeBase(resolved, true);
                    if (descriptor >= 0)
                        return {UnixDescriptor(descriptor)};
                }
            }

            std::error_code error;
            const auto configuredTemporary = std::filesystem::temp_directory_path(error);
            if (error)
                throw std::runtime_error("Could not locate a secure Hub runtime directory.");
            const auto temporary = std::filesystem::canonical(configuredTemporary, error);
            if (error)
                throw std::runtime_error("Could not locate a secure Hub runtime directory.");
            const auto descriptor = OpenValidatedRuntimeBase(temporary, false);
            if (descriptor < 0)
                throw std::runtime_error("Could not open a secure Hub runtime directory.");
            return {UnixDescriptor(descriptor)};
        }

        [[nodiscard]] int CreateUnixRuntimeDirectory()
        {
            auto base = SelectUnixRuntimeBase();
            const auto name = "keire-hub-" + std::to_string(static_cast<unsigned long long>(geteuid()));
            bool created = false;
            if (mkdirat(base.Descriptor.Get(), name.c_str(), 0700) == 0)
                created = true;
            else if (errno != EEXIST)
                throw std::runtime_error("Could not create the Hub runtime directory.");

            int flags = O_RDONLY;
#if defined(O_DIRECTORY)
            flags |= O_DIRECTORY;
#endif
            UnixDescriptor directory(OpenAt(base.Descriptor.Get(), name, flags));
            if (directory.Get() < 0)
                throw std::runtime_error("Could not securely open the Hub runtime directory.");
            if (created && fchmod(directory.Get(), 0700) != 0)
                throw std::runtime_error("Could not secure the Hub runtime directory.");

            struct stat status{};
            if (fstat(directory.Get(), &status) != 0 || !IsPrivateDirectory(status))
                throw std::runtime_error("The Hub runtime directory has unsafe ownership or permissions.");
            return directory.Release();
        }

        [[nodiscard]] int OpenUnixLock(const int directory, const std::string& name)
        {
            bool created = true;
            UnixDescriptor descriptor(OpenAt(directory, name, O_CREAT | O_EXCL | O_RDWR, 0600));
            if (descriptor.Get() < 0 && errno == EEXIST)
            {
                created = false;
                descriptor = UnixDescriptor(OpenAt(directory, name, O_RDWR));
            }
            if (descriptor.Get() < 0)
                throw std::runtime_error("Could not securely open the Hub instance lock.");
            if (created && fchmod(descriptor.Get(), 0600) != 0)
                throw std::runtime_error("Could not secure the Hub instance lock.");

            struct stat status{};
            if (fstat(descriptor.Get(), &status) != 0 || !IsPrivateRegularFile(status))
                throw std::runtime_error("The Hub instance lock has unsafe ownership or permissions.");
            return descriptor.Release();
        }

        void RemoveStaleUnixFifo(const int directory, const std::string& name)
        {
            struct stat status{};
            if (fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            {
                if (errno == ENOENT)
                    return;
                throw std::runtime_error("Could not inspect the Hub activation channel.");
            }
            if (!IsOwnedFifo(status))
                throw std::runtime_error("The Hub activation channel has unsafe ownership or permissions.");
            if (unlinkat(directory, name.c_str(), 0) != 0)
                throw std::runtime_error("Could not replace the stale Hub activation channel.");
        }
#endif

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

#if !defined(_WIN32)
        [[nodiscard]] std::uint16_t ActivationFrameSize(const std::string_view prefix) noexcept
        {
            const auto high = static_cast<unsigned char>(prefix[6]);
            const auto low = static_cast<unsigned char>(prefix[7]);
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
        }

        [[nodiscard]] std::optional<HubActivationRequest> ConsumeActivation(std::string& buffered)
        {
            while (true)
            {
                const auto magic = buffered.find(ActivationStreamMagic);
                if (magic == std::string::npos)
                {
                    if (buffered.size() > ActivationStreamMagic.size() - 1)
                        buffered.erase(0, buffered.size() - (ActivationStreamMagic.size() - 1));
                    return std::nullopt;
                }
                if (magic != 0)
                    buffered.erase(0, magic);
                if (buffered.size() < ActivationHeaderBytes)
                    return std::nullopt;

                const auto size = ActivationFrameSize(buffered);
                if (size < ActivationHeaderBytes || size > MaximumHubActivationFrameBytes)
                {
                    buffered.erase(0, 1);
                    continue;
                }
                if (buffered.size() < size)
                {
                    const auto nextMagic = buffered.find(ActivationStreamMagic, ActivationStreamMagic.size());
                    if (nextMagic == std::string::npos)
                        return std::nullopt;
                    buffered.erase(0, nextMagic);
                    continue;
                }

                const std::string frame = buffered.substr(0, size);
                buffered.erase(0, size);
                auto decoded = DecodeHubActivation(frame);
                if (decoded)
                    return std::move(decoded).Value();
            }
        }
#endif

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
            auto& context = *std::bit_cast<ExistingHubWindow*>(contextValue);
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
                (void)EnumWindows(&FindExistingHubWindow, std::bit_cast<LPARAM>(&context));
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
                const auto validation = ValidateHubActivation(activation);
                if (!validation)
                    throw std::invalid_argument(validation.Error().Message);
                if (!coordinate)
                    return;
                const auto identity = HexHash(ExecutableHash(executable));
#if defined(_WIN32)
                InitializeWindows(identity, executable, activation);
#else
                InitializeUnix(identity, activation);
#endif
                if (m_Primary && activation.Action != HubActivationAction::Show && !activation.RequestsBuildSupport())
                    m_PendingActivation = activation;
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
            RemoveOwnedUnixFifo();
            if (m_Fifo >= 0)
                close(std::exchange(m_Fifo, -1));
            if (m_Lock >= 0)
                close(std::exchange(m_Lock, -1));
            if (m_RuntimeDirectory >= 0)
                close(std::exchange(m_RuntimeDirectory, -1));
            m_OwnsUnixLock = false;
#endif
        }

        [[nodiscard]] bool IsPrimary() const noexcept { return m_Primary; }

        [[nodiscard]] std::optional<HubActivationRequest> PollActivation()
        {
            if (!m_Primary)
                return std::nullopt;
            if (m_PendingActivation)
                return std::exchange(m_PendingActivation, std::nullopt);
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
            const auto length = m_Shared->Length;
            const std::string payload = length <= MaximumHubActivationFrameBytes
                                            ? std::string(m_Shared->Payload, m_Shared->Payload + length)
                                            : std::string{};
            (void)ReleaseMutex(m_ActivationGuard);
            if (sequence == m_LastSequence)
                return std::nullopt;
            m_LastSequence = sequence;
            if (payload.empty())
                return std::nullopt;
            auto decoded = DecodeHubActivation(payload);
            if (!decoded)
                return std::nullopt;
            return std::move(decoded).Value();
#else
            if (m_Fifo < 0)
                return std::nullopt;
            if (auto queued = ConsumeActivation(m_ActivationBuffer))
                return queued;
            std::array<char, MaximumHubActivationFrameBytes * 4> incoming{};
            while (true)
            {
                const auto bytes = read(m_Fifo, incoming.data(), incoming.size());
                if (bytes > 0)
                {
                    constexpr auto maximumBufferedBytes = MaximumHubActivationFrameBytes * 8;
                    if (m_ActivationBuffer.size() + static_cast<std::size_t>(bytes) > maximumBufferedBytes)
                        m_ActivationBuffer.clear();
                    m_ActivationBuffer.append(incoming.data(), static_cast<std::size_t>(bytes));
                    if (auto activation = ConsumeActivation(m_ActivationBuffer))
                        return activation;
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                return std::nullopt;
            }
#endif
        }

      private:
#if defined(_WIN32)
        struct SharedActivation final
        {
            volatile LONG Sequence = 0;
            std::uint32_t Length = 0;
            char Payload[MaximumHubActivationFrameBytes]{};
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
            auto encoded = EncodeHubActivation(activation);
            if (!encoded)
                throw std::invalid_argument(encoded.Error().Message);
            const auto& payload = encoded.Value();
            const auto guard = WaitForSingleObject(m_ActivationGuard, 2000);
            if (guard != WAIT_OBJECT_0 && guard != WAIT_ABANDONED)
                throw std::runtime_error("The active Hub did not accept an activation request.");
            std::memset(m_Shared->Payload, 0, sizeof(m_Shared->Payload));
            std::memcpy(m_Shared->Payload, payload.data(), payload.size());
            m_Shared->Length = static_cast<std::uint32_t>(payload.size());
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
        void RemoveOwnedUnixFifo() noexcept
        {
            if (!m_OwnsUnixLock || !m_OwnsUnixFifo || m_RuntimeDirectory < 0)
                return;

            struct stat status{};
            if (fstatat(m_RuntimeDirectory, m_FifoName.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0 &&
                IsOwnedFifo(status) && status.st_dev == m_FifoDevice && status.st_ino == m_FifoInode)
            {
                (void)unlinkat(m_RuntimeDirectory, m_FifoName.c_str(), 0);
            }
            m_OwnsUnixFifo = false;
        }

        void InitializeUnix(const std::string& identity, const HubActivationRequest& activation)
        {
            m_RuntimeDirectory = CreateUnixRuntimeDirectory();
            m_LockName = identity + ".lock";
            m_FifoName = identity + ".activate";
            m_Lock = OpenUnixLock(m_RuntimeDirectory, m_LockName);
            if (flock(m_Lock, LOCK_EX | LOCK_NB) == 0)
            {
                m_Primary = true;
                m_OwnsUnixLock = true;
            }
            else if (errno != EWOULDBLOCK && errno != EAGAIN)
                throw std::runtime_error("Could not acquire the Hub instance lock.");
            else
                m_Primary = false;
            if (m_Primary)
            {
                RemoveStaleUnixFifo(m_RuntimeDirectory, m_FifoName);
                if (mkfifoat(m_RuntimeDirectory, m_FifoName.c_str(), 0600) != 0)
                    throw std::runtime_error("Could not create the Hub activation channel.");

                struct stat createdStatus{};
                if (fstatat(m_RuntimeDirectory, m_FifoName.c_str(), &createdStatus, AT_SYMLINK_NOFOLLOW) != 0 ||
                    !IsOwnedFifo(createdStatus))
                {
                    throw std::runtime_error("The Hub activation channel has unsafe ownership or permissions.");
                }
                m_FifoDevice = createdStatus.st_dev;
                m_FifoInode = createdStatus.st_ino;
                m_OwnsUnixFifo = true;
                if (fchmodat(m_RuntimeDirectory, m_FifoName.c_str(), 0600, 0) != 0)
                    throw std::runtime_error("Could not secure the Hub activation channel.");

                if (fstatat(m_RuntimeDirectory, m_FifoName.c_str(), &createdStatus, AT_SYMLINK_NOFOLLOW) != 0 ||
                    !IsPrivateFifo(createdStatus))
                {
                    throw std::runtime_error("The Hub activation channel has unsafe ownership or permissions.");
                }
                if (createdStatus.st_dev != m_FifoDevice || createdStatus.st_ino != m_FifoInode)
                    throw std::runtime_error("The Hub activation channel changed while it was secured.");

                m_Fifo = OpenAt(m_RuntimeDirectory, m_FifoName, O_RDWR | O_NONBLOCK);
                if (m_Fifo < 0)
                    throw std::runtime_error("Could not open the Hub activation channel.");
                struct stat openedStatus{};
                if (fstat(m_Fifo, &openedStatus) != 0 || !IsPrivateFifo(openedStatus) ||
                    openedStatus.st_dev != m_FifoDevice || openedStatus.st_ino != m_FifoInode)
                {
                    throw std::runtime_error("The Hub activation channel changed while it was opened.");
                }
                return;
            }
            close(std::exchange(m_Lock, -1));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do
            {
                m_Fifo = OpenAt(m_RuntimeDirectory, m_FifoName, O_WRONLY | O_NONBLOCK);
                if (m_Fifo >= 0)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            if (m_Fifo < 0)
                throw std::runtime_error("Could not notify the active Hub.");
            struct stat status{};
            if (fstat(m_Fifo, &status) != 0 || !IsPrivateFifo(status))
                throw std::runtime_error("The active Hub activation channel is unsafe.");
            auto encoded = EncodeHubActivation(activation);
            if (!encoded)
                throw std::invalid_argument(encoded.Error().Message);
            const auto& payload = encoded.Value();
            if (write(m_Fifo, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size()))
                throw std::runtime_error("Could not send the Hub activation request.");
            close(std::exchange(m_Fifo, -1));
        }

        int m_RuntimeDirectory = -1;
        int m_Lock = -1;
        int m_Fifo = -1;
        std::string m_LockName;
        std::string m_FifoName;
        dev_t m_FifoDevice = 0;
        ino_t m_FifoInode = 0;
        bool m_OwnsUnixLock = false;
        bool m_OwnsUnixFifo = false;
        std::string m_ActivationBuffer;
#endif
        bool m_Primary = true;
        std::optional<HubActivationRequest> m_PendingActivation;
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
