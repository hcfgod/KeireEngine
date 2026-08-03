#include "Keire/Core.h"

#include <doctest/doctest.h>

TEST_CASE("lighting component registrations expose versioned bake and probe authoring")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto directional = registry->Find(Keire::DirectionalLightComponent::StaticType());
    const auto point = registry->Find(Keire::PointLightComponent::StaticType());
    const auto spot = registry->Find(Keire::SpotLightComponent::StaticType());
    const auto renderer = registry->Find(Keire::MeshRendererComponent::StaticType());
    const auto reflection = registry->Find(Keire::ReflectionProbeComponent::StaticType());
    const auto volume = registry->Find(Keire::LightProbeVolumeComponent::StaticType());
    REQUIRE(directional);
    REQUIRE(point);
    REQUIRE(spot);
    REQUIRE(renderer);
    REQUIRE(reflection);
    REQUIRE(volume);
    CHECK(directional->SchemaVersion == 2);
    CHECK(point->SchemaVersion == 2);
    CHECK(spot->SchemaVersion == 2);
    CHECK(renderer->SchemaVersion == 3);
    CHECK(reflection->SchemaVersion == 1);
    CHECK(volume->SchemaVersion == 1);
}

TEST_CASE("legacy lights migrate without changing realtime behavior")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registration = registry->Find(Keire::DirectionalLightComponent::StaticType());
    REQUIRE(registration);
    const Keire::ComponentPropertyBag legacy{{"intensity", 3.0}, {"shadows", std::int64_t{1}}};
    const auto migrated = registration->Migrate(legacy, 1);
    const auto component = registration->Factory();
    registration->Deserialize(*component, migrated, registration->SchemaVersion);
    const auto light = Keire::DynamicRefCast<Keire::DirectionalLightComponent>(component);
    REQUIRE(light);
    CHECK(light->Intensity() == doctest::Approx(3.0F));
    CHECK(light->Shadows() == Keire::ShadowQuality::Hard);
    CHECK(light->BakeMode() == Keire::LightBakeMode::Realtime);
    CHECK(light->CookieScale() == (Keire::Vector2{1.0F, 1.0F}));
    CHECK_FALSE(light->ContactShadows());
}

TEST_CASE("probe components reject invalid spatial grids and influence settings")
{
    Keire::ReflectionProbeComponent reflection;
    CHECK_THROWS_AS(reflection.SetBlendDistance(6.0F), std::invalid_argument);
    CHECK_THROWS_AS(reflection.SetResolution(static_cast<Keire::ReflectionProbeResolution>(32)), std::invalid_argument);

    Keire::LightProbeVolumeComponent volume;
    CHECK_THROWS_AS(volume.SetSpacing({0.001F, 0.001F, 0.001F}), std::invalid_argument);
    CHECK_THROWS_AS(volume.SetNormalBias(-1.0F), std::invalid_argument);
}
