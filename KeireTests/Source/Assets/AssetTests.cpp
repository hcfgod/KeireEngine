#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Log.h"
#include "KeireTests/TestSupport.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
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

TEST_CASE("Single asset creation and rename avoid unrelated project rescans")
{
    TemporaryAssetProject project;
    project.Write("Unrelated.fast", "unrelated");
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.FastMutation";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000011");
    importer.Extensions = {".fast"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});

    project.Write("Unrelated.fast.keiremeta", "invalid metadata");
    const std::string source = "new material-sized asset";
    const auto created =
        database->CreateAsset("Created.fast", importer, std::as_bytes(std::span(source.data(), source.size())));
    REQUIRE(database->Find(created));
    database->MoveAsset(created, "Renamed.fast");
    REQUIRE(database->Find(created));
    CHECK(database->Find(created)->RelativePath == std::filesystem::path("Renamed.fast"));
    CHECK_THROWS((void)database->Refresh());
}

TEST_CASE("Successful compatible imports upgrade metadata without losing project fields")
{
    TemporaryAssetProject project;
    const auto id = Keire::AssetId::Generate();
    project.Write("Versioned.upgrade", "versioned source");
    const auto metadata = std::string("{\n") +
                          "  \"schemaVersion\": 1,\n"
                          "  \"id\": \"" +
                          id.ToString() +
                          "\",\n"
                          "  \"type\": \"" +
                          Keire::TextAsset::StaticType().ToString() +
                          "\",\n"
                          "  \"importer\": \"Test.MetadataUpgrade\",\n"
                          "  \"importerVersion\": 1,\n"
                          "  \"dependencies\": [],\n"
                          "  \"subAssets\": [],\n"
                          "  \"importSettings\": {\"quality\": 7},\n"
                          "  \"projectExtension\": {\"keep\": true}\n"
                          "}\n";
    project.Write("Versioned.upgrade.keiremeta", metadata);

    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.MetadataUpgrade";
    importer.Version = 2;
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".upgrade"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    (void)database->ImportAll();

    REQUIRE(database->Find(id));
    CHECK(database->Find(id)->ImporterVersion == 2);
    const auto upgraded = ReadAll(project.Root / "Assets/Versioned.upgrade.keiremeta");
    const std::string upgradedText(upgraded.begin(), upgraded.end());
    CHECK(upgradedText.find("\"importerVersion\": 2") != std::string::npos);
    CHECK(upgradedText.find(id.ToString()) != std::string::npos);
    CHECK(upgradedText.find("projectExtension") != std::string::npos);
    CHECK(upgradedText.find("\"quality\": 7") != std::string::npos);

    auto failingImporter = importer;
    failingImporter.Version = 3;
    failingImporter.Import = [](std::span<const std::byte>) -> std::vector<std::byte>
    { throw std::runtime_error("intentional metadata upgrade import failure"); };
    auto failingDatabase = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {failingImporter}});
    (void)failingDatabase->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
    CHECK(ReadAll(project.Root / "Assets/Versioned.upgrade.keiremeta") == upgraded);
}

TEST_CASE("External asset imports persist normalized options and preserve identities on replace")
{
    TemporaryAssetProject project;
    const auto incoming = project.Root / "Incoming.opt";
    {
        std::ofstream stream(incoming, std::ios::binary | std::ios::trunc);
        stream << "first";
    }
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.Options";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000010");
    importer.Extensions = {".opt"};
    importer.ImportOptions = {{"uppercase", "Uppercase", "Text", Keire::AssetImportOptionKind::Boolean, false}};
    std::atomic_size_t importCalls = 0;
    importer.ContextualImport =
        [&importCalls](const Keire::AssetImportContext& context, const std::span<const std::byte> bytes)
    {
        ++importCalls;
        CHECK(context.ImportSettings.contains("uppercase"));
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});

    Keire::ExternalAssetImportItem item{incoming, "Imported/Incoming.opt", {{"uppercase", true}}};
    const auto created = database->ImportExternal(std::span(&item, 1));
    REQUIRE(created.Entries.size() == 1);
    REQUIRE(created.Receipt);
    CHECK(importCalls.load() == 1);
    const auto id = created.Entries.front().Id;
    const auto catalogBytes = ReadAll(created.Import.CatalogPath);
    CHECK(std::string(catalogBytes.begin(), catalogBytes.end()).find(id.ToString()) != std::string::npos);
    REQUIRE(database->Find(id));
    CHECK(std::get<bool>(database->Find(id)->ImportSettings.at("uppercase")));
    CHECK(std::filesystem::is_regular_file(project.Root / "Assets/Imported/Incoming.opt"));
    database->UndoExternalImport(created.Receipt);
    CHECK_FALSE(database->Find(id));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/Incoming.opt"));
    database->RedoExternalImport(created.Receipt);
    REQUIRE(database->Find(id));
    CHECK(std::get<bool>(database->Find(id)->ImportSettings.at("uppercase")));

    item.Conflict = Keire::ExternalAssetConflictPolicy::UniqueName;
    const auto unique = database->ImportExternal(std::span(&item, 1));
    REQUIRE(unique.Entries.size() == 1);
    CHECK(unique.Entries.front().Id != id);
    CHECK(unique.Entries.front().RelativeDestination == std::filesystem::path("Imported/Incoming 2.opt"));

    {
        std::ofstream stream(incoming, std::ios::binary | std::ios::trunc);
        stream << "replacement";
    }
    item.Conflict = Keire::ExternalAssetConflictPolicy::Replace;
    const auto replaced = database->ImportExternal(std::span(&item, 1));
    REQUIRE(replaced.Entries.size() == 1);
    CHECK(replaced.Entries.front().Id == id);
    CHECK(replaced.Entries.front().Replaced);
    const auto replacedBytes = ReadAll(project.Root / "Assets/Imported/Incoming.opt");
    CHECK(std::string(replacedBytes.begin(), replacedBytes.end()) == "replacement");
    database->UndoExternalImport(replaced.Receipt);
    const auto restoredBytes = ReadAll(project.Root / "Assets/Imported/Incoming.opt");
    CHECK(std::string(restoredBytes.begin(), restoredBytes.end()) == "first");
    CHECK(database->Find(id)->Id == id);
    database->RedoExternalImport(replaced.Receipt);
    CHECK(ReadAll(project.Root / "Assets/Imported/Incoming.opt") == replacedBytes);
    CHECK(database->Find(id)->Id == id);

    item.Settings = {{"unknown", true}};
    CHECK_THROWS_AS((void)database->ImportExternal(std::span(&item, 1)), std::invalid_argument);
    CHECK(database->Find(id));

    item.Settings = {{"uppercase", false}};
    item.RelativeDestination = "Imported/Cancelled.opt";
    item.Conflict = Keire::ExternalAssetConflictPolicy::UniqueName;
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK_THROWS_AS((void)database->ImportExternal(std::span(&item, 1), cancellation.get_token()),
                    Keire::AssetOperationCancelled);
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/Cancelled.opt"));

    auto validBatchItem = item;
    validBatchItem.RelativeDestination = "Batch/First.opt";
    auto invalidBatchItem = item;
    invalidBatchItem.RelativeDestination = "Batch/Second.opt";
    invalidBatchItem.Settings = {{"unsupported", true}};
    const std::array batch{validBatchItem, invalidBatchItem};
    CHECK_THROWS_AS((void)database->ImportExternal(batch), std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Batch/First.opt"));

    const auto droppedFolder = project.Root / "DroppedFolder";
    std::filesystem::create_directories(droppedFolder);
    {
        std::ofstream supported(droppedFolder / "Supported.opt");
        supported << "supported";
        std::ofstream unsupported(droppedFolder / "Unsupported.unknown");
        unsupported << "unsupported";
    }
    Keire::ExternalAssetImportItem folderItem{droppedFolder, "Folder"};
    CHECK_THROWS_WITH_AS((void)database->ImportExternal(std::span(&folderItem, 1)),
                         doctest::Contains("No importer supports a dropped directory entry"), std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Folder/Supported.opt"));
}

TEST_CASE("Asset database serializes catalog operations and reports cancellable progress")
{
    TemporaryAssetProject project;
    project.Write("Serialized.txt", "serialized operation");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});

    std::atomic_int ready = 0;
    std::atomic_bool start = false;
    std::atomic_int callbacks = 0;
    std::atomic_int activeCallbacks = 0;
    std::atomic_int maximumCallbacks = 0;
    std::atomic_int failures = 0;
    const auto run = [&]
    {
        ++ready;
        while (!start.load())
            std::this_thread::yield();
        try
        {
            (void)database->ImportAll(Keire::AssetImportPolicy::FailFast, {},
                                      [&](const Keire::AssetOperationProgress& progress)
                                      {
                                          ++callbacks;
                                          if (progress.Phase != Keire::AssetOperationPhase::Scanning)
                                              return;
                                          const auto active = ++activeCallbacks;
                                          auto observed = maximumCallbacks.load();
                                          while (observed < active &&
                                                 !maximumCallbacks.compare_exchange_weak(observed, active))
                                          {
                                          }
                                          std::this_thread::sleep_for(std::chrono::milliseconds(25));
                                          --activeCallbacks;
                                      });
        }
        catch (...)
        {
            ++failures;
        }
    };
    std::jthread first(run);
    std::jthread second(run);
    while (ready.load() != 2)
        std::this_thread::yield();
    start = true;
    first.join();
    second.join();

    CHECK(failures.load() == 0);
    CHECK(callbacks.load() >= 2);
    CHECK(maximumCallbacks.load() == 1);
    CHECK_NOTHROW(Keire::AssetCooker::Validate(project.Root / "Library/AssetCache/Runtime/catalog.json"));

    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK_THROWS_AS((void)database->ImportAll(Keire::AssetImportPolicy::FailFast, cancellation.get_token()),
                    Keire::AssetOperationCancelled);
}

TEST_CASE("External import staging cancels without publication and startup rolls back an interrupted journal")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.Transaction";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000012");
    importer.Extensions = {".txn"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});

    const auto first = project.Root / "First.txn";
    const auto second = project.Root / "Second.txn";
    {
        std::ofstream stream(first);
        stream << "first";
    }
    {
        std::ofstream stream(second);
        stream << "second";
    }
    const std::array items{Keire::ExternalAssetImportItem{first, "Imported/First.txn"},
                           Keire::ExternalAssetImportItem{second, "Imported/Second.txn"}};
    std::stop_source cancellation;
    CHECK_THROWS_AS((void)database->ImportExternal(items, cancellation.get_token(),
                                                   [&](const Keire::AssetOperationProgress& progress)
                                                   {
                                                       if (progress.Phase == Keire::AssetOperationPhase::Staging &&
                                                           progress.Completed == 1)
                                                           cancellation.request_stop();
                                                   }),
                    Keire::AssetOperationCancelled);
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/First.txn"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/Second.txn"));

    const std::string original = "original";
    const auto id =
        database->CreateAsset("Recover.txn", importer, std::as_bytes(std::span(original.data(), original.size())));
    const auto originalMetadata = ReadAll(project.Root / "Assets/Recover.txn.keiremeta");
    const auto transaction = project.Root / "Library/AssetImport/interrupted";
    std::filesystem::create_directories(transaction / "before");
    const auto write = [](const std::filesystem::path& path, const std::span<const char> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    };
    write(transaction / "before/0.source", std::span(original.data(), original.size()));
    write(transaction / "before/0.metadata", originalMetadata);
    const std::string damaged = "damaged";
    write(project.Root / "Assets/Recover.txn", std::span(damaged.data(), damaged.size()));
    const std::string journal =
        "{\"schemaVersion\":1,\"state\":\"publishing\",\"entries\":[{\"destination\":\"Recover.txn\","
        "\"replaced\":true}]}";
    write(transaction / "journal.json", std::span(journal.data(), journal.size()));

    database.Reset();
    database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    REQUIRE(database->Find(id));
    CHECK(ReadAll(project.Root / "Assets/Recover.txn") == std::vector<char>(original.begin(), original.end()));
    CHECK_FALSE(std::filesystem::exists(transaction));
}

TEST_CASE("Texture importer exposes UI-independent production import options")
{
    const auto importer = Keire::CreateTexture2DAssetImporter();
    CHECK(importer.ImportOptions.size() == 12);
    CHECK(std::ranges::any_of(importer.ImportOptions, [](const auto& option) { return option.Key == "semantic"; }));
    CHECK(std::ranges::any_of(importer.ImportOptions, [](const auto& option) { return option.Key == "flipGreen"; }));
    CHECK(std::ranges::any_of(importer.ImportOptions, [](const auto& option) { return option.Key == "anisotropy"; }));
    REQUIRE(importer.SuggestImportSettings);
    Keire::AssetImportSettings defaults;
    for (const auto& option : importer.ImportOptions)
        defaults.emplace(option.Key, option.DefaultValue);
    const auto normal = importer.SuggestImportSettings("cartoon_monster_normal.png", defaults);
    CHECK(std::get<std::string>(normal.at("semantic")) == "normal");
    CHECK(std::get<std::string>(normal.at("colorSpace")) == "linear");
    const auto roughness = importer.SuggestImportSettings("cartoon-monster-roughness.png", defaults);
    CHECK(std::get<std::string>(roughness.at("semantic")) == "data");
    CHECK(std::get<std::string>(roughness.at("colorSpace")) == "linear");
    const auto baseColor = importer.SuggestImportSettings("cartoon_monster_diffuse.png", defaults);
    CHECK(std::get<std::string>(baseColor.at("semantic")) == "color");
    CHECK(std::get<std::string>(baseColor.at("colorSpace")) == "srgb");
}

TEST_CASE("External asset publication rolls back when the new catalog is invalid")
{
    TemporaryAssetProject project;
    const auto incoming = project.Root / "Broken.dep";
    {
        std::ofstream stream(incoming, std::ios::binary | std::ios::trunc);
        stream << "broken dependency";
    }
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.BrokenDependency";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000011");
    importer.Extensions = {".dep"};
    importer.ContextualImport = [](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    importer.Cook = [](const std::span<const std::byte>, const Keire::AssetTargetPlatform) -> std::vector<std::byte>
    { throw std::runtime_error("intentional cook failure"); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    Keire::ExternalAssetImportItem item{incoming, "Imported/Broken.dep"};
    CHECK_THROWS_WITH_AS((void)database->ImportExternal(std::span(&item, 1)),
                         doctest::Contains("intentional cook failure"), std::runtime_error);
    CHECK_FALSE(database->Find("Imported/Broken.dep"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/Broken.dep"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Imported/Broken.dep.keiremeta"));
}

TEST_CASE("Dependency-free importers restore unchanged cached output without rerunning expensive source import")
{
    TemporaryAssetProject project;
    project.Write("Model.cache", "expensive source");
    std::atomic_size_t importCalls = 0;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.RestorableCache";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000012");
    importer.Extensions = {".cache"};
    importer.ContextualImport = [&importCalls](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        ++importCalls;
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    importer.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const auto first = database->ImportAll();
    CHECK(first.Imported == 1);
    CHECK(importCalls.load() == 1);
    const auto second = database->ImportAll();
    CHECK(second.CacheHits == 1);
    CHECK(importCalls.load() == 1);
}

TEST_CASE("Asset record snapshots remain responsive while an import operation is blocked")
{
    TemporaryAssetProject project;
    std::atomic_bool blockImport = false;
    std::atomic_bool importerEntered = false;
    std::atomic_bool releaseImporter = false;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.BlockingSnapshot";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000021");
    importer.Extensions = {".blocking"};
    importer.ContextualImport = [&](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        if (blockImport.load())
        {
            importerEntered.store(true);
            while (!releaseImporter.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const std::string initialSource = "initial source";
    const auto id = database->CreateAsset("Responsive.blocking", importer,
                                          std::as_bytes(std::span(initialSource.data(), initialSource.size())));
    REQUIRE_NOTHROW((void)database->ImportAll());

    project.Write("Responsive.blocking", "changed source that invalidates the cached import");
    blockImport.store(true);
    auto import = std::async(std::launch::async, [&] { return database->ImportAll(); });
    const auto importDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!importerEntered.load() && std::chrono::steady_clock::now() < importDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const bool entered = importerEntered.load();
    CHECK(entered);
    auto query = std::async(std::launch::async,
                            [&]
                            {
                                const auto records = database->Records();
                                const auto byId = database->Find(id);
                                const auto byPath = database->Find("Responsive.blocking");
                                const auto status = database->ImportStatus(id);
                                return records.size() == 1 && byId && byPath && byId->Id == id && byPath->Id == id &&
                                       status.Id == id;
                            });
    const auto queryState = query.wait_for(std::chrono::milliseconds(250));
    releaseImporter.store(true);

    CHECK(queryState == std::future_status::ready);
    CHECK(query.get());
    CHECK_NOTHROW((void)import.get());
}

TEST_CASE("Development catalogs tolerate missing references while strict cooking rejects them")
{
    TemporaryAssetProject project;
    project.Write("MissingReference.dep", "source");
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.MissingReference";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000013");
    importer.Extensions = {".dep"};
    importer.ContextualImport = [](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        output.AssetDependencies.push_back(Keire::AssetId::Parse("f1000000-0000-4000-8000-00000000ffff"));
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    CHECK_NOTHROW((void)database->ImportAll());
    Keire::AssetBuildProfile strict;
    strict.Strict = true;
    CHECK_THROWS_WITH_AS((void)Keire::AssetCooker::Cook(*database, strict, project.Root / "StrictCook"),
                         doctest::Contains("dependency is missing"), std::runtime_error);
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

TEST_CASE("Catalog replacement recovers a thumbnail load queued before import publication")
{
    TemporaryAssetProject project;
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto initial = database->ImportAll();

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.WorkerCount = 1;
    specification.DevelopmentCatalog = initial.CatalogPath;
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);

    project.Write("Imported/Monster.txt", "cartoon monster");
    const auto imported = database->ImportAll();
    const auto record = database->Find("Imported/Monster.txt");
    REQUIRE(record);

    const auto earlyThumbnail = assets->Load<Keire::TextAsset>(record->Id);
    CHECK(earlyThumbnail.State() == Keire::AssetState::Queued);
    CHECK(earlyThumbnail.UsingFallback());

    REQUIRE(assets->Unmount(imported.CatalogPath));
    assets->Mount({imported.CatalogPath, 0, true});
    WaitFor(*assets, [&earlyThumbnail] { return earlyThumbnail.State() == Keire::AssetState::Ready; });
    CHECK(earlyThumbnail.Get()->Text() == "cartoon monster");
    CHECK_FALSE(earlyThumbnail.UsingFallback());
    assets->Close();
}

TEST_CASE("Catalog replacement recovers a thumbnail load that failed before import publication")
{
    TemporaryAssetProject project;
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto initial = database->ImportAll();

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.WorkerCount = 1;
    specification.DevelopmentCatalog = initial.CatalogPath;
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);

    project.Write("Imported/Monster.txt", "cartoon monster");
    const auto imported = database->ImportAll();
    const auto record = database->Find("Imported/Monster.txt");
    REQUIRE(record);

    const auto earlyThumbnail = assets->Load<Keire::TextAsset>(record->Id);
    (void)assets->PumpCompletions();
    REQUIRE(earlyThumbnail.State() == Keire::AssetState::Failed);
    CHECK(earlyThumbnail.UsingFallback());

    REQUIRE(assets->Unmount(imported.CatalogPath));
    assets->Mount({imported.CatalogPath, 0, true});
    WaitFor(*assets, [&earlyThumbnail] { return earlyThumbnail.State() == Keire::AssetState::Ready; });
    CHECK(earlyThumbnail.Get()->Text() == "cartoon monster");
    CHECK_FALSE(earlyThumbnail.UsingFallback());
    assets->Close();
}

TEST_CASE("Development asset publication advances live handles without rebuilding a catalog")
{
    TemporaryAssetProject project;
    project.Write("Greeting.txt", "catalog value");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto record = database->Records().front();
    const auto imported = database->ImportAll();

    auto events = Keire::CreateRef<Keire::EventBus>();
    std::vector<Keire::AssetLoadedEvent> loaded;
    auto listener = events->Subscribe<Keire::AssetLoadedEvent>(
        [&loaded](const Keire::AssetLoadedEvent& event)
        {
            loaded.push_back(event);
            return Keire::EventFlow::Continue;
        });
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog = imported.CatalogPath;
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification, events);
    const auto handle = assets->Load<Keire::TextAsset>(record.Id);
    WaitFor(*assets, [&handle] { return handle.State() == Keire::AssetState::Ready; });
    REQUIRE(handle.Get());
    CHECK(handle.Get()->Text() == "catalog value");
    const auto revision = handle.Revision();

    REQUIRE(assets->PublishDevelopmentAsset(record.Id, Keire::CreateRef<Keire::TextAsset>("live preview")));
    REQUIRE(handle.Get());
    CHECK(handle.Get()->Text() == "live preview");
    CHECK(handle.Revision() == revision + 1);
    REQUIRE_FALSE(loaded.empty());
    CHECK(loaded.back().Id == record.Id);
    CHECK(loaded.back().Reload);

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
