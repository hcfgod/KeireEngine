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
    CHECK(renderer->SchemaVersion == 4);
    CHECK(reflection->SchemaVersion == 1);
    CHECK(volume->SchemaVersion == 1);
}

TEST_CASE("mesh renderer always-visible authoring is versioned and defaults to bounded culling")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registration = registry->Find(Keire::MeshRendererComponent::StaticType());
    REQUIRE(registration);

    const auto legacyValues = registration->Migrate({}, 3);
    CHECK(std::get<bool>(legacyValues.at("alwaysVisible")) == false);

    const auto source = Keire::CreateRef<Keire::MeshRendererComponent>();
    source->SetAlwaysVisible(true);
    const auto encoded = registration->Serialize(*source);
    CHECK(std::get<bool>(encoded.at("alwaysVisible")));

    const auto restored = registration->Factory();
    registration->Deserialize(*restored, encoded, registration->SchemaVersion);
    const auto renderer = Keire::DynamicRefCast<Keire::MeshRendererComponent>(restored);
    REQUIRE(renderer);
    CHECK(renderer->AlwaysVisible());
    renderer->Reset();
    CHECK_FALSE(renderer->AlwaysVisible());
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

TEST_CASE("light probe volume registration deserializes grid fields atomically")
{
    const auto registration = Keire::CreateLightProbeVolumeComponentRegistration();
    const Keire::ComponentPropertyBag valid{{"boxExtents", Keire::Vector3{92.0F, 13.0F, 71.0F}},
                                            {"spacing", Keire::Vector3{4.0F, 2.0F, 4.0F}}};
    const auto restored = registration.Factory();
    CHECK_NOTHROW(registration.Deserialize(*restored, valid, registration.SchemaVersion));
    const auto volume = Keire::DynamicRefCast<Keire::LightProbeVolumeComponent>(restored);
    REQUIRE(volume);
    CHECK(volume->BoxExtents() == Keire::Vector3{92.0F, 13.0F, 71.0F});
    CHECK(volume->Spacing() == Keire::Vector3{4.0F, 2.0F, 4.0F});

    auto invalid = valid;
    invalid.insert_or_assign("spacing", Keire::Vector3{0.001F, 0.001F, 0.001F});
    const auto rejected = registration.Factory();
    CHECK_THROWS_AS(registration.Deserialize(*rejected, invalid, registration.SchemaVersion), std::invalid_argument);
    const auto unchanged = Keire::DynamicRefCast<Keire::LightProbeVolumeComponent>(rejected);
    REQUIRE(unchanged);
    CHECK(unchanged->BoxExtents() == Keire::Vector3{5.0F, 3.0F, 5.0F});
    CHECK(unchanged->Spacing() == Keire::Vector3{1.0F, 1.0F, 1.0F});
}
