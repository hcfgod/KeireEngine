#include "KeireInternal/Rendering/VolumeTransport.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

namespace ShaderKernel
{
    using std::exp;
#include "../../../KeireCore/Shaders/VolumeTransport.hlsli"
} // namespace ShaderKernel

TEST_CASE("Shared shader medium integration agrees with the independent double precision reference")
{
    for (const float extinction : {0.0F, 1.0e-8F, 0.001F, 0.1F, 1.0F, 100.0F})
    {
        for (const float distance : {0.0F, 0.001F, 1.0F, 1000.0F})
        {
            const std::array media{Keire::Detail::VolumeMediumSample{extinction, 0.25}};
            const auto reference = Keire::Detail::IntegrateOverlappingMedia(media, distance);
            const auto shader = ShaderKernel::IntegrateVolumeMedium(extinction, 0.25F, distance);
            CHECK(shader.Transmittance == doctest::Approx(reference.Transmittance).epsilon(1.0e-5));
            CHECK(shader.Radiance == doctest::Approx(reference.Radiance).epsilon(1.0e-5));
        }
    }
}

TEST_CASE("Overlapping fog and cloud coefficients integrate independently of layer order")
{
    const std::array media{Keire::Detail::VolumeMediumSample{0.1, 0.4}, Keire::Detail::VolumeMediumSample{0.3, 0.2}};
    const std::array reversed{media[1], media[0]};
    const auto result = Keire::Detail::IntegrateOverlappingMedia(media, 8.0);
    const auto reverseResult = Keire::Detail::IntegrateOverlappingMedia(reversed, 8.0);
    CHECK(result.Transmittance == reverseResult.Transmittance);
    CHECK(result.Radiance == reverseResult.Radiance);
    CHECK(result.Transmittance == doctest::Approx(std::exp(-3.2)));
    CHECK(result.Radiance == doctest::Approx(1.5 * (1.0 - std::exp(-3.2))));

    for (const int count : {1, 2, 16, 64, 128})
    {
        Keire::Detail::VolumeRadiance accumulated;
        const auto segment = Keire::Detail::IntegrateOverlappingMedia(media, 8.0 / count);
        for (int index = 0; index < count; ++index)
        {
            accumulated = Keire::Detail::ComposeVolumeRadiance(accumulated, segment);
        }
        CHECK(accumulated.Transmittance == doctest::Approx(result.Transmittance));
        CHECK(accumulated.Radiance == doctest::Approx(result.Radiance));
    }
}

TEST_CASE("Overlapping media retain emission in vacuum and reject invalid coefficients")
{
    const std::array emission{Keire::Detail::VolumeMediumSample{0.0, 2.0}};
    const auto result = Keire::Detail::IntegrateOverlappingMedia(emission, 3.0);
    CHECK(result.Transmittance == 1.0);
    CHECK(result.Radiance == 6.0);
    CHECK(Keire::Detail::IntegrateOverlappingMedia({}, 3.0).Radiance == 0.0);
    CHECK(Keire::Detail::IntegrateOverlappingMedia(emission, 0.0).Radiance == 0.0);
    for (const double invalid :
         {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        const std::array badExtinction{Keire::Detail::VolumeMediumSample{invalid, 0.0}};
        const std::array badSource{Keire::Detail::VolumeMediumSample{0.0, invalid}};
        CHECK_THROWS_AS((void)Keire::Detail::IntegrateOverlappingMedia(badExtinction, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)Keire::Detail::IntegrateOverlappingMedia(badSource, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)Keire::Detail::IntegrateOverlappingMedia({}, invalid), std::invalid_argument);
    }
    const double maximum = std::numeric_limits<double>::max();
    const std::array overflowingSource{Keire::Detail::VolumeMediumSample{0.0, maximum}};
    CHECK_THROWS_AS((void)Keire::Detail::IntegrateOverlappingMedia(overflowingSource, 2.0), std::invalid_argument);
    const std::array overflowingExtinction{Keire::Detail::VolumeMediumSample{maximum, 0.0},
                                           Keire::Detail::VolumeMediumSample{maximum, 0.0}};
    CHECK_THROWS_AS((void)Keire::Detail::IntegrateOverlappingMedia(overflowingExtinction, 1.0), std::invalid_argument);
}

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
