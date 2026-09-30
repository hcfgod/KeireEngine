#include "KeireInternal/VisualStudioDiscovery.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <system_error>

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

    std::filesystem::path ResolveCompatibleVisualStudioExecutable(const std::uint32_t minimumMajor) noexcept
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
            const auto selected = SelectCompatibleVisualStudioInstallation(installations, minimumMajor);
            if (!selected)
                return {};
            return std::filesystem::absolute(selected->Executable).lexically_normal();
        }
        catch (...)
        {
            return {};
        }
#else
        static_cast<void>(minimumMajor);
        return {};
#endif
    }
} // namespace Keire::Detail
