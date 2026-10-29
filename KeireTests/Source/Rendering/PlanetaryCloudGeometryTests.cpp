#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using uint = std::uint32_t;
    using std::isfinite;
    using std::max;
    using std::min;
    using std::sqrt;

#include "../../../KeireCore/Shaders/PlanetaryCloudIntersections.hlsli"
} // namespace

TEST_CASE("Planetary cloud shells retain thin surface layers and orbital entry")
{
    const auto surface = IntersectPlanetaryCloudShell(6360000.0F, 0.125F, 1.0F, 1000.0F, 500.0F, 100000.0F);
    CHECK(surface.FirstEntry == doctest::Approx(999.875F));
    CHECK(surface.FirstExit == doctest::Approx(1499.875F));

    const auto orbit = IntersectPlanetaryCloudShell(6360000.0F, 1000000.0F, -1.0F, 1000.0F, 500.0F, 2000000.0F);
    CHECK(orbit.FirstEntry == doctest::Approx(998500.0F));
    CHECK(orbit.FirstExit == doctest::Approx(999000.0F));
    CHECK(orbit.SecondExit == orbit.SecondEntry);
}

TEST_CASE("Planetary clouds preserve two limb intervals without sampling the empty core")
{
    const float radius = 1000.0F;
    const float observerRadius = 2000.0F;
    const float impactRadius = 1050.0F;
    const float cosine = -std::sqrt(1.0F - impactRadius * impactRadius / (observerRadius * observerRadius));
    const auto shell = IntersectPlanetaryCloudShell(radius, observerRadius - radius, cosine, 100.0F, 100.0F, 5000.0F);
    CHECK(shell.FirstExit > shell.FirstEntry);
    CHECK(shell.SecondEntry > shell.FirstExit);
    CHECK(shell.SecondExit > shell.SecondEntry);
    CHECK(shell.FirstExit - shell.FirstEntry == doctest::Approx(shell.SecondExit - shell.SecondEntry));
}

TEST_CASE("Planetary clouds clip scene depth planet occlusion and rays outside the shell")
{
    const auto ground = IntersectPlanetaryCloudShell(1000.0F, 500.0F, -1.0F, 100.0F, 100.0F, 5000.0F);
    CHECK(ground.FirstEntry == doctest::Approx(300.0F));
    CHECK(ground.FirstExit == doctest::Approx(400.0F));
    CHECK(ground.SecondExit == ground.SecondEntry);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 500.0F, -1.0F, 100.0F, 100.0F, 200.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 500.0F, 1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, -1.0F, 1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 0.0F, -1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
}

TEST_CASE("Planetary clouds handle inside tangent and invalid geometry")
{
    const auto inside = IntersectPlanetaryCloudShell(6360000.0F, 1250.0F, 0.0F, 1000.0F, 500.0F, 100000.0F);
    CHECK(inside.FirstEntry == 0.0F);
    CHECK(inside.FirstExit > 50000.0F);
    CHECK(inside.FirstExit < 60000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(PlanetaryCloudSphereRoots(1000.0F, nan, 1.0F, 100.0F).Hit);
    CHECK_FALSE(PlanetaryCloudSphereRoots(0.0F, 1.0F, 1.0F, 100.0F).Hit);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 0.0F, 1.0F, 100.0F, 0.0F, 10000.0F).FirstExit == 0.0F);
}

TEST_CASE("Planetary cloud shared sampling budget skips vacuum and preserves optical length")
{
    PlanetaryCloudIntervals intervals{1000000.0F, 1001000.0F, 2000000.0F, 2002000.0F};
    const auto limited = LimitPlanetaryCloudTravel(intervals, 1500.0F);
    CHECK(limited.FirstEntry == 1000000.0F);
    CHECK(limited.FirstExit == 1001000.0F);
    CHECK(limited.SecondEntry == 2000000.0F);
    CHECK(limited.SecondExit == 2000500.0F);
    float length = 0.0F;
    for (uint index = 0U; index < 48U; ++index)
    {
        const auto sample = PlanetaryCloudSample(limited, 48U, index);
        CHECK(sample.Exit >= sample.Entry);
        CHECK((sample.Exit <= limited.FirstExit || sample.Entry >= limited.SecondEntry));
        length += sample.Exit - sample.Entry;
    }
    CHECK(length == doctest::Approx(1500.0F));
}
