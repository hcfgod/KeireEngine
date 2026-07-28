#include "KeireInternal/Rendering/TransparencyInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

TEST_CASE("transparent view depths sort far to near for the negative-Z camera convention")
{
    std::array depths{-1.0F, -10.0F, -3.0F};
    std::ranges::stable_sort(depths, Keire::Detail::TransparentBackToFront);

    CHECK(depths == std::array{-10.0F, -3.0F, -1.0F});
    CHECK_FALSE(Keire::Detail::TransparentBackToFront(-2.0F, -2.0F));
}
