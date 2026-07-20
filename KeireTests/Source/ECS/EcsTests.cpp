#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace
{
    class LifecycleProbeComponent final : public Keire::Component
    {
      public:
        explicit LifecycleProbeComponent(std::shared_ptr<std::vector<std::string>> calls)
            : Component(StaticType()), m_Calls(std::move(calls))
        {
        }

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x746573742d6c6966ULL, 0x656379636c650001ULL));
        }

      protected:
        void Awake() override { m_Calls->push_back("Awake"); }
        void OnEnable() override { m_Calls->push_back("OnEnable"); }
        void Start() override { m_Calls->push_back("Start"); }
        void FixedUpdate(float) override { m_Calls->push_back("FixedUpdate"); }
        void Update(float) override { m_Calls->push_back("Update"); }
        void OnDisable() override { m_Calls->push_back("OnDisable"); }
        void OnDestroy() override { m_Calls->push_back("OnDestroy"); }

      private:
        std::shared_ptr<std::vector<std::string>> m_Calls;
    };

    [[nodiscard]] Keire::ComponentRegistration
    MakeProbeRegistration(const std::shared_ptr<std::vector<std::string>>& calls)
    {
        Keire::ComponentRegistration result;
        result.Type = LifecycleProbeComponent::StaticType();
        result.Name = "Lifecycle Probe";
        result.Factory = [calls]
        { return Keire::Ref<Keire::Component>(Keire::CreateRef<LifecycleProbeComponent>(calls)); };
        result.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
        result.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
        return result;
    }

    [[nodiscard]] std::vector<std::byte> Bytes(const std::string& text)
    {
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }
} // namespace

TEST_CASE("Kéire math and Transform preserve stable public types across a private GLM implementation")
{
    const auto rotation = Keire::Math::EulerDegreesToQuaternion({15.0F, 30.0F, 45.0F});
    CHECK(std::abs(Keire::Math::Length(rotation) - 1.0F) < 0.0001F);
    const auto matrix = Keire::Math::ComposeTransform({1.0F, 2.0F, 3.0F}, rotation, {2.0F, 3.0F, 4.0F});
    Keire::Vector3 position;
    Keire::Quaternion decomposedRotation;
    Keire::Vector3 scale;
    REQUIRE(Keire::Math::DecomposeTransform(matrix, position, decomposedRotation, scale));
    CHECK(position.X == doctest::Approx(1.0F));
    CHECK(position.Y == doctest::Approx(2.0F));
    CHECK(position.Z == doctest::Approx(3.0F));
    CHECK(scale.X == doctest::Approx(2.0F));
    CHECK(scale.Y == doctest::Approx(3.0F));
    CHECK(scale.Z == doctest::Approx(4.0F));
}

TEST_CASE("Entities own required Transforms and stale handles become inert after hierarchy destruction")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto root = scene->CreateEntity("Root");
    auto child = scene->CreateEntity("Child", root);
    REQUIRE(root.GetComponent<Keire::TransformComponent>());
    CHECK_FALSE(root.RemoveComponent<Keire::TransformComponent>());
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({2.0F, 0.0F, 0.0F});
    root.GetComponent<Keire::TransformComponent>()->SetLocalPosition({3.0F, 0.0F, 0.0F});
    CHECK(child.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(5.0F));
    CHECK_THROWS_AS(root.SetParent(child), std::invalid_argument);
    REQUIRE(root.AddComponent<Keire::DirectionalLightComponent>());
    CHECK_THROWS_AS((void)root.AddComponent<Keire::DirectionalLightComponent>(), std::invalid_argument);
    const auto lights = scene->Query<Keire::DirectionalLightComponent>();
    REQUIRE(lights.size() == 1);
    CHECK(lights.front() == root);
    CHECK(scene->DestroyEntity(root.Id()));
    CHECK(scene->Query<Keire::DirectionalLightComponent>().empty());
    CHECK_FALSE(root);
    CHECK_FALSE(child);
    CHECK_FALSE(root.GetComponent<Keire::TransformComponent>());
}

TEST_CASE("Component lifecycle callbacks are deterministic and component handles become inert after removal")
{
    auto calls = std::make_shared<std::vector<std::string>>();
    auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(MakeProbeRegistration(calls));
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(), registry);
    auto entity = scene->CreateEntity("Lifecycle");
    auto component = entity.AddComponent<LifecycleProbeComponent>();
    scene->BeginPlay();
    scene->FixedUpdate(1.0F / 60.0F);
    scene->Update(1.0F / 60.0F);
    component->SetEnabled(false);
    REQUIRE(entity.RemoveComponent<LifecycleProbeComponent>());
    CHECK_FALSE(component->IsAttached());
    const std::vector<std::string> expected{"Awake",  "OnEnable",  "Start",    "FixedUpdate",
                                            "Update", "OnDisable", "OnDestroy"};
    CHECK(*calls == expected);
}

TEST_CASE("Play mode clones authored state and discards runtime mutations on Stop")
{
    auto edit = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto authored = edit->CreateEntity("Authored");
    edit->MarkSaved();
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(edit);
    session->Play();
    REQUIRE(session->RuntimeScene());
    auto runtimeEntity = session->RuntimeScene()->FindEntity(authored.Id());
    REQUIRE(runtimeEntity);
    runtimeEntity.SetName("Runtime Only");
    session->Pause();
    CHECK(session->Step(1.0F / 60.0F));
    session->Stop();
    CHECK(edit->FindEntity(authored.Id()).Name() == "Authored");
    CHECK(edit->ObjectCount() == 1);
}

TEST_CASE("Play mode retains the authored active camera and its transform")
{
    auto edit = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto cameraEntity = edit->CreateEntity("Main Camera");
    const auto camera = cameraEntity.AddComponent<Keire::CameraComponent>();
    camera->SetPrimary(true);
    camera->SetPriority(42);
    camera->SetClearColor({0.12F, 0.24F, 0.36F, 1.0F});
    cameraEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({3.0F, 4.0F, -8.0F});

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(edit);
    session->Play();
    const auto runtimeCameraEntity = session->RuntimeScene()->FindEntity(cameraEntity.Id());
    REQUIRE(runtimeCameraEntity);
    const auto runtimeCamera = runtimeCameraEntity.GetComponent<Keire::CameraComponent>();
    const auto runtimeTransform = runtimeCameraEntity.GetComponent<Keire::TransformComponent>();
    REQUIRE(runtimeCamera);
    REQUIRE(runtimeTransform);
    CHECK(runtimeCamera->Primary());
    CHECK(runtimeCamera->Priority() == 42);
    CHECK(runtimeCamera->ClearColor() == (Keire::Color{0.12F, 0.24F, 0.36F, 1.0F}));
    CHECK(runtimeTransform->LocalPosition() == (Keire::Vector3{3.0F, 4.0F, -8.0F}));
}

TEST_CASE("Scene schema v1 migrates to canonical component schema v2")
{
    const std::string versionOne = R"({
  "schemaVersion": 1,
  "name": "Legacy",
  "objects": [
    {
      "id": "11111111-1111-4111-8111-111111111111",
      "parent": null,
      "name": "Legacy Object",
      "active": true,
      "transform": {
        "position": [1, 2, 3],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      }
    }
  ]
})";
    const auto migrated = Keire::SceneAsset::Decode(Bytes(versionOne));
    REQUIRE(migrated);
    CHECK(migrated->Definition().SchemaVersion == 2);
    REQUIRE(migrated->Definition().Objects.size() == 1);
    REQUIRE(migrated->Definition().Objects.front().Components.size() == 1);
    CHECK(migrated->Definition().Objects.front().Components.front().Type == Keire::TransformComponent::StaticType());
    const auto encoded = Keire::SceneAsset::Encode(migrated->Definition());
    const std::string text(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    CHECK(text.find("\"entities\"") != std::string::npos);
    CHECK(text.find("\"objects\"") == std::string::npos);
}

TEST_CASE("Unregistered scene components survive load edit and save without losing payload")
{
    auto definition = Keire::SceneAsset::EmptyDefinition("MissingPlugin");
    Keire::SceneObjectDefinition object{Keire::AssetId::Generate(), {}, "Plugin Object"};
    const Keire::ComponentTypeId missing(Keire::AssetId(0x6d697373696e672dULL, 0x706c7567696e0001ULL));
    object.Components.push_back({missing, 7, false, R"({"pluginValue":42,"nested":{"name":"kept"}})"});
    definition.Objects.push_back(std::move(object));
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), definition);
    const auto snapshot = scene->Snapshot();
    REQUIRE(snapshot.Objects.size() == 1);
    const auto found =
        std::ranges::find(snapshot.Objects.front().Components, missing, &Keire::SceneComponentDefinition::Type);
    REQUIRE(found != snapshot.Objects.front().Components.end());
    CHECK(found->SchemaVersion == 7);
    CHECK_FALSE(found->Enabled);
    CHECK(found->Data == R"({"pluginValue":42,"nested":{"name":"kept"}})");
    const auto decoded = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(snapshot));
    const auto& canonicalPayload = decoded->Definition().Objects.front().Components.back().Data;
    CHECK(canonicalPayload.find("\"pluginValue\":42") != std::string::npos);
    CHECK(canonicalPayload.find("\"name\":\"kept\"") != std::string::npos);
    const auto decodedAgain = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(decoded->Definition()));
    CHECK(decodedAgain->Definition().Objects.front().Components.back().Data == canonicalPayload);
}
