#include "ExecutableProcessProbe.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <TlHelp32.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <fstream>
#endif

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumExecutablePathBytes = 32U * 1024U;
        constexpr std::size_t MaximumProcessRecords = 65536U;

        [[nodiscard]] std::filesystem::path NormalizeExecutablePath(const std::filesystem::path& executable)
        {
            std::error_code error;
            auto normalized = std::filesystem::weakly_canonical(executable, error);
            if (!error)
                return normalized.lexically_normal();
            error.clear();
            normalized = std::filesystem::absolute(executable, error);
            return error ? executable.lexically_normal() : normalized.lexically_normal();
        }

        [[nodiscard]] bool ValidExecutablePath(const std::filesystem::path& executable)
        {
            if (executable.empty() || !executable.is_absolute() || !executable.has_filename() ||
                executable == executable.root_path())
            {
                return false;
            }
            try
            {
                return executable.generic_u8string().size() <= MaximumExecutablePathBytes;
            }
            catch (...)
            {
                return false;
            }
        }

#if defined(_WIN32)
        class UniqueHandle final
        {
          public:
            explicit UniqueHandle(const HANDLE handle) noexcept : m_Handle(handle) {}
            ~UniqueHandle()
            {
                if (m_Handle && m_Handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Handle);
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            [[nodiscard]] HANDLE Get() const noexcept { return m_Handle; }
            [[nodiscard]] bool Valid() const noexcept { return m_Handle && m_Handle != INVALID_HANDLE_VALUE; }

          private:
            HANDLE m_Handle = nullptr;
        };

        [[nodiscard]] bool SameWindowsText(const std::wstring_view left, const std::wstring_view right) noexcept
        {
            if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }
            return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                        static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
        }

        [[nodiscard]] bool SameExecutablePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return SameWindowsText(NormalizeExecutablePath(left).native(), NormalizeExecutablePath(right).native());
        }

        [[nodiscard]] bool ProcessStillActive(const HANDLE process) noexcept
        {
            DWORD exitCode = 0;
            return GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
        }

        [[nodiscard]] EditorEntrypointActivity ProbePlatform(const std::filesystem::path& executable)
        {
            const auto targetName = executable.filename().native();
            UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (!snapshot.Valid())
                return EditorEntrypointActivity::Indeterminate;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (!Process32FirstW(snapshot.Get(), &entry))
            {
                return GetLastError() == ERROR_NO_MORE_FILES ? EditorEntrypointActivity::NotRunning
                                                             : EditorEntrypointActivity::Indeterminate;
            }
            std::size_t processCount = 0;
            do
            {
                if (++processCount > MaximumProcessRecords)
                    return EditorEntrypointActivity::Indeterminate;
                if (!SameWindowsText(entry.szExeFile, targetName))
                    continue;

                UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
                if (!process.Valid())
                {
                    const auto error = GetLastError();
                    if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND)
                        continue;
                    return EditorEntrypointActivity::Indeterminate;
                }

                std::wstring image(32768U, L'\0');
                DWORD imageLength = static_cast<DWORD>(image.size());
                if (!QueryFullProcessImageNameW(process.Get(), 0, image.data(), &imageLength))
                {
                    if (!ProcessStillActive(process.Get()))
                        continue;
                    return EditorEntrypointActivity::Indeterminate;
                }
                image.resize(imageLength);
                if (SameExecutablePath(executable, std::filesystem::path(image)))
                    return EditorEntrypointActivity::Running;
            } while (Process32NextW(snapshot.Get(), &entry));

            return GetLastError() == ERROR_NO_MORE_FILES ? EditorEntrypointActivity::NotRunning
                                                         : EditorEntrypointActivity::Indeterminate;
        }
#elif defined(__APPLE__) || defined(__linux__)
        [[nodiscard]] std::string ProcessNameKey(std::string value)
        {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == '\0'))
                value.pop_back();
            return value;
        }

        [[nodiscard]] bool RelevantProcessName(const std::string_view processName,
                                               const std::string_view targetName) noexcept
        {
            if (processName == targetName)
                return true;
            constexpr std::size_t MinimumTruncatedProcessNameBytes = 15;
            return processName.size() >= MinimumTruncatedProcessNameBytes && targetName.size() > processName.size() &&
                   targetName.starts_with(processName);
        }

        [[nodiscard]] bool SameExecutablePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return NormalizeExecutablePath(left) == NormalizeExecutablePath(right);
        }
#endif

#if defined(__APPLE__)
        [[nodiscard]] EditorEntrypointActivity ProbePlatform(const std::filesystem::path& executable)
        {
            const std::string targetName = executable.filename().string();
            std::size_t byteCount = 0;
            constexpr std::array query{CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
            std::vector<kinfo_proc> processes;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto mutableQuery = query;
                if (sysctl(mutableQuery.data(), static_cast<u_int>(mutableQuery.size()), nullptr, &byteCount, nullptr,
                           0) != 0)
                {
                    return EditorEntrypointActivity::Indeterminate;
                }
                const auto requiredRecords = byteCount / sizeof(kinfo_proc) + 1U;
                if (requiredRecords > MaximumProcessRecords - 32U)
                    return EditorEntrypointActivity::Indeterminate;
                processes.resize(requiredRecords + 32U);
                byteCount = processes.size() * sizeof(kinfo_proc);
                if (sysctl(mutableQuery.data(), static_cast<u_int>(mutableQuery.size()), processes.data(), &byteCount,
                           nullptr, 0) == 0)
                {
                    break;
                }
                if (errno != ENOMEM || attempt == 2)
                    return EditorEntrypointActivity::Indeterminate;
            }

            const auto processCount = byteCount / sizeof(kinfo_proc);
            std::array<char, PROC_PIDPATHINFO_MAXSIZE> image{};
            for (std::size_t index = 0; index < processCount; ++index)
            {
                const auto& process = processes[index].kp_proc;
                if (process.p_pid <= 0)
                    continue;
                const auto imageLength = proc_pidpath(process.p_pid, image.data(), static_cast<uint32_t>(image.size()));
                if (imageLength <= 0)
                {
                    const auto processNameEnd = std::ranges::find(process.p_comm, '\0');
                    if (RelevantProcessName(ProcessNameKey(std::string(process.p_comm, processNameEnd)), targetName))
                        return EditorEntrypointActivity::Indeterminate;
                    continue;
                }
                if (SameExecutablePath(executable, std::filesystem::path(image.data())))
                    return EditorEntrypointActivity::Running;
            }
            return EditorEntrypointActivity::NotRunning;
        }
#elif defined(__linux__)
        [[nodiscard]] std::string ReadLinuxProcessName(const std::filesystem::path& processRoot)
        {
            std::ifstream stream(processRoot / "comm", std::ios::binary);
            std::string name;
            if (stream)
                std::getline(stream, name);
            return ProcessNameKey(std::move(name));
        }

        [[nodiscard]] bool IsNumericProcessDirectory(const std::filesystem::path& path)
        {
            const auto name = path.filename().string();
            return !name.empty() && std::ranges::all_of(name, [](const unsigned char character)
                                                        { return std::isdigit(character) != 0; });
        }

        [[nodiscard]] EditorEntrypointActivity ProbePlatform(const std::filesystem::path& executable)
        {
            const std::string targetName = executable.filename().string();
            std::error_code error;
            std::filesystem::directory_iterator iterator(
                "/proc", std::filesystem::directory_options::skip_permission_denied, error);
            if (error)
                return EditorEntrypointActivity::Indeterminate;

            const std::filesystem::directory_iterator end;
            std::size_t processCount = 0;
            while (iterator != end)
            {
                const auto processRoot = iterator->path();
                if (IsNumericProcessDirectory(processRoot))
                {
                    if (++processCount > MaximumProcessRecords)
                        return EditorEntrypointActivity::Indeterminate;
                    const auto processName = ReadLinuxProcessName(processRoot);
                    std::error_code linkError;
                    auto image = std::filesystem::read_symlink(processRoot / "exe", linkError);
                    if (!linkError)
                    {
                        constexpr std::string_view DeletedSuffix = " (deleted)";
                        auto imageText = image.native();
                        if (imageText.ends_with(DeletedSuffix))
                            image = imageText.substr(0, imageText.size() - DeletedSuffix.size());
                        if (SameExecutablePath(executable, image))
                            return EditorEntrypointActivity::Running;
                    }
                    else if (linkError != std::errc::no_such_file_or_directory &&
                             RelevantProcessName(processName, targetName))
                    {
                        return EditorEntrypointActivity::Indeterminate;
                    }
                }

                iterator.increment(error);
                if (error)
                    return EditorEntrypointActivity::Indeterminate;
            }
            return EditorEntrypointActivity::NotRunning;
        }
#elif !defined(_WIN32)
        [[nodiscard]] EditorEntrypointActivity ProbePlatform(const std::filesystem::path&)
        {
            return EditorEntrypointActivity::Indeterminate;
        }
#endif
    } // namespace

    EditorEntrypointActivity ProbeEditorEntrypointActivity(const std::filesystem::path& executable) noexcept
    {
        try
        {
            if (!ValidExecutablePath(executable))
                return EditorEntrypointActivity::Indeterminate;
            return ProbePlatform(NormalizeExecutablePath(executable));
        }
        catch (...)
        {
            return EditorEntrypointActivity::Indeterminate;
        }
    }
} // namespace KeireHub::Detail
