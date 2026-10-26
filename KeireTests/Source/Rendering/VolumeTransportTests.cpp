#include "KeireInternal/Rendering/VolumeTransport.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

TEST_CASE("Volume integration preserves vacuum and zero-length limits")
{
    const auto empty = Keire::Detail::IntegrateVolumeSegment(4.0, 0.0);
    CHECK(empty.Transmittance == 1.0);
    CHECK(empty.SourceWeight == 0.0);
    const auto vacuum = Keire::Detail::IntegrateVolumeSegment(0.0, 200.0);
    CHECK(vacuum.Transmittance == 1.0);
    CHECK(vacuum.SourceWeight == 200.0);
    const auto thin = Keire::Detail::IntegrateVolumeSegment(1.0e-20, 1.0);
    CHECK(thin.SourceWeight == doctest::Approx(1.0));
    const auto opaque = Keire::Detail::IntegrateVolumeSegment(std::numeric_limits<double>::max(), 2.0);
    CHECK(opaque.Transmittance == 0.0);
    CHECK(std::isfinite(opaque.SourceWeight));
    CHECK(opaque.SourceWeight > 0.0);
}

TEST_CASE("Homogeneous volume transport is invariant under segment subdivision")
{
    for (const double extinction : {0.0, 1.0e-12, 1.0e-5, 0.01, 1.0, 100.0})
    {
        for (const int count : {1, 2, 8, 64, 256})
        {
            const auto whole = Keire::Detail::IntegrateVolumeSegment(extinction, 100.0);
            const auto segment = Keire::Detail::IntegrateVolumeSegment(extinction, 100.0 / count);
            Keire::Detail::VolumeRadiance accumulated;
            for (int index = 0; index < count; ++index)
            {
                accumulated = Keire::Detail::ComposeVolumeRadiance(accumulated,
                                                                   {segment.Transmittance, segment.SourceWeight * 2.0});
            }
            CHECK(accumulated.Transmittance == doctest::Approx(whole.Transmittance).epsilon(1.0e-10));
            CHECK(accumulated.Radiance == doctest::Approx(whole.SourceWeight * 2.0).epsilon(1.0e-10));
            CHECK(accumulated.Transmittance >= 0.0);
            CHECK(accumulated.Transmittance <= 1.0);
        }
    }
}

TEST_CASE("Volume composition attenuates only material behind the front segment")
{
    const auto composed = Keire::Detail::ComposeVolumeRadiance({0.25, 3.0}, {0.5, 8.0});
    CHECK(composed.Transmittance == 0.125);
    CHECK(composed.Radiance == 5.0);
    const auto opaque = Keire::Detail::ComposeVolumeRadiance({0.0, 3.0}, {0.5, 8.0});
    CHECK(opaque.Radiance == 3.0);
}

TEST_CASE("Volume integration rejects invalid physical inputs")
{
    for (const double invalid :
         {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS((void)Keire::Detail::IntegrateVolumeSegment(invalid, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)Keire::Detail::IntegrateVolumeSegment(1.0, invalid), std::invalid_argument);
    }
}
