#include "KeireInternal/VisualStudioDiscovery.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winver.h>
#endif

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumVswhereOutputBytes = std::size_t{1} << 20U;
        constexpr std::size_t MaximumInstallations = 128;
        constexpr std::size_t MaximumVersionBytes = 64;
        constexpr std::size_t MaximumPathBytes = 4096;

        [[nodiscard]] std::optional<std::array<std::uint32_t, 4>> ParseVersion(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > MaximumVersionBytes)
                return std::nullopt;
            std::array<std::uint32_t, 4> result{};
            std::size_t component = 0;
            std::size_t offset = 0;
            while (offset < value.size() && component < result.size())
            {
                const auto end = value.find('.', offset);
                const auto text =
                    value.substr(offset, end == std::string_view::npos ? value.size() - offset : end - offset);
                if (text.empty())
                    return std::nullopt;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result[component]);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                    return std::nullopt;
                ++component;
                if (end == std::string_view::npos)
                {
                    offset = value.size();
                    break;
                }
                if (end + 1U == value.size())
                {
                    return std::nullopt;
                }
                offset = end + 1;
            }
            if (component == 0 || offset < value.size())
                return std::nullopt;
            return result;
        }

        [[nodiscard]] std::filesystem::path VswherePath()
        {
#if defined(_WIN32)
            char* value = nullptr;
            std::size_t size = 0;
            if (_dupenv_s(&value, &size, "ProgramFiles(x86)") != 0 || !value)
                return {};
            const std::filesystem::path root(value);
            std::free(value);
            return root / "Microsoft Visual Studio/Installer/vswhere.exe";
#else
            return {};
#endif
        }
    } // namespace

    std::vector<VisualStudioInstallation> ParseVisualStudioInstallations(const std::string_view document) noexcept
    {
        std::vector<VisualStudioInstallation> result;
        if (document.empty() || document.size() > MaximumVswhereOutputBytes)
            return result;
        try
        {
            const auto parsed = nlohmann::json::parse(document);
            if (!parsed.is_array() || parsed.size() > MaximumInstallations)
                return result;
            result.reserve(parsed.size());
            for (const auto& item : parsed)
            {
                try
                {
                    if (!item.is_object() || !item.value("isComplete", false) || !item.value("isLaunchable", false))
                        continue;
                    const auto versionText = item.value("installationVersion", std::string{});
                    const auto productPath = item.value("productPath", std::string{});
                    const auto version = ParseVersion(versionText);
                    if (!version || productPath.empty() || productPath.size() > MaximumPathBytes)
                        continue;
                    result.push_back({.Version = *version, .Executable = PathFromUtf8(productPath)});
                }
                catch (...)
                {
                    continue;
                }
            }
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }

    std::optional<VisualStudioInstallation>
    SelectCompatibleVisualStudioInstallation(const std::span<const VisualStudioInstallation> installations,
                                             const std::uint32_t minimumMajor)
    {
        std::optional<VisualStudioInstallation> selected;
        for (const auto& installation : installations)
        {
            if (installation.Version[0] < minimumMajor || installation.Executable.empty())
                continue;
            if (!selected || selected->Version < installation.Version ||
                (selected->Version == installation.Version &&
                 selected->Executable.generic_u8string() < installation.Executable.generic_u8string()))
            {
                selected = installation;
            }
        }
        return selected;
    }

    bool IsCompatibleVisualStudioDteMoniker(const std::wstring_view moniker, const std::uint32_t minimumMajor) noexcept
    {
        constexpr std::wstring_view prefix = L"!VisualStudio.DTE.";
        if (!moniker.starts_with(prefix))
            return false;
        auto version = moniker.substr(prefix.size());
        const auto end = version.find_first_of(L".:");
        version = version.substr(0, end);
        if (version.empty())
            return false;
        std::uint32_t major = 0;
        for (const auto character : version)
        {
            if (character < L'0' || character > L'9')
                return false;
            const auto digit = static_cast<std::uint32_t>(character - L'0');
            if (major > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U)
                return false;
            major = major * 10U + digit;
        }
        return major >= minimumMajor;
    }

    std::uint32_t ReadVisualStudioSolutionMinimumMajor(const std::filesystem::path& solution) noexcept
    {
        constexpr auto fallback = ManagedWorkspaceMinimumVisualStudioMajor;
        try
        {
            std::ifstream input(solution, std::ios::binary);
            std::array<char, 4096> buffer{};
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            std::string_view header(buffer.data(), static_cast<std::size_t>(input.gcount()));
            const bool boundedHeader = static_cast<std::size_t>(input.gcount()) == buffer.size();
            if (header.starts_with("\xEF\xBB\xBF"))
            {
                header.remove_prefix(3U);
            }
            if (!header.starts_with("Microsoft Visual Studio Solution File, Format Version 12.00"))
            {
                return fallback;
            }

            std::optional<std::uint32_t> minimum;
            bool reachedProjects = false;
            constexpr std::string_view field = "MinimumVisualStudioVersion";
            while (!header.empty())
            {
                const auto newline = header.find('\n');
                if (newline == std::string_view::npos)
                {
                    break;
                }
                auto line = header.substr(0, newline);
                header.remove_prefix(newline + 1U);
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string_view::npos)
                {
                    continue;
                }
                line.remove_prefix(first);
                if (line.starts_with("Project("))
                {
                    reachedProjects = true;
                    break;
                }
                if (!line.starts_with(field))
                {
                    continue;
                }
                if (minimum)
                {
                    return fallback;
                }
                line.remove_prefix(field.size());
                const auto assignment = line.find_first_not_of(" \t");
                if (assignment == std::string_view::npos || line[assignment] != '=')
                {
                    return fallback;
                }
                line.remove_prefix(assignment + 1U);
                const auto versionBegin = line.find_first_not_of(" \t");
                if (versionBegin == std::string_view::npos)
                {
                    return fallback;
                }
                line.remove_prefix(versionBegin);
                line = line.substr(0, line.find_last_not_of(" \t\r") + 1U);
                const auto version = ParseVersion(line);
                if (!version || (*version)[0] < ManagedWorkspaceCompatibilityVisualStudioMajor)
                {
                    return fallback;
                }
                minimum = (*version)[0];
            }
            return boundedHeader && !reachedProjects ? fallback : minimum.value_or(fallback);
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::filesystem::path SelectManagedVisualStudioSolution(const std::filesystem::path& primarySolution,
                                                            const std::uint32_t visualStudioMajor)
    {
        if (visualStudioMajor != ManagedWorkspaceCompatibilityVisualStudioMajor || primarySolution.empty())
        {
            return primarySolution;
        }
        const auto directory = primarySolution.parent_path() / "Library/Ide/VisualStudio2022";
        const auto solution = directory / primarySolution.filename();
        const auto ready = directory / ".keire-workspace-ready";
        if (!std::filesystem::is_regular_file(solution) || !std::filesystem::is_regular_file(ready))
        {
            return primarySolution;
        }
        try
        {
            if (ReadTextFile(ready, 16U) == "1\n" &&
                ReadVisualStudioSolutionMinimumMajor(solution) == ManagedWorkspaceCompatibilityVisualStudioMajor)
            {
                return solution;
            }
        }
        catch (...)
        {
            // An interrupted/corrupt companion cannot replace the primary workspace.
        }
        return primarySolution;
    }

    bool IsVisualStudioExecutableCompatible(const std::filesystem::path& executable,
                                            const std::uint32_t minimumMajor) noexcept
    {
#if defined(_WIN32)
        try
        {
            const auto release = [](HMODULE module)
            {
                if (module)
                {
                    FreeLibrary(module);
                }
            };
            const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(release)> module(
                LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), release);
            if (!module)
            {
                return false;
            }
            using VersionSizeFunction = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
            using VersionInfoFunction = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
            using QueryValueFunction = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
            const auto sizeFunction =
                reinterpret_cast<VersionSizeFunction>(GetProcAddress(module.get(), "GetFileVersionInfoSizeW"));
            const auto infoFunction =
                reinterpret_cast<VersionInfoFunction>(GetProcAddress(module.get(), "GetFileVersionInfoW"));
            const auto queryFunction =
                reinterpret_cast<QueryValueFunction>(GetProcAddress(module.get(), "VerQueryValueW"));
            if (!sizeFunction || !infoFunction || !queryFunction)
            {
                return false;
            }
            DWORD unused = 0;
            const auto size = sizeFunction(executable.c_str(), &unused);
            constexpr DWORD maximumVersionBytes = 1024U * 1024U;
            if (size == 0 || size > maximumVersionBytes)
            {
                return false;
            }
            std::vector<std::byte> data(size);
            if (!infoFunction(executable.c_str(), 0, size, data.data()))
            {
                return false;
            }
            void* value = nullptr;
            UINT valueBytes = 0;
            if (!queryFunction(data.data(), L"\\", &value, &valueBytes) || !value ||
                valueBytes < sizeof(VS_FIXEDFILEINFO))
            {
                return false;
            }
            const auto& version = *static_cast<const VS_FIXEDFILEINFO*>(value);
            const auto major = static_cast<std::uint32_t>(HIWORD(version.dwFileVersionMS));
            const auto minor = static_cast<std::uint32_t>(LOWORD(version.dwFileVersionMS));
            return version.dwSignature == VS_FFI_SIGNATURE && major >= minimumMajor &&
                   (major != ManagedWorkspaceCompatibilityVisualStudioMajor || minor >= 8U);
        }
        catch (...)
        {
            return false;
        }
#else
        static_cast<void>(executable);
        static_cast<void>(minimumMajor);
        return false;
#endif
    }

    std::vector<VisualStudioInstallation> DiscoverVisualStudioInstallations() noexcept
    {
#if defined(_WIN32)
        try
        {
            const auto vswhere = VswherePath();
            if (!std::filesystem::is_regular_file(vswhere))
                return {};
            const std::array arguments{std::string("-all"),    std::string("-products"), std::string("*"),
                                       std::string("-format"), std::string("json"),      std::string("-utf8")};
            const auto query = RunProcess(vswhere, arguments, vswhere.parent_path(), std::chrono::seconds(5));
            if (query.TimedOut || query.ExitCode != 0 || query.Output.size() > MaximumVswhereOutputBytes)
                return {};
            auto installations = ParseVisualStudioInstallations(query.Output);
            std::erase_if(installations, [](const VisualStudioInstallation& installation)
                          { return !std::filesystem::is_regular_file(installation.Executable); });
            std::ranges::sort(installations,
                              [](const auto& first, const auto& second)
                              {
                                  if (first.Version != second.Version)
                                  {
                                      return first.Version > second.Version;
                                  }
                                  return first.Executable.generic_u8string() < second.Executable.generic_u8string();
                              });
            return installations;
        }
        catch (...)
        {
            return {};
        }
#else
        return {};
#endif
    }

    std::filesystem::path ResolveCompatibleVisualStudioExecutable(const std::uint32_t minimumMajor) noexcept
    {
        try
        {
            auto installations = DiscoverVisualStudioInstallations();
            std::erase_if(installations, [minimumMajor](const auto& installation)
                          { return !IsVisualStudioExecutableCompatible(installation.Executable, minimumMajor); });
            const auto selected = SelectCompatibleVisualStudioInstallation(installations, minimumMajor);
            if (!selected)
                return {};
            return std::filesystem::absolute(selected->Executable).lexically_normal();
        }
        catch (...)
        {
            return {};
        }
    }
} // namespace Keire::Detail
