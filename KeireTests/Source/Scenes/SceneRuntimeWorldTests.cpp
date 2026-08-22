#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <atomic>
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
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
            Scenes = Keire::CreateRef<Keire::SceneSystem>(
                Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, Assets);
            World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
                Keire::SceneRuntimeWorldSpecification{.Scenes = Scenes, .Assets = Assets});
        }

        ~RuntimeWorldFixture()
        {
            World->Close();
            Scenes->Close();
            Assets->Close();
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
