#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/WaterBodyComponent.h"
#include "Keire/Scenes/Scene.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <string>

TEST_CASE("default scenes expose water bodies with required transforms")
{
    const auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneDefinition{.Name = "Water fixture"});
    auto entity = scene->CreateEntity("Water");
    const auto water = entity.AddComponent<Keire::WaterBodyComponent>();
    REQUIRE(water);
    CHECK(entity.GetComponent<Keire::TransformComponent>());
    CHECK(water->Settings() == Keire::WaterBodySettings{});
    scene->Close();
}

TEST_CASE("water body defaults and bounded wave slots round trip precise authored settings")
{
    using namespace Keire;
    const auto registration = CreateWaterBodyComponentRegistration();
    const auto source = CreateRef<WaterBodyComponent>();
    registration.Deserialize(*source, {}, 1);
    CHECK(source->Settings() == WaterBodySettings{});
    CHECK(registration.RequiredComponents == std::vector{TransformComponent::StaticType()});
    auto settings = source->Settings();
    settings.Material = AssetId::Generate();
    settings.SurfaceColor = {0.1F, 0.3F, 0.5F, 0.7F};
    settings.Roughness = 0.35F;
    settings.CrestFoamStrength = 0.6F;
    settings.GridSpacingMetres = 1.125;
    settings.RenderDistanceMetres = 123456789.125;
    settings.Shape = WaterSurfaceShape::Ellipse;
    settings.HalfExtentXMetres = 1.0e12 + 0.125;
    settings.HalfExtentZMetres = 123.25;
    settings.RadiusMetres = 6200000.25;
    settings.DepthMetres = 12.5;
    settings.DensityKilogramsPerCubicMetre = 1025.0;
    settings.FlowVelocityLocal = {1.25, 0.0, -2.75};
    for (std::size_t index = 0; index < WaterWaveField::MaximumWaves; ++index)
    {
        const double offset = static_cast<double>(index);
        settings.Waves.push_back(
            {10.0 + offset, 0.01 + offset * 0.001, 2.0 + offset, offset * 20.0, offset * 10.0, 0.3});
    }
    source->Configure(settings);
    const auto restored = registration.Factory();
    registration.Deserialize(*restored, registration.Serialize(*source), 1);
    CHECK(dynamic_cast<const WaterBodyComponent&>(*restored).Settings() == settings);
    CHECK(registration.Serialize(*restored).size() == registration.Properties.size());
    source->Reset();
    CHECK(source->Settings() == WaterBodySettings{});
}

TEST_CASE("water body invalid replacements preserve every authored field")
{
    using namespace Keire;
    const auto component = CreateRef<WaterBodyComponent>();
    auto baseline = component->Settings();
    baseline.Material = AssetId::Generate();
    baseline.Waves.push_back({});
    component->Configure(baseline);
    for (const double invalid :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        auto candidate = baseline;
        candidate.Material = {};
        candidate.DepthMetres = invalid;
        CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
        CHECK(component->Settings() == baseline);
    }
    auto candidate = baseline;
    candidate.Waves.back().WavelengthMetres = 0.0;
    CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
    candidate = baseline;
    candidate.Waves.resize(WaterWaveField::MaximumWaves + 1);
    CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
    candidate = baseline;
    candidate.Shape = WaterSurfaceShape::Sphere;
    CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
    candidate = baseline;
    candidate.FlowVelocityLocal.X = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
    CHECK(component->Settings() == baseline);
}

TEST_CASE("water body malformed serialized edits are atomic")
{
    using namespace Keire;
    const auto registration = CreateWaterBodyComponentRegistration();
    const auto component = CreateRef<WaterBodyComponent>();
    const auto baseline = component->Settings();
    for (const std::int64_t count : {-1, 17})
    {
        auto values = registration.Serialize(*component);
        values["material"] = AssetId::Generate();
        values["waveCount"] = count;
        CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
        CHECK(component->Settings() == baseline);
    }
    auto values = registration.Serialize(*component);
    values["waveCount"] = std::int64_t{16};
    values["wave15.steepness"] = std::string("invalid");
    CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
    values["wave15.steepness"] = -1.0;
    CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
    values["shape"] = std::numeric_limits<std::int64_t>::max();
    CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
    CHECK_THROWS_AS(registration.Deserialize(*component, {}, 2), std::invalid_argument);
    CHECK(component->Settings() == baseline);
}

TEST_CASE("water body adapters preserve resolved double frames and still sphere support")
{
    using namespace Keire;
    const auto component = CreateRef<WaterBodyComponent>();
    const Vector3d origin{1.0e12, -2.0e12, 3.0e12};
    const auto definition = component->CreateSurfaceDefinition(origin, {0.0, 0.0, 0.0, 2.0}, {2.0, 0.0, 0.0});
    CHECK(definition.Origin == origin);
    CHECK(definition.Rotation[3] == 1.0);
    const auto sample = WaterSurface(definition).Sample({origin.X + 0.125, origin.Y - 0.25, origin.Z});
    REQUIRE(sample);
    CHECK(sample->SignedDistanceMetres == -0.25);
    CHECK(sample->Velocity.X == 2.0);
    CHECK(component->CreateWaveField(origin).SampleParametric(origin, 0.0).Displacement == Vector3d{});
    auto settings = component->Settings();
    settings.Shape = WaterSurfaceShape::Sphere;
    component->Configure(settings);
    CHECK(component->CreateSurfaceDefinition(origin).Shape == WaterSurfaceShape::Sphere);
    CHECK_THROWS_AS((void)component->CreateSurfaceDefinition(origin, {0.0, 0.0, 0.0, 0.0}), std::invalid_argument);
}

TEST_CASE("water body scene ownership dirtiness dependencies and close are explicit")
{
    using namespace Keire;
    const auto registry = CreateRef<ComponentRegistry>();
    registry->Register(CreateTransformComponentRegistration());
    registry->Register(CreateWaterBodyComponentRegistration());
    const auto scene = CreateRef<Scene>(AssetId::Generate(), SceneDefinition{.Name = "Water fixture"}, registry);
    auto entity = scene->CreateEntity("Lake");
    const auto body = entity.AddComponent<WaterBodyComponent>();
    REQUIRE(body);
    REQUIRE(entity.GetComponent<TransformComponent>());
    scene->MarkSaved();
    body->Configure(body->Settings());
    CHECK_FALSE(scene->Dirty());
    auto settings = body->Settings();
    settings.DepthMetres = 4.0;
    body->Configure(settings);
    CHECK(scene->Dirty());
    scene->MarkSaved();
    settings.DepthMetres = -1.0;
    CHECK_THROWS_AS(body->Configure(settings), std::invalid_argument);
    CHECK_FALSE(scene->Dirty());
    body->SetEnabled(false);
    CHECK_FALSE(body->Enabled());
    CHECK(body->Settings().DepthMetres == 4.0);
    scene->Close();
    CHECK_FALSE(body->Owner());
    CHECK(body->Settings().DepthMetres == 4.0);
}

TEST_CASE("water body appearance and quality replacements validate before changing state")
{
    using namespace Keire;
    const auto component = CreateRef<WaterBodyComponent>();
    const auto baseline = component->Settings();
    for (const float invalid :
         {-0.1F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        for (const auto member : {&WaterBodySettings::Roughness, &WaterBodySettings::CrestFoamStrength})
        {
            auto candidate = baseline;
            candidate.*member = invalid;
            CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
        }
        for (const auto channel : {&Color::Red, &Color::Green, &Color::Blue, &Color::Alpha})
        {
            auto candidate = baseline;
            candidate.SurfaceColor.*channel = invalid;
            CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
        }
        CHECK(component->Settings() == baseline);
    }
    for (const double invalid :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        for (const auto member : {&WaterBodySettings::GridSpacingMetres, &WaterBodySettings::RenderDistanceMetres})
        {
            auto candidate = baseline;
            candidate.*member = invalid;
            CHECK_THROWS_AS(component->Configure(candidate), std::invalid_argument);
            CHECK(component->Settings() == baseline);
        }
    }
    auto boundary = baseline;
    boundary.SurfaceColor = {0.0F, 1.0F, 0.0F, 1.0F};
    boundary.Roughness = 0.0F;
    boundary.CrestFoamStrength = 1.0F;
    CHECK_NOTHROW(component->Configure(boundary));
    const auto registration = CreateWaterBodyComponentRegistration();
    auto values = registration.Serialize(*component);
    values["roughness"] = std::numeric_limits<double>::max();
    CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
    CHECK(component->Settings() == boundary);
    values["roughness"] = 0.0;
    values["surfaceColor"] = std::string("invalid");
    CHECK_THROWS_AS(registration.Deserialize(*component, values, 1), std::invalid_argument);
    CHECK(component->Settings() == boundary);
}
