#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <vector>

TEST_CASE("UI Toolkit reuses a ten-thousand-element layout and bounds leaf invalidation")
{
    constexpr std::size_t groupCount = 99U;
    constexpr std::size_t leavesPerGroup = 100U;
    constexpr std::size_t measuredFrames = 200U;

    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>(10'000U);
    const auto root = tree->Create(Keire::RuntimeUiElementType::VerticalLayout);
    std::vector<Keire::RuntimeUiElementId> leaves;
    leaves.reserve(groupCount * leavesPerGroup);

    Keire::RuntimeUiStyle groupStyle;
    groupStyle.Height = 10.0F;
    groupStyle.ForceExpandHeight = false;
    Keire::RuntimeUiStyle leafStyle;
    leafStyle.Width = 10.0F;
    leafStyle.Height = 10.0F;
    leafStyle.ForceExpandWidth = false;
    leafStyle.ForceExpandHeight = false;

    for (std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
    {
        const auto group = tree->Create(Keire::RuntimeUiElementType::HorizontalLayout, root);
        REQUIRE(tree->SetStyle(group, groupStyle));
        for (std::size_t leafIndex = 0; leafIndex < leavesPerGroup; ++leafIndex)
        {
            const auto leaf = tree->Create(Keire::RuntimeUiElementType::Panel, group);
            REQUIRE(tree->SetStyle(leaf, leafStyle));
            leaves.push_back(leaf);
        }
    }

    REQUIRE(tree->Statistics().Elements == 10'000U);
    tree->Layout(1920.0F, 1080.0F);
    const auto warmed = tree->Statistics();
    REQUIRE(warmed.LayoutPasses == 1U);
    REQUIRE(warmed.DirtyElements == 0U);

    std::vector<double> frameMilliseconds;
    frameMilliseconds.reserve(measuredFrames);
    for (std::size_t frame = 0; frame < measuredFrames; ++frame)
    {
        const auto started = std::chrono::steady_clock::now();
        tree->Layout(1920.0F, 1080.0F);
        frameMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }

    const auto unchanged = tree->Statistics();
    CHECK(unchanged.LayoutPasses == warmed.LayoutPasses);
    CHECK(unchanged.ReusedLayoutPasses == warmed.ReusedLayoutPasses + measuredFrames);
    CHECK(unchanged.DirtyElements == 0U);

    std::ranges::sort(frameMilliseconds);
    const auto percentileIndex =
        std::min(frameMilliseconds.size() - 1U, (frameMilliseconds.size() * 95U + 99U) / 100U - 1U);
    const double p95Milliseconds = frameMilliseconds[percentileIndex];
    INFO("10,000-element unchanged UI p95: " << p95Milliseconds << " ms");
#if defined(NDEBUG)
    CHECK(p95Milliseconds < 2.0);
#endif

    Keire::RuntimeUiContent changedContent;
    changedContent.Text = "Changed leaf";
    REQUIRE(tree->SetContent(leaves.back(), changedContent));
    CHECK(tree->Statistics().DirtyElements == 3U);

    tree->Layout(1920.0F, 1080.0F);
    const auto changed = tree->Statistics();
    CHECK(changed.LayoutPasses == unchanged.LayoutPasses + 1U);
    CHECK(changed.DirtyElements == 0U);
}
