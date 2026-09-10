#include "KeireInternal/Rendering/SceneDrawOrderInternal.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"

#include "Keire/Math/Math.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

TEST_CASE("transparent view depths sort far to near for the left-handed camera convention")
{
    const auto view = Keire::Math::LookAt({2.0F, 3.0F, -4.0F}, {2.0F, 3.0F, -3.0F}, {0.0F, 1.0F, 0.0F});
    std::array depths{Keire::Math::TransformPoint(view, {2.0F, 3.0F, -3.0F}).Z,
                      Keire::Math::TransformPoint(view, {2.0F, 3.0F, 6.0F}).Z,
                      Keire::Math::TransformPoint(view, {2.0F, 3.0F, -1.0F}).Z};
    std::ranges::stable_sort(depths, Keire::Detail::TransparentBackToFront);

    CHECK(depths[0] == doctest::Approx(10.0F));
    CHECK(depths[1] == doctest::Approx(3.0F));
    CHECK(depths[2] == doctest::Approx(1.0F));
    CHECK_FALSE(Keire::Detail::TransparentBackToFront(2.0F, 2.0F));
}

TEST_CASE("transparent and decal queues preserve a strict depth order across mixed authored alpha modes")
{
    using Key = Keire::Detail::SceneDrawOrderKey;
    const auto firstEntity = Keire::AssetId(0, 1);
    const auto secondEntity = Keire::AssetId(0, 2);
    const std::array expected{
        Key{.AlphaMode = Keire::MaterialAlphaMode::Blend, .Depth = 8.0F, .Entity = firstEntity},
        Key{.AlphaMode = Keire::MaterialAlphaMode::Opaque, .Depth = 4.0F, .Entity = firstEntity},
        Key{.AlphaMode = Keire::MaterialAlphaMode::Additive, .Depth = 4.0F, .Entity = secondEntity},
        Key{.AlphaMode = Keire::MaterialAlphaMode::Mask, .Depth = 4.0F, .ContributionOrder = 1, .Entity = firstEntity},
        Key{.AlphaMode = Keire::MaterialAlphaMode::Opaque, .Depth = 1.0F, .Entity = firstEntity}};
    const auto less = [](const Key& left, const Key& right) { return Keire::Detail::SceneDrawLess(left, right, true); };
    auto draws = expected;
    std::ranges::reverse(draws);
    std::ranges::stable_sort(draws, less);
    CHECK(draws == expected);
    for (const auto& left : expected)
    {
        CHECK_FALSE(less(left, left));
        for (const auto& right : expected)
        {
            if (less(left, right))
                CHECK_FALSE(less(right, left));
            for (const auto& last : expected)
                if (less(left, right) && less(right, last))
                    CHECK(less(left, last));
        }
    }
}

TEST_CASE("opaque draw ordering retains batching priority and deterministic equal-depth ties")
{
    using Key = Keire::Detail::SceneDrawOrderKey;
    const auto firstMaterial = Keire::AssetId(0, 1);
    const auto secondMaterial = Keire::AssetId(0, 2);
    const std::array expected{
        Key{.Material = firstMaterial, .Depth = 2.0F, .Entity = Keire::AssetId(0, 1)},
        Key{.Material = firstMaterial, .Depth = 2.0F, .Entity = Keire::AssetId(0, 2)},
        Key{.Material = firstMaterial, .Depth = 2.0F, .ContributionOrder = 1, .Entity = Keire::AssetId(0, 1)},
        Key{.Material = firstMaterial, .Depth = 8.0F}, Key{.Material = secondMaterial, .Depth = 1.0F}};
    auto draws = expected;
    std::ranges::reverse(draws);
    std::ranges::stable_sort(draws, [](const Key& left, const Key& right)
                             { return Keire::Detail::SceneDrawLess(left, right, false); });
    CHECK(draws == expected);
}
