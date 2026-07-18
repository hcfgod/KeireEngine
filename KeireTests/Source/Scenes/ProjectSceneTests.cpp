#include "KeireTests/TestSupport.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <thread>
#include <utility>

namespace
{
    struct TemporaryDirectory final
    {
        explicit TemporaryDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    void UseDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    struct SceneProbe final
    {
        Keire::AssetId First;
        Keire::AssetId Second;
        bool SingleReady = false;
        bool AdditiveReady = false;
        bool ActiveChanged = false;
        bool FailedLoadPreservedActive = false;
        int ActiveChangeEvents = 0;
    };

    class SceneProbeLayer final : public Keire::Layer
    {
      public:
        explicit SceneProbeLayer(std::shared_ptr<SceneProbe> probe)
            : Keire::Layer("SceneProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override
        {
            Listen<Keire::ActiveSceneChangedEvent>(
                [this](const auto&)
                {
                    ++m_Probe->ActiveChangeEvents;
                    return Keire::EventFlow::Continue;
                });
            m_First = Owner().Scenes()->Load(m_Probe->First);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!m_Probe->SingleReady && m_First->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->SingleReady = Owner().Scenes()->Active()->Asset() == m_Probe->First;
                m_Second = Owner().Scenes()->Load(m_Probe->Second, Keire::SceneLoadMode::Additive);
                return;
            }
            if (m_Second && m_Second->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->AdditiveReady = Owner().Scenes()->LoadedScenes().size() == 2;
                REQUIRE(Owner().Scenes()->SetActive(m_Probe->Second));
                m_Second.Reset();
                return;
            }
            if (m_Probe->AdditiveReady && !m_Missing && Owner().Scenes()->Active()->Asset() == m_Probe->Second)
            {
                m_Probe->ActiveChanged = true;
                m_Missing = Owner().Scenes()->Load(Keire::AssetId::Generate());
                return;
            }
            if (m_Missing && m_Missing->State() == Keire::SceneLoadState::Failed)
            {
                m_Probe->FailedLoadPreservedActive = Owner().Scenes()->Active()->Asset() == m_Probe->Second &&
                                                     Owner().Scenes()->LoadedScenes().size() == 2;
                Owner().RequestExit();
            }
        }

      private:
        std::shared_ptr<SceneProbe> m_Probe;
        Keire::Ref<Keire::SceneLoadOperation> m_First;
        Keire::Ref<Keire::SceneLoadOperation> m_Second;
        Keire::Ref<Keire::SceneLoadOperation> m_Missing;
    };

    class SceneProbeApplication final : public Keire::Application
    {
      public:
        SceneProbeApplication(Keire::ApplicationSpecification specification, std::shared_ptr<SceneProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<SceneProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<SceneProbe> m_Probe;
    };
} // namespace

TEST_CASE("Projects create isolated starter assets and hold exclusive editor locks")
{
    TemporaryDirectory directory("ProjectTests");
    const auto created = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Starter});
    REQUIRE(created);
    CHECK(created->Descriptor().Name == "Game");
    CHECK(created->Descriptor().DefaultInput);
    CHECK(created->Descriptor().StartupScene);
    CHECK(std::filesystem::exists(created->Root() / "Assets/Input/DefaultInput.keireinput"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Scenes/SampleScene.keirescene"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Project.keireproject"));
    CHECK(Keire::Project::Inspect(created->Root()) == Keire::ProjectStatus::Ready);

    const auto exclusive = Keire::Project::Open(created->Root(), Keire::ProjectOpenMode::Exclusive);
    CHECK(Keire::Project::IsLocked(created->Root()));
    CHECK_THROWS_AS((void)Keire::Project::Open(created->Root(), Keire::ProjectOpenMode::Exclusive), std::runtime_error);

    auto descriptor = exclusive->Descriptor();
    descriptor.Name = "Game Renamed";
    exclusive->Save(descriptor);
    CHECK(exclusive->Descriptor().Name == "Game Renamed");

    const auto registryPath = directory.Path / "Registry/projects.json";
    auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
    registry->RecordOpened(*exclusive);
    REQUIRE(registry->Entries().size() == 1);
    CHECK(registry->Entries().front().Status == Keire::ProjectStatus::InUse);
    CHECK(registry->SetPinned(exclusive->Descriptor().Id, true));
    CHECK(registry->Entries().front().Pinned);
    CHECK(registry->Remove(exclusive->Descriptor().Id));
    CHECK(registry->Entries().empty());

    CHECK_THROWS_AS((void)Keire::Project::Create({directory.Path, "../Unsafe", Keire::ProjectTemplate::Empty}),
                    std::invalid_argument);
    const auto corruptRegistry = directory.Path / "Registry/corrupt.json";
    std::filesystem::create_directories(corruptRegistry.parent_path());
    {
        std::ofstream output(corruptRegistry);
        output << "not json";
    }
    const auto recoveredRegistry = Keire::CreateRef<Keire::ProjectRegistry>(corruptRegistry);
    CHECK(recoveredRegistry->Entries().empty());
}

TEST_CASE("Project registry preserves UTF-8 paths across save and reload")
{
    TemporaryDirectory directory("ProjectUtf8Tests");
    const auto unicodeParent = directory.Path / std::filesystem::path(u8"Kéire Projects");
    std::filesystem::create_directories(unicodeParent);
    const auto project = Keire::Project::Create({unicodeParent, "Sandbox", Keire::ProjectTemplate::Empty});
    const auto registryPath = directory.Path / "Registry/projects.json";

    {
        const auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
        registry->RecordOpened(*project);
    }

    const auto reloaded = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
    REQUIRE(reloaded->Entries().size() == 1);
    CHECK(reloaded->Entries().front().Root == project->Root());
    CHECK(reloaded->Entries().front().Status == Keire::ProjectStatus::Ready);
}

TEST_CASE("Scene assets and mutable scenes preserve validated hierarchy ordering")
{
    const auto definition = Keire::SceneAsset::SampleDefinition();
    const auto encoded = Keire::SceneAsset::Encode(definition);
    const auto decoded = Keire::SceneAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(Keire::SceneAsset::Encode(decoded->Definition()) == encoded);

    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), definition);
    const auto root = scene->CreateObject("Root");
    REQUIRE(root);
    const auto child = scene->CreateObject("Child", root.Id());
    REQUIRE(child);
    CHECK(scene->ObjectCount() == 4);
    CHECK_THROWS_AS((void)scene->ReparentObject(root.Id(), child.Id()), std::invalid_argument);
    CHECK(scene->RenameObject(child.Id(), "Renamed"));
    const auto duplicate = scene->DuplicateObject(root.Id());
    REQUIRE(duplicate);
    CHECK(scene->ObjectCount() == 6);
    const auto duplicateChildren =
        std::ranges::count(scene->Objects(), duplicate.Id(), &Keire::SceneObjectDefinition::Parent);
    CHECK(duplicateChildren == 1);
    CHECK(scene->DestroyObject(duplicate.Id()));
    REQUIRE(child.Snapshot());
    CHECK(child.Snapshot()->Name == "Renamed");
    CHECK(scene->DestroyObject(root.Id()));
    CHECK_FALSE(child);
    CHECK(scene->ObjectCount() == 2);
    std::atomic_bool rejectedOffThread = false;
    std::jthread worker(
        [&]
        {
            try
            {
                (void)scene->CreateObject("Wrong Thread");
            }
            catch (const std::logic_error&)
            {
                rejectedOffThread = true;
            }
        });
    worker.join();
    CHECK(rejectedOffThread);
    scene->Close();
    CHECK_FALSE(root);
}

TEST_CASE("Application scene system activates single and additive scene loads at frame boundaries")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneSystemTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    auto firstDefinition = Keire::SceneAsset::EmptyDefinition("First");
    auto secondDefinition = Keire::SceneAsset::EmptyDefinition("Second");
    const auto first = database->CreateAsset("First.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(firstDefinition));
    const auto second = database->CreateAsset("Second.keirescene", Keire::CreateSceneAssetImporter(),
                                              Keire::SceneAsset::Encode(secondDefinition));
    const auto catalog = database->ImportAll().CatalogPath;

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<SceneProbe>();
    probe->First = first;
    probe->Second = second;
    SceneProbeApplication application(std::move(specification), probe);
    CHECK(application.Run() == 0);
    CHECK(probe->SingleReady);
    CHECK(probe->AdditiveReady);
    CHECK(probe->ActiveChanged);
    CHECK(probe->FailedLoadPreservedActive);
    CHECK(probe->ActiveChangeEvents == 2);
}
