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
        void LateUpdate() override { m_Calls->push_back("LateUpdate"); }
        void OnAnimationEvent(const Keire::AnimationEventMessage& event) override
        {
            m_Calls->push_back("Animation:" + event.Name);
        }
        void OnCollisionEnter(const Keire::PhysicsContactMessage&) override { m_Calls->push_back("CollisionEnter"); }
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

    TEST_CASE("Character controller deserialization repairs missing and empty runtime IDs")
    {
        const auto registration = Keire::CreateCharacterControllerComponentRegistration();

        const auto missingIdComponent = registration.Factory();
        REQUIRE(static_cast<bool>(missingIdComponent));
        CHECK_NOTHROW(registration.Deserialize(*missingIdComponent, {}, registration.SchemaVersion));
        CHECK(static_cast<bool>(
            dynamic_cast<const Keire::CharacterControllerComponent&>(*missingIdComponent).RuntimeId()));

        const Keire::ComponentPropertyBag legacyValues{{"runtimeId", Keire::AssetId{}}};
        const auto migratedValues = registration.Migrate(legacyValues, 1);
        const auto migratedComponent = registration.Factory();
        REQUIRE(static_cast<bool>(migratedComponent));
        CHECK_NOTHROW(registration.Deserialize(*migratedComponent, migratedValues, registration.SchemaVersion));
        CHECK(static_cast<bool>(
            dynamic_cast<const Keire::CharacterControllerComponent&>(*migratedComponent).RuntimeId()));
    }

    struct PhysicsLifecycleProbeState
    {
        Keire::SceneRuntimeSession* Session = nullptr;
        Keire::EntityId Target;
        std::vector<std::string> Calls;
        std::vector<bool> WorldReady;
        std::vector<bool> QueryReady;
    };

    class PhysicsLifecycleProbeComponent final : public Keire::Component
    {
      public:
        explicit PhysicsLifecycleProbeComponent(std::shared_ptr<PhysicsLifecycleProbeState> state)
            : Component(StaticType()), m_State(std::move(state))
        {
        }

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x746573742d706879ULL, 0x736c696665000001ULL));
        }

      protected:
        void Awake() override { Observe("Awake"); }
        void OnEnable() override { Observe("OnEnable"); }

      private:
        void Observe(std::string callback)
        {
            const bool worldReady =
                m_State->Session && m_State->Session->Physics() && m_State->Session->Physics()->IsOpen();
            bool queryReady = false;
            if (worldReady)
            {
                const auto hits = m_State->Session->RayCast(
                    {.Origin = {0.0F, 3.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 6.0F});
                queryReady =
                    std::ranges::find(hits, m_State->Target, &Keire::ScenePhysicsQueryHit::Entity) != hits.end();
            }
            m_State->Calls.push_back(std::move(callback));
            m_State->WorldReady.push_back(worldReady);
            m_State->QueryReady.push_back(queryReady);
        }

        std::shared_ptr<PhysicsLifecycleProbeState> m_State;
    };

    [[nodiscard]] Keire::ComponentRegistration
    MakePhysicsLifecycleProbeRegistration(const std::shared_ptr<PhysicsLifecycleProbeState>& state)
    {
        Keire::ComponentRegistration result;
        result.Type = PhysicsLifecycleProbeComponent::StaticType();
        result.Name = "Physics Lifecycle Probe";
        result.Factory = [state]
        { return Keire::Ref<Keire::Component>(Keire::CreateRef<PhysicsLifecycleProbeComponent>(state)); };
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

TEST_CASE("Transform scale edits preserve invertible matrices transactionally")
{
    Keire::TransformComponent transform;
    transform.SetLocalScale({2.0F, -3.0F, 4.0F});
    const auto previousScale = transform.LocalScale();

    CHECK(Keire::TransformComponent::IsValidLocalScale(previousScale));
    CHECK_FALSE(Keire::TransformComponent::IsValidLocalScale({1.0F, 0.0F, 1.0F}));
    CHECK_FALSE(Keire::TransformComponent::IsValidLocalScale({1.0F, 0.0000001F, 1.0F}));
    CHECK_THROWS_AS(transform.SetLocalScale({1.0F, 0.0F, 1.0F}), std::invalid_argument);
    CHECK(transform.LocalScale() == previousScale);

    const Keire::Vector3 tinyScale{0.0001F, 0.0002F, 0.0003F};
    REQUIRE(Keire::TransformComponent::IsValidLocalScale(tinyScale));
    const auto tinyMatrix = Keire::Math::ComposeTransform({}, {}, tinyScale);
    const auto inverse = Keire::Math::Inverse(tinyMatrix);
    const Keire::Vector3 point{4.0F, 5.0F, 6.0F};
    const auto roundTripped = Keire::Math::TransformPoint(inverse, Keire::Math::TransformPoint(tinyMatrix, point));
    CHECK(roundTripped.X == doctest::Approx(point.X).epsilon(0.0001));
    CHECK(roundTripped.Y == doctest::Approx(point.Y).epsilon(0.0001));
    CHECK(roundTripped.Z == doctest::Approx(point.Z).epsilon(0.0001));

    const auto singular = Keire::Math::ComposeTransform({}, {}, {1.0F, 0.0F, 1.0F});
    CHECK_THROWS_AS((void)Keire::Math::Inverse(singular), std::invalid_argument);
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

TEST_CASE("Hierarchy snapshots omit component payload serialization")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto root = scene->CreateEntity("Root");
    auto child = scene->CreateEntity("Child", root);
    child.SetActive(false);

    const auto hierarchy = scene->HierarchySnapshot();
    REQUIRE(hierarchy.Objects.size() == 2);
    CHECK(hierarchy.Objects[0].Id == root.Id().Value());
    CHECK(hierarchy.Objects[0].Name == "Root");
    CHECK(hierarchy.Objects[0].Components.empty());
    CHECK(hierarchy.Objects[1].Id == child.Id().Value());
    CHECK(hierarchy.Objects[1].Parent == root.Id().Value());
    CHECK_FALSE(hierarchy.Objects[1].Active);
    CHECK(hierarchy.Objects[1].Components.empty());
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
    scene->DispatchAnimationEvent(entity.Id(), {"Footstep", 0.5F});
    scene->LateUpdate();
    component->SetEnabled(false);
    REQUIRE(entity.RemoveComponent<LifecycleProbeComponent>());
    CHECK_FALSE(component->IsAttached());
    const std::vector<std::string> expected{"Awake",       "OnEnable",  "Start",
                                            "FixedUpdate", "Update",    "Animation:Footstep",
                                            "LateUpdate",  "OnDisable", "OnDestroy"};
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

TEST_CASE("Play session eagerly owns physics and runs gameplay sync step pullback and contact dispatch")
{
    auto calls = std::make_shared<std::vector<std::string>>();
    auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(MakeProbeRegistration(calls));
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(), registry);

    auto floor = scene->CreateEntity("Floor");
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
    const auto floorCollider = floor.AddComponent<Keire::ColliderComponent>();
    floorCollider->SetHalfExtent({5.0F, 0.5F, 5.0F});

    auto falling = scene->CreateEntity("Falling");
    falling.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 2.0F, 0.0F});
    const auto fallingCollider = falling.AddComponent<Keire::ColliderComponent>();
    fallingCollider->SetShape(Keire::ColliderShape::Sphere);
    fallingCollider->SetRadius(0.5F);
    const auto body = falling.AddComponent<Keire::RigidBodyComponent>();
    body->SetMotion(Keire::PhysicsMotionType::Dynamic);
    REQUIRE(falling.AddComponent<LifecycleProbeComponent>());

    Keire::PhysicsSystemSpecification physicsSpecification;
    physicsSpecification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(physicsSpecification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->Physics());
    for (int step = 0; step < 180 && session->State() == Keire::ScenePlayState::Playing; ++step)
        session->FixedUpdate(1.0F / 60.0F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);

    const auto runtimeFalling = session->RuntimeScene()->FindEntity(falling.Id());
    REQUIRE(runtimeFalling);
    CHECK(runtimeFalling.GetComponent<Keire::TransformComponent>()->LocalPosition().Y < 2.0F);
    CHECK(std::ranges::find(*calls, "CollisionEnter") != calls->end());

    const auto hits =
        session->RayCast({.Origin = {0.0F, 5.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 10.0F});
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().Entity == falling.Id());

    session->Stop();
    CHECK_FALSE(session->Physics());
    physics->Close();
    scene->Close();
}

TEST_CASE("Play lifecycle observes eager physics before Awake and OnEnable including runtime replacement")
{
    auto observations = std::make_shared<PhysicsLifecycleProbeState>();
    auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(MakePhysicsLifecycleProbeRegistration(observations));
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(), registry);

    auto target = scene->CreateEntity("Query Target");
    const auto collider = target.AddComponent<Keire::ColliderComponent>();
    collider->SetHalfExtent({1.0F, 0.5F, 1.0F});
    auto probe = scene->CreateEntity("Lifecycle Probe");
    REQUIRE(probe.AddComponent<PhysicsLifecycleProbeComponent>());

    Keire::PhysicsSystemSpecification physicsSpecification;
    physicsSpecification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(physicsSpecification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    observations->Session = session.Get();
    observations->Target = target.Id();

    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    REQUIRE((observations->Calls == std::vector<std::string>{"Awake", "OnEnable"}));
    CHECK(std::ranges::all_of(observations->WorldReady, [](const bool ready) { return ready; }));
    CHECK(std::ranges::all_of(observations->QueryReady, [](const bool ready) { return ready; }));
    const auto initialWorld = session->Physics();
    REQUIRE(initialWorld);
    REQUIRE(initialWorld->IsOpen());

    session->ReplaceRuntime(scene->Snapshot());
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK((observations->Calls == std::vector<std::string>{"Awake", "OnEnable", "Awake", "OnEnable"}));
    CHECK(std::ranges::all_of(observations->WorldReady, [](const bool ready) { return ready; }));
    CHECK(std::ranges::all_of(observations->QueryReady, [](const bool ready) { return ready; }));
    CHECK_FALSE(initialWorld->IsOpen());
    const auto replacementWorld = session->Physics();
    REQUIRE(replacementWorld);
    CHECK(replacementWorld != initialWorld);
    CHECK(replacementWorld->IsOpen());

    session->Stop();
    CHECK_FALSE(replacementWorld->IsOpen());
    observations->Session = nullptr;
    physics->Close();
    scene->Close();
}

TEST_CASE("Character Controller smoothing tails stay below capsule-query resolution without faulting Play Mode")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Controller"));
    auto floor = scene->CreateEntity("Floor");
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
    const auto floorCollider = floor.AddComponent<Keire::ColliderComponent>();
    floorCollider->SetHalfExtent({5.0F, 0.5F, 5.0F});

    auto player = scene->CreateEntity("Player");
    player.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.0F, 0.0F});
    const auto controller = player.AddComponent<Keire::CharacterControllerComponent>();
    controller->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);

    Keire::PhysicsSystemSpecification physicsSpecification;
    physicsSpecification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(physicsSpecification);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    REQUIRE(runtimePlayer);
    const auto runtimeController = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    REQUIRE(runtimeController);

    REQUIRE(runtimeController->QueueDesiredMovement({0.0001F, 0.0F, 0.0F}));
    session->FixedUpdate(1.0F / 60.0F);
    CHECK(session->State() == Keire::ScenePlayState::Playing);
    CHECK(session->Diagnostic().Message.empty());

    REQUIRE(runtimeController->QueueDesiredMovement({0.25F, 0.0F, 0.0F}));
    session->FixedUpdate(1.0F / 60.0F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK(runtimePlayer.GetComponent<Keire::TransformComponent>()->LocalPosition().X > 0.2F);

    session->Stop();
    physics->Close();
    scene->Close();
}

TEST_CASE("Play mode retains the authored active camera and its transform")
{
    auto edit = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto cameraEntity = edit->CreateEntity("Main Camera");
    const auto camera = cameraEntity.AddComponent<Keire::CameraComponent>();
    camera->SetPrimary(true);
    camera->SetPriority(42);
    camera->SetClearMode(Keire::CameraClearMode::SolidColor);
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
    CHECK(runtimeCamera->ClearMode() == Keire::CameraClearMode::SolidColor);
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
    CHECK(migrated->Definition().SchemaVersion == 3);
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
