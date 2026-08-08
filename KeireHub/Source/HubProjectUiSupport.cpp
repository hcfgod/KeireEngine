#include "KeireHub/HubProjectUiSupport.h"

#include "KeireHub/HubChromeLayout.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>

#ifndef KEIRE_EDITOR_TARGET
#define KEIRE_EDITOR_TARGET "KeireClient"
#endif

namespace KeireHub
{
    std::string Utf8Path(const std::filesystem::path& path) { return Keire::Detail::PathToUtf8(path); }

    void ReloadProjectRegistry(Keire::Ref<Keire::ProjectRegistry>& registry)
    {
        if (registry)
            registry = Keire::CreateRef<Keire::ProjectRegistry>(registry->Path(),
                                                                Keire::ProjectRegistryLoadMode::CachedMetadata);
    }

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const char character)
                               { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        return value;
    }

    const char* StatusLabel(const Keire::ProjectStatus status) noexcept
    {
        switch (status)
        {
        case Keire::ProjectStatus::Ready:
            return "Ready";
        case Keire::ProjectStatus::UpgradeAvailable:
            return "Upgrade available";
        case Keire::ProjectStatus::RecoveryRequired:
            return "Recovery required";
        case Keire::ProjectStatus::Missing:
            return "Missing";
        case Keire::ProjectStatus::Invalid:
            return "Invalid";
        case Keire::ProjectStatus::RequiresNewerEngine:
            return "Requires newer engine";
        case Keire::ProjectStatus::InUse:
            return "Open in another editor";
        case Keire::ProjectStatus::UnsupportedSchema:
            return "Requires a newer Hub";
        }
        return "Unknown";
    }

    Keire::UiColor StatusColor(const Keire::ProjectStatus status) noexcept
    {
        switch (status)
        {
        case Keire::ProjectStatus::Ready:
            return {0.32F, 0.84F, 0.58F, 1.0F};
        case Keire::ProjectStatus::InUse:
        case Keire::ProjectStatus::UpgradeAvailable:
            return {0.96F, 0.72F, 0.28F, 1.0F};
        case Keire::ProjectStatus::RecoveryRequired:
            return {0.96F, 0.50F, 0.25F, 1.0F};
        case Keire::ProjectStatus::Missing:
        case Keire::ProjectStatus::Invalid:
        case Keire::ProjectStatus::RequiresNewerEngine:
        case Keire::ProjectStatus::UnsupportedSchema:
            return {0.96F, 0.38F, 0.42F, 1.0F};
        }
        return {0.62F, 0.66F, 0.74F, 1.0F};
    }

    bool CanOpenOrUpgrade(const Keire::ProjectStatus status) noexcept
    {
        return status == Keire::ProjectStatus::Ready || status == Keire::ProjectStatus::UpgradeAvailable ||
               status == Keire::ProjectStatus::RecoveryRequired;
    }

    std::string FormatLastOpened(const std::uint64_t seconds)
    {
        if (seconds == 0)
            return "Never";
        if (seconds > static_cast<std::uint64_t>(std::numeric_limits<std::time_t>::max()))
            return "Unknown";
        const std::time_t time = static_cast<std::time_t>(seconds);
        std::tm local{};
#if defined(_WIN32)
        if (localtime_s(&local, &time) != 0)
            return "Unknown";
#else
        if (!localtime_r(&time, &local))
            return "Unknown";
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%b %d, %Y");
        return stream.str();
    }

    std::filesystem::path ResolveEditorExecutable(const std::filesystem::path& hubExecutable)
    {
        return Keire::Detail::ResolveCompanionExecutable(hubExecutable, KEIRE_EDITOR_TARGET);
    }

    std::filesystem::path ResolveAssetToolExecutable(const std::filesystem::path& hubExecutable)
    {
        return Keire::Detail::ResolveCompanionExecutable(hubExecutable, "KeireAssetTool");
    }

    void UpdateHubChromeLayout(Keire::Window& window, const Keire::LogicalExtent size,
                               const bool reserveProductControls)
    {
        if (window.Specification().Decoration != Keire::WindowDecoration::Custom)
            return;
        window.SetChromeLayout(BuildHubChromeLayout(size, reserveProductControls));
    }
} // namespace KeireHub
