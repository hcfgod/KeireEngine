#include "KeireHub/HubProjectUiSupport.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
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

    std::string FormatLastOpened(const std::int64_t seconds)
    {
        if (seconds <= 0)
            return "Never";
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
        const auto width = static_cast<std::int32_t>(size.Width);
        const auto height = static_cast<std::int32_t>(size.Height);
        constexpr std::int32_t edge = 6;
        constexpr std::int32_t corner = 10;
        Keire::WindowChromeLayout layout;
        const auto add = [&](const Keire::WindowChromeRole role, const std::int32_t x, const std::int32_t y,
                             const std::int32_t regionWidth, const std::int32_t regionHeight)
        {
            (void)layout.Add(
                {role, {x, y, static_cast<std::uint32_t>(regionWidth), static_cast<std::uint32_t>(regionHeight)}});
        };
        add(Keire::WindowChromeRole::Drag, 0, 0, width, 40);
#if defined(__APPLE__)
        add(Keire::WindowChromeRole::Client, 0, 0, 120, 40);
        if (reserveProductControls)
            add(Keire::WindowChromeRole::Client, width - 290, 0, 290, 40);
#else
        constexpr std::int32_t captionWidth = 44;
        add(Keire::WindowChromeRole::SystemMenu, 0, 0, 48, 40);
        // DrawTitleBar reserves four logical pixels at the right edge and a 286-pixel command strip immediately
        // before the three caption buttons. Keep native hit-test roles pixel-aligned with those visible controls so
        // Windows Snap Layout hover and caption clicks agree with ImGui at every DPI scale.
        if (reserveProductControls)
            add(Keire::WindowChromeRole::Client, width - 422, 0, 286, 40);
        add(Keire::WindowChromeRole::Minimize, width - 136, 0, captionWidth, 40);
        add(Keire::WindowChromeRole::MaximizeRestore, width - 92, 0, captionWidth, 40);
        add(Keire::WindowChromeRole::Close, width - 48, 0, captionWidth, 40);
#endif
        add(Keire::WindowChromeRole::ResizeLeft, 0, corner, edge, height - corner * 2);
        add(Keire::WindowChromeRole::ResizeRight, width - edge, corner, edge, height - corner * 2);
        add(Keire::WindowChromeRole::ResizeTop, corner, 0, width - corner * 2, edge);
        add(Keire::WindowChromeRole::ResizeBottom, corner, height - edge, width - corner * 2, edge);
        add(Keire::WindowChromeRole::ResizeTopLeft, 0, 0, corner, corner);
        add(Keire::WindowChromeRole::ResizeTopRight, width - corner, 0, corner, corner);
        add(Keire::WindowChromeRole::ResizeBottomLeft, 0, height - corner, corner, corner);
        add(Keire::WindowChromeRole::ResizeBottomRight, width - corner, height - corner, corner, corner);
        window.SetChromeLayout(layout);
    }
} // namespace KeireHub
