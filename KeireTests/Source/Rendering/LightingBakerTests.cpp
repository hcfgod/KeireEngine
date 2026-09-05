#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    class TemporaryLightingProject final
    {
      public:
        TemporaryLightingProject() : Root(KeireTests::MakeTestDirectory("LightingBaker"))
        {
            std::filesystem::create_directories(Root);
        }

        ~TemporaryLightingProject()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        std::filesystem::path Root;
    };

    [[nodiscard]] Keire::SceneDefinition LightingScene(const Keire::AssetId sceneId)
    {
        auto scene = Keire::CreateRef<Keire::Scene>(sceneId, Keire::SceneAsset::EmptyDefinition("Bake Test"));

        auto rendererEntity = scene->CreateEntity("Static Renderer");
        auto renderer = rendererEntity.AddComponent<Keire::MeshRendererComponent>();
        renderer->SetStaticLighting(true);
        renderer->SetGIReceive(Keire::GIReceiveMode::Lightmaps);

        auto directionalEntity = scene->CreateEntity("Mixed Sun");
        auto directional = directionalEntity.AddComponent<Keire::DirectionalLightComponent>();
        directional->SetBakeMode(Keire::LightBakeMode::Mixed);
        directional->SetIntensity(2.0F);

        auto reflectionEntity = scene->CreateEntity("Reflection Probe");
        auto reflection = reflectionEntity.AddComponent<Keire::ReflectionProbeComponent>();
        reflection->SetResolution(Keire::ReflectionProbeResolution::Size64);

        auto volumeEntity = scene->CreateEntity("Probe Volume");
        auto volume = volumeEntity.AddComponent<Keire::LightProbeVolumeComponent>();
        volume->SetBoxExtents({1.0F, 1.0F, 1.0F});
        volume->SetSpacing({1.0F, 1.0F, 1.0F});

        auto definition = scene->Snapshot();
        definition.Lighting.Backend = Keire::LightingBakeBackend::Automatic;
        definition.Lighting.LightmapResolution = 64;
        definition.Lighting.MaximumLightmapResolution = 64;
        definition.Lighting.SamplesPerTexel = 1;
        scene->Close();
        return definition;
    }
} // namespace

TEST_CASE("lighting baker publishes deterministic assets and reuses its disk cache")
{
    TemporaryLightingProject project;
    const auto sceneId = Keire::AssetId::Parse("9b000000-0000-4000-8000-000000000001");
    Keire::LightingBakeRequest request;
    request.Scene = sceneId;
    request.Definition = LightingScene(sceneId);
    request.ProjectRoot = project.Root;

    const auto first = Keire::LightingBaker::Bake(request);
    CHECK_FALSE(first.CacheHit);
    CHECK(first.Backend == Keire::LightingBakeBackend::CPU);
    CHECK(first.InputFingerprint.size() == 64);
    CHECK(first.LightingSet);
    CHECK(first.Assets.size() == 6);
    for (const auto& asset : first.Assets)
    {
        CHECK(std::filesystem::is_regular_file(project.Root / asset.RelativePath));
        auto metadata = project.Root / asset.RelativePath;
        metadata += ".keiremeta";
        CHECK(std::filesystem::is_regular_file(metadata));
    }

    const auto second = Keire::LightingBaker::Bake(request);
    CHECK(second.CacheHit);
    CHECK(second.LightingSet == first.LightingSet);
    CHECK(second.InputFingerprint == first.InputFingerprint);

    const auto cachedLightmap =
        project.Root / "Library/LightingCache" / first.InputFingerprint / "Lightmaps.keirelightingtexture";
    {
        std::ofstream corrupted(cachedLightmap, std::ios::binary | std::ios::trunc);
        REQUIRE(corrupted);
        corrupted << "corrupt";
    }
    const auto repaired = Keire::LightingBaker::Bake(request);
    CHECK_FALSE(repaired.CacheHit);
    CHECK(std::filesystem::file_size(cachedLightmap) > 7U);

    request.Definition.BakedLighting = first.LightingSet;
    CHECK(Keire::LightingBaker::Fingerprint(request) == first.InputFingerprint);
}

TEST_CASE("lighting bake fingerprint changes with dependency or quality inputs")
{
    TemporaryLightingProject project;
    const auto sceneId = Keire::AssetId::Parse("9b000000-0000-4000-8000-000000000002");
    Keire::LightingBakeRequest request;
    request.Scene = sceneId;
    request.Definition = LightingScene(sceneId);
    request.ProjectRoot = project.Root;
    const auto baseline = Keire::LightingBaker::Fingerprint(request);
    request.Inputs.push_back({Keire::AssetId::Parse("9b000000-0000-4000-8000-000000000003"), std::string(64, 'a')});
    CHECK(Keire::LightingBaker::Fingerprint(request) != baseline);
    request.Inputs.clear();
    request.Definition.Lighting.SamplesPerTexel = 2;
    CHECK(Keire::LightingBaker::Fingerprint(request) != baseline);
}

TEST_CASE("lighting bake publishes stable metadata before sources become visible to asset scans")
{
    TemporaryLightingProject project;
    const auto sceneId = Keire::AssetId::Generate();
    Keire::LightingBakeRequest request;
    request.Scene = sceneId;
    request.Definition = LightingScene(sceneId);
    request.ProjectRoot = project.Root;
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .Importers = Keire::CreateBuiltinAssetImporters()});
    bool sawPreparedMetadata = false;
    request.Progress = [&](const Keire::LightingBakeProgress& progress)
    {
        if (progress.Phase != Keire::LightingBakePhase::Publishing)
            return;
        if (progress.Completed == 0)
        {
            std::size_t metadataCount = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(project.Root / "Assets"))
            {
                if (!entry.is_regular_file())
                    continue;
                CHECK(entry.path().extension() == ".keiremeta");
                ++metadataCount;
            }
            CHECK(metadataCount == progress.Total);
            sawPreparedMetadata = true;
        }
        CHECK(database->Refresh() == progress.Completed);
    };
    const auto baked = Keire::LightingBaker::Bake(request);
    CHECK(sawPreparedMetadata);
    REQUIRE(baked.Assets.size() == 6);
    for (const auto& output : baked.Assets)
    {
        const auto record = database->Find(output.RelativePath.lexically_relative("Assets"));
        REQUIRE(record);
        CHECK(record->Id == output.Id);
        CHECK(record->Type == output.Type);
    }
    CHECK_NOTHROW(database->ImportAll());
    database.Reset();
}
