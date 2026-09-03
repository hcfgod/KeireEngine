#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{
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
