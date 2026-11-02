#include "Keire/ECS/Component.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/WaterBodyComponent.h"
#include "KeireInternal/Rendering/WaterDrawOrder.h"
#include "KeireInternal/Rendering/WaterRenderCapture.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>

TEST_CASE("water patch sorting preserves equal-depth capture order and rejects invalid depths")
{
    using namespace Keire::RenderBackend;
    std::vector<WaterRenderPatch> patches(4);
    const std::array depths{20.0F, 80.0F, 20.0F, 40.0F};
    for (std::size_t index = 0; index < patches.size(); ++index)
    {
        patches[index].SortDepth = depths[index];
        patches[index].VertexCount = static_cast<std::uint32_t>(index);
    }
    SortWaterPatches(patches);
    const std::array expected{1U, 3U, 0U, 2U};
    for (std::size_t index = 0; index < patches.size(); ++index)
    {
        CHECK(patches[index].VertexCount == expected[index]);
    }
    patches[1].SortDepth = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(SortWaterPatches(patches), std::invalid_argument);
    CHECK(patches[0].VertexCount == 1U);
    CHECK(patches[1].VertexCount == 3U);
    patches.clear();
    CHECK_NOTHROW(SortWaterPatches(patches));
}

namespace
{
    [[nodiscard]] Keire::Ref<Keire::Scene> CreateWaterScene()
    {
        const auto registry = Keire::CreateRef<Keire::ComponentRegistry>();
        registry->Register(Keire::CreateTransformComponentRegistration());
        registry->Register(Keire::CreateWaterBodyComponentRegistration());
        return Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                              Keire::SceneDefinition{.Name = "Water fixture"}, registry);
    }

    [[nodiscard]] Keire::Matrix4 WaterTestViewProjection()
    {
        return Keire::Math::Multiply(Keire::Math::Orthographic(200.0F, 1.0F, 0.1F, 1000.0F),
                                     Keire::Math::LookAt({0.0F, 20.0F, 0.0F}, {}, {0.0F, 0.0F, 1.0F}));
    }

    [[nodiscard]] Keire::RenderBackend::WaterRenderCapture Capture(const Keire::Scene& scene,
                                                                   const Keire::Vector3d origin = {})
    {
        Keire::RenderBackend::WaterRenderCapture result;
        Keire::RenderBackend::CaptureWaterBodies(scene, origin, {0.0F, 20.0F, 0.0F},
                                                 Keire::Math::LookAt({0.0F, 20.0F, 0.0F}, {}, {0.0F, 0.0F, 1.0F}),
                                                 WaterTestViewProjection(), 123.0, result);
        return result;
    }
} // namespace

TEST_CASE("water draw capture preserves metre geometry at astronomical camera origins")
{
    using namespace Keire;
    const auto scene = CreateWaterScene();
    auto entity = scene->CreateEntity("Lake");
    const auto body = entity.AddComponent<WaterBodyComponent>();
    auto settings = body->Settings();
    settings.Waves = {{12.0, 0.4, 2.0, 35.0, 10.0, 0.3}};
    settings.SurfaceColor = {0.1F, 0.2F, 0.3F, 0.4F};
    settings.Roughness = 0.2F;
    settings.CrestFoamStrength = 0.6F;
    body->Configure(settings);
    const auto near = Capture(*scene);
    REQUIRE(near.Patches.size() == 1);
    const Vector3d farOrigin{1.0e12, -2.0e12, 3.0e12};
    entity.GetComponent<TransformComponent>()->SetWorldPositionPrecise(farOrigin);
    const auto far = Capture(*scene, farOrigin);
    REQUIRE(far.Patches.size() == 1);
    CHECK(far.Patches[0].Origin == near.Patches[0].Origin);
    CHECK(far.Patches[0].Grid == near.Patches[0].Grid);
    CHECK(far.Patches[0].SurfaceColor == settings.SurfaceColor);
    CHECK(far.Patches[0].Appearance.X == settings.Roughness);
    CHECK(far.Patches[0].Appearance.Y == settings.CrestFoamStrength);
    CHECK(far.Patches[0].Waves.Waves[0].DirectionFrequencyPhase ==
          near.Patches[0].Waves.Waves[0].DirectionFrequencyPhase);
    CHECK(far.Patches[0].Grid.Z == 100.0F);
    CHECK(far.Patches[0].Grid.W == 100.0F);
    scene->Close();
}

TEST_CASE("water capture bounds procedural work and respects visibility and component lifetime")
{
    using namespace Keire;
    const auto scene = CreateWaterScene();
    auto entity = scene->CreateEntity("Ocean patch");
    const auto body = entity.AddComponent<WaterBodyComponent>();
    auto settings = body->Settings();
    settings.Shape = WaterSurfaceShape::InfinitePlane;
    settings.GridSpacingMetres = 0.00001;
    body->Configure(settings);
    const auto captured = Capture(*scene);
    REQUIRE(captured.Patches.size() == 1);
    CHECK(captured.Patches[0].VertexCount == 128U * 128U * 6U);
    body->SetEnabled(false);
    CHECK(Capture(*scene).Patches.empty());
    // Already captured packets are immutable and own no component/GPU references.
    CHECK(captured.Patches.size() == 1);
    body->SetEnabled(true);
    entity.GetComponent<TransformComponent>()->SetLocalPosition({0.0F, -2000.0F, 0.0F});
    CHECK(Capture(*scene).Patches.empty());
    REQUIRE(entity.RemoveComponent<WaterBodyComponent>());
    CHECK(Capture(*scene).Patches.empty());
    scene->Close();
    CHECK(captured.Patches[0].VertexCount == 128U * 128U * 6U);
}

TEST_CASE("water capture explicitly distinguishes ellipse and unsupported material or sphere")
{
    using namespace Keire;
    const auto scene = CreateWaterScene();
    auto entity = scene->CreateEntity("Pond");
    const auto body = entity.AddComponent<WaterBodyComponent>();
    auto settings = body->Settings();
    settings.Shape = WaterSurfaceShape::Ellipse;
    settings.HalfExtentXMetres = 30.0;
    settings.HalfExtentZMetres = 20.0;
    body->Configure(settings);
    const auto ellipse = Capture(*scene);
    REQUIRE(ellipse.Patches.size() == 1);
    CHECK(ellipse.Patches[0].Appearance.W == 1.0F);
    CHECK(ellipse.Patches[0].Footprint.X == doctest::Approx(-1.0));
    CHECK(ellipse.Patches[0].Footprint.Y == doctest::Approx(-1.0));
    settings.Material = AssetId::Generate();
    body->Configure(settings);
    const auto customMaterial = Capture(*scene);
    CHECK(customMaterial.Patches.empty());
    CHECK(customMaterial.UnsupportedMaterialBodies == 1);
    settings.Material = {};
    settings.Shape = WaterSurfaceShape::Sphere;
    body->Configure(settings);
    const auto sphere = Capture(*scene);
    CHECK(sphere.Patches.empty());
    CHECK(sphere.UnsupportedSphereBodies == 1);
    scene->Close();
}

TEST_CASE("water capture conservatively rejects a finite body outside the camera frustum")
{
    const auto scene = CreateWaterScene();
    auto entity = scene->CreateEntity("Outside view");
    entity.AddComponent<Keire::WaterBodyComponent>();
    entity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({300.0F, 0.0F, 0.0F});
    CHECK(Capture(*scene).Patches.empty());
    scene->Close();
}

TEST_CASE("water draws interleave with transparent mesh depth and survive chunk boundaries")
{
    const std::array meshDepths{100.0F, 60.0F, 20.0F};
    const std::array waterDepths{80.0F, 40.0F, 10.0F};
    const auto order = Keire::RenderBackend::BuildWaterDrawOrder(meshDepths, waterDepths);
    REQUIRE(order.size() == 6);
    // A two-entry command-buffer chunk budget must preserve the same global order.
    for (std::size_t chunk = 0; chunk < 3; ++chunk)
    {
        const auto first = chunk * 2;
        CHECK_FALSE(order[first].Water);
        CHECK(order[first].Index == chunk);
        CHECK(order[first + 1].Water);
        CHECK(order[first + 1].Index == chunk);
    }
    const std::array equalDepth{20.0F};
    const auto stable = Keire::RenderBackend::BuildWaterDrawOrder(equalDepth, equalDepth);
    REQUIRE(stable.size() == 2);
    CHECK_FALSE(stable[0].Water);
    CHECK(stable[1].Water);
    CHECK(Keire::RenderBackend::BuildWaterDrawOrder({}, {}).empty());
    const auto waterOnly = Keire::RenderBackend::BuildWaterDrawOrder({}, waterDepths);
    REQUIRE(waterOnly.size() == waterDepths.size());
    for (std::size_t index = 0; index < waterOnly.size(); ++index)
    {
        CHECK(waterOnly[index].Water);
        CHECK(waterOnly[index].Index == index);
    }
    const auto meshesOnly = Keire::RenderBackend::BuildWaterDrawOrder(meshDepths, {});
    REQUIRE(meshesOnly.size() == meshDepths.size());
    CHECK_FALSE(meshesOnly[0].Water);
}
