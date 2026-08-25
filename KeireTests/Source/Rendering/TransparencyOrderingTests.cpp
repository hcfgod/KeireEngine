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
