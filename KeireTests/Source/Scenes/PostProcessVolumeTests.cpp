#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/PostProcessVolumeComponent.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <string>

TEST_CASE("camera volume participation round trips all layer bits and preserves legacy defaults")
{
    using namespace Keire;
    const auto registration = CreateCameraComponentRegistration();
    const auto camera = CreateRef<CameraComponent>();
    registration.Deserialize(*camera, {}, 1);
    CHECK(camera->PostProcessingEnabled());
    CHECK(camera->PostProcessVolumeMask() == ~0U);
    camera->SetPostProcessingEnabled(false);
    camera->SetPostProcessVolumeMask(0x80000001U);
    const auto restored = CreateRef<CameraComponent>();
    registration.Deserialize(*restored, registration.Serialize(*camera), 1);
    CHECK_FALSE(restored->PostProcessingEnabled());
    CHECK(restored->PostProcessVolumeMask() == 0x80000001U);
    auto record = registration.Serialize(*restored);
    record["postProcessVolumeMask"] = std::int64_t{-1};
    CHECK_THROWS_AS(registration.Deserialize(*restored, record, 1), std::invalid_argument);
    CHECK(restored->PostProcessVolumeMask() == 0x80000001U);
    restored->Reset();
    CHECK(restored->PostProcessingEnabled());
    CHECK(restored->PostProcessVolumeMask() == ~0U);
}

TEST_CASE("post process volumes round trip precise bounds and profile references")
{
    using namespace Keire;
    const auto registration = CreatePostProcessVolumeComponentRegistration();
    const auto source = CreateRef<PostProcessVolumeComponent>();
    PostProcessVolumeSettings settings;
    settings.Profile = AssetId::Generate();
    settings.Global = false;
    settings.Shape = PostProcessVolumeShape::Sphere;
    settings.HalfExtents = {1.0e12 + 0.125, 8.5, 20.0};
    settings.Radius = 1234.125;
    settings.BlendDistance = 10.25;
    settings.Weight = 0.375;
    settings.Priority = -12;
    source->Configure(settings);
    const auto restored = CreateRef<PostProcessVolumeComponent>();
    registration.Deserialize(*restored, registration.Serialize(*source), 1);
    CHECK(restored->Settings() == settings);
    restored->Reset();
    CHECK(restored->Settings() == PostProcessVolumeSettings{});
}

TEST_CASE("invalid post process volume edits preserve authored state")
{
    using namespace Keire;
    const auto volume = CreateRef<PostProcessVolumeComponent>();
    auto baseline = volume->Settings();
    baseline.Profile = AssetId::Generate();
    baseline.Weight = 0.5;
    volume->Configure(baseline);
    for (const double invalid :
         {-1.0, 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        auto candidate = baseline;
        candidate.Weight = invalid;
        CHECK_THROWS_AS(volume->Configure(candidate), std::invalid_argument);
        CHECK(volume->Settings() == baseline);
    }
    auto candidate = baseline;
    candidate.HalfExtents.Y = 0.0;
    CHECK_THROWS_AS(volume->Configure(candidate), std::invalid_argument);
    candidate = baseline;
    candidate.Radius = -1.0;
    CHECK_THROWS_AS(volume->Configure(candidate), std::invalid_argument);
    candidate = baseline;
    candidate.BlendDistance = -1.0;
    CHECK_THROWS_AS(volume->Configure(candidate), std::invalid_argument);
    CHECK(volume->Settings() == baseline);
}

TEST_CASE("post process volume deserialization validates the entire replacement")
{
    using namespace Keire;
    const auto registration = CreatePostProcessVolumeComponentRegistration();
    const auto volume = CreateRef<PostProcessVolumeComponent>();
    const auto baseline = volume->Settings();
    auto record = registration.Serialize(*volume);
    record["global"] = false;
    record["profile"] = AssetId::Generate();
    record["priority"] = std::numeric_limits<std::int64_t>::max();
    CHECK_THROWS_AS(registration.Deserialize(*volume, record, 1), std::invalid_argument);
    CHECK(volume->Settings() == baseline);
    record["priority"] = std::int64_t{0};
    record["weight"] = std::string("invalid");
    CHECK_THROWS_AS(registration.Deserialize(*volume, record, 1), std::invalid_argument);
    CHECK(volume->Settings() == baseline);
    CHECK_THROWS_AS(registration.Deserialize(*volume, {}, 2), std::invalid_argument);
    CHECK(volume->Settings() == baseline);
}
