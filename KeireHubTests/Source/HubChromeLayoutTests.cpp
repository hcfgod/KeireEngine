#include "KeireHub/HubChromeLayout.h"

#include <doctest/doctest.h>

using namespace KeireHub;

namespace
{
    [[nodiscard]] Keire::WindowChromeRole Hit(const Keire::WindowChromeLayout& layout, const std::int32_t x,
                                              const std::int32_t y)
    {
        const auto regions = layout.Regions();
        for (std::size_t index = regions.size(); index > 0; --index)
        {
            if (regions[index - 1].Bounds.Contains({x, y}))
                return regions[index - 1].Role;
        }
        return Keire::WindowChromeRole::Client;
    }
} // namespace

TEST_CASE("Hub chrome keeps every visible product control interactive")
{
    const auto layout = BuildHubChromeLayout({1280, 800});
#if defined(__APPLE__)
    CHECK(Hit(layout, 895, 20) == Keire::WindowChromeRole::Client);
    CHECK(Hit(layout, 1275, 20) == Keire::WindowChromeRole::Client);
#else
    static_assert(HubCaptionButtonSpacing == 0);
    static_assert(HubCaptionControlsWidth == HubCaptionButtonWidth * 3);
    static_assert(HubCaptionStripWidth == HubCaptionControlsWidth + HubCaptionRightInset);
    CHECK(Hit(layout, 711, 20) == Keire::WindowChromeRole::Drag);
    CHECK(Hit(layout, 712, 20) == Keire::WindowChromeRole::Client);
    CHECK(Hit(layout, 1143, 20) == Keire::WindowChromeRole::Client);
    CHECK(Hit(layout, 1144, 20) == Keire::WindowChromeRole::Minimize);
    CHECK(Hit(layout, 1187, 20) == Keire::WindowChromeRole::Minimize);
    CHECK(Hit(layout, 1188, 20) == Keire::WindowChromeRole::MaximizeRestore);
    CHECK(Hit(layout, 1231, 20) == Keire::WindowChromeRole::MaximizeRestore);
    CHECK(Hit(layout, 1232, 20) == Keire::WindowChromeRole::Close);
    CHECK(Hit(layout, 1273, 20) == Keire::WindowChromeRole::Close);
#endif
    CHECK(Hit(layout, 600, 20) == Keire::WindowChromeRole::Drag);
}

TEST_CASE("Hub fatal chrome leaves the product command strip draggable")
{
    const auto layout = BuildHubChromeLayout({1280, 800}, false);
    CHECK(Hit(layout, 800, 20) == Keire::WindowChromeRole::Drag);
}
