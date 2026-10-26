#include "KeireRuntimeInternal/RuntimeFrameTiming.h"
#include "KeireTests/TestSupport.h"

#include "KeireInternal/Environment/SceneRuntimeEnvironmentAccess.h"
#include "KeireInternal/Spatial/FrameRegistry.h"
#include "KeireInternal/Spatial/SceneRuntimeSpatialAccess.h"

#include <doctest/doctest.h>

#include <atomic>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{
    class WorldStepProbe final : public Keire::Component
    {
      public:
        WorldStepProbe() : Component(StaticType()) {}

        static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x576f726c64537465ULL, 1));
        }

        int FixedCalls = 0;
        int UpdateCalls = 0;
        bool ThrowOnFixed = false;
        bool RecursiveStepRejected = false;
        bool RecursiveFixedRejected = false;
        bool ClockRestoreRejected = false;
        bool LightingRestoreRejected = false;
        Keire::WeakRef<Keire::SceneRuntimeWorld> RecursiveWorld;

      protected:
        void FixedUpdate(const float deltaSeconds) override
        {
            ++FixedCalls;
            if (const auto world = RecursiveWorld.Lock())
            {
                try
                {
                    (void)world->Step(deltaSeconds);
                }
                catch (const std::logic_error&)
                {
                    RecursiveStepRejected = true;
                }
                try
                {
                    world->FixedUpdate(deltaSeconds);
                }
                catch (const std::logic_error&)
                {
                    RecursiveFixedRejected = true;
                }
                try
                {
                    using Environment = Keire::Detail::SceneRuntimeEnvironmentAccess;
                    Environment::Restore(*world, Environment::State(*world));
                }
                catch (const std::logic_error&)
                {
                    ClockRestoreRejected = true;
                }
                try
                {
                    using Environment = Keire::Detail::SceneRuntimeEnvironmentAccess;
                    Environment::Restore(*world, Environment::State(*world), Environment::Lighting(*world));
                }
                catch (const std::logic_error&)
                {
                    LightingRestoreRejected = true;
                }
            }
            if (ThrowOnFixed)
            {
                throw std::runtime_error("Deliberate world-step failure");
            }
        }

        void Update(float) override { ++UpdateCalls; }
    };

    class RuntimeWorldFixture final
    {
      public:
        RuntimeWorldFixture()
        {
            Keire::AssetSystemSpecification assetSpecification;
            assetSpecification.Mode = Keire::AssetMode::Development;
            assetSpecification.Decoders.push_back(Keire::CreateSceneAssetDecoder());
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
            Events = Keire::CreateRef<Keire::EventBus>();
            Scenes = Keire::CreateRef<Keire::SceneSystem>(
                Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, Assets, Events);
            World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
                Keire::SceneRuntimeWorldSpecification{.Scenes = Scenes, .Assets = Assets});
        }

        ~RuntimeWorldFixture()
        {
            World->Close();
            Scenes->Close();
            Assets->Close();
            Events->Close();
        }

        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession>
        Session(const std::string& name, Keire::EntityId* root = nullptr, Keire::EntityId* child = nullptr)
        {
            auto scene =
                Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(name));
            auto parent = scene->CreateEntity(name + " Root");
            REQUIRE(parent.AddTag(name));
            auto nested = scene->CreateEntity(name + " Child", parent);
            REQUIRE(nested.AddTag(name));
            if (root)
                *root = parent.Id();
            if (child)
                *child = nested.Id();
            auto result = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Assets);
            result->Play();
            REQUIRE(result->State() == Keire::ScenePlayState::Playing);
            return result;
        }

        Keire::Ref<Keire::AssetSystem> Assets;
        Keire::Ref<Keire::EventBus> Events;
        Keire::Ref<Keire::SceneSystem> Scenes;
        Keire::Ref<Keire::SceneRuntimeWorld> World;
    };
} // namespace

TEST_CASE("Runtime world pause lighting restore is isolated atomic and owner-thread bound")
{
    using Environment = Keire::Detail::SceneRuntimeEnvironmentAccess;
    RuntimeWorldFixture fixture;
    RuntimeWorldFixture independent;
    const Keire::Detail::EnvironmentLightingProfile lighting(
        Keire::Detail::EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -80.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 80.0, {1.0, 0.9, 0.8}, 12.0}},
        {Keire::ShadowQuality::Hard, Keire::ShadowResolutionHint::Low, 0.4F, 0.01F});
    const Keire::Detail::EnvironmentClockState time{1234567890123456789LL, 0.25};
    Environment::Restore(*fixture.World, time, lighting);
    CHECK(Environment::State(*fixture.World) == time);
    CHECK(Environment::Lighting(*fixture.World) == lighting);
    const auto snapshot = Environment::CaptureLighting(*fixture.World);
    REQUIRE(snapshot.Directional);
    CHECK(snapshot.Source);
    CHECK(snapshot.Source != Environment::CaptureLighting(*independent.World).Source);
    CHECK(snapshot.Revision == Environment::Revision(*fixture.World));
    CHECK(snapshot.Directional->Intensity == 6.0F);
    CHECK(snapshot.Directional->Shadows == lighting.Shadows());
    CHECK(Environment::Lighting(*independent.World).Shadows() == Keire::EnvironmentShadowSettings{});
    CHECK(Environment::Lighting(*independent.World).Ownership() ==
          Keire::Detail::EnvironmentDirectionalOwnership::Authored);
    const auto revision = Environment::Revision(*fixture.World);
    CHECK_THROWS_AS(Environment::Restore(*fixture.World, {0, 1.0}, Keire::Detail::EnvironmentLightingProfile{}),
                    std::invalid_argument);
    CHECK(Environment::State(*fixture.World) == time);
    CHECK(Environment::Lighting(*fixture.World) == lighting);
    CHECK(Environment::Revision(*fixture.World) == revision);
    std::atomic<int> rejected = 0;
    std::thread worker(
        [&]
        {
            try
            {
                (void)Environment::Lighting(*fixture.World);
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
            try
            {
                Environment::Restore(*fixture.World, {}, Keire::Detail::EnvironmentLightingProfile{});
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
        });
    worker.join();
    CHECK(rejected == 2);
    CHECK(Environment::Lighting(*fixture.World) == lighting);
    CHECK(Environment::Revision(*fixture.World) == revision);
    fixture.World->Close();
    CHECK_THROWS_AS((void)Environment::Lighting(*fixture.World), std::logic_error);
    CHECK_THROWS_AS((void)Environment::CaptureLighting(*fixture.World), std::logic_error);
    CHECK_THROWS_AS(Environment::Restore(*fixture.World, time, lighting), std::logic_error);
    CHECK(snapshot.Directional->Shadows == lighting.Shadows());
    CHECK_FALSE(Environment::CaptureLighting(*independent.World).Directional);
}

TEST_CASE("Runtime world pause environment clock is world-owned and advances once")
{
    using Environment = Keire::Detail::SceneRuntimeEnvironmentAccess;
    RuntimeWorldFixture fixture;
    RuntimeWorldFixture independent;
    auto state = Environment::State(*fixture.World);
    state.SecondsPerDay = 1.0;
    Environment::Restore(*fixture.World, state);
    const auto revision = Environment::Revision(*fixture.World);
    fixture.World->FixedUpdate(0.25F);
    CHECK(Environment::State(*fixture.World) == state);
    CHECK_FALSE(fixture.World->Step(0.25F));
    (void)fixture.World->Adopt(fixture.Session("Primary"));
    (void)fixture.World->Adopt(fixture.Session("Additive"));
    fixture.World->FixedUpdate(0.25F);
    CHECK(Environment::State(*fixture.World).DayFraction == 0.25);
    fixture.World->Update(0.25F);
    CHECK(Environment::State(*fixture.World).DayFraction == 0.25);
    CHECK(Environment::State(*independent.World).DayFraction == 0.0);
    fixture.World->Pause();
    fixture.World->FixedUpdate(0.25F);
    CHECK(Environment::State(*fixture.World).DayFraction == 0.25);
    REQUIRE(fixture.World->Step(0.25F));
    CHECK(Environment::State(*fixture.World).DayFraction == 0.5);
    CHECK(Environment::Revision(*fixture.World) == revision);

    state = Environment::State(*fixture.World);
    state.Paused = true;
    Environment::Restore(*fixture.World, state);
    REQUIRE(fixture.World->Step(0.25F));
    CHECK(Environment::State(*fixture.World) == state);
    state.Paused = false;
    state.Day = std::numeric_limits<std::int64_t>::max();
    state.DayFraction = 0.75;
    Environment::Restore(*fixture.World, state);
    CHECK_THROWS_AS((void)fixture.World->Step(0.5F), std::overflow_error);
    CHECK(Environment::State(*fixture.World) == state);
    REQUIRE(fixture.World->Step(0.125F));
    CHECK(Environment::State(*fixture.World).DayFraction == 0.875);

    const auto before = Environment::State(*fixture.World);
    const auto beforeRevision = Environment::Revision(*fixture.World);
    state.DayFraction = 1.0;
    CHECK_THROWS_AS(Environment::Restore(*fixture.World, state), std::invalid_argument);
    CHECK_THROWS_AS(fixture.World->FixedUpdate(-1.0F), std::invalid_argument);
    CHECK(Environment::State(*fixture.World) == before);
    CHECK(Environment::Revision(*fixture.World) == beforeRevision);
    std::atomic<int> rejected = 0;
    std::thread worker(
        [&]
        {
            try
            {
                (void)Environment::State(*fixture.World);
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
            try
            {
                Environment::Restore(*fixture.World, before);
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
        });
    worker.join();
    CHECK(rejected == 2);
    CHECK(Environment::State(*fixture.World) == before);
    fixture.World->Close();
    CHECK_THROWS_AS((void)Environment::State(*fixture.World), std::logic_error);
    CHECK_THROWS_AS(Environment::Restore(*fixture.World, before), std::logic_error);
    CHECK(before.DayFraction == 0.875);
}

TEST_CASE("Runtime world pause and step invoke every scene exactly once")
{
    RuntimeWorldFixture fixture;
    auto components = Keire::ComponentRegistry::CreateDefault();
    Keire::ComponentRegistration registration;
    registration.Type = WorldStepProbe::StaticType();
    registration.Name = "World step probe";
    registration.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<WorldStepProbe>()); };
    registration.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
    registration.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
    components->Register(std::move(registration));
    std::vector<Keire::Ref<WorldStepProbe>> probes;
    for (int index = 0; index < 2; ++index)
    {
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(),
                                                    components);
        auto entity = scene->CreateEntity("Tick probe");
        (void)entity.AddComponent<WorldStepProbe>();
        auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, fixture.Assets);
        session->Play();
        REQUIRE(session->State() == Keire::ScenePlayState::Playing);
        (void)fixture.World->Adopt(session);
        probes.push_back(session->RuntimeScene()->FindEntity(entity.Id()).GetComponent<WorldStepProbe>());
    }
    fixture.World->Pause();
    fixture.World->FixedUpdate(0.02F);
    fixture.World->Update(0.02F);
    for (const auto& probe : probes)
    {
        CHECK(probe->FixedCalls == 0);
        CHECK(probe->UpdateCalls == 0);
    }
    REQUIRE(fixture.World->Step(0.02F));
    for (const auto& probe : probes)
    {
        CHECK(probe->FixedCalls == 1);
        CHECK(probe->UpdateCalls == 1);
    }
    std::atomic_int rejected = 0;
    std::thread worker(
        [&]
        {
            try
            {
                fixture.World->Pause(false);
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
            try
            {
                (void)fixture.World->Step(0.02F);
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
        });
    worker.join();
    CHECK(rejected == 2);
    fixture.World->FixedUpdate(0.02F);
    for (const auto& probe : probes)
    {
        CHECK(probe->FixedCalls == 1);
    }
    fixture.World->Pause(false);
    fixture.World->FixedUpdate(0.02F);
    fixture.World->Update(0.02F);
    for (const auto& probe : probes)
    {
        CHECK(probe->FixedCalls == 2);
        CHECK(probe->UpdateCalls == 2);
    }
    fixture.World->Pause();
    probes.front()->RecursiveWorld = fixture.World;
    REQUIRE(fixture.World->Step(0.02F));
    CHECK(probes.front()->RecursiveStepRejected);
    CHECK(probes.front()->RecursiveFixedRejected);
    CHECK(probes.front()->ClockRestoreRejected);
    CHECK(probes.front()->LightingRestoreRejected);
    for (const auto& probe : probes)
    {
        CHECK(probe->FixedCalls == 3);
        CHECK(probe->UpdateCalls == 3);
    }
    probes.front()->ThrowOnFixed = true;
    using Environment = Keire::Detail::SceneRuntimeEnvironmentAccess;
    const auto beforeFault = Environment::State(*fixture.World);
    CHECK_FALSE(fixture.World->Step(0.02F));
    const auto afterFault = Environment::State(*fixture.World);
    CHECK(afterFault.DayFraction > beforeFault.DayFraction);
    CHECK(probes.front()->FixedCalls == 4);
    CHECK(probes.front()->UpdateCalls == 3);
    CHECK(probes.back()->FixedCalls == 4);
    CHECK(probes.back()->UpdateCalls == 4);
    CHECK_FALSE(fixture.World->Step(0.02F));
    CHECK(Environment::State(*fixture.World) == afterFault);
    CHECK(probes.back()->FixedCalls == 4);
    REQUIRE(fixture.World->Unload(fixture.World->LoadedScenes().front()));
    fixture.World->Process();
    CHECK(fixture.World->Step(0.02F));
    CHECK(probes.back()->FixedCalls == 5);
    CHECK(probes.back()->UpdateCalls == 5);
}

TEST_CASE("Runtime world pause and step coordinate additive sessions")
{
    RuntimeWorldFixture fixture;
    CHECK_FALSE(fixture.World->Step(0.02F));
    const auto primary = fixture.Session("PausePrimary");
    const auto additive = fixture.Session("PauseAdditive");
    (void)fixture.World->Adopt(primary);
    (void)fixture.World->Adopt(additive);
    CHECK_FALSE(fixture.World->Step(0.02F));
    fixture.World->Pause();
    CHECK(primary->State() == Keire::ScenePlayState::Paused);
    CHECK(additive->State() == Keire::ScenePlayState::Paused);
    CHECK(fixture.World->Step(0.02F));
    CHECK(primary->State() == Keire::ScenePlayState::Paused);
    CHECK(additive->State() == Keire::ScenePlayState::Paused);
    const auto later = fixture.Session("PauseLater");
    (void)fixture.World->Adopt(later);
    CHECK(later->State() == Keire::ScenePlayState::Paused);
    CHECK_THROWS_AS((void)fixture.World->Step(0.0F), std::invalid_argument);
    fixture.World->Pause(false);
    CHECK(primary->State() == Keire::ScenePlayState::Playing);
    CHECK(additive->State() == Keire::ScenePlayState::Playing);
    CHECK(later->State() == Keire::ScenePlayState::Playing);
    primary->Pause();
    CHECK_FALSE(fixture.World->Step(0.02F));
    fixture.World->Close();
    CHECK_THROWS_AS(fixture.World->Pause(), std::logic_error);
    CHECK_THROWS_AS((void)fixture.World->Step(0.02F), std::logic_error);
}

TEST_CASE("Runtime spatial ownership follows loaded and persistent scene lifetimes")
{
    using Keire::Detail::SceneRuntimeSpatialAccess;
    RuntimeWorldFixture fixture;
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    const auto rootFrame = frames.Find(SceneRuntimeSpatialAccess::RootFrame);
    CHECK(frames.AttachmentCount(rootFrame) == 0);

    Keire::EntityId persistentRoot;
    const auto persistentSession = fixture.Session("SpatialPersistent", &persistentRoot);
    const auto persistent = fixture.World->Adopt(persistentSession);
    const auto replacement = fixture.World->Adopt(fixture.Session("SpatialReplacement"));
    const auto transient = fixture.World->Adopt(fixture.Session("SpatialTransient"));
    CHECK(frames.AttachmentCount(rootFrame) == 3);
    REQUIRE(fixture.World->MakePersistent(persistentSession->RuntimeScene()->FindEntity(persistentRoot)));
    REQUIRE(fixture.World->SetActive(replacement));
    fixture.World->Process();
    REQUIRE(fixture.World->Unload(transient));
    fixture.World->Process();
    CHECK(frames.AttachmentCount(rootFrame) == 2);
    REQUIRE(fixture.World->Unload(persistent));
    fixture.World->Process();
    CHECK_FALSE(fixture.World->IsLoaded(persistent));
    CHECK(fixture.World->Session(persistent) == persistentSession);
    CHECK(frames.AttachmentCount(rootFrame) == 2);

    std::atomic_bool workerRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                (void)SceneRuntimeSpatialAccess::Frames(*fixture.World);
            }
            catch (const std::logic_error&)
            {
                workerRejected = true;
            }
        });
    worker.join();
    CHECK(workerRejected.load());

    auto retained = frames.Attach(rootFrame);
    fixture.World->Close();
    CHECK_FALSE(frames.IsLive(rootFrame));
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::Frames(*fixture.World), std::logic_error);
    CHECK_NOTHROW(retained.Reset());
    CHECK_NOTHROW(fixture.World->Close());

    RuntimeWorldFixture anotherWorld;
    const auto& anotherFrames = SceneRuntimeSpatialAccess::Frames(*anotherWorld.World);
    CHECK_FALSE(anotherFrames.IsLive(rootFrame));
    CHECK(anotherFrames.IsLive(anotherFrames.Find(SceneRuntimeSpatialAccess::RootFrame)));
}

TEST_CASE("Runtime spatial ownership resolves only entities in the current runtime scene instance")
{
    using Keire::Detail::SceneRuntimeSpatialAccess;
    RuntimeWorldFixture fixture;
    Keire::EntityId root;
    const auto session = fixture.Session("SpatialEntity", &root);
    const auto handle = fixture.World->Adopt(session);
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    const auto runtimeEntity = session->RuntimeScene()->FindEntity(root);
    const auto editEntity = session->EditScene()->FindEntity(root);
    REQUIRE(runtimeEntity);
    REQUIRE(editEntity);
    REQUIRE(runtimeEntity.Id() == editEntity.Id());
    CHECK(frames.IsLive(SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, runtimeEntity)));
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, editEntity), std::invalid_argument);
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, {}), std::invalid_argument);

    RuntimeWorldFixture other;
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*other.World, runtimeEntity),
                    std::invalid_argument);
    session->Stop();
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, runtimeEntity),
                    std::invalid_argument);
    const auto restartedEntity = session->RuntimeScene()->FindEntity(root);
    CHECK(frames.IsLive(SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, restartedEntity)));

    REQUIRE(fixture.World->Adopt(fixture.Session("SpatialOther")));
    REQUIRE(fixture.World->MakePersistent(restartedEntity));
    REQUIRE(fixture.World->Unload(handle));
    fixture.World->Process();
    CHECK_FALSE(fixture.World->IsLoaded(handle));
    CHECK(frames.IsLive(SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, restartedEntity)));
    REQUIRE(session->RuntimeScene()->DestroyEntity(root));
    session->RuntimeScene()->Update(0.0F);
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, restartedEntity),
                    std::invalid_argument);
    fixture.World->Close();
    CHECK_THROWS_AS((void)SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, restartedEntity), std::logic_error);
}

TEST_CASE("Scene runtime world exposes stable additive handles and explicit query scopes")
{
    RuntimeWorldFixture fixture;
    const auto firstSession = fixture.Session("First");
    const auto secondSession = fixture.Session("Second");
    const auto first = fixture.World->Adopt(firstSession);
    const auto second = fixture.World->Adopt(secondSession);

    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(fixture.World->Active() == first);
    CHECK(fixture.World->LoadedScenes() == std::vector<Keire::SceneHandle>{first, second});
    CHECK(fixture.World->QueryTag("First", Keire::SceneQueryScope::Active).size() == 2);
    CHECK(fixture.World->QueryTag("Second", Keire::SceneQueryScope::Active).empty());
    CHECK(fixture.World->QueryTag("Second", Keire::SceneQueryScope::Loaded).size() == 2);
    CHECK(fixture.World->QueryTag("Second", Keire::SceneQueryScope::Specific, second).size() == 2);
    CHECK(fixture.World->QueryTag("Second", Keire::SceneQueryScope::Specific, first).empty());

    REQUIRE(fixture.World->SetActive(second));
    CHECK(fixture.World->Active() == first);
    fixture.World->Process();
    CHECK(fixture.World->Active() == second);
    CHECK(fixture.World->QueryTag("Second", Keire::SceneQueryScope::Active).size() == 2);
}

TEST_CASE("Scene runtime world can adopt a stopped session before Play lifecycle callbacks")
{
    RuntimeWorldFixture fixture;
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Prepared"));
    const auto target = scene->CreateEntity("Query Target");
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, fixture.Assets);

    const auto handle = fixture.World->Adopt(session);
    REQUIRE(handle);
    CHECK(fixture.World->Active() == handle);
    CHECK(fixture.World->Asset(handle) == scene->Asset());
    CHECK_FALSE(fixture.World->Find(handle));

    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    REQUIRE(fixture.World->Find(handle) == session->RuntimeScene());
    const auto matches = fixture.World->QueryName("Query Target", Keire::SceneQueryScope::Active);
    REQUIRE(matches.size() == 1);
    CHECK(matches.front().Id() == target.Id());
}

TEST_CASE("Persistent scene objects retain their hierarchy identity when their loaded scene retires")
{
    RuntimeWorldFixture fixture;
    Keire::EntityId root;
    Keire::EntityId child;
    const auto session = fixture.Session("Persistent", &root, &child);
    const auto scene = session->RuntimeScene();
    const auto worldId = scene->FindEntity(root).World();
    const auto first = fixture.World->Adopt(session);
    const auto replacement = fixture.World->Adopt(fixture.Session("Replacement"));

    REQUIRE(fixture.World->MakePersistent(scene->FindEntity(child)));
    CHECK(fixture.World->IsPersistent(scene->FindEntity(root)));
    CHECK(fixture.World->IsPersistent(scene->FindEntity(child)));
    REQUIRE(fixture.World->SetActive(replacement));
    fixture.World->Process();
    REQUIRE(fixture.World->Unload(first));
    CHECK(fixture.World->IsLoaded(first));
    fixture.World->Process();

    CHECK_FALSE(fixture.World->IsLoaded(first));
    REQUIRE(fixture.World->Session(first) == session);
    REQUIRE(fixture.World->FindWorld(worldId) == scene);
    CHECK(scene->FindEntity(root).World() == worldId);
    CHECK(scene->FindEntity(child).Parent().Id() == root);
    CHECK(fixture.World->QueryTag("Persistent", Keire::SceneQueryScope::Loaded).empty());
    CHECK(fixture.World->QueryTag("Persistent", Keire::SceneQueryScope::Persistent).size() == 2);

    CHECK(replacement != first);
    CHECK(fixture.World->Active() == replacement);
    CHECK(fixture.World->QueryTag("Replacement", Keire::SceneQueryScope::Loaded).size() == 2);
    CHECK(fixture.World->QueryTag("Persistent", Keire::SceneQueryScope::Persistent).size() == 2);

    Keire::EntityId disposableRoot;
    const auto disposableSession = fixture.Session("Disposable", &disposableRoot);
    const auto disposableScene = disposableSession->RuntimeScene();
    const auto disposable = fixture.World->Adopt(disposableSession);
    REQUIRE(fixture.World->MakePersistent(disposableScene->FindEntity(disposableRoot)));
    REQUIRE(disposableScene->DestroyEntity(disposableRoot));
    disposableScene->Update(0.0F);
    REQUIRE(fixture.World->Unload(disposable));
    fixture.World->Process();
    CHECK_FALSE(fixture.World->Session(disposable));
}

TEST_CASE("Scene runtime world rejects invalid lifecycle and cross-thread mutations without changing state")
{
    RuntimeWorldFixture fixture;
    const auto first = fixture.World->Adopt(fixture.Session("Owner"));
    CHECK_THROWS_AS((void)fixture.World->Adopt({}), std::invalid_argument);
    CHECK(fixture.World->LoadedScenes() == std::vector<Keire::SceneHandle>{first});

    std::atomic_bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                (void)fixture.World->Unload(first);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected.load());
    CHECK(fixture.World->IsLoaded(first));
    CHECK(fixture.World->Active() == first);
    CHECK_FALSE(fixture.World->Unload(first));

    fixture.World->Close();
    CHECK_NOTHROW(fixture.World->Close());
    CHECK_FALSE(fixture.World->IsOpen());
    CHECK_FALSE(fixture.World->Active());
    CHECK_THROWS_AS((void)fixture.World->SetActive(first), std::logic_error);
}

TEST_CASE("Scene load polling and cancellation are thread-safe while scene service state stays owner-thread-affine")
{
    RuntimeWorldFixture fixture;
    const auto missing = Keire::AssetId::Generate();
    const auto operation = fixture.Scenes->Load(missing);
    int serviceRejections = 0;
    bool observedOpen = false;
    bool observedOperation = false;

    std::thread worker(
        [&]
        {
            observedOpen = fixture.Scenes->IsOpen();
            observedOperation = operation->Asset() == missing && operation->Mode() == Keire::SceneLoadMode::Single &&
                                operation->State() == Keire::SceneLoadState::Queued &&
                                operation->Diagnostic().Message.empty() && !operation->Result();
            operation->Cancel();
            const auto expectOwnerRejection = [&](auto&& action)
            {
                try
                {
                    action();
                }
                catch (const std::logic_error&)
                {
                    ++serviceRejections;
                }
            };
            expectOwnerRejection([&] { (void)fixture.Scenes->Load(Keire::AssetId::Generate()); });
            expectOwnerRejection([&] { (void)fixture.Scenes->Unload(missing); });
            expectOwnerRejection([&] { (void)fixture.Scenes->SetActive(missing); });
            expectOwnerRejection([&] { (void)fixture.Scenes->Active(); });
            expectOwnerRejection([&] { (void)fixture.Scenes->Find(missing); });
            expectOwnerRejection([&] { (void)fixture.Scenes->LoadedScenes(); });
            expectOwnerRejection([&] { (void)fixture.Scenes->Components(); });
            expectOwnerRejection([&] { fixture.Scenes->Close(); });
        });
    worker.join();

    CHECK(observedOpen);
    CHECK(observedOperation);
    CHECK(serviceRejections == 8);
    CHECK(operation->State() == Keire::SceneLoadState::Cancelled);
    CHECK(fixture.Scenes->IsOpen());
    CHECK_FALSE(fixture.Scenes->Active());
    CHECK_FALSE(fixture.Scenes->Find(missing));
    CHECK(fixture.Scenes->LoadedScenes().empty());
    CHECK(fixture.Scenes->Components());

    fixture.Scenes->Close();
    std::atomic_bool closedServiceRejectedWorker = false;
    std::thread closedWorker(
        [&]
        {
            try
            {
                fixture.Scenes->Close();
            }
            catch (const std::logic_error&)
            {
                closedServiceRejectedWorker = true;
            }
        });
    closedWorker.join();
    CHECK(closedServiceRejectedWorker.load());
}

TEST_CASE("standalone runtime frame routing interpolates character presentation without advancing physics")
{
    RuntimeWorldFixture fixture;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto player = scene->CreateEntity("Player");
    player.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
    auto child = scene->CreateEntity("Visual", player);
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
    auto physics =
        Keire::CreateRef<Keire::PhysicsSystem>(Keire::PhysicsSystemSpecification{.Mode = Keire::PhysicsMode::Enabled});
    auto session =
        Keire::CreateRef<Keire::SceneRuntimeSession>(scene, fixture.Assets, Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(fixture.World->Adopt(session));
    const auto runtimePlayer = session->RuntimeScene()->FindEntity(player.Id());
    const auto motor = runtimePlayer.GetComponent<Keire::CharacterControllerComponent>();
    const auto transform = runtimePlayer.GetComponent<Keire::TransformComponent>();
    const auto visual = session->RuntimeScene()->FindEntity(child.Id()).GetComponent<Keire::TransformComponent>();
    fixture.World->FixedUpdate(0.02F);
    const auto previous = transform->WorldPosition();
    REQUIRE(motor->QueueDesiredMovement({0.0F, 0.0F, 0.2F}));
    fixture.World->FixedUpdate(0.02F);
    const auto current = transform->WorldPosition();
    REQUIRE(current.Z > previous.Z);
    Keire::Time clock({.FixedDeltaTime = Keire::TimeStep::FromSeconds(0.02)});
    for (const double expectedAlpha : {0.25, 0.5, 0.75})
    {
        clock.AdvanceFrame(Keire::TimeStep::FromSeconds(0.005));
        REQUIRE(clock.PendingFixedSteps() == 0);
        REQUIRE(clock.InterpolationAlpha() == doctest::Approx(expectedAlpha));
        KeireRuntime::UpdateRuntimeWorldFrame(*fixture.World, clock);
        const auto expected = previous.Z + (current.Z - previous.Z) * static_cast<float>(expectedAlpha);
        CHECK(transform->PresentationWorldPosition().Z == doctest::Approx(expected));
        CHECK(visual->PresentationWorldPosition().Z == doctest::Approx(expected));
        CHECK(transform->WorldPosition().Z == doctest::Approx(current.Z));
    }
    transform->SetLocalPosition({0.0F, 1.0F, 10.0F});
    transform->ResetPresentationInterpolation();
    KeireRuntime::UpdateRuntimeWorldFrame(*fixture.World, clock);
    CHECK(transform->PresentationWorldPosition().Z == doctest::Approx(10.0F));
    fixture.World->Close();
    physics->Close();
    scene->Close();
}
