#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace
{
    using std::clamp;
    using std::exp;
    using std::max;
    using std::sqrt;

#include "../../../KeireCore/Shaders/CloudVolumeLighting.hlsli"

    struct LayerTransport
    {
        double Radiance = 0.0;
        double Transmission = 1.0;
        int Samples = 0;
    };

    LayerTransport IntegrateLayers(const double density, const bool terminateOpaque)
    {
        LayerTransport result;
        for (int layer = 0; layer < 128; ++layer)
        {
            if (terminateOpaque && VolumeCloudTransportComplete(static_cast<float>(result.Transmission)))
            {
                result.Transmission = 0.0;
                break;
            }

            // Empty intervals and bright sources behind dark foreground layers exercise the omitted tail.
            const double opticalDepth = static_cast<double>(layer % 7) * 0.29 * density;
            const double sourceRadiance = static_cast<double>((layer * 37) % 101) * 10.0;
            const double intervalTransmission = std::exp(-opticalDepth);
            result.Radiance += result.Transmission * sourceRadiance * (1.0 - intervalTransmission);
            result.Transmission *= intervalTransmission;
            ++result.Samples;
        }
        return result;
    }
} // namespace

TEST_CASE("Opaque cloud termination retains thin rays and bounds the omitted transport")
{
    CHECK_FALSE(VolumeCloudTransportComplete(1.0F));
    CHECK_FALSE(VolumeCloudTransportComplete(std::nextafter(VolumeCloudTransmissionCutoff, 1.0F)));
    CHECK(VolumeCloudTransportComplete(VolumeCloudTransmissionCutoff));
    CHECK(VolumeCloudTransportComplete(0.0F));

    double previousTransmission = 1.0;
    for (const double density : {0.0, 0.0001, 0.01, 0.1, 1.0, 3.0, 10.0, 1000.0})
    {
        const auto reference = IntegrateLayers(density, false);
        const auto bounded = IntegrateLayers(density, true);
        CHECK(bounded.Transmission <= previousTransmission);
        CHECK(bounded.Transmission >= 0.0);
        CHECK(bounded.Transmission <= reference.Transmission);
        CHECK(reference.Transmission - bounded.Transmission <= VolumeCloudTransmissionCutoff);
        CHECK(bounded.Radiance <= reference.Radiance);
        CHECK(reference.Radiance - bounded.Radiance <= 1000.0 * VolumeCloudTransmissionCutoff);
        if (reference.Transmission > VolumeCloudTransmissionCutoff)
        {
            CHECK(bounded.Samples == 128);
            CHECK(bounded.Radiance == reference.Radiance);
            CHECK(bounded.Transmission == reference.Transmission);
        }
        if (density >= 1.0)
        {
            CHECK(bounded.Samples < 128);
        }
        previousTransmission = bounded.Transmission;
    }
}

TEST_CASE("Cloud scattering preserves the disabled approximation and stays finite at supported extremes")
{
    for (const float anisotropy : {-0.95F, 0.0F, 0.65F, 0.95F})
    {
        for (const float cosine : {-1.0F, 0.0F, 1.0F})
        {
            for (const float depth : {0.0F, 1.0F, 10.0F, 10000000.0F})
            {
                const float direct = VolumeCloudPhase(cosine, anisotropy) * std::exp(-depth);
                CHECK(VolumeCloudIncidentLight(cosine, anisotropy, depth, 0.0F, 0.5F) == direct);
                const float multiple = VolumeCloudIncidentLight(cosine, anisotropy, depth, 1.0F, 0.5F);
                CHECK(std::isfinite(multiple));
                CHECK(multiple >= direct);
                CHECK(VolumeCloudIncidentLight(cosine, anisotropy, depth, 0.5F, 0.5F) <= multiple);
                CHECK(VolumeCloudIncidentLight(cosine, anisotropy, depth, 1.0F, 0.25F) >= multiple);
            }
        }
    }
}

TEST_CASE("Isotropic cloud scattering matches the bounded two-order sum")
{
    const float phase = VolumeCloudPhase(0.0F, 0.0F);
    CHECK(phase == doctest::Approx(1.0F / (4.0F * 3.1415926536F)));
    CHECK(VolumeCloudIncidentLight(0.0F, 0.0F, 0.0F, 0.5F, 0.5F) == doctest::Approx(phase * 1.75F));
    CHECK(VolumeCloudIncidentLight(0.0F, 0.0F, 10.0F, 0.5F, 1.0F) == doctest::Approx(phase * std::exp(-10.0F) * 1.75F));
    CHECK(VolumeCloudIncidentLight(0.0F, 0.0F, 10000000.0F, 0.5F, 0.0F) == doctest::Approx(phase * 0.75F));
}

TEST_CASE("Cloud representative depth matches independent extinction-event quadrature")
{
    for (const float opticalDepth : {0.0F, 0.001F, 0.1F, 0.249F, 0.25F, 0.5F, 1.0F, 4.0F, 20.0F, 80.0F})
    {
        constexpr int intervals = 8192;
        double mass = 0.0;
        double firstMoment = 0.0;
        for (int index = 0; index <= intervals; ++index)
        {
            const double position = static_cast<double>(index) / intervals;
            const double weight = index == 0 || index == intervals ? 1.0 : index % 2 == 0 ? 2.0 : 4.0;
            const double density = std::exp(-static_cast<double>(opticalDepth) * position);
            mass += weight * density;
            firstMoment += weight * density * position;
        }
        const float fraction = VolumeCloudMeanEventFraction(opticalDepth);
        CHECK(std::isfinite(fraction));
        CHECK(fraction >= 0.0F);
        CHECK(fraction <= 0.5F);
        CHECK(fraction == doctest::Approx(firstMoment / mass).epsilon(0.000003));
    }
    CHECK(VolumeCloudMeanEventFraction(10000000.0F) == doctest::Approx(0.0000001F));
}

TEST_CASE("Cloud representative depth is independent of homogeneous interval subdivision")
{
    for (const float totalDepth : {0.001F, 0.25F, 1.0F, 20.0F, 80.0F})
    {
        for (const int intervals : {1, 7, 48, 128})
        {
            double totalWeight = 0.0;
            double weightedDistance = 0.0;
            const double segmentDepth = static_cast<double>(totalDepth) / intervals;
            const double width = 3200.0 / intervals;
            for (int index = 0; index < intervals; ++index)
            {
                const double weight = std::exp(-segmentDepth * index) * -std::expm1(-segmentDepth);
                const double fraction = VolumeCloudMeanEventFraction(static_cast<float>(segmentDepth));
                weightedDistance += weight * (1000.0 + (index + fraction) * width);
                totalWeight += weight;
            }
            const double expected = 1000.0 + 3200.0 * VolumeCloudMeanEventFraction(totalDepth);
            CHECK(weightedDistance / totalWeight == doctest::Approx(expected).epsilon(0.000003));
        }
    }
}
