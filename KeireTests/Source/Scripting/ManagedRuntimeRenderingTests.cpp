#include "Keire/Core.h"

#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

#include <doctest/doctest.h>

#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    class NativeSceneRuntimeServices final : public Keire::Detail::ManagedRuntimeSceneServices
    {
      public:
        std::map<std::uint64_t, Keire::Ref<Keire::Scene>> Worlds;
        void WriteManagedLog(Keire::ManagedLogLevel, std::string_view) noexcept override {}
        float ManagedDeltaTime() const noexcept override { return 0.0F; }
        Keire::Vector2 ReadManagedInput(std::string_view) noexcept override { return {}; }

      protected:
        Keire::Ref<Keire::Scene> ManagedRuntimeScene(Keire::AssetId) const noexcept override { return {}; }
        Keire::Ref<Keire::AssetSystem> ManagedRuntimeAssets() const noexcept override { return {}; }
        Keire::Ref<Keire::Scene> ManagedRuntimeSceneForWorld(std::uint64_t world,
                                                             Keire::AssetId) const noexcept override
        {
            const auto found = Worlds.find(world);
            return found == Worlds.end() ? Keire::Ref<Keire::Scene>{} : found->second;
        }
    };
} // namespace

TEST_CASE("managed entity resolution reaches native-only worlds beside a persistent session")
{
    auto arena =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Native arena"));
    auto player = arena->CreateEntity("Player");
    REQUIRE(player.AddComponent<Keire::CharacterControllerComponent>());
    auto persistent = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), arena->Snapshot());
    auto duplicate = persistent->FindEntity(player.Id());
    REQUIRE(duplicate);
    REQUIRE(duplicate.World() != player.World());
    NativeSceneRuntimeServices services;
    services.Worlds.emplace(player.World(), arena);
    services.Worlds.emplace(duplicate.World(), persistent);
    const auto id = player.Id().Value();
    auto resolved = Keire::Detail::ResolveManagedServiceEntity(&services, player.World(), id);
    REQUIRE(resolved);
    REQUIRE(resolved.GetComponent<Keire::CharacterControllerComponent>());
    resolved.GetComponent<Keire::CharacterControllerComponent>()->SetLayer(2);
    CHECK(player.GetComponent<Keire::CharacterControllerComponent>()->Layer() == 2);
    CHECK(Keire::Detail::ResolveManagedServiceEntity(&services, duplicate.World(), id).World() == duplicate.World());
    CHECK_FALSE(Keire::Detail::ResolveManagedServiceEntity(&services, 0, id));
    CHECK_FALSE(Keire::Detail::ResolveManagedServiceEntity(nullptr, player.World(), id));
    CHECK_FALSE(Keire::Detail::ResolveManagedServiceEntity(&services, player.World(), Keire::AssetId::Generate()));
    const auto world = player.World();
    REQUIRE(arena->DestroyEntity(player.Id()));
    CHECK_FALSE(Keire::Detail::ResolveManagedServiceEntity(&services, world, id));
    CHECK(Keire::Detail::ResolveManagedServiceEntity(&services, duplicate.World(), id));
    persistent->Close();
    CHECK_FALSE(Keire::Detail::ResolveManagedServiceEntity(&services, duplicate.World(), id));
    arena->Close();
}

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
    CHECK(Keire::Detail::SetManagedRenderingFlag(scene, id, Keire::ManagedRenderingComponent::MeshRenderer,
                                                 Keire::ManagedRenderingFlagProperty::AlwaysVisible, true));
    REQUIRE(Keire::Detail::ReadManagedRendererMaterials(scene, id));
    CHECK(*Keire::Detail::ReadManagedRendererMaterials(scene, id) == materials);
    CHECK(renderer->Mesh() == mesh);
    CHECK_FALSE(renderer->CastShadows());
    CHECK(renderer->AlwaysVisible());
    CHECK(*Keire::Detail::ReadManagedRenderingFlag(scene, id, Keire::ManagedRenderingComponent::MeshRenderer,
                                                   Keire::ManagedRenderingFlagProperty::AlwaysVisible));

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
    CHECK(Keire::Detail::SetManagedMaterialInstanceProperty(scene, id, 1, "Roughness", 0.18F));
    CHECK(Keire::Detail::SetManagedMaterialInstanceProperty(scene, id, 1, "Albedo", texture));
    REQUIRE(renderer->MaterialProperties().size() == 4);
    CHECK(std::get<float>(renderer->MaterialProperties().at("Roughness")) == doctest::Approx(0.42F));
    CHECK(std::get<Keire::AssetId>(renderer->MaterialProperties().at("Albedo")) == texture);
    REQUIRE(renderer->MaterialInstanceProperties(1).size() == 2);
    CHECK(std::get<float>(renderer->MaterialInstanceProperties(1).at("Roughness")) == doctest::Approx(0.18F));
    CHECK_FALSE(
        Keire::Detail::SetManagedMaterialProperty(scene, id, "Invalid", std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(renderer->MaterialProperties().contains("Invalid"));

    CHECK(Keire::Detail::ResetManagedMaterialProperty(scene, id, "Wind"));
    CHECK_FALSE(Keire::Detail::ResetManagedMaterialProperty(scene, id, "Wind"));
    CHECK(Keire::Detail::ClearManagedMaterialProperties(scene, id));
    CHECK(renderer->MaterialProperties().empty());
    CHECK(Keire::Detail::ResetManagedMaterialInstanceProperty(scene, id, 1, "Roughness"));
    CHECK(Keire::Detail::ClearManagedMaterialInstanceProperties(scene, id, 1));
    CHECK(renderer->MaterialInstanceProperties(1).empty());

    renderer->SetMaterialProperty("RuntimeOnly", 1.0F);
    const auto registration = scene->Components()->Find(Keire::MeshRendererComponent::StaticType());
    REQUIRE(registration);
    CHECK_FALSE(registration->Serialize(*renderer).contains("RuntimeOnly"));
}

TEST_CASE("managed material parameter collections preserve compatible overrides across hot reload")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.Decoders.push_back(Keire::CreateMaterialParameterCollectionAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    const auto collection = Keire::AssetId::Parse("32b82dcb-c795-42bf-b33f-9eb5d4dc5e38");
    const auto wind = Keire::AssetId::Parse("51625c39-209d-47aa-a6d4-f42b1d3a1e14");
    const auto snow = Keire::AssetId::Parse("955cb44f-8fbc-43bb-a080-fac27311270d");

    Keire::MaterialParameterCollectionDefinition definition;
    definition.Parameters.push_back(
        {.Id = wind, .Name = "WindStrength", .DisplayName = "Wind Strength", .DefaultValue = 0.25F});
    REQUIRE(assets->PublishDevelopmentAsset(collection,
                                            Keire::CreateRef<Keire::MaterialParameterCollectionAsset>(definition)));

    Keire::Detail::ManagedMaterialParameterStore parameters;
    REQUIRE(parameters.Ready(assets, collection));
    REQUIRE(parameters.Set(assets, collection, "WindStrength", 0.75F));
    CHECK(std::get<float>(parameters.Snapshot().at("WindStrength")) == doctest::Approx(0.75F));
    CHECK_FALSE(parameters.Set(assets, collection, "Missing", 1.0F));
    CHECK_THROWS_AS((void)parameters.Set(assets, collection, "WindStrength", Keire::Color{}), std::invalid_argument);

    definition.Parameters.front().DefaultValue = 0.5F;
    definition.Parameters.push_back(
        {.Id = snow, .Name = "SnowAmount", .DisplayName = "Snow Amount", .DefaultValue = 0.1F});
    REQUIRE(assets->PublishDevelopmentAsset(collection,
                                            Keire::CreateRef<Keire::MaterialParameterCollectionAsset>(definition)));
    const auto reloaded = parameters.Snapshot();
    CHECK(std::get<float>(reloaded.at("WindStrength")) == doctest::Approx(0.75F));
    CHECK(std::get<float>(reloaded.at("SnowAmount")) == doctest::Approx(0.1F));

    CHECK(parameters.Reset(assets, collection, "WindStrength"));
    CHECK(std::get<float>(parameters.Snapshot().at("WindStrength")) == doctest::Approx(0.5F));
    REQUIRE(parameters.Set(assets, collection, "SnowAmount", 0.9F));
    REQUIRE(parameters.Clear(assets, collection));
    const auto cleared = parameters.Snapshot();
    CHECK(std::get<float>(cleared.at("WindStrength")) == doctest::Approx(0.5F));
    CHECK(std::get<float>(cleared.at("SnowAmount")) == doctest::Approx(0.1F));

    parameters.Close();
    CHECK(parameters.Snapshot().empty());
    assets->Close();
}

TEST_CASE("managed camera fullscreen materials validate slots and clear independently")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Effects"));
    auto entity = scene->CreateEntity("Camera");
    const auto camera = entity.AddComponent<Keire::CameraComponent>();
    const auto id = entity.Id().Value();
    for (const auto property : {Keire::ManagedRenderingAssetProperty::EffectBeforeTonemapping,
                                Keire::ManagedRenderingAssetProperty::EffectAfterTonemapping,
                                Keire::ManagedRenderingAssetProperty::EffectAfterUi})
    {
        const auto material = Keire::AssetId::Generate();
        CHECK(Keire::Detail::SetManagedRenderingAsset(scene, id, Keire::ManagedRenderingComponent::Camera, property,
                                                      material));
        CHECK(Keire::Detail::ReadManagedRenderingAsset(scene, id, Keire::ManagedRenderingComponent::Camera, property) ==
              material);
    }
    const auto before = camera->FullscreenEffects();
    CHECK_FALSE(Keire::Detail::SetManagedRenderingAsset(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                        Keire::ManagedRenderingAssetProperty::Cookie, {}));
    CHECK(camera->FullscreenEffects() == before);
    CHECK(Keire::Detail::SetManagedRenderingAsset(scene, id, Keire::ManagedRenderingComponent::Camera,
                                                  Keire::ManagedRenderingAssetProperty::EffectAfterUi, {}));
    CHECK_FALSE(camera->FullscreenEffects()[2]);
    CHECK(camera->FullscreenEffects()[0] == before[0]);
    CHECK(camera->FullscreenEffects()[1] == before[1]);
}
