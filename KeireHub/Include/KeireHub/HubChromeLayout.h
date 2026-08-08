#pragma once

#include "Keire/Window.h"

#include <cstdint>

namespace KeireHub
{
    inline constexpr std::int32_t HubCaptionButtonWidth = 44;
    inline constexpr std::int32_t HubCaptionButtonHeight = 38;
    inline constexpr std::int32_t HubCaptionButtonSpacing = 0;
    inline constexpr std::int32_t HubCaptionRightInset = 4;
    inline constexpr std::int32_t HubCaptionControlsWidth = HubCaptionButtonWidth * 3 + HubCaptionButtonSpacing * 2;
    inline constexpr std::int32_t HubCaptionStripWidth = HubCaptionControlsWidth + HubCaptionRightInset;
    inline constexpr std::int32_t HubProductControlsWidth = 432;

    [[nodiscard]] Keire::WindowChromeLayout BuildHubChromeLayout(Keire::LogicalExtent size,
                                                                 bool reserveProductControls = true);
} // namespace KeireHub
