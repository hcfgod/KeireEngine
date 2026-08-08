#include "KeireHub/HubChromeLayout.h"

#include <cstdint>

namespace KeireHub
{
    Keire::WindowChromeLayout BuildHubChromeLayout(const Keire::LogicalExtent size, const bool reserveProductControls)
    {
        const auto width = static_cast<std::int32_t>(size.Width);
        const auto height = static_cast<std::int32_t>(size.Height);
        constexpr std::int32_t edge = 6;
        constexpr std::int32_t corner = 10;
        constexpr std::int32_t titleBarHeight = 40;
        Keire::WindowChromeLayout layout;
        const auto add = [&](const Keire::WindowChromeRole role, const std::int32_t x, const std::int32_t y,
                             const std::int32_t regionWidth, const std::int32_t regionHeight)
        {
            (void)layout.Add(
                {role, {x, y, static_cast<std::uint32_t>(regionWidth), static_cast<std::uint32_t>(regionHeight)}});
        };
        add(Keire::WindowChromeRole::Drag, 0, 0, width, titleBarHeight);
#if defined(__APPLE__)
        add(Keire::WindowChromeRole::Client, 0, 0, 120, titleBarHeight);
        if (reserveProductControls)
            add(Keire::WindowChromeRole::Client, width - 4 - HubProductControlsWidth, 0, HubProductControlsWidth,
                titleBarHeight);
#else
        add(Keire::WindowChromeRole::SystemMenu, 0, 0, 48, titleBarHeight);
        if (reserveProductControls)
        {
            add(Keire::WindowChromeRole::Client, width - HubCaptionStripWidth - HubProductControlsWidth, 0,
                HubProductControlsWidth, titleBarHeight);
        }
        add(Keire::WindowChromeRole::Minimize, width - HubCaptionStripWidth, 0, HubCaptionButtonWidth, titleBarHeight);
        add(Keire::WindowChromeRole::MaximizeRestore, width - HubCaptionRightInset - HubCaptionButtonWidth * 2, 0,
            HubCaptionButtonWidth, titleBarHeight);
        add(Keire::WindowChromeRole::Close, width - HubCaptionRightInset - HubCaptionButtonWidth, 0,
            HubCaptionButtonWidth, titleBarHeight);
#endif
        add(Keire::WindowChromeRole::ResizeLeft, 0, corner, edge, height - corner * 2);
        add(Keire::WindowChromeRole::ResizeRight, width - edge, corner, edge, height - corner * 2);
        add(Keire::WindowChromeRole::ResizeTop, corner, 0, width - corner * 2, edge);
        add(Keire::WindowChromeRole::ResizeBottom, corner, height - edge, width - corner * 2, edge);
        add(Keire::WindowChromeRole::ResizeTopLeft, 0, 0, corner, corner);
        add(Keire::WindowChromeRole::ResizeTopRight, width - corner, 0, corner, corner);
        add(Keire::WindowChromeRole::ResizeBottomLeft, 0, height - corner, corner, corner);
        add(Keire::WindowChromeRole::ResizeBottomRight, width - corner, height - corner, corner, corner);
        return layout;
    }
} // namespace KeireHub
