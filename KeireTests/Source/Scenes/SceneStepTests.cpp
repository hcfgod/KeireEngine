#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct StepProbeState
    {
        std::vector<std::string> Calls;
        std::string ThrowAt;
        bool MoveInFixed = false;
        std::function<void()> DuringFixedUpdate;
        std::vector<Keire::AnimationIkMessage> IkContexts;
    };

    class StepProbe final : public Keire::Component
    {
      public:
        explicit StepProbe(std::shared_ptr<StepProbeState> state) : Component(StaticType()), m_State(std::move(state))
        {
        }
        static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x5374657050726f62ULL, 1));
        }

      protected:
        void Awake() override
        {
            if (m_State->ThrowAt == "Awake")
                Record("Awake");
        }
        void FixedUpdate(float) override
        {
            Record("FixedUpdate");
            if (m_State->DuringFixedUpdate)
            {
                m_State->DuringFixedUpdate();
            }
            if (m_State->MoveInFixed)
            {
                const auto transform = Owner().GetComponent<Keire::TransformComponent>();
                auto position = transform->LocalPosition();
                position.X += 2.0F;
                transform->SetLocalPosition(position);
            }
        }
        void Update(float) override { Record("Update"); }
        void LateUpdate() override { Record("LateUpdate"); }
        void OnAnimatorIk(const Keire::AnimationIkMessage& context) override
        {
            m_State->IkContexts.push_back(context);
            Record("AnimatorIK");
        }

      private:
        void Record(const std::string& phase)
        {
            m_State->Calls.push_back(phase);
            if (m_State->ThrowAt == phase)
                throw std::runtime_error("step probe failure");
        }
        std::shared_ptr<StepProbeState> m_State;
    };

    struct StepFixture
    {
        StepFixture()
        {
            auto registry = Keire::ComponentRegistry::CreateDefault();
            Keire::ComponentRegistration registration;
            registration.Type = StepProbe::StaticType();
            registration.Name = "Step probe";
            registration.Factory = [state = Probe]
            { return Keire::Ref<Keire::Component>(Keire::CreateRef<StepProbe>(state)); };
            registration.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
            registration.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
            registry->Register(std::move(registration));
            Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(),
                                                   registry);
            Character = Scene->CreateEntity("Stepped character");
            (void)Character.AddComponent<StepProbe>();
        }
        ~StepFixture()
        {
            if (Session)
                Session->Stop();
            if (Physics)
                Physics->Close();
            Scene->Close();
            if (Assets)
                Assets->Close();
            Database = {};
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
        void Start()
        {
            Session =
                Keire::CreateRef<Keire::SceneRuntimeSession>(Scene, Assets, Keire::Ref<Keire::AudioSystem>{}, Physics);
            Session->Play();
            Session->Pause();
            Probe->Calls.clear();
        }
        void Write(const std::string& name, const std::vector<std::byte>& bytes)
        {
            std::ofstream stream(Root / "Assets" / name, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }
        void AddAnimation(const float stateSpeed = 1.0F)
        {
            std::filesystem::create_directories(Root / "Assets");
            Keire::AssetDatabaseSpecification specification;
            specification.ProjectRoot = Root;
            const auto importer = [&](const std::string& extension, const Keire::AssetTypeId type)
            {
                Keire::AssetImporterRegistration value;
                value.Name = "StepTest" + extension;
                value.Type = type;
                value.Extensions = {extension};
                value.Import = [](std::span<const std::byte> bytes)
                { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
                specification.Importers.push_back(std::move(value));
            };
            importer(".stepskeleton", Keire::SkeletonAsset::StaticType());
            importer(".stepclip", Keire::AnimationClipAsset::StaticType());
            importer(".stepgraph", Keire::AnimationGraphAsset::StaticType());
            Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
            Write("Rig.stepskeleton",
                  Keire::SkeletonAsset::Encode(std::vector<Keire::SkeletonBone>{{"Root", -1, {}, {}}}));
            (void)Database->ImportAll();
            const auto skeleton = Database->Find("Rig.stepskeleton");
            REQUIRE(skeleton);
            Keire::AnimationTrack track;
            track.Bone = 0;
            track.Keys = {{0.0F, {}}, {1.0F, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
            Write("Move.stepclip",
                  Keire::AnimationClipAsset::Encode(skeleton->Id, 1.0F, std::span(&track, 1), {}, false));
            (void)Database->ImportAll();
            const auto clip = Database->Find("Move.stepclip");
            REQUIRE(clip);
            Keire::AnimationGraphDefinition graph;
            graph.EntryState = "Move";
            graph.States = {{"Move", clip->Id}};
            graph.States.front().Speed = stateSpeed;
            Write("Controller.stepgraph", Keire::AnimationGraphAsset::Encode(graph));
            const auto imported = Database->ImportAll();
            const auto controller = Database->Find("Controller.stepgraph");
            REQUIRE(controller);
            Keire::AssetSystemSpecification assets;
            assets.Mode = Keire::AssetMode::Development;
            assets.DevelopmentCatalog = imported.CatalogPath;
            assets.WorkerCount = 1;
            assets.Decoders = {Keire::CreateSkeletonAssetDecoder(), Keire::CreateAnimationClipAssetDecoder(),
                               Keire::CreateAnimationGraphAssetDecoder()};
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assets));
            const auto animator = Character.AddComponent<Keire::AnimatorComponent>();
            animator->SetSkeleton(skeleton->Id);
            animator->SetGraph(controller->Id);
        }

        std::filesystem::path Root = KeireTests::MakeTestDirectory("scene-step");
        std::shared_ptr<StepProbeState> Probe = std::make_shared<StepProbeState>();
        Keire::Ref<Keire::Scene> Scene;
        Keire::Entity Character;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::Ref<Keire::AssetSystem> Assets;
        Keire::Ref<Keire::PhysicsSystem> Physics;
        Keire::Ref<Keire::SceneRuntimeSession> Session;
    };
} // namespace

TEST_CASE("Simulation focus follows a dynamic body out and back across repeated origin thresholds")
{
    for (const double offset : {0.0, 1.0e12})
    {
        CAPTURE(offset);
        StepFixture fixture;
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        fixture.Character.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise(
            {offset + 0.125, 0.0, 0.0});
        (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
        fixture.Character.AddComponent<Keire::RigidBodyComponent>()->SetUseGravity(false);
        fixture.Start();
        fixture.Session->Pause(false);
        const auto focus = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
        const auto transform = focus.GetComponent<Keire::TransformComponent>();
        const auto body = focus.GetComponent<Keire::RigidBodyComponent>();
        fixture.Session->SetSimulationFocus(focus);
        float velocity = 480.0F;
        fixture.Probe->DuringFixedUpdate = [&] { body->SetLinearVelocity({velocity, 0.0F, 0.0F}); };
        auto previousOrigin = fixture.Session->Physics()->WorldOrigin();
        int migrations = 0;
        for (int direction = 0; direction < 2; ++direction)
        {
            velocity = direction == 0 ? 480.0F : -480.0F;
            for (int tick = 0; tick < 1280; ++tick)
            {
                fixture.Session->FixedUpdate(0.1F);
                REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
                const auto origin = fixture.Session->Physics()->WorldOrigin();
                CHECK(origin == fixture.Session->Vfx()->WorldOrigin());
                CHECK(std::abs(transform->WorldPositionPrecise().X - origin.X) <= 8240.0);
                if (origin != previousOrigin)
                {
                    ++migrations;
                    previousOrigin = origin;
                }
            }
            if (direction == 0)
            {
                CHECK(transform->WorldPositionPrecise().X - offset > 60000.0);
            }
        }
        CHECK(migrations >= 12);
        CHECK(std::abs(transform->WorldPositionPrecise().X - (offset + 0.125)) < 0.1);
    }
}

TEST_CASE("Simulation focus preserves floor support and wall collision across automatic origin shifts")
{
    for (const double offset : {0.0, 1.0e12})
    {
        CAPTURE(offset);
        StepFixture fixture;
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        auto floor = fixture.Scene->CreateEntity("Travel floor");
        floor.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({offset + 12.0, -0.5, 0.0});
        floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({32.0F, 0.5F, 4.0F});
        auto wall = fixture.Scene->CreateEntity("Travel barrier");
        wall.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({offset + 24.0, 2.0, 0.0});
        wall.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({0.5F, 2.0F, 4.0F});
        fixture.Character.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({offset, 0.5, 0.0});
        (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
        (void)fixture.Character.AddComponent<Keire::RigidBodyComponent>();
        fixture.Start();
        fixture.Session->Pause(false);
        const auto focus = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
        const auto transform = focus.GetComponent<Keire::TransformComponent>();
        const auto body = focus.GetComponent<Keire::RigidBodyComponent>();
        fixture.Session->SetSimulationFocus(focus, 8.0, 4.0);
        fixture.Probe->DuringFixedUpdate = [&] { body->SetLinearVelocity({12.0F, 0.0F, 0.0F}); };
        auto previousOrigin = fixture.Session->Physics()->WorldOrigin();
        int migrations = 0;
        for (int tick = 0; tick < 300; ++tick)
        {
            fixture.Session->FixedUpdate(0.02F);
            REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
            const auto position = transform->WorldPositionPrecise();
            CHECK(position.Y > 0.4);
            CHECK(position.Y < 0.6);
            CHECK(position.X - offset < 23.1);
            const auto origin = fixture.Session->Physics()->WorldOrigin();
            CHECK(origin == fixture.Session->Vfx()->WorldOrigin());
            if (origin != previousOrigin)
            {
                ++migrations;
                previousOrigin = origin;
            }
        }
        CHECK(migrations >= 2);
        CHECK(transform->WorldPositionPrecise().X - offset > 22.9);
    }
}

TEST_CASE("Physics checkpoint restoration rejects gameplay callbacks without changing world state")
{
    StepFixture fixture;
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
    fixture.Start();
    const auto original = fixture.Session->CapturePhysicsWorldCheckpoint();
    REQUIRE(original.Gravity);
    REQUIRE(original.Bodies.size() == 1);
    auto changed = original;
    changed.Gravity = Keire::Vector3{12.0F, 0.0F, 0.0F};
    changed.Bodies.front().Position.X += 20.0F;
    for (const bool worldCheckpoint : {false, true})
    {
        CAPTURE(worldCheckpoint);
        bool rejected = false;
        fixture.Probe->DuringFixedUpdate = [&]
        {
            try
            {
                if (worldCheckpoint)
                {
                    fixture.Session->RestorePhysicsWorldCheckpoint(changed);
                }
                else
                {
                    fixture.Session->RestorePhysicsCheckpoint(changed.Bodies);
                }
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        };
        REQUIRE(fixture.Session->Step(1.0F / 60.0F));
        CHECK(rejected);
        const auto after = fixture.Session->CapturePhysicsWorldCheckpoint();
        CHECK(after.Gravity == original.Gravity);
        REQUIRE(after.Bodies.size() == original.Bodies.size());
        CHECK(after.Bodies.front().Position == original.Bodies.front().Position);
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
    }
}

TEST_CASE("Simulation focus setter guards preserve ownership and support VFX-only sessions")
{
    StepFixture fixture;
    fixture.Start();
    auto focus = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    auto alternative = fixture.Session->RuntimeScene()->CreateEntity("Alternative focus");
    fixture.Session->SetSimulationFocus(focus);
    bool callbackRejected = false;
    fixture.Probe->DuringFixedUpdate = [&]
    {
        try
        {
            fixture.Session->SetSimulationFocus(alternative);
        }
        catch (const std::logic_error&)
        {
            callbackRejected = true;
        }
    };
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(callbackRejected);
    CHECK(fixture.Session->SimulationFocus() == focus);
    bool threadRejected = false;
    std::thread foreign(
        [&]
        {
            try
            {
                fixture.Session->SetSimulationFocus(alternative);
            }
            catch (const std::logic_error&)
            {
                threadRejected = true;
            }
        });
    foreign.join();
    CHECK(threadRejected);
    CHECK(fixture.Session->SimulationFocus() == focus);
    fixture.Probe->DuringFixedUpdate = {};
    alternative.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({20000.0, 0.0, 0.0});
    fixture.Session->SetSimulationFocus(alternative);
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->Vfx()->WorldOrigin().X == 20480.0);
    fixture.Session->SetSimulationFocus();
    alternative.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({40000.0, 0.0, 0.0});
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->Vfx()->WorldOrigin().X == 20480.0);
    CHECK_FALSE(fixture.Session->SimulationFocus());
}

TEST_CASE("Simulation focus follows fixed-step travel and preserves explicit lifetime ownership")
{
    StepFixture fixture;
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
    fixture.Start();
    auto focus = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    CHECK_THROWS_AS(fixture.Session->SetSimulationFocus(fixture.Character), std::invalid_argument);
    REQUIRE_NOTHROW(fixture.Session->SetSimulationFocus(focus));
    CHECK_THROWS_AS(fixture.Session->SetSimulationFocus({}, 0.0), std::invalid_argument);
    CHECK(fixture.Session->SimulationFocus() == focus);
    const auto transform = focus.GetComponent<Keire::TransformComponent>();
    fixture.Probe->DuringFixedUpdate = [&] { transform->SetWorldPositionPrecise({9000.125, 0.0, 0.0}); };
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->Physics()->WorldOrigin() == Keire::Vector3d{9216.0, 0.0, 0.0});
    CHECK(fixture.Session->Vfx()->WorldOrigin() == fixture.Session->Physics()->WorldOrigin());
    CHECK(transform->WorldPositionPrecise().X == 9000.125);
    fixture.Probe->DuringFixedUpdate = {};
    focus.SetActive(false);
    transform->SetWorldPositionPrecise({20000.125, 0.0, 0.0});
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->Physics()->WorldOrigin().X == 9216.0);
    focus.SetActive(true);
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->Physics()->WorldOrigin().X == 20480.0);
    const auto snapshot = fixture.Session->RuntimeScene()->Snapshot();
    fixture.Session->ReplaceRuntime(snapshot);
    CHECK_FALSE(fixture.Session->SimulationFocus());
    CHECK_THROWS_AS(fixture.Session->SetSimulationFocus(focus), std::invalid_argument);
    auto replacement = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    fixture.Session->SetSimulationFocus(replacement);
    REQUIRE(replacement.Destroy());
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK_FALSE(fixture.Session->SimulationFocus());
}

TEST_CASE("Simulation focus failure faults the step without committing either origin")
{
    StepFixture fixture;
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto obstacle = fixture.Scene->CreateEntity("Precision obstacle");
    obstacle.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({0.015625, 0.0, 0.0});
    (void)obstacle.AddComponent<Keire::ColliderComponent>();
    fixture.Start();
    const auto focus = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    fixture.Session->SetSimulationFocus(focus);
    focus.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({0x1p60, 0.0, 0.0});
    CHECK_FALSE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Faulted);
    CHECK(fixture.Session->Diagnostic().Callback == "Simulation origin");
    CHECK(fixture.Session->Physics()->WorldOrigin() == Keire::Vector3d{});
    CHECK(fixture.Session->Vfx()->WorldOrigin() == Keire::Vector3d{});
    CHECK(fixture.Probe->Calls.empty());
}

TEST_CASE("Physics origin changes reject callbacks and foreign threads without mutation")
{
    bool sharedOrigin = false;
    SUBCASE("physics only") {}
    SUBCASE("physics and VFX") { sharedOrigin = true; }
    StepFixture fixture;
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
    fixture.Start();
    const auto origin = fixture.Session->Physics()->WorldOrigin();
    const auto rebase = [&]
    {
        if (sharedOrigin)
        {
            fixture.Session->RebaseSimulationOrigin({128.0, 0.0, 0.0});
        }
        else
        {
            fixture.Session->RebasePhysicsOrigin({128.0, 0.0, 0.0});
        }
    };
    bool callbackRejected = false;
    fixture.Probe->DuringFixedUpdate = [&]
    {
        try
        {
            rebase();
        }
        catch (const std::logic_error&)
        {
            callbackRejected = true;
        }
    };
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(callbackRejected);
    CHECK(fixture.Session->Physics()->WorldOrigin() == origin);
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
    bool sessionRejected = false;
    bool worldRejected = false;
    const auto physicsWorld = fixture.Session->Physics();
    std::thread foreign(
        [&]
        {
            try
            {
                rebase();
            }
            catch (const std::logic_error&)
            {
                sessionRejected = true;
            }
            try
            {
                physicsWorld->RebaseOrigin({128.0, 0.0, 0.0});
            }
            catch (const std::logic_error&)
            {
                worldRejected = true;
            }
        });
    foreign.join();
    CHECK(sessionRejected);
    CHECK(worldRejected);
    CHECK(fixture.Session->Physics()->WorldOrigin() == origin);
    fixture.Probe->DuringFixedUpdate = {};
    REQUIRE_NOTHROW(rebase());
    CHECK(fixture.Session->Physics()->WorldOrigin() == Keire::Vector3d{128.0, 0.0, 0.0});
    REQUIRE(fixture.Session->Step(1.0F / 60.0F));
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
}

TEST_CASE("Runtime replacement reports initialization failure and recovers the previous play state")
{
    for (const bool paused : {false, true})
    {
        CAPTURE(paused);
        StepFixture fixture;
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
        fixture.Start();
        fixture.Session->Pause(paused);
        const auto expectedState = paused ? Keire::ScenePlayState::Paused : Keire::ScenePlayState::Playing;
        const auto baseline = fixture.Session->RuntimeScene()->Snapshot();
        const auto originalPhysics = fixture.Session->Physics();
        REQUIRE(originalPhysics);
        fixture.Probe->ThrowAt = "Awake";
        CHECK_THROWS_AS(fixture.Session->ReplaceRuntime(baseline), std::runtime_error);
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Faulted);
        CHECK(fixture.Session->Diagnostic().Callback == "Awake/OnEnable");
        CHECK_FALSE(originalPhysics->IsOpen());
        CHECK_THROWS_AS(fixture.Session->ReplaceRuntime(baseline), std::runtime_error);
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Faulted);

        fixture.Probe->ThrowAt.clear();
        fixture.Probe->Calls.clear();
        REQUIRE_NOTHROW(fixture.Session->ReplaceRuntime(baseline));
        CHECK(fixture.Session->State() == expectedState);
        CHECK(fixture.Session->Diagnostic().Message.empty());
        REQUIRE(fixture.Session->Physics());
        CHECK(fixture.Session->Physics()->IsOpen());
        CHECK(fixture.Session->Physics() != originalPhysics);
        if (paused)
        {
            REQUIRE(fixture.Session->Step(1.0F / 60.0F));
        }
        else
        {
            fixture.Session->FixedUpdate(1.0F / 60.0F);
            fixture.Session->Update(1.0F / 60.0F);
        }
        CHECK(fixture.Probe->Calls == std::vector<std::string>{"FixedUpdate", "Update", "LateUpdate"});
        CHECK(fixture.Session->State() == expectedState);
    }
}

TEST_CASE("Runtime replacement recovers a fault from ordinary paused stepping")
{
    StepFixture fixture;
    fixture.Start();
    const auto baseline = fixture.Session->RuntimeScene()->Snapshot();
    fixture.Probe->ThrowAt = "FixedUpdate";
    CHECK_FALSE(fixture.Session->Step(1.0F / 60.0F));
    REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Faulted);
    fixture.Probe->ThrowAt.clear();
    REQUIRE_NOTHROW(fixture.Session->ReplaceRuntime(baseline));
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
    CHECK(fixture.Session->Step(1.0F / 60.0F));
}

TEST_CASE("Paused scene stepping runs a complete frame and stays paused")
{
    StepFixture fixture;
    fixture.Start();
    REQUIRE(fixture.Session->Step(0.1F));
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
    CHECK(fixture.Probe->Calls == std::vector<std::string>{"FixedUpdate", "Update", "LateUpdate"});
    fixture.Probe->Calls.clear();
    fixture.Session->Update(0.5F);
    fixture.Session->FixedUpdate(0.5F);
    CHECK(fixture.Probe->Calls.empty());
}

TEST_CASE("Paused scene stepping rejects invalid deltas before invoking gameplay")
{
    StepFixture fixture;
    fixture.Start();
    for (const float delta :
         {0.0F, -0.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        CHECK_THROWS_AS((void)fixture.Session->Step(delta), std::invalid_argument);
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
        CHECK(fixture.Probe->Calls.empty());
    }
}

TEST_CASE("Paused scene stepping stops later phases when a callback fails")
{
    const std::vector<std::string> phases{"FixedUpdate", "Update", "LateUpdate"};
    for (std::size_t index = 0; index < phases.size(); ++index)
    {
        StepFixture fixture;
        fixture.Start();
        fixture.Probe->ThrowAt = phases[index];
        CHECK_FALSE(fixture.Session->Step(0.1F));
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Faulted);
        CHECK(fixture.Probe->Calls == std::vector<std::string>(phases.begin(), phases.begin() + index + 1));
        CHECK_FALSE(fixture.Session->Step(0.1F));
    }
}

TEST_CASE("Paused scene stepping advances an ordinary animation graph exactly once")
{
    StepFixture fixture;
    fixture.AddAnimation();
    fixture.Start();
    const auto animator =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::AnimatorComponent>();
    fixture.Session->Pause(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!animator->RuntimeDebugSnapshot() || animator->RuntimeDebugSnapshot()->Layers.empty()) &&
           std::chrono::steady_clock::now() < deadline)
    {
        (void)fixture.Assets->PumpCompletions();
        fixture.Session->Update(0.0F);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(animator->RuntimeDebugSnapshot());
    REQUIRE_FALSE(animator->RuntimeDebugSnapshot()->Layers.empty());
    fixture.Session->Pause();
    fixture.Probe->Calls.clear();
    const auto before = animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime;
    REQUIRE(fixture.Session->Step(0.1F));
    CHECK(fixture.Probe->Calls == std::vector<std::string>{"FixedUpdate", "Update", "AnimatorIK", "LateUpdate"});
    CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(before + 0.1F));
    REQUIRE_FALSE(animator->RuntimeDebugSnapshot()->Pose.empty());
    CHECK(animator->RuntimeDebugSnapshot()->Pose.front().LocalTransform.Translation.X == doctest::Approx(0.1F));
    fixture.Session->Update(0.5F);
    CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(before + 0.1F));
    CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
}

TEST_CASE("Animator playback speed multiplies state speed and zero preserves live IK evaluation")
{
    for (const float stateSpeed : {0.0F, 0.2F, 2.0F})
    {
        StepFixture fixture;
        fixture.AddAnimation(stateSpeed);
        fixture.Character.GetComponent<Keire::AnimatorComponent>()->SetSpeed(0.0F);
        fixture.Start();
        const auto animator = fixture.Session->RuntimeScene()
                                  ->FindEntity(fixture.Character.Id())
                                  .GetComponent<Keire::AnimatorComponent>();
        fixture.Session->Pause(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!animator->RuntimeDebugSnapshot() || animator->RuntimeDebugSnapshot()->Layers.empty()) &&
               std::chrono::steady_clock::now() < deadline)
        {
            (void)fixture.Assets->PumpCompletions();
            fixture.Session->Update(0.0F);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(animator->RuntimeDebugSnapshot());
        REQUIRE_FALSE(animator->RuntimeDebugSnapshot()->Layers.empty());
        fixture.Session->Pause();
        fixture.Probe->Calls.clear();
        REQUIRE(fixture.Session->Step(0.1F));
        CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == 0.0F);
        CHECK(fixture.Probe->Calls == std::vector<std::string>{"FixedUpdate", "Update", "AnimatorIK", "LateUpdate"});
        animator->SetSpeed(2.0F);
        REQUIRE(fixture.Session->Step(0.1F));
        CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(0.2F * stateSpeed));
        animator->SetPaused(true);
        REQUIRE(fixture.Session->Step(0.1F));
        CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(0.2F * stateSpeed));
        animator->SetPaused(false);
        animator->SetSpeed(0.5F);
        REQUIRE(fixture.Session->Step(0.1F));
        CHECK(animator->RuntimeDebugSnapshot()->Layers.front().NormalizedTime == doctest::Approx(0.25F * stateSpeed));
    }
}

TEST_CASE("Paused scene stepping presents the new physics pose without host interpolation drift")
{
    for (const bool characterController : {false, true})
    {
        StepFixture fixture;
        if (characterController)
            (void)fixture.Character.AddComponent<Keire::CharacterControllerComponent>();
        else
        {
            (void)fixture.Character.AddComponent<Keire::ColliderComponent>();
            const auto body = fixture.Character.AddComponent<Keire::RigidBodyComponent>();
            body->SetUseGravity(false);
            body->SetLinearVelocity({0.0F, 0.0F, 1.0F});
        }
        Keire::PhysicsSystemSpecification physics;
        physics.Mode = Keire::PhysicsMode::Enabled;
        fixture.Physics = Keire::CreateRef<Keire::PhysicsSystem>(physics);
        fixture.Start();
        const auto entity = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
        const auto transform = entity.GetComponent<Keire::TransformComponent>();
        transform->SetFixedPresentationInterpolation(true);
        if (characterController)
            REQUIRE(
                entity.GetComponent<Keire::CharacterControllerComponent>()->QueueDesiredMovement({0.0F, 0.0F, 0.2F}));
        REQUIRE(fixture.Session->Step(1.0F / 60.0F));
        const auto current = transform->WorldPosition();
        REQUIRE(current.Z > 0.0F);
        CHECK(transform->PresentationWorldPosition().Z == doctest::Approx(current.Z));
        fixture.Session->Update(1.0F / 144.0F, 0.0F);
        CHECK(transform->WorldPosition().Z == doctest::Approx(current.Z));
        CHECK(transform->PresentationWorldPosition().Z == doctest::Approx(current.Z));
        CHECK(fixture.Session->State() == Keire::ScenePlayState::Paused);
    }
}

TEST_CASE("Custom fixed presentation preserves simulation and samples zero multiple ticks and reset")
{
    StepFixture fixture;
    fixture.Probe->MoveInFixed = true;
    fixture.Start();
    const auto entity = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    const auto transform = entity.GetComponent<Keire::TransformComponent>();
    transform->SetFixedPresentationInterpolation(true);
    fixture.Session->Pause(false);
    fixture.Session->Update(0.0F, 0.25F);
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(0.0F));
    fixture.Session->FixedUpdate(0.02F);
    fixture.Session->Update(0.0F, 0.25F);
    CHECK(transform->WorldPosition().X == doctest::Approx(2.0F));
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(0.5F));
    fixture.Session->Update(0.0F, 0.75F);
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(1.5F));
    fixture.Session->FixedUpdate(0.02F);
    fixture.Session->FixedUpdate(0.02F);
    fixture.Session->Update(0.0F, 0.5F);
    CHECK(transform->WorldPosition().X == doctest::Approx(6.0F));
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(5.0F));
    transform->SetWorldPosition({100.0F, 0.0F, 0.0F});
    transform->ResetPresentationInterpolation();
    fixture.Session->Update(0.0F, 0.0F);
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(100.0F));
    fixture.Session->Pause();
    REQUIRE(fixture.Session->Step(0.02F));
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(102.0F));
    fixture.Session->Update(0.0F, 0.0F);
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(102.0F));
    transform->SetFixedPresentationInterpolation(false);
    CHECK(transform->PresentationWorldPosition() == transform->WorldPosition());
}

TEST_CASE("Custom fixed presentation composes parent poses and rejects invalid alpha without mutation")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto parent = scene->CreateEntity("carrier");
    auto child = scene->CreateEntity("rider");
    child.SetParent(parent, false);
    const auto carrier = parent.GetComponent<Keire::TransformComponent>();
    const auto rider = child.GetComponent<Keire::TransformComponent>();
    rider->SetLocalPosition({1.0F, 0.0F, 0.0F});
    for (const auto& transform : {carrier, rider})
    {
        transform->SetFixedPresentationInterpolation(true);
        transform->BeginRuntimeFixedPresentationSample();
    }
    carrier->SetLocalPosition({10.0F, 0.0F, 0.0F});
    rider->SetLocalPosition({3.0F, 0.0F, 0.0F});
    for (const auto& transform : {carrier, rider})
    {
        transform->EndRuntimeFixedPresentationSample();
        transform->ApplyRuntimeFixedPresentation(0.5F);
    }
    CHECK(rider->WorldPosition().X == doctest::Approx(13.0F));
    CHECK(rider->PresentationWorldPosition().X == doctest::Approx(7.0F));
    CHECK_THROWS_AS(rider->ApplyRuntimeFixedPresentation(std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
    CHECK(rider->PresentationWorldPosition().X == doctest::Approx(7.0F));
    child.SetParent({}, true);
    CHECK(rider->PresentationWorldPosition().X == doctest::Approx(13.0F));
    scene->Close();
}

TEST_CASE("Animation graph IK context carries presentation alpha while paused step uses current time")
{
    StepFixture fixture;
    fixture.AddAnimation();
    fixture.Start();
    fixture.Session->Pause(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (fixture.Probe->IkContexts.empty() && std::chrono::steady_clock::now() < deadline)
    {
        (void)fixture.Assets->PumpCompletions();
        fixture.Session->Update(0.0F, 0.35F);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(fixture.Probe->IkContexts.empty());
    CHECK(fixture.Probe->IkContexts.back().InterpolationAlpha == doctest::Approx(0.35F));
    CHECK_FALSE(fixture.Probe->IkContexts.back().IsFixedUpdate);
    fixture.Session->Pause();
    REQUIRE(fixture.Session->Step(0.02F));
    CHECK(fixture.Probe->IkContexts.back().InterpolationAlpha == doctest::Approx(1.0F));
    CHECK_FALSE(fixture.Probe->IkContexts.back().IsFixedUpdate);
}

TEST_CASE("Custom fixed presentation opt-in leaves physics override and quaternion rotation intact")
{
    const auto transform = Keire::CreateRef<Keire::TransformComponent>();
    transform->SetRuntimePresentationWorldPosition({9.0F, 0.0F, 0.0F});
    const auto revision = transform->PresentationResetRevision();
    transform->SetFixedPresentationInterpolation(true);
    CHECK(transform->PresentationResetRevision() == revision);
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(9.0F));
    transform->ResetPresentationInterpolation();
    transform->BeginRuntimeFixedPresentationSample();
    transform->SetLocalEulerAngles({0.0F, 90.0F, 0.0F});
    transform->EndRuntimeFixedPresentationSample();
    transform->ApplyRuntimeFixedPresentation(0.5F);
    const auto forward = Keire::Math::TransformDirection(transform->PresentationWorldMatrix(), {0.0F, 0.0F, 1.0F});
    CHECK(forward.X == doctest::Approx(0.70710678F));
    CHECK(forward.Z == doctest::Approx(0.70710678F));
    CHECK(transform->LocalEulerAngles().Y == doctest::Approx(90.0F));
    transform->SetFixedPresentationInterpolation(false);
    CHECK(transform->PresentationWorldMatrix() == transform->WorldMatrix());
}

TEST_CASE("Custom fixed presentation opt-ins survive detached toggles and reject foreign thread mutation")
{
    auto detached = Keire::CreateRef<Keire::TransformComponent>();
    detached->SetFixedPresentationInterpolation(true);
    CHECK(detached->FixedPresentationInterpolation());
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("custom mover");
    const auto transform = entity.GetComponent<Keire::TransformComponent>();
    bool rejected = false;
    std::thread foreign(
        [&]
        {
            try
            {
                transform->SetFixedPresentationInterpolation(true);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    foreign.join();
    CHECK(rejected);
    CHECK_FALSE(transform->FixedPresentationInterpolation());
    rejected = false;
    std::thread sameValue(
        [&]
        {
            try
            {
                transform->SetFixedPresentationInterpolation(false);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    sameValue.join();
    CHECK(rejected);
    transform->SetFixedPresentationInterpolation(true);
    CHECK(scene->DestroyEntity(entity.Id()));
    CHECK_NOTHROW(transform->SetFixedPresentationInterpolation(false));
    auto survivor = scene->CreateEntity("retained").GetComponent<Keire::TransformComponent>();
    survivor->SetFixedPresentationInterpolation(true);
    scene->Close();
    CHECK_NOTHROW(survivor->SetFixedPresentationInterpolation(false));
}

TEST_CASE("Custom fixed presentation remains available with a disabled character controller")
{
    StepFixture fixture;
    fixture.Probe->MoveInFixed = true;
    fixture.Character.AddComponent<Keire::CharacterControllerComponent>()->SetEnabled(false);
    fixture.Start();
    const auto transform =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::TransformComponent>();
    transform->SetFixedPresentationInterpolation(true);
    fixture.Session->Pause(false);
    fixture.Session->FixedUpdate(0.02F);
    fixture.Session->Update(0.0F, 0.5F);
    CHECK(transform->WorldPosition().X == doctest::Approx(2.0F));
    CHECK(transform->PresentationWorldPosition().X == doctest::Approx(1.0F));
}

TEST_CASE("Scene component publication rolls back and preserves an Awake failure")
{
    StepFixture fixture;
    fixture.Start();
    auto runtime = fixture.Session->RuntimeScene();
    auto entity = runtime->CreateEntity("rejected component");
    const auto before = runtime->Query<StepProbe>().size();
    fixture.Probe->ThrowAt = "Awake";
    CHECK_THROWS_WITH_AS(entity.AddComponent<StepProbe>(), "step probe failure", std::runtime_error);
    CHECK_FALSE(entity.GetComponent<StepProbe>());
    CHECK(runtime->Query<StepProbe>().size() == before);
    CHECK(entity.GetComponent<Keire::TransformComponent>());
    fixture.Probe->ThrowAt.clear();
    CHECK_NOTHROW((void)entity.AddComponent<StepProbe>());
    CHECK(runtime->Query<StepProbe>().size() == before + 1);
}
