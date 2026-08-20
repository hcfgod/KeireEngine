#include "Keire/Core.h"

#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

#include <doctest/doctest.h>

#include <limits>
#include <vector>

TEST_CASE("managed rendering services expose validated camera renderer and light state")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Managed rendering"));
    auto entity = scene->CreateEntity("Presentation");
    const auto camera = entity.AddComponent<Keire::CameraComponent>();
    const auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
    const auto directional = entity.AddComponent<Keire::DirectionalLightComponent>();
    const auto point = entity.AddComponent<Keire::PointLightComponent>();
    const auto spot = entity.AddComponent<Keire::SpotLightComponent>();
    REQUIRE(camera);
    REQUIRE(renderer);
    REQUIRE(directional);
    REQUIRE(point);
    REQUIRE(spot);

    const auto id = entity.Id().Value();
    CHECK(Keire::Detail::SetManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                   Keire::ManagedRenderingScalarProperty::VerticalFieldOfView, 82.0F));
    const auto fieldOfView =
        Keire::Detail::ReadManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                  Keire::ManagedRenderingScalarProperty::VerticalFieldOfView);
    REQUIRE(fieldOfView);
    CHECK(*fieldOfView == doctest::Approx(82.0F));
    CHECK_FALSE(Keire::Detail::SetManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                         Keire::ManagedRenderingScalarProperty::NearPlane,
                                                         camera->FarPlane()));
    CHECK(camera->NearPlane() == doctest::Approx(0.1F));
    CHECK(Keire::Detail::SetManagedRenderingInteger(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                    Keire::ManagedRenderingIntegerProperty::Projection,
                                                    static_cast<std::int32_t>(Keire::CameraProjection::Orthographic)));
    CHECK(camera->Projection() == Keire::CameraProjection::Orthographic);
    CHECK(Keire::Detail::SetManagedRenderingColor(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                  Keire::ManagedRenderingColorProperty::ClearColor,
                                                  {0.02F, 0.04F, 0.08F, 1.0F}));

    const auto mesh = Keire::AssetId::Generate();
    const std::vector materials{Keire::AssetId::Generate(), Keire::AssetId::Generate()};
    CHECK(Keire::Detail::SetManagedRenderingAsset(scene, id, Keire::ManagedRenderingComponent::MeshRenderer,
                                                  Keire::ManagedRenderingAssetProperty::Mesh, mesh));
    CHECK(Keire::Detail::SetManagedRendererMaterials(scene, id, materials));
    CHECK(Keire::Detail::SetManagedRenderingFlag(scene, id, Keire::ManagedRenderingComponent::MeshRenderer,
                                                 Keire::ManagedRenderingFlagProperty::CastShadows, false));
    REQUIRE(Keire::Detail::ReadManagedRendererMaterials(scene, id));
    CHECK(*Keire::Detail::ReadManagedRendererMaterials(scene, id) == materials);
    CHECK(renderer->Mesh() == mesh);
    CHECK_FALSE(renderer->CastShadows());

    std::vector<Keire::AssetId> tooManyMaterials(257, Keire::AssetId::Generate());
    CHECK_FALSE(Keire::Detail::SetManagedRendererMaterials(scene, id, tooManyMaterials));
    CHECK(std::vector(renderer->Materials().begin(), renderer->Materials().end()) == materials);

    CHECK(Keire::Detail::SetManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::DirectionalLight,
                                                   Keire::ManagedRenderingScalarProperty::ColorTemperature, 4800.0F));
    CHECK(Keire::Detail::SetManagedRenderingVector(scene, id, Keire::ManagedRenderingComponent::DirectionalLight,
                                                   Keire::ManagedRenderingVectorProperty::CookieScale, {2.0F, 3.0F}));
    CHECK(Keire::Detail::SetManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::PointLight,
                                                   Keire::ManagedRenderingScalarProperty::Range, 24.0F));
    CHECK(Keire::Detail::SetManagedRenderingScalar(scene, id, Keire::ManagedRenderingComponent::SpotLight,
                                                   Keire::ManagedRenderingScalarProperty::OuterAngle, 52.0F));
    CHECK(directional->ColorTemperatureKelvin() == doctest::Approx(4800.0F));
    CHECK(directional->CookieScale() == (Keire::Vector2{2.0F, 3.0F}));
    CHECK(point->Range() == doctest::Approx(24.0F));
    CHECK(spot->OuterAngleDegrees() == doctest::Approx(52.0F));
    CHECK_FALSE(Keire::Detail::SetManagedRenderingVector(scene, id, Keire::ManagedRenderingComponent::PointLight,
                                                         Keire::ManagedRenderingVectorProperty::CookieScale,
                                                         {2.0F, 2.0F}));
}

TEST_CASE("managed material property blocks are bounded transient renderer state")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Material properties"));
    auto entity = scene->CreateEntity("Surface");
    const auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
    REQUIRE(renderer);
    const auto id = entity.Id().Value();
    const auto texture = Keire::AssetId::Generate();

    CHECK(Keire::Detail::SetManagedMaterialProperty(scene, id, "Roughness", 0.42F));
    CHECK(Keire::Detail::SetManagedMaterialProperty(scene, id, "Wind", Keire::Vector3{1.0F, 2.0F, 3.0F}));
    CHECK(Keire::Detail::SetManagedMaterialProperty(scene, id, "BaseColor", Keire::Color{0.1F, 0.2F, 0.3F, 1.0F}));
    CHECK(Keire::Detail::SetManagedMaterialProperty(scene, id, "Albedo", texture));
    REQUIRE(renderer->MaterialProperties().size() == 4);
    CHECK(std::get<float>(renderer->MaterialProperties().at("Roughness")) == doctest::Approx(0.42F));
    CHECK(std::get<Keire::AssetId>(renderer->MaterialProperties().at("Albedo")) == texture);
    CHECK_FALSE(
        Keire::Detail::SetManagedMaterialProperty(scene, id, "Invalid", std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(renderer->MaterialProperties().contains("Invalid"));

    CHECK(Keire::Detail::ResetManagedMaterialProperty(scene, id, "Wind"));
    CHECK_FALSE(Keire::Detail::ResetManagedMaterialProperty(scene, id, "Wind"));
    CHECK(Keire::Detail::ClearManagedMaterialProperties(scene, id));
    CHECK(renderer->MaterialProperties().empty());

    renderer->SetMaterialProperty("RuntimeOnly", 1.0F);
    const auto registration = scene->Components()->Find(Keire::MeshRendererComponent::StaticType());
    REQUIRE(registration);
    CHECK_FALSE(registration->Serialize(*renderer).contains("RuntimeOnly"));
}
