#include "Keire/Rendering/Cloud.h"
#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Environment/CloudRecord.h"
#include "KeireInternal/Rendering/CloudUniforms.h"
#include "KeireInternal/Rendering/CloudVolumePacket.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

TEST_CASE("Cloud capture preserves weather coordinates across render origin changes")
{
    Keire::VolumetricCloudSettings settings;
    settings.Enabled = true;
    Keire::RenderCamera absoluteCamera;
    absoluteCamera.View =
        Keire::Math::Inverse(Keire::Math::ComposeTransform({1024.25F, 128.5F, -512.0F}, {}, {1.0F, 1.0F, 1.0F}));
    const auto expected = Keire::RenderBackend::CaptureCloudVolume(absoluteCamera, settings, {});
    Keire::RenderCamera rebasedCamera;
    rebasedCamera.View =
        Keire::Math::Inverse(Keire::Math::ComposeTransform({0.25F, 0.5F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}));
    for (const double distance : {0.0, 1e15})
    {
        const Keire::Vector3d anchor{distance, distance, distance};
        const Keire::Vector3d origin{distance + 1024.0, distance + 128.0, distance - 512.0};
        const auto actual =
            Keire::RenderBackend::CaptureCloudVolume(rebasedCamera, settings, {}, false, origin, anchor);
        CHECK(actual.Geometry == expected.Geometry);
        CHECK(actual.PatternOrigin == expected.PatternOrigin);
        CHECK(actual.Quality == expected.Quality);
        const auto atBase = Keire::RenderBackend::CaptureCloudVolume(rebasedCamera, settings, {}, true, origin, anchor);
        CHECK(atBase.Geometry[0] == 0.0F);
    }
    CHECK_THROWS_AS(Keire::RenderBackend::CaptureCloudVolume(rebasedCamera, settings, {}, false,
                                                             {std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                    std::invalid_argument);
}

TEST_CASE("Cloud sheets default disabled and pack a bounded explicit GPU contract")
{
    const Keire::CloudSettings settings;
    const auto packet = Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, 100.0, 0.0}, 0.0);
    CHECK(packet.Layers[0].Geometry[0] == 1900.0F);
    CHECK(packet.Layers[0].Geometry[1] == doctest::Approx(1.0 / 12000.0));
    CHECK(packet.Layers[0].Geometry[3] == 0.0F);
    CHECK(packet.Layers[1].Geometry[3] == 0.0F);
    CHECK(sizeof(packet) == 128);
    CHECK(alignof(Keire::RenderBackend::CloudUniforms) == 16);
    CHECK(sizeof(Keire::RenderBackend::CloudLayerUniforms) == 64);
    CHECK(offsetof(Keire::RenderBackend::CloudLayerUniforms, Pattern) == 16);
    CHECK(offsetof(Keire::RenderBackend::CloudLayerUniforms, Color) == 32);
    CHECK(offsetof(Keire::RenderBackend::CloudLayerUniforms, Noise) == 48);
    CHECK(packet.Layers[0].Noise == std::array<std::uint32_t, 4>{1U, 1U, 0U, 0U});
    CHECK(packet.Layers[1].Noise == std::array<std::uint32_t, 4>{1U, 1U, 0U, 0U});
}

TEST_CASE("Cloud advection uses signed explicit time and stable observer-relative phase")
{
    Keire::CloudLayer layer;
    layer.PatternSizeMeters = 1000.0;
    layer.WindMetersPerSecond = {10.0, -20.0};
    const auto phase = Keire::Detail::EvaluateCloudPhase(layer, {100.0, -100.0}, 10.0);
    CHECK(phase[0] == doctest::Approx(0.0));
    CHECK(phase[1] == doctest::Approx(0.1));
    const auto reverse = Keire::Detail::EvaluateCloudPhase(layer, {100.0, -100.0}, -10.0);
    CHECK(reverse[0] == doctest::Approx(0.2));
    CHECK(reverse[1] == doctest::Approx(0.7));
    // Moving the observer by an exact pattern repeat retains the same density, including negative coordinates.
    const auto repeated = Keire::Detail::EvaluateCloudPhase(layer, {1000100.0, -1000100.0}, 10.0);
    CHECK(repeated[0] == doctest::Approx(phase[0]));
    CHECK(repeated[1] == doctest::Approx(phase[1]));
    layer.WindMetersPerSecond = {1000.0, -1000.0};
    const auto distantTime = Keire::Detail::EvaluateCloudPhase(layer, {0.25, -0.25}, 1.0e9);
    CHECK(distantTime[0] == doctest::Approx(0.00025));
    CHECK(distantTime[1] == doctest::Approx(0.99975));
}

TEST_CASE("Cloud packet keeps independent layers and subtracts observer height before narrowing")
{
    Keire::CloudSettings settings;
    settings.Layers[0].Enabled = true;
    settings.Layers[1].Enabled = true;
    settings.Layers[1].AltitudeMeters = 5000.0;
    settings.Layers[1].Opacity = 0.25;
    settings.Layers[1].WindMetersPerSecond = {-10.0, 2.0};
    settings.Layers[0].Appearance = Keire::CloudAppearance::LegacyUnlit;
    settings.Layers[0].Seed = 0;
    settings.Layers[1].Seed = std::numeric_limits<std::uint32_t>::max();
    const auto above = Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, 3000.0, 0.0}, 1.0);
    CHECK(above.Layers[0].Geometry[0] == -1000.0F);
    CHECK(above.Layers[1].Geometry[0] == 2000.0F);
    CHECK(above.Layers[0].Geometry[3] == doctest::Approx(0.8));
    CHECK(above.Layers[1].Geometry[3] == 0.25F);
    CHECK(above.Layers[0].Pattern[0] != above.Layers[1].Pattern[0]);
    CHECK(above.Layers[0].Noise == std::array<std::uint32_t, 4>{0U, 0U, 0U, 0U});
    CHECK(above.Layers[1].Noise == std::array<std::uint32_t, 4>{1U, std::numeric_limits<std::uint32_t>::max(), 0U, 0U});
}

TEST_CASE("Cloud evaluation rejects invalid disabled layers and unsupported coordinate domains")
{
    Keire::CloudSettings settings;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double invalid : {nan, infinity, -1.0, 1.01})
    {
        settings.Layers[1].Coverage = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateCloudSettings(settings), std::invalid_argument);
    }
    settings = {};
    settings.Layers[0].PatternSizeMeters = 0.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateCloudSettings(settings), std::invalid_argument);
    settings = {};
    settings.Layers[0].HorizonFade = 0.001;
    CHECK_THROWS_AS(Keire::Detail::ValidateCloudSettings(settings), std::invalid_argument);
    settings = {};
    for (const double invalid : {nan, infinity, 1.0e9 + 1.0, -1.0e9 - 1.0})
    {
        CHECK_THROWS_AS((void)Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, 0.0, 0.0}, invalid),
                        std::invalid_argument);
        CHECK_THROWS_AS((void)Keire::RenderBackend::BuildCloudUniforms(settings, {invalid * 1000.0, 0.0, 0.0}, 0.0),
                        std::invalid_argument);
        CHECK_THROWS_AS((void)Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, invalid, 0.0}, 0.0),
                        std::invalid_argument);
    }
}

TEST_CASE("Cloud records round trip authored values and strictly reject malformed versions and fields")
{
    Keire::CloudSettings settings;
    settings.Layers[1].Enabled = true;
    settings.Layers[1].WindMetersPerSecond = {-23.25, 17.5};
    const auto record = Keire::Detail::EncodeCloudSettings(settings);
    CHECK(Keire::Detail::DecodeCloudSettings(nlohmann::json::parse(record.dump())) == settings);
    auto invalid = record;
    invalid["version"] = 3;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    invalid = record;
    invalid["version"] = 1.0;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    invalid = record;
    invalid["layers"].push_back(record["layers"][0]);
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    invalid = record;
    invalid["layers"][0]["enabled"] = 1;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    for (const auto* field :
         {"altitudeMeters", "patternSizeMeters", "coverage", "opacity", "edgeSoftness", "horizonFade"})
    {
        invalid = record;
        invalid["layers"][0][field] = "invalid";
        CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
        invalid = record;
        invalid["layers"][0].erase(field);
        CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    }
    invalid = record;
    invalid["layers"][0]["linearColor"] = {1.0, 1.0};
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    invalid = record;
    invalid["layers"][0]["windMetersPerSecond"][0] = false;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    invalid = record;
    invalid["extra"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
}

TEST_CASE("Cloud version two rejects ambiguous appearance and seed values")
{
    const auto record = Keire::Detail::EncodeCloudSettings({});
    for (const auto& appearance : {nlohmann::json("unknown"), nlohmann::json(1), nlohmann::json(true)})
    {
        auto invalid = record;
        invalid["layers"][0]["appearance"] = appearance;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    }
    for (const auto& seed : {nlohmann::json(-1), nlohmann::json(4294967296ULL), nlohmann::json(1.5),
                             nlohmann::json("1"), nlohmann::json(true)})
    {
        auto invalid = record;
        invalid["layers"][1]["seed"] = seed;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(invalid), std::invalid_argument);
    }
    for (const auto* field : {"appearance", "seed"})
    {
        auto missing = record;
        missing["layers"][0].erase(field);
        CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(missing), std::invalid_argument);
    }
    for (const auto seed : {0U, std::numeric_limits<std::uint32_t>::max()})
    {
        auto valid = record;
        valid["layers"][1]["seed"] = seed;
        CHECK(Keire::Detail::DecodeCloudSettings(valid).Layers[1].Seed == seed);
    }
}

TEST_CASE("Cloud artistic pattern is bounded and repeats across both seams")
{
    for (int index = -100; index <= 100; ++index)
    {
        const double x = index / 37.0;
        const double z = index / 53.0;
        const auto sample = Keire::Detail::SampleCloudPattern(x, z);
        CHECK(sample >= 0.0);
        CHECK(sample <= 1.0);
        CHECK(Keire::Detail::SampleCloudPattern(x + 1.0, z) == doctest::Approx(sample));
        CHECK(Keire::Detail::SampleCloudPattern(x, z - 1.0) == doctest::Approx(sample));
    }
}

TEST_CASE("Cloud owner-provided wrapped displacement matches bounded elapsed evaluation")
{
    CHECK(Keire::RenderBackend::CloudObserverWithinLocalDomain({1.0e9, -1.0e9, 0.0}));
    CHECK_FALSE(Keire::RenderBackend::CloudObserverWithinLocalDomain({1.0e12 + 1.0, 0.0, 0.0}));
    CHECK_FALSE(
        Keire::RenderBackend::CloudObserverWithinLocalDomain({0.0, std::numeric_limits<double>::quiet_NaN(), 0.0}));
    Keire::CloudSettings settings;
    settings.Layers[0].WindMetersPerSecond = {10.0, -20.0};
    settings.Layers[0].PatternSizeMeters = 1000.0;
    const auto timed = Keire::RenderBackend::BuildCloudUniforms(settings, {100.0, 100.0, -100.0}, 10.0);
    std::array<std::array<double, 2>, Keire::MaximumCloudLayers> displacement{{{0.1, 0.8}, {0.0, 0.0}}};
    const auto ownerSample = Keire::RenderBackend::BuildCloudUniforms(settings, {100.0, 100.0, -100.0}, displacement);
    CHECK(ownerSample.Layers[0].Pattern[0] == doctest::Approx(timed.Layers[0].Pattern[0]));
    CHECK(ownerSample.Layers[0].Pattern[1] == doctest::Approx(timed.Layers[0].Pattern[1]));
    for (const double invalid : {-0.01, 1.0, std::numeric_limits<double>::quiet_NaN()})
    {
        displacement[1][1] = invalid;
        CHECK_THROWS_AS((void)Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, 0.0, 0.0}, displacement),
                        std::invalid_argument);
    }
}

TEST_CASE("Layered clouds preserve wrapped phases at distant horizontal coordinates")
{
    Keire::CloudSettings settings;
    settings.Layers[0].PatternSizeMeters = 1000.0;
    settings.Layers[0].WindMetersPerSecond = {};
    for (const double distance : {1.0e10, -1.0e10, 1.0e12 - 1.0, -1.0e12 + 1.0})
    {
        CAPTURE(distance);
        const double x = distance + 0.125;
        const double z = distance - 0.25;
        REQUIRE(Keire::RenderBackend::CloudObserverWithinLocalDomain({x, 100.0, z}));
        const auto distant = Keire::RenderBackend::BuildCloudUniforms(settings, {x, 100.0, z}, 0.0);
        const auto local = Keire::RenderBackend::BuildCloudUniforms(
            settings, {std::fmod(x, 1000.0), 100.0, std::fmod(z, 1000.0)}, 0.0);
        CHECK(distant.Layers[0].Pattern == local.Layers[0].Pattern);
        CHECK(distant.Layers[0].Geometry == local.Layers[0].Geometry);
    }
    CHECK_FALSE(Keire::RenderBackend::CloudObserverWithinLocalDomain({0.0, 1.0e9 + 1.0, 0.0}));
}
