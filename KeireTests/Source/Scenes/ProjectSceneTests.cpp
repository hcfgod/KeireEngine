#include "KeireTests/TestSupport.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string_view>
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

    [[nodiscard]] std::string JsonString(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        result.push_back('"');
        for (const char character : value)
        {
            switch (character)
            {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(character);
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    [[nodiscard]] std::string ManagedReference(const Keire::AssetId id)
    {
        return "{\"Id\":{\"High\":" + std::to_string(id.High()) + ",\"Low\":" + std::to_string(id.Low()) + "}}";
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
    CHECK(std::filesystem::exists(created->Root() / "Assets/Shaders/DefaultUnlit.hlsl"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Shaders/DefaultUnlit.keireshader"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Materials/DefaultUnlit.keirematerial"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Project.keireproject"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Rendering.keiresettings"));
    auto rendering = Keire::LoadRenderEnvironmentSettings(created->Root());
    CHECK(rendering.AmbientIntensity == doctest::Approx(0.75F));
    rendering.AmbientColor = {0.1F, 0.2F, 0.3F, 1.0F};
    rendering.AmbientIntensity = 1.5F;
    rendering.Exposure = 1.25F;
    rendering.DirectionalShadowDistance = 250.0F;
    rendering.DirectionalShadowCascadeCount = 3;
    rendering.DirectionalShadowResolution = 4096;
    rendering.DirectionalShadowSplitLambda = 0.8F;
    Keire::SaveRenderEnvironmentSettings(created->Root(), rendering);
    CHECK(Keire::LoadRenderEnvironmentSettings(created->Root()) == rendering);
    for (int revision = 1; revision <= 16; ++revision)
    {
        rendering.AmbientIntensity = static_cast<float>(revision) * 0.25F;
        Keire::SaveRenderEnvironmentSettings(created->Root(), rendering);
        CHECK(Keire::LoadRenderEnvironmentSettings(created->Root()) == rendering);
    }
    rendering.Exposure = 0.0F;
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(created->Root(), rendering), std::invalid_argument);
    rendering.Exposure = 1.0F;
    rendering.DirectionalShadowResolution = 3000;
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(created->Root(), rendering), std::invalid_argument);
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
    CHECK(scene->ObjectCount() == 5);
    CHECK_THROWS_AS((void)scene->ReparentObject(root.Id(), child.Id()), std::invalid_argument);
    CHECK(scene->RenameObject(child.Id(), "Renamed"));
    const auto duplicate = scene->DuplicateObject(root.Id());
    REQUIRE(duplicate);
    CHECK(scene->ObjectCount() == 7);
    const auto duplicateChildren =
        std::ranges::count(scene->Objects(), duplicate.Id(), &Keire::SceneObjectDefinition::Parent);
    CHECK(duplicateChildren == 1);
    CHECK(scene->DestroyObject(duplicate.Id()));
    REQUIRE(child.Snapshot());
    CHECK(child.Snapshot()->Name == "Renamed");
    CHECK(scene->DestroyObject(root.Id()));
    CHECK_FALSE(child);
    CHECK(scene->ObjectCount() == 3);
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

TEST_CASE("Scene import discovers deterministic authored and managed asset dependencies")
{
    const auto graph = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    const auto skeleton = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000002");
    const auto skinnedMesh = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000003");
    const auto avatarMask = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000004");
    const auto collisionMesh = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000005");
    const auto physicsMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000006");
    const auto clip = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000007");
    const auto mixer = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000008");
    const auto effect = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000009");
    const auto managedDirect = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000a");
    const auto managedArray = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000b");
    const auto overrideAsset = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000c");
    const auto addedEffect = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000d");
    const auto prefab = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000e");
    const auto ignoredEntity = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000f");

    const std::string managedState =
        "{\"Version\":1,\"Fields\":["
        "{\"StableId\":\"20000000-0000-4000-8000-000000000001\",\"Name\":\"Definition\","
        "\"Type\":\"Keire.AssetReference`1[[Game.Definition, Game]]\",\"Aliases\":[],\"Value\":" +
        ManagedReference(managedDirect) +
        "},"
        "{\"StableId\":\"20000000-0000-4000-8000-000000000002\",\"Name\":\"Effects\","
        "\"Type\":\"Keire.AssetReference`1[[Keire.VfxEffect, Keire.Managed]][]\",\"Aliases\":[],\"Value\":[" +
        ManagedReference(managedArray) + "," + ManagedReference(effect) +
        "]},"
        "{\"StableId\":\"20000000-0000-4000-8000-000000000003\",\"Name\":\"Target\","
        "\"Type\":\"Keire.Entity\",\"Aliases\":[],\"Value\":" +
        ManagedReference(ignoredEntity) + "}]}";

    auto definition = Keire::SceneAsset::EmptyDefinition("Authored Dependencies");
    Keire::SceneObjectDefinition object;
    object.Id = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001");
    object.Name = "Authoring";
    object.Components = {
        {Keire::AnimatorComponent::StaticType(), 1, true,
         "{\"graph\":" + JsonString(graph.ToString()) + ",\"skeleton\":" + JsonString(skeleton.ToString()) +
             ",\"skinnedMesh\":" + JsonString(skinnedMesh.ToString()) + ",\"avatarMasks\":[" +
             JsonString(avatarMask.ToString()) + "," + JsonString(avatarMask.ToString()) + "]}"},
        {Keire::ColliderComponent::StaticType(), 2, true,
         "{\"collisionMesh\":" + JsonString(collisionMesh.ToString()) +
             ",\"physicsMaterial\":" + JsonString(physicsMaterial.ToString()) + "}"},
        {Keire::AudioSourceComponent::StaticType(), 2, true,
         "{\"clip\":" + JsonString(clip.ToString()) + ",\"mixer\":" + JsonString(mixer.ToString()) + "}"},
        {Keire::VfxEmitterComponent::StaticType(), 1, true, "{\"effect\":" + JsonString(effect.ToString()) + "}"},
        {Keire::ComponentTypeId::Parse("40000000-0000-4000-8000-000000000001"), 1, true,
         "{\"managedState\":" + JsonString(managedState) + "}"},
    };
    definition.Objects.push_back(object);
    definition.PrefabInstances.push_back(
        {.Prefab = prefab,
         .Root = object.Id,
         .Objects = {{Keire::AssetId::Parse("50000000-0000-4000-8000-000000000001"), object.Id}}});
    definition.PrefabOverrides.push_back({.Kind = Keire::PrefabOverrideKind::SetComponentProperty,
                                          .Object = object.Id,
                                          .Component = Keire::VfxEmitterComponent::StaticType(),
                                          .Property = "effect",
                                          .Value = overrideAsset});
    definition.PrefabOverrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::AddComponent,
         .Object = object.Id,
         .AddedComponent = Keire::SceneComponentDefinition{Keire::VfxEmitterComponent::StaticType(), 1, true,
                                                           "{\"effect\":" + JsonString(addedEffect.ToString()) + "}"}});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto first = importer.ContextualImport({}, Keire::SceneAsset::Encode(definition));
    const auto second = importer.ContextualImport({}, Keire::SceneAsset::Encode(definition));
    auto expected = std::vector{graph, skeleton, skinnedMesh,   avatarMask,   collisionMesh, physicsMaterial, clip,
                                mixer, effect,   managedDirect, managedArray, overrideAsset, addedEffect,     prefab};
    std::ranges::sort(expected);
    CHECK(first.AssetDependencies == expected);
    CHECK(second.AssetDependencies == expected);
    CHECK(std::ranges::find(first.AssetDependencies, ignoredEntity) == first.AssetDependencies.end());
}

TEST_CASE("A rooted scene cook includes managed data dependency closure")
{
    TemporaryDirectory directory("SceneManagedDependencyCook");
    std::filesystem::create_directories(directory.Path / "Assets");

    const auto leafType = Keire::AssetTypeId::Parse("60000000-0000-4000-8000-000000000001");
    Keire::AssetImporterRegistration leafImporter;
    leafImporter.Name = "Test.ManagedDependencyLeaf";
    leafImporter.Type = leafType;
    leafImporter.Extensions = {".leaf"};
    leafImporter.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };

    Keire::AssetDatabaseSpecification specification{.ProjectRoot = directory.Path};
    specification.Importers = {leafImporter, Keire::CreateManagedDataAssetImporter(),
                               Keire::CreateSceneAssetImporter()};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
    const std::string leafSource = "managed dependency";
    const auto leaf = database->CreateAsset("Data/Dependency.leaf", leafImporter,
                                            std::as_bytes(std::span(leafSource.data(), leafSource.size())));
    const auto unrelated = database->CreateAsset("Data/Unrelated.leaf", leafImporter,
                                                 std::as_bytes(std::span(leafSource.data(), leafSource.size())));

    Keire::ManagedDataDefinition managedDefinition;
    managedDefinition.ManagedType = Keire::ManagedTypeId::Parse("70000000-0000-4000-8000-000000000001");
    managedDefinition.ManagedTypeName = "Game.SceneSettings";
    managedDefinition.Dependencies = {{.Asset = leaf, .AssetType = leafType}};
    const auto managedBytes = Keire::ManagedDataAsset::Encode(managedDefinition);
    const auto managed =
        database->CreateAsset("Data/SceneSettings.keiredata", Keire::CreateManagedDataAssetImporter(), managedBytes);

    const std::string state =
        "{\"Version\":1,\"Fields\":["
        "{\"StableId\":\"80000000-0000-4000-8000-000000000001\",\"Name\":\"Settings\","
        "\"Type\":\"Keire.AssetReference`1[[Game.SceneSettings, Game]]\",\"Aliases\":[],\"Value\":" +
        ManagedReference(managed) + "}]}";
    auto sceneDefinition = Keire::SceneAsset::EmptyDefinition("Managed Root");
    sceneDefinition.Objects.push_back(
        {.Id = Keire::AssetId::Parse("90000000-0000-4000-8000-000000000001"),
         .Name = "Root",
         .Components = {{Keire::ComponentTypeId::Parse("90000000-0000-4000-8000-000000000002"), 1, true,
                         "{\"managedState\":" + JsonString(state) + "}"}}});
    const auto sceneBytes = Keire::SceneAsset::Encode(sceneDefinition);
    const auto scene =
        database->CreateAsset("Scenes/ManagedRoot.keirescene", Keire::CreateSceneAssetImporter(), sceneBytes);

    Keire::AssetBuildProfile profile;
    profile.Roots = {scene};
    const auto first = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookA");
    const auto second = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookB");
    CHECK(first.AssetCount == 3);
    CHECK(KeireTests::ReadFile(first.CatalogPath) == KeireTests::ReadFile(second.CatalogPath));
    const auto catalog = KeireTests::ReadFile(first.CatalogPath);
    CHECK(catalog.find(scene.ToString()) != std::string::npos);
    CHECK(catalog.find(managed.ToString()) != std::string::npos);
    CHECK(catalog.find(leaf.ToString()) != std::string::npos);
    CHECK(catalog.find(unrelated.ToString()) == std::string::npos);
}

TEST_CASE("Scene entity moves preserve hierarchy order and reject invalid insertion targets")
{
    Keire::SceneDefinition definition;
    definition.Name = "Ordering";
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), std::move(definition));
    const auto first = scene->CreateEntity("First");
    const auto second = scene->CreateEntity("Second");
    const auto third = scene->CreateEntity("Third");
    const auto child = scene->CreateEntity("Child", first);

    scene->MoveEntity(third.Id(), {}, first.Id());
    auto objects = scene->Objects();
    REQUIRE(objects.size() == 4);
    CHECK(objects[0].Id == third.Id().Value());
    CHECK(objects[1].Id == first.Id().Value());
    CHECK(objects[2].Id == child.Id().Value());
    CHECK(objects[3].Id == second.Id().Value());

    scene->MoveEntity(second.Id(), first.Id(), child.Id());
    objects = scene->Objects();
    REQUIRE(objects.size() == 4);
    CHECK(objects[0].Id == third.Id().Value());
    CHECK(objects[1].Id == first.Id().Value());
    CHECK(objects[2].Id == second.Id().Value());
    CHECK(objects[3].Id == child.Id().Value());
    CHECK(objects[2].Parent == first.Id().Value());

    scene->MoveEntity(second.Id());
    objects = scene->Objects();
    CHECK(objects.back().Id == second.Id().Value());
    CHECK_FALSE(objects.back().Parent);
    CHECK_THROWS_AS(scene->MoveEntity(first.Id(), child.Id()), std::invalid_argument);
    CHECK_THROWS_AS(scene->MoveEntity(third.Id(), {}, child.Id()), std::invalid_argument);
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

TEST_CASE("Newer scene importers upgrade older metadata revisions but reject future revisions")
{
    TemporaryDirectory directory("SceneImporterUpgradeTests");
    const auto sourceDirectory = directory.Path / "Assets";
    std::filesystem::create_directories(sourceDirectory);
    const auto source = sourceDirectory / "Legacy.keirescene";
    const auto metadata = sourceDirectory / "Legacy.keirescene.keiremeta";
    const auto sourceBytes = Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Legacy"));
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(sourceBytes.data()),
                     static_cast<std::streamsize>(sourceBytes.size()));
        REQUIRE(stream.good());
    }
    const auto writeMetadata = [&](const std::uint32_t importerVersion)
    {
        std::ofstream stream(metadata, std::ios::binary | std::ios::trunc);
        stream << "{\n"
                  "  \"schemaVersion\": 1,\n"
                  "  \"id\": \"11111111-1111-4111-8111-111111111111\",\n"
                  "  \"type\": \"4b454952-4553-4345-4e45-415353455401\",\n"
                  "  \"importer\": \"Keire.Scene\",\n"
                  "  \"importerVersion\": "
               << importerVersion << ",\n  \"dependencies\": [],\n  \"subAssets\": []\n}\n";
        REQUIRE(stream.good());
    };
    writeMetadata(2);
    Keire::AssetDatabaseSpecification specification{.ProjectRoot = directory.Path};
    specification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(specification);
    CHECK_NOTHROW((void)database->ImportAll());

    writeMetadata(4);
    database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
    CHECK_THROWS_WITH_AS((void)database->ImportAll(),
                         "No compatible importer is registered for asset: Legacy.keirescene", std::runtime_error);
}
