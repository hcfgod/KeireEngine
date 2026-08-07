#pragma once

#include "Keire/Window.h"

namespace KeireHub
{
    [[nodiscard]] Keire::WindowChromeLayout BuildHubChromeLayout(Keire::LogicalExtent size,
                                                                 bool reserveProductControls = true);
} // namespace KeireHub
