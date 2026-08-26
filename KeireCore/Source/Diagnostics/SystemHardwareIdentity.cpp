#include "KeireInternal/Diagnostics/SystemHardwareIdentityInternal.h"

#include "Keire/BuildInfo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

namespace Keire::Internal
{
    namespace
    {
        [[nodiscard]] std::string TrimSystemValue(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n\"");
            const auto last = value.find_last_not_of(" \t\r\n\"");
            return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1U);
        }

#if defined(_WIN32)
        [[nodiscard]] std::string Utf8(const std::wstring_view value)
        {
            if (value.empty())
                return {};
            const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
                return {};
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), size, nullptr, nullptr) != size)
                return {};
            return result;
        }

        [[nodiscard]] std::string RegistrySystemString(const wchar_t* key, const wchar_t* name)
        {
            DWORD bytes = 0;
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
                bytes < sizeof(wchar_t))
                return {};
            std::vector<wchar_t> value(static_cast<std::size_t>(bytes / sizeof(wchar_t)), L'\0');
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) !=
                ERROR_SUCCESS)
                return {};
            while (!value.empty() && value.back() == L'\0')
                value.pop_back();
            return Utf8({value.data(), value.size()});
        }

        void PopulateOperatingSystemAndCpu(SystemHardwareIdentity& result)
        {
            constexpr auto windowsVersionKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
            result.OperatingSystemDescription = RegistrySystemString(windowsVersionKey, L"ProductName");
            const auto displayVersion = RegistrySystemString(windowsVersionKey, L"DisplayVersion");
            if (!displayVersion.empty())
                result.OperatingSystemDescription += " " + displayVersion;
            OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
            const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
            if (rtlGetVersion && rtlGetVersion(&version) >= 0)
            {
                result.OperatingSystemVersion = std::to_string(version.dwMajorVersion) + "." +
                                                std::to_string(version.dwMinorVersion) + "." +
                                                std::to_string(version.dwBuildNumber);
            }
            if (result.OperatingSystemDescription.empty())
                result.OperatingSystemDescription = "Windows";
            result.CpuModel =
                RegistrySystemString(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
        }

        [[nodiscard]] std::uint64_t PhysicalMemoryBytes() noexcept
        {
            MEMORYSTATUSEX state{};
            state.dwLength = sizeof(state);
            return GlobalMemoryStatusEx(&state) ? state.ullTotalPhys : 0U;
        }
#elif defined(__APPLE__)
        [[nodiscard]] std::string SysctlString(const char* name)
        {
            std::size_t size = 0;
            if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size <= 1U)
                return {};
            std::string result(size, '\0');
            if (sysctlbyname(name, result.data(), &size, nullptr, 0) != 0)
                return {};
            result.resize(size);
            while (!result.empty() && result.back() == '\0')
                result.pop_back();
            return TrimSystemValue(std::move(result));
        }

        void PopulateOperatingSystemAndCpu(SystemHardwareIdentity& result)
        {
            result.OperatingSystemVersion = SysctlString("kern.osproductversion");
            result.OperatingSystemDescription =
                result.OperatingSystemVersion.empty() ? "macOS" : "macOS " + result.OperatingSystemVersion;
            result.CpuModel = SysctlString("machdep.cpu.brand_string");
            if (result.CpuModel.empty())
                result.CpuModel = SysctlString("hw.model");
        }

        [[nodiscard]] std::uint64_t PhysicalMemoryBytes() noexcept
        {
            std::uint64_t bytes = 0;
            std::size_t size = sizeof(bytes);
            return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0U;
        }
#elif defined(__linux__)
        [[nodiscard]] std::string ReadSystemField(const std::filesystem::path& path, const std::string_view key,
                                                  const char separator)
        {
            std::ifstream stream(path);
            std::string line;
            while (std::getline(stream, line))
            {
                const auto split = line.find(separator);
                if (split != std::string::npos && TrimSystemValue(line.substr(0, split)) == key)
                    return TrimSystemValue(line.substr(split + 1U));
            }
            return {};
        }

        void PopulateOperatingSystemAndCpu(SystemHardwareIdentity& result)
        {
            result.OperatingSystemDescription = ReadSystemField("/etc/os-release", "PRETTY_NAME", '=');
            struct utsname system{};
            if (uname(&system) == 0)
                result.OperatingSystemVersion = system.release;
            if (result.OperatingSystemDescription.empty())
                result.OperatingSystemDescription = "Linux";
            result.CpuModel = ReadSystemField("/proc/cpuinfo", "model name", ':');
            if (result.CpuModel.empty())
                result.CpuModel = ReadSystemField("/proc/cpuinfo", "Hardware", ':');
        }

        [[nodiscard]] std::uint64_t PhysicalMemoryBytes() noexcept
        {
            struct sysinfo state{};
            return sysinfo(&state) == 0 ? static_cast<std::uint64_t>(state.totalram) * state.mem_unit : 0U;
        }
#else
        void PopulateOperatingSystemAndCpu(SystemHardwareIdentity& result)
        {
            result.OperatingSystemDescription = std::string(GetBuildInfo().Platform);
        }

        [[nodiscard]] std::uint64_t PhysicalMemoryBytes() noexcept { return 0U; }
#endif
    } // namespace

    SystemHardwareIdentity QuerySystemHardwareIdentity()
    {
        SystemHardwareIdentity result;
        PopulateOperatingSystemAndCpu(result);
        result.LogicalProcessorCount = (std::max)(1U, std::thread::hardware_concurrency());
        result.PhysicalMemoryBytes = PhysicalMemoryBytes();
        return result;
    }
} // namespace Keire::Internal
