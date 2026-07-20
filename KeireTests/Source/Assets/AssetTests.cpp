#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Log.h"
#include "KeireTests/TestSupport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    class TemporaryAssetProject final
    {
      public:
        TemporaryAssetProject()
            : Root(std::filesystem::absolute(std::filesystem::path("Build") /
                                             ("AssetTests-" + Keire::AssetId::Generate().ToString())))
        {
            std::filesystem::create_directories(Root / "Assets");
        }

        ~TemporaryAssetProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        void Write(const std::filesystem::path& relative, const std::string_view content) const
        {
            const auto path = Root / "Assets" / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            REQUIRE(stream.good());
        }

        std::filesystem::path Root;
    };

    template <typename Predicate> void WaitFor(Keire::AssetSystem& assets, Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            (void)assets.PumpCompletions();
        }
        REQUIRE(predicate());
    }

    [[nodiscard]] std::vector<char> ReadAll(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("Asset identifiers are stable canonical 128-bit values")
{
    const auto id = Keire::AssetId::Generate();
    CHECK(id);
    CHECK(Keire::AssetId::Parse(id.ToString()) == id);
    CHECK_THROWS_AS((void)Keire::AssetId::Parse("not-an-asset-id"), std::invalid_argument);
}

TEST_CASE("Asset database preserves metadata identities and produces validated deterministic packs")
{
    TemporaryAssetProject project;
    project.Write("Greeting.txt", "hello assets");
    project.Write("Payload.bin", std::string("\0\1\2", 3));

    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto first = database->Records();
    REQUIRE(first.size() == 2);
    CHECK(std::filesystem::exists(project.Root / "Assets/Greeting.txt.keiremeta"));
    CHECK(database->Refresh() == 2);
    const auto second = database->Records();
    CHECK(second[0].Id == first[0].Id);
    CHECK(second[1].Id == first[1].Id);

    const auto imported = database->ImportAll();
    CHECK(imported.Imported == 2);
    CHECK(imported.CacheHits == 0);
    CHECK(std::filesystem::exists(imported.CatalogPath));
    CHECK_NOTHROW(Keire::AssetCooker::Validate(imported.CatalogPath));
    const auto catalogBytes = ReadAll(imported.CatalogPath);
    const std::string catalog(catalogBytes.begin(), catalogBytes.end());
    CHECK(catalog.find("3b8b6ce74adb7b04f735540db6d8f175935aa33f22b03d02115e1a3dd10434db") != std::string::npos);
    const auto firstCook = Keire::AssetCooker::Cook(*database, {}, project.Root / "CookA");
    const auto secondCook = Keire::AssetCooker::Cook(*database, {}, project.Root / "CookB");
    CHECK(ReadAll(firstCook.CatalogPath) == ReadAll(secondCook.CatalogPath));
    CHECK(ReadAll(firstCook.CatalogPath.parent_path() / "content-0.keirepak") ==
          ReadAll(secondCook.CatalogPath.parent_path() / "content-0.keirepak"));
    const auto greeting = database->Find("Greeting.txt");
    REQUIRE(greeting);
    Keire::AssetBuildProfile rootedProfile;
    rootedProfile.Roots = {greeting->Id};
    const auto rootedCook = Keire::AssetCooker::Cook(*database, rootedProfile, project.Root / "CookRooted");
    CHECK(rootedCook.AssetCount == 1);
    const auto rootedCatalogBytes = ReadAll(rootedCook.CatalogPath);
    const std::string rootedCatalog(rootedCatalogBytes.begin(), rootedCatalogBytes.end());
    CHECK(rootedCatalog.find(greeting->Id.ToString()) != std::string::npos);
    rootedProfile.Roots = {Keire::AssetId::Generate()};
    CHECK_THROWS_AS((void)Keire::AssetCooker::Cook(*database, rootedProfile, project.Root / "CookMissing"),
                    std::runtime_error);
    const auto cached = database->ImportAll();
    CHECK(cached.Imported == 0);
    CHECK(cached.CacheHits == 2);

    database->CreateFolder("Generated/Subfolder");
    CHECK(std::filesystem::is_directory(project.Root / "Assets/Generated/Subfolder"));
    const auto original = database->Find("Greeting.txt");
    REQUIRE(original);
    const auto duplicateId = database->Duplicate(original->Id, "Greeting Copy.txt");
    CHECK(duplicateId != original->Id);
    CHECK(std::filesystem::exists(project.Root / "Assets/Greeting Copy.txt.keiremeta"));
    database->Rename(duplicateId, "Greeting Renamed.txt");
    REQUIRE(database->Find(duplicateId));
    CHECK(database->Find(duplicateId)->RelativePath == std::filesystem::path("Greeting Renamed.txt"));
    const auto trash = database->MoveToTrash(duplicateId);
    CHECK(std::filesystem::exists(trash / "Greeting Renamed.txt"));
    CHECK(std::filesystem::exists(trash / "Greeting Renamed.txt.keiremeta"));
    CHECK_FALSE(database->Find(duplicateId));

    const auto trashRecords = database->TrashRecords();
    REQUIRE(trashRecords.size() == 1);
    CHECK(trashRecords.front().Assets == std::vector<Keire::AssetId>{duplicateId});
    database->RestoreTrash(trashRecords.front().Id);
    REQUIRE(database->Find(duplicateId));
    CHECK(database->Find(duplicateId)->RelativePath == std::filesystem::path("Greeting Renamed.txt"));

    database->MoveAsset(duplicateId, "Generated/Greeting Moved.txt");
    REQUIRE(database->Find(duplicateId));
    CHECK(database->Find(duplicateId)->RelativePath == std::filesystem::path("Generated/Greeting Moved.txt"));
    database->MoveFolder("Generated", "Organized");
    CHECK(database->Find(duplicateId)->RelativePath == std::filesystem::path("Organized/Greeting Moved.txt"));
    const auto copiedAssets = database->DuplicateFolder("Organized", "Organized Copy");
    REQUIRE(copiedAssets.size() == 1);
    CHECK(copiedAssets.front() != duplicateId);
    const auto folderTrash = database->TrashFolder("Organized Copy");
    CHECK(folderTrash.Folder);
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Organized Copy"));
    database->RestoreTrash(folderTrash.Id);
    CHECK(std::filesystem::is_directory(project.Root / "Assets/Organized Copy"));
    CHECK_THROWS_AS(database->MoveFolder("Organized", "Organized/Nested"), std::invalid_argument);
    CHECK_THROWS_AS(database->MoveAsset(duplicateId, "Greeting.txt"), std::runtime_error);

    auto changeDatabase = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .ChangeDebounce = std::chrono::milliseconds(0)});
    project.Write("Greeting.txt", "updated assets");
    const auto changed = changeDatabase->PollChangedAssets();
    CHECK(std::ranges::find(changed, original->Id) != changed.end());
    CHECK(changeDatabase->ImportAll().Imported > 0);
    {
        std::ofstream metadata(project.Root / "Assets/Greeting.txt.keiremeta", std::ios::app);
        metadata << ' ';
    }
    const auto metadataChanged = changeDatabase->PollChangedAssets();
    CHECK(std::ranges::find(metadataChanged, original->Id) != metadataChanged.end());
    const auto metadataImport = changeDatabase->ImportAll();
    CHECK(metadataImport.Imported == 1);
}

TEST_CASE("Mesh bounds are persisted in catalog metadata and queried without loading the asset")
{
    TemporaryAssetProject project;
    project.Write("Triangle.obj", "v 0 0 0\nv 2 0 0\nv 0 3 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n");
    Keire::AssetDatabaseSpecification databaseSpecification;
    databaseSpecification.ProjectRoot = project.Root;
    databaseSpecification.Importers.push_back(Keire::CreateMeshAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto record = database->Find("Triangle.obj");
    REQUIRE(record);
    const auto imported = database->ImportAll();

    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.DevelopmentCatalog = imported.CatalogPath;
    assetSpecification.Decoders.push_back(Keire::CreateMeshAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto metadata = assets->TryGetMetadata(record->Id);
    REQUIRE(metadata);
    REQUIRE(metadata->LocalBounds);
    CHECK(metadata->LocalBounds->Minimum == std::array{0.0F, 0.0F, 0.0F});
    CHECK(metadata->LocalBounds->Maximum == std::array{2.0F, 3.0F, 0.0F});
    CHECK(assets->Statistics().KnownAssets == 0);
    assets->Close();
}

TEST_CASE("Asset database editor imports report failures without discarding the last-good catalog")
{
    KeireTests::LogFixture logs("asset-import-diagnostics");
    Keire::Log::Initialize(logs.Config);
    TemporaryAssetProject project;
    project.Write("Broken.bad", "invalid");
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.Failing";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000001");
    importer.Extensions = {".bad"};
    importer.Import = [](std::span<const std::byte>) -> std::vector<std::byte>
    { throw std::runtime_error("intentional import failure"); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {std::move(importer)}});
    const auto record = database->Records().front();

    const auto bestEffort = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
    REQUIRE(bestEffort.Statuses.size() == 1);
    CHECK(bestEffort.Statuses.front().State == Keire::AssetImportState::Failed);
    REQUIRE(bestEffort.Statuses.front().Diagnostics.size() == 1);
    CHECK(bestEffort.Statuses.front().Diagnostics.front().Message == "intentional import failure");
    CHECK(database->ImportStatus(record.Id).State == Keire::AssetImportState::Failed);
    Keire::Log::Shutdown();
    const auto logContents = KeireTests::ReadFile(logs.Directory / logs.Config.CoreLogFile);
    CHECK(logContents.find("Asset import failed for 'Broken.bad'") != std::string::npos);
    CHECK(logContents.find("intentional import failure") != std::string::npos);
    CHECK_THROWS_WITH_AS((void)database->ImportAll(), "intentional import failure", std::runtime_error);
}

TEST_CASE("Asset handles use fallbacks asynchronously and preserve last-good data after reload failure")
{
    TemporaryAssetProject project;
    project.Write("Greeting.txt", "hello assets");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto record = database->Records().front();
    const auto imported = database->ImportAll();

    auto events = Keire::CreateRef<Keire::EventBus>();
    int failures = 0;
    auto failureListener = events->Subscribe<Keire::AssetLoadFailedEvent>(
        [&failures](const Keire::AssetLoadFailedEvent&)
        {
            ++failures;
            return Keire::EventFlow::Continue;
        });
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Cooked;
    specification.WorkerCount = 1;
    specification.Mounts.push_back({imported.CatalogPath});
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification, events);
    const auto handle = assets->Load<Keire::TextAsset>(record.Id);
    REQUIRE(handle.Get());
    CHECK(handle.Get()->Text().empty());
    CHECK(handle.UsingFallback());

    WaitFor(*assets, [&handle] { return handle.State() == Keire::AssetState::Ready; });
    REQUIRE(handle.Get());
    CHECK(handle.Get()->Text() == "hello assets");
    CHECK_FALSE(handle.UsingFallback());
    CHECK(handle.Revision() == 1);
    CHECK(handle.Require()->Text() == "hello assets");

    const auto pack = imported.CatalogPath.parent_path() / "content-0.keirepak";
    std::fstream corrupt(pack, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekp(16, std::ios::beg);
    corrupt.put('\0');
    corrupt.close();
    REQUIRE(assets->Reload(record.Id));
    CHECK_FALSE(assets->Reload(record.Id));
    WaitFor(*assets, [&failures] { return failures == 1; });
    CHECK(handle.State() == Keire::AssetState::Ready);
    CHECK(handle.Revision() == 1);
    CHECK(handle.Get()->Text() == "hello assets");
    CHECK_FALSE(handle.Diagnostic().Message.empty());

    assets->Close();
    events->Close();
}

TEST_CASE("Missing assets become explicit failures while retaining typed defaults")
{
    TemporaryAssetProject project;
    project.Write("Known.txt", "known");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto imported = database->ImportAll();

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Cooked;
    specification.WorkerCount = 1;
    specification.Mounts.push_back({imported.CatalogPath});
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const auto missing = assets->Load<Keire::TextAsset>(Keire::AssetId::Generate());
    REQUIRE(missing.Get());
    CHECK(missing.Get()->Text().empty());
    (void)assets->PumpCompletions();
    CHECK(missing.State() == Keire::AssetState::Failed);
    CHECK(missing.UsingFallback());
    CHECK_THROWS_AS((void)missing.Require(), Keire::AssetLoadError);
    assets->Close();
}
