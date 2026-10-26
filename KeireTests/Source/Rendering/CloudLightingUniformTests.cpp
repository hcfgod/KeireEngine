#include "KeireInternal/Rendering/CloudLightingUniforms.h"

#include <doctest/doctest.h>

TEST_CASE("Cloud light packing preserves evaluated tint and gates disabled sunlight")
{
    using Keire::RenderBackend::MakeCloudLightingUniforms;
    const auto lit = MakeCloudLightingUniforms({0.25F, 0.5F, 1.0F, 0.1F}, 2.0F, {1.0F, 0.5F, 0.25F, 4.0F}, true,
                                               {0.0F, 1.0F, 0.0F, 100.0F});
    CHECK(lit.Ambient.X == 0.5F);
    CHECK(lit.Ambient.Y == 1.0F);
    CHECK(lit.Ambient.Z == 2.0F);
    CHECK(lit.Ambient.W == 0.0F);
    CHECK(lit.Sun.X == 4.0F);
    CHECK(lit.Sun.Y == 2.0F);
    CHECK(lit.Sun.Z == 1.0F);
    CHECK(lit.Sun.W == 0.0F);
    CHECK(lit.SunDirection.Y == 1.0F);
    CHECK(lit.SunDirection.W == 0.0F);

    const auto dark = MakeCloudLightingUniforms({1.0F, 1.0F, 1.0F, 1.0F}, 0.0F, {1.0F, 0.5F, 0.25F, 4.0F}, false,
                                                {0.0F, -1.0F, 0.0F, 100.0F});
    CHECK(dark.Ambient.X == 0.0F);
    CHECK(dark.Ambient.Y == 0.0F);
    CHECK(dark.Ambient.Z == 0.0F);
    CHECK(dark.Sun.X == 0.0F);
    CHECK(dark.Sun.Y == 0.0F);
    CHECK(dark.Sun.Z == 0.0F);
    CHECK(dark.SunDirection.Y == -1.0F);
}
