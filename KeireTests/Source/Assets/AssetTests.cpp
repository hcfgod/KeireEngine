#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Log.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "Keire/Streaming/StreamingSystem.h"
#include "KeireTests/TestSupport.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

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

    struct ConstrainedStackHash final
    {
        std::filesystem::path Path;
        std::uintmax_t MaximumBytes = 0;
        Keire::Detail::Sha256Digest Digest{};
        std::exception_ptr Failure;
    };

    void RunConstrainedStackHash(ConstrainedStackHash& invocation) noexcept
    {
        try
        {
            invocation.Digest = Keire::Detail::Sha256File(invocation.Path, invocation.MaximumBytes);
        }
        catch (...)
        {
            invocation.Failure = std::current_exception();
        }
    }

#if defined(_WIN32)
    unsigned int __stdcall ConstrainedStackHashEntry(void* context) noexcept
    {
        RunConstrainedStackHash(*static_cast<ConstrainedStackHash*>(context));
        return 0U;
    }
#else
    void* ConstrainedStackHashEntry(void* context) noexcept
    {
        RunConstrainedStackHash(*static_cast<ConstrainedStackHash*>(context));
        return nullptr;
    }
#endif

    [[nodiscard]] Keire::Detail::Sha256Digest HashOnConstrainedStack(const std::filesystem::path& path,
                                                                     const std::uintmax_t maximumBytes)
    {
        constexpr std::size_t StackBytes = std::size_t{256U} * 1024U;
        ConstrainedStackHash invocation{.Path = path, .MaximumBytes = maximumBytes};
#if defined(_WIN32)
        const auto nativeThread = _beginthreadex(nullptr, StackBytes, ConstrainedStackHashEntry, &invocation,
                                                 STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (nativeThread == 0U)
            throw std::runtime_error("Could not start the constrained-stack hashing test thread.");
        const auto thread = reinterpret_cast<HANDLE>(nativeThread);
        const auto waitResult = WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        if (waitResult != WAIT_OBJECT_0)
            throw std::runtime_error("Could not join the constrained-stack hashing test thread.");
#else
        pthread_attr_t attributes;
        if (pthread_attr_init(&attributes) != 0)
            throw std::runtime_error("Could not initialize constrained-stack thread attributes.");
        const auto stackBytes = std::max(StackBytes, static_cast<std::size_t>(PTHREAD_STACK_MIN));
        if (pthread_attr_setstacksize(&attributes, stackBytes) != 0)
        {
            pthread_attr_destroy(&attributes);
            throw std::runtime_error("Could not configure the constrained-stack hashing test thread.");
        }
        pthread_t thread;
        const auto createResult = pthread_create(&thread, &attributes, ConstrainedStackHashEntry, &invocation);
        pthread_attr_destroy(&attributes);
        if (createResult != 0)
            throw std::runtime_error("Could not start the constrained-stack hashing test thread.");
        if (pthread_join(thread, nullptr) != 0)
            throw std::runtime_error("Could not join the constrained-stack hashing test thread.");
#endif
        if (invocation.Failure)
            std::rethrow_exception(invocation.Failure);
        return invocation.Digest;
    }
} // namespace

TEST_CASE("Asset identifiers are stable canonical 128-bit values")
{
    const auto id = Keire::AssetId::Generate();
    CHECK(id);
    CHECK(Keire::AssetId::Parse(id.ToString()) == id);
    CHECK_THROWS_AS((void)Keire::AssetId::Parse("not-an-asset-id"), std::invalid_argument);
}

TEST_CASE("File SHA-256 hashing streams safely on an Editor-sized caller stack")
{
    TemporaryAssetProject project;
    std::string contents(std::size_t{1024U} * 1024U + 257U, 'x');
    for (std::size_t index = 0; index < contents.size(); index += 251U)
        contents[index] = static_cast<char>('a' + index % 26U);
    project.Write("LargeHashInput.bin", contents);

    const auto bytes = std::as_bytes(std::span(contents));
    CHECK(HashOnConstrainedStack(project.Root / "Assets/LargeHashInput.bin", contents.size()) ==
          Keire::Detail::Sha256(bytes));
    CHECK_THROWS_AS(
        static_cast<void>(Keire::Detail::Sha256File(project.Root / "Assets/LargeHashInput.bin", contents.size() - 1U)),
        std::runtime_error);
}

TEST_CASE("Asset worker protocol and published source index round trip without rescanning")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.WorkerProtocol";
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".worker"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const std::string source = "worker protocol source";
    const auto id =
        database->CreateAsset("Unicode-é.worker", importer, std::as_bytes(std::span(source.data(), source.size())));

    const auto operationId = Keire::AssetId::Generate().ToString();
    const auto operation = project.Root / "Library/AssetOperations" / operationId;
    std::filesystem::create_directories(operation);
    const auto sourceIndex = operation / "source-index.json";
    Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, sourceIndex);
    REQUIRE(std::filesystem::is_regular_file(sourceIndex));
    CHECK(Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*database, sourceIndex) == 1);
    REQUIRE(database->Find(id));
    CHECK(database->Find(id)->RelativePath == std::filesystem::path("Unicode-é.worker"));

    Keire::Detail::AssetWorkerRequest request;
    request.OperationId = operationId;
    request.ProjectRoot = project.Root;
    request.SourceIndexPath = sourceIndex;
    request.Kind = Keire::Detail::AssetWorkerOperationKind::Cook;
    request.CreateRelativePath = "Scenes/Created.keirescene";
    request.CreatePayloadPath = operation / "source.keirescene";
    request.CreateSettings = {{"quality", std::int64_t{2}}};
    Keire::ExternalAssetImportItem externalItem;
    externalItem.SourcePath = project.Root / "Source/Character.fbx";
    externalItem.RelativeDestination = "Models/Character.fbx";
    externalItem.Conflict = Keire::ExternalAssetConflictPolicy::UniqueName;
    externalItem.Settings = {{"maximumInfluences", std::string("8")},
                             {"rigProfile", std::string("humanoid")},
                             {"rigSource", std::string("generate")},
                             {"skinningMethod", std::string("linearBlend")}};
    request.ExternalItems.push_back(externalItem);
    request.CreateAuxiliarySources = {{"Shaders/Created.hlsl", operation / "auxiliary-0.hlsl"}};
    request.ExtractModel = id;
    request.ExtractDirectory = "Materials/Extracted";
    request.Mutation = {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                        .Asset = id,
                        .Trash = Keire::AssetTrashId::Parse(Keire::AssetId::Generate().ToString()),
                        .Source = "Old/Unicode-é.worker",
                        .Destination = "New/Unicode-é.worker"};
    request.CookOutput = project.Root / "Build/Cooked";
    request.BuildProfile.Name = "Test";
    request.BuildProfile.Roots = {id};
    request.BuildProfile.ManagedTypeDiscoveryComplete = true;
    request.BuildProfile.ManagedTypeCatalog = R"({"schemaVersion":1,"types":[]})";
    request.BakeScene = id;
    request.BakeForce = true;
    const auto requestPath = operation / "request.json";
    Keire::Detail::WriteAssetWorkerRequest(requestPath, request);
    const auto restored = Keire::Detail::ReadAssetWorkerRequest(requestPath);
    CHECK(restored.OperationId == request.OperationId);
    CHECK(restored.ProjectRoot == request.ProjectRoot);
    CHECK(restored.SourceIndexPath == request.SourceIndexPath);
    CHECK(restored.CookOutput == request.CookOutput);
    CHECK(restored.CreateRelativePath == request.CreateRelativePath);
    CHECK(restored.CreatePayloadPath == request.CreatePayloadPath);
    CHECK(restored.CreateSettings == request.CreateSettings);
    REQUIRE(restored.ExternalItems.size() == 1);
    CHECK(restored.ExternalItems.front().SourcePath == externalItem.SourcePath);
    CHECK(restored.ExternalItems.front().RelativeDestination == externalItem.RelativeDestination);
    CHECK(restored.ExternalItems.front().Conflict == externalItem.Conflict);
    CHECK(restored.ExternalItems.front().Settings == externalItem.Settings);
    CHECK(restored.ExtractModel == request.ExtractModel);
    CHECK(restored.ExtractDirectory == request.ExtractDirectory);
    REQUIRE(restored.CreateAuxiliarySources.size() == 1);
    CHECK(restored.CreateAuxiliarySources.front().RelativePath == request.CreateAuxiliarySources.front().RelativePath);
    CHECK(restored.CreateAuxiliarySources.front().PayloadPath == request.CreateAuxiliarySources.front().PayloadPath);
    CHECK(restored.Mutation.Kind == request.Mutation.Kind);
    CHECK(restored.Mutation.Asset == request.Mutation.Asset);
    CHECK(restored.Mutation.Trash == request.Mutation.Trash);
    CHECK(restored.Mutation.Source == request.Mutation.Source);
    CHECK(restored.Mutation.Destination == request.Mutation.Destination);
    CHECK(restored.BuildProfile.Roots == request.BuildProfile.Roots);
    CHECK(restored.BuildProfile.ManagedTypeDiscoveryComplete);
    CHECK(restored.BuildProfile.ManagedTypeCatalog == request.BuildProfile.ManagedTypeCatalog);
    CHECK(restored.BakeScene == request.BakeScene);
    CHECK(restored.BakeForce);

    Keire::Detail::AssetWorkerResult result;
    result.Success = true;
    result.CreatedAsset = id;
    result.LightingCacheHit = true;
    const auto resultPath = operation / "result.json";
    Keire::Detail::WriteAssetWorkerResult(resultPath, result);
    const auto restoredResult = Keire::Detail::ReadAssetWorkerResult(resultPath);
    CHECK(restoredResult.Success);
    CHECK(restoredResult.CreatedAsset == id);
    CHECK(restoredResult.LightingCacheHit);
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

TEST_CASE("Asset database ignores files used for atomic writes and editor backups")
{
    TemporaryAssetProject project;
    project.Write("Stable.fast", "stable");
    project.Write("Stable.fast.tmp.123456789", "in-flight source");
    project.Write("Stable.fast.keiremeta.tmp.987654321", "in-flight metadata");
    project.Write("Stable.fast.asset-operation.tmp", "asset operation");
    project.Write("Stable.fast~", "editor backup");

    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.TransientFiles";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000012");
    importer.Extensions = {".fast"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };

    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    REQUIRE(database->Records().size() == 1);
    CHECK(database->Records().front().RelativePath == std::filesystem::path("Stable.fast"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Stable.fast.tmp.123456789.keiremeta"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Stable.fast.keiremeta.tmp.987654321.keiremeta"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Stable.fast.asset-operation.tmp.keiremeta"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Stable.fast~.keiremeta"));
    CHECK(database->Refresh() == 1);
}

TEST_CASE("C sharp source files use the text asset fallback")
{
    TemporaryAssetProject project;
    project.Write("Scripts/PlayerController.cs", "public sealed class PlayerController {}\n");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto record = database->Find("Scripts/PlayerController.cs");
    REQUIRE(record);
    CHECK(record->Type == Keire::TextAsset::StaticType());
    CHECK(record->Importer == "Keire.Text");

    Keire::AssetDatabaseSpecification registeredSpecification{.ProjectRoot = project.Root};
    registeredSpecification.Importers.push_back(Keire::CreateTextAssetImporter());
    auto registeredDatabase = Keire::CreateRef<Keire::AssetDatabase>(std::move(registeredSpecification));
    const auto importer = registeredDatabase->FindImporterForPath("source.cs");
    REQUIRE(importer);
    CHECK(importer->Name == "Keire.Text");
    CHECK(importer->Type == Keire::TextAsset::StaticType());
    const std::string source = "public sealed class CreatedScript {}\n";
    const auto sourceBytes = std::as_bytes(std::span(source.data(), source.size()));
    CHECK(importer->Import(sourceBytes) == std::vector<std::byte>(sourceBytes.begin(), sourceBytes.end()));

    const auto created = registeredDatabase->CreateAsset("Scripts/CreatedScript.cs", *importer, sourceBytes);
    const auto createdRecord = registeredDatabase->Find(created);
    REQUIRE(createdRecord);
    CHECK(createdRecord->RelativePath == std::filesystem::path("Scripts/CreatedScript.cs"));
    CHECK(createdRecord->Importer == "Keire.Text");
    CHECK(std::filesystem::is_regular_file(project.Root / "Assets/Scripts/CreatedScript.cs.keiremeta"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Library/AssetCache/Runtime/catalog.json"));
}

TEST_CASE("Asset source replacement preserves identity and rolls back invalid content")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.Replace";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000012");
    importer.Extensions = {".replace"};
    importer.Import = [](const std::span<const std::byte> bytes)
    {
        if (!bytes.empty() && bytes.front() == std::byte{'!'})
            throw std::invalid_argument("invalid replacement");
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const std::string original = "original";
    const auto id =
        database->CreateAsset("Stable.replace", importer, std::as_bytes(std::span(original.data(), original.size())));
    const auto before = database->Find(id);
    REQUIRE(before);

    const std::string replacement = "replacement";
    database->ReplaceAssetSource(id, std::as_bytes(std::span(replacement.data(), replacement.size())));
    const auto replaced = database->Find(id);
    REQUIRE(replaced);
    CHECK(replaced->Id == id);
    CHECK(replaced->SourceDigest != before->SourceDigest);

    const std::string invalid = "!invalid";
    CHECK_THROWS(database->ReplaceAssetSource(id, std::as_bytes(std::span(invalid.data(), invalid.size()))));
    const auto restored = database->Find(id);
    REQUIRE(restored);
    CHECK(restored->Id == id);
    CHECK(restored->SourceDigest == replaced->SourceDigest);
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
    const auto folderImport = database->ImportExternal(std::span(&folderItem, 1));
    REQUIRE(folderImport.Entries.size() == 1);
    CHECK(folderImport.Entries.front().RelativeDestination == std::filesystem::path("Folder/Supported.opt"));
    CHECK(std::filesystem::is_regular_file(project.Root / "Assets/Folder/Supported.opt"));
    CHECK_FALSE(std::filesystem::exists(project.Root / "Assets/Folder/Unsupported.unknown"));
}

TEST_CASE("Development catalog publication preserves packs that are still in use")
{
    TemporaryAssetProject project;
    project.Write("Live.live", "generation one");
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.LivePack";
    importer.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000013");
    importer.Extensions = {".live"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});

    const auto first = database->ImportAll();
    const auto firstCatalog = Keire::Detail::LoadCatalog(first.CatalogPath);
    REQUIRE(firstCatalog.Entries.size() == 1);
    const auto firstPack = firstCatalog.Entries.front().PackPath;
    REQUIRE(std::filesystem::is_regular_file(firstPack));
    std::ifstream livePack(firstPack, std::ios::binary);
    REQUIRE(livePack);

    project.Write("Live.live", "generation two");
    const auto second = database->ImportAll();
    CHECK(second.Imported == 1);
    const auto secondCatalog = Keire::Detail::LoadCatalog(second.CatalogPath);
    REQUIRE(secondCatalog.Entries.size() == 1);
    CHECK(secondCatalog.Entries.front().PackPath != firstPack);
    CHECK(std::filesystem::is_regular_file(secondCatalog.Entries.front().PackPath));
    CHECK(std::filesystem::is_regular_file(firstPack));

    std::array<char, Keire::Detail::PackMagic.size()> magic{};
    livePack.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    CHECK(livePack.good());
    CHECK(magic == Keire::Detail::PackMagic);
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

TEST_CASE("Asset database startup restores the last-good catalog after interrupted directory publication")
{
    TemporaryAssetProject project;
    const auto runtime = std::filesystem::absolute(project.Root / "Library/AssetCache/Runtime").lexically_normal();
    const auto backup = std::filesystem::path(runtime.string() + ".bak");
    const auto temporary = std::filesystem::path(runtime.string() + ".tmp-interrupted");
    const auto journal = std::filesystem::path(runtime.string() + ".publish.json");
    std::filesystem::create_directories(runtime);
    std::filesystem::create_directories(backup);
    std::filesystem::create_directories(temporary);
    {
        std::ofstream current(runtime / "generation.txt", std::ios::binary | std::ios::trunc);
        current << "partial-new";
        std::ofstream previous(backup / "generation.txt", std::ios::binary | std::ios::trunc);
        previous << "last-good";
        std::ofstream staged(temporary / "generation.txt", std::ios::binary | std::ios::trunc);
        staged << "staged";
        std::ofstream state(journal, std::ios::binary | std::ios::trunc);
        state << "{\n"
                 "  \"schemaVersion\": 1,\n"
                 "  \"state\": \"backedUp\",\n"
                 "  \"temporary\": \""
              << Keire::Detail::PathToUtf8(temporary) << "\",\n"
              << "  \"destination\": \"" << Keire::Detail::PathToUtf8(runtime) << "\",\n"
              << "  \"backup\": \"" << Keire::Detail::PathToUtf8(backup) << "\",\n"
              << "  \"hadDestination\": true\n"
                 "}\n";
    }

    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    CHECK(ReadAll(runtime / "generation.txt") == std::vector<char>{'l', 'a', 's', 't', '-', 'g', 'o', 'o', 'd'});
    CHECK_FALSE(std::filesystem::exists(backup));
    CHECK_FALSE(std::filesystem::exists(temporary));
    CHECK_FALSE(std::filesystem::exists(journal));
}

TEST_CASE("Texture importer exposes UI-independent production import options")
{
    const auto importer = Keire::CreateTexture2DAssetImporter();
    CHECK(importer.ImportOptions.size() == 13);
    CHECK(std::ranges::any_of(importer.ImportOptions, [](const auto& option) { return option.Key == "semantic"; }));
    CHECK(std::ranges::any_of(importer.ImportOptions,
                              [](const auto& option) { return option.Key == "environmentLayout"; }));
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

TEST_CASE("Cooking restores dependency-free outputs cached by a separate importer process")
{
    TemporaryAssetProject project;
    const auto id = Keire::AssetId::Generate();
    const auto type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000013");
    project.Write("Voice.private", "worker-produced audio");
    project.Write("Voice.private.keiremeta", std::string("{\n") +
                                                 "  \"schemaVersion\": 1,\n"
                                                 "  \"id\": \"" +
                                                 id.ToString() +
                                                 "\",\n"
                                                 "  \"type\": \"" +
                                                 type.ToString() +
                                                 "\",\n"
                                                 "  \"importer\": \"Test.PrivateWorker\",\n"
                                                 "  \"importerVersion\": 1,\n"
                                                 "  \"dependencies\": [],\n"
                                                 "  \"subAssets\": []\n"
                                                 "}\n");
    Keire::AssetImporterRegistration workerImporter;
    workerImporter.Name = "Test.PrivateWorker";
    workerImporter.Version = 2;
    workerImporter.Type = type;
    workerImporter.Extensions = {".private"};
    workerImporter.ContextualImport = [](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    workerImporter.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    {
        auto workerDatabase = Keire::CreateRef<Keire::AssetDatabase>(
            Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {workerImporter}});
        const auto imported = workerDatabase->ImportAll();
        CHECK(imported.Imported == 1);
        REQUIRE(workerDatabase->Find(id));
        CHECK(workerDatabase->Find(id)->ImporterVersion == 2);
    }

    std::atomic_size_t forbiddenImports = 0;
    std::atomic_size_t restoredOutputs = 0;
    auto toolImporter = workerImporter;
    toolImporter.ContextualImport = [&forbiddenImports](const Keire::AssetImportContext&,
                                                        const std::span<const std::byte>) -> Keire::AssetImportOutput
    {
        ++forbiddenImports;
        throw std::runtime_error("the public tool importer cannot decode this source");
    };
    toolImporter.RestoreCachedOutput = [&restoredOutputs](const std::span<const std::byte> bytes)
    {
        ++restoredOutputs;
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        return output;
    };
    auto toolDatabase = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {std::move(toolImporter)}});
    const auto cooked = Keire::AssetCooker::Cook(*toolDatabase, {}, project.Root / "CookFromWorkerCache");
    CHECK_NOTHROW(Keire::AssetCooker::Validate(cooked.CatalogPath));
    CHECK(forbiddenImports.load() == 0);
    CHECK(restoredOutputs.load() == 1);
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

TEST_CASE("Strict cooking requires discovered managed types and retains the previous output on rejection")
{
    TemporaryAssetProject project;
    const auto importer = Keire::CreateManagedDataAssetImporter();
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});

    Keire::ManagedAssetTypeDescriptor descriptor;
    descriptor.StableTypeId = Keire::ManagedTypeId::Parse("f2000000-0000-4000-8000-000000000001");
    descriptor.FullName = "Example.CookSettings";
    descriptor.DisplayName = "Cook Settings";
    descriptor.Properties = {{.StableFieldId = Keire::AssetId::Parse("f2000000-0000-4000-8000-000000000002"),
                              .Name = "Quality",
                              .DisplayName = "Quality",
                              .ManagedTypeName = "System.Int32",
                              .Kind = Keire::ManagedAssetPropertyKind::Integer}};

    Keire::ManagedDataDefinition definition;
    definition.ManagedType = descriptor.StableTypeId;
    definition.ManagedTypeName = descriptor.FullName;
    definition.Fields = {{.StableFieldId = descriptor.Properties.front().StableFieldId,
                          .Name = "Quality",
                          .ManagedTypeName = "System.Int32",
                          .Value = "3"}};
    const auto source = Keire::ManagedDataAsset::Encode(definition);
    (void)database->CreateAsset("CookSettings.keiredata", importer, source);

    const auto output = project.Root / "StrictManagedCook";
    std::filesystem::create_directories(output);
    {
        std::ofstream sentinel(output / "last-good.txt", std::ios::binary | std::ios::trunc);
        sentinel << "last good";
    }
    Keire::AssetBuildProfile strict;
    strict.Strict = true;
    CHECK_THROWS_WITH_AS((void)Keire::AssetCooker::Cook(*database, strict, output),
                         doctest::Contains("requires managed runtime compilation"), std::runtime_error);
    CHECK(ReadAll(output / "last-good.txt") == std::vector<char>{'l', 'a', 's', 't', ' ', 'g', 'o', 'o', 'd'});

    strict.ManagedTypeDiscoveryComplete = true;
    const std::array descriptors{descriptor};
    strict.ManagedTypeCatalog = Keire::EncodeManagedAssetTypeCatalog(descriptors);
    const auto cooked = Keire::AssetCooker::Cook(*database, strict, output);
    CHECK_NOTHROW(Keire::AssetCooker::Validate(cooked.CatalogPath));
    CHECK_FALSE(std::filesystem::exists(output / "last-good.txt"));
}

#if defined(_WIN32)
TEST_CASE("Asset cooking bounds transactional paths below the legacy Windows limit")
{
    TemporaryAssetProject project;
    project.Write("Payload.bin", "long publication path");
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    (void)database->ImportAll();

    const auto packName = std::filesystem::path("content-" + std::string(64, '0') + "-0.keirepak");
    auto output = project.Root / "Cook";
    constexpr std::size_t desiredFinalPackPath = 230;
    const auto initialLength = (output / packName).native().size();
    REQUIRE(initialLength + 1 < desiredFinalPackPath);
    output /= std::string(desiredFinalPackPath - initialLength - 1, 'p');
    REQUIRE((output / packName).native().size() == desiredFinalPackPath);
    CHECK((Keire::Detail::PathWithSuffix(output, ".tmp-" + Keire::AssetId::Generate().ToString()) / packName)
              .native()
              .size() >= 260);
    CHECK((Keire::Detail::PathWithSuffix(output, ".tmp-" + std::string(20, '0')) / packName).native().size() < 260);

    const auto cooked = Keire::AssetCooker::Cook(*database, {}, output);
    CHECK_NOTHROW(Keire::AssetCooker::Validate(cooked.CatalogPath));
}
#endif

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
    const auto firstCookCatalog = Keire::Detail::LoadCatalog(firstCook.CatalogPath);
    const auto secondCookCatalog = Keire::Detail::LoadCatalog(secondCook.CatalogPath);
    REQUIRE(!firstCookCatalog.Entries.empty());
    REQUIRE(!secondCookCatalog.Entries.empty());
    CHECK(ReadAll(firstCookCatalog.Entries.front().PackPath) == ReadAll(secondCookCatalog.Entries.front().PackPath));
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

    const auto collisionId = database->Duplicate(original->Id, "Collision.txt");
    const auto collisionTrash = database->TrashAsset(collisionId);
    project.Write("Collision.txt", "replacement");
    (void)database->Refresh();
    const auto replacement = database->Find("Collision.txt");
    REQUIRE(replacement);
    CHECK(replacement->Id != collisionId);
    database->RestoreTrash(collisionTrash.Id);
    CHECK(database->Find("Collision.txt")->Id == replacement->Id);
    CHECK(std::ranges::none_of(database->TrashRecords(),
                               [&](const Keire::AssetTrashRecord& record) { return record.Id == collisionTrash.Id; }));

    auto changeDatabase = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root,
                                          .ChangeDebounce = std::chrono::milliseconds(0),
                                          .ChangeMonitorInterval = std::chrono::milliseconds(1)});
    const auto waitForChange = [&](const Keire::AssetId expected)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do
        {
            const auto changed = changeDatabase->PollChangedAssets();
            if (std::ranges::find(changed, expected) != changed.end())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    project.Write("Greeting.txt", "updated assets");
    CHECK(waitForChange(original->Id));
    CHECK(changeDatabase->ImportAll().Imported > 0);
    {
        std::ofstream metadata(project.Root / "Assets/Greeting.txt.keiremeta", std::ios::app);
        metadata << ' ';
    }
    CHECK(waitForChange(original->Id));
    const auto metadataImport = changeDatabase->ImportAll();
    CHECK(metadataImport.Imported == 1);

    const auto idleStarted = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < 100; ++iteration)
        CHECK(changeDatabase->PollChangedAssets().empty());
    CHECK(std::chrono::steady_clock::now() - idleStarted < std::chrono::milliseconds(50));
    CHECK(changeDatabase->ChangeMonitorStatistics().PublishedScans > 0);
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

TEST_CASE("Generated subassets keep stable identities and are published with their parent")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.GeneratedSubAssets";
    importer.Version = 1;
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".generated"};
    importer.ContextualImport = [](const Keire::AssetImportContext& context, const std::span<const std::byte> bytes)
    {
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        const std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (source == "without child")
            return output;
        REQUIRE(context.ResolveSubAssetId);
        REQUIRE(context.ResolveSubAssetIdFor);
        REQUIRE(context.ResolveAssetSource);
        const auto child = context.ResolveSubAssetId("material/stable");
        CHECK(context.ResolveSubAssetIdFor(context.Asset, "material/stable") == child);
        const auto parentSource = context.ResolveAssetSource(context.Asset);
        REQUIRE(parentSource);
        CHECK(parentSource->Id == context.Asset);
        CHECK(parentSource->Type == Keire::TextAsset::StaticType());
        CHECK(parentSource->RelativePath == context.RelativePath);
        const std::string generated = "generated material payload";
        const auto generatedBytes = std::as_bytes(std::span(generated.data(), generated.size()));
        output.SubAssets.push_back({child,
                                    Keire::BinaryAsset::StaticType(),
                                    "material/stable",
                                    "Stable Material",
                                    {generatedBytes.begin(), generatedBytes.end()}});
        output.AssetDependencies.push_back(child);
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const std::string source = "with child";
    const auto parent = database->CreateAsset("Models/Generated.generated", importer,
                                              std::as_bytes(std::span(source.data(), source.size())));

    const auto firstImport = database->ImportAll();
    const auto firstRecord = database->Find(parent);
    REQUIRE(firstRecord);
    REQUIRE(firstRecord->SubAssets.size() == 1);
    const auto child = firstRecord->SubAssets.front();
    CHECK(child);
    Keire::AssetCooker::Validate(firstImport.CatalogPath);

    Keire::AssetSystemSpecification assetsSpecification;
    assetsSpecification.Mode = Keire::AssetMode::Development;
    assetsSpecification.DevelopmentCatalog = firstImport.CatalogPath;
    assetsSpecification.WorkerCount = 1;
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetsSpecification));
    const auto childHandle = assets->Load<Keire::BinaryAsset>(child);
    WaitFor(*assets, [&] { return childHandle.State() == Keire::AssetState::Ready; });
    const std::string expected = "generated material payload";
    CHECK(std::ranges::equal(childHandle.Get()->Bytes(), std::as_bytes(std::span(expected))));
    assets->Close();

    (void)database->ImportAll();
    REQUIRE(database->Find(parent));
    REQUIRE(database->Find(parent)->SubAssets.size() == 1);
    CHECK(database->Find(parent)->SubAssets.front() == child);

    project.Write("Models/Generated.generated", "without child");
    const auto reconciled = database->ImportAll();
    REQUIRE(database->Find(parent));
    CHECK(database->Find(parent)->SubAssets.empty());
    Keire::AssetCooker::Validate(reconciled.CatalogPath);
}

TEST_CASE("Generated model materials can be extracted as editable source assets")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration modelImporter;
    modelImporter.Name = "Test.ExtractableModel";
    modelImporter.Type = Keire::MeshAsset::StaticType();
    modelImporter.Extensions = {".model"};
    modelImporter.ContextualImport = [](const Keire::AssetImportContext& context, std::span<const std::byte>)
    {
        const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{1.0F, 0.0F, 0.0F}},
                                  Keire::MeshVertex{{0.0F, 1.0F, 0.0F}}};
        constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
        const Keire::MeshBounds bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
        const auto material = context.ResolveSubAssetId("material/Paint/0");
        const std::array submeshes{Keire::MeshSubmesh{0, 3, 0, bounds}};
        const std::array slots{Keire::MeshMaterialSlot{"Paint", material}};
        const std::array lods{Keire::MeshLod{0.0F, 0, 1, bounds}};
        Keire::MaterialAssetDefinition definition;
        definition.Properties.emplace("Tint", Keire::Color{0.2F, 0.4F, 0.6F, 1.0F});
        Keire::AssetImportOutput output;
        output.Bytes = Keire::MeshAsset::Encode(vertices, indices, submeshes, slots, lods);
        output.SubAssets.push_back({material, Keire::MaterialAsset::StaticType(), "material/Paint/0", "Paint",
                                    Keire::MaterialAsset::Encode(definition)});
        output.AssetDependencies.push_back(material);
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .Importers = {modelImporter, Keire::CreateMaterialAssetImporter()}});
    const std::string source = "model";
    const auto model = database->CreateAsset("Models/Vehicle.model", modelImporter,
                                             std::as_bytes(std::span(source.data(), source.size())));
    const auto extracted = database->ExtractMaterials(model, "Materials/Vehicle");
    REQUIRE(extracted.size() == 1);
    const auto record = database->Find(extracted.front());
    REQUIRE(record);
    CHECK(record->RelativePath == std::filesystem::path("Materials/Vehicle/Paint.keirematerial"));
    const auto materialSource = ReadAll(project.Root / "Assets" / record->RelativePath);
    const auto definition = Keire::MaterialAsset::DecodeSource(std::as_bytes(std::span(materialSource)));
    REQUIRE(std::holds_alternative<Keire::Vector4>(definition.Properties.at("Tint")));
    CHECK(std::get<Keire::Vector4>(definition.Properties.at("Tint")).Z == doctest::Approx(0.6F));
}

TEST_CASE("Cooked asset pages support bounded asynchronous range reads")
{
    TemporaryAssetProject project;
    std::string payload(std::size_t{20} * 1024U, '\0');
    for (std::size_t index = 0; index < payload.size(); ++index)
        payload[index] = static_cast<char>((index * 31U) & 0xffU);
    project.Write("Stream.bin", payload);
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto record = database->Find("Stream.bin");
    REQUIRE(record);
    Keire::AssetBuildProfile profile;
    profile.StreamPageBytes = 4096;
    const auto cooked = Keire::AssetCooker::Cook(*database, profile, project.Root / "StreamCook");
    CHECK_NOTHROW(Keire::AssetCooker::Validate(cooked.CatalogPath));
    const auto catalogCharacters = ReadAll(cooked.CatalogPath);
    const std::string catalog(catalogCharacters.begin(), catalogCharacters.end());
    CHECK(catalog.find("\"pages\"") != std::string::npos);
    CHECK(catalog.find("\"uncompressedOffset\": 4096") != std::string::npos);

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Cooked;
    specification.WorkerCount = 1;
    specification.MaximumStreamReadBytes = 8192;
    specification.Mounts.push_back({cooked.CatalogPath});
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const auto operation = assets->ReadRangeAsync(record->Id, 3584, 6144);
    REQUIRE(operation->Wait(std::chrono::seconds(5)));
    CHECK(operation->State() == Keire::AssetStreamState::Succeeded);
    const auto result = operation->Result();
    REQUIRE(result.size() == 6144);
    CHECK(std::ranges::equal(result, std::as_bytes(std::span(payload)).subspan(3584, 6144)));

    Keire::StreamingBudgetSpecification streamingSpecification;
    streamingSpecification.General.CpuBytes = 8192;
    auto streaming = Keire::CreateRef<Keire::StreamingSystem>(streamingSpecification, assets);
    const auto residency = streaming->Request(
        {.Asset = record->Id, .Range = {.Offset = 3584, .Bytes = 6144}, .Priority = Keire::AssetPriority::High});
    for (std::size_t attempt = 0; attempt < 500 && !streaming->Snapshot(residency).CpuBytes; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        (void)streaming->Pump();
    }
    CHECK(streaming->Snapshot(residency).State == Keire::ResidencyState::Resident);
    CHECK(streaming->ResidentData(residency) == result);
    REQUIRE(streaming->Statistics().size() == 5);
    CHECK(streaming->Statistics().front().ResidentCpuBytes == result.size());
    CHECK(streaming->Statistics().front().CompletedRequests == 1U);
    CHECK(streaming->Statistics().front().AverageLatencyMilliseconds >= 0.0);
    bool workerControlsSucceeded = false;
    std::thread observer(
        [&]
        {
            workerControlsSucceeded = streaming->Snapshot(residency).State == Keire::ResidencyState::Resident &&
                                      streaming->Statistics().front().CompletedRequests == 1U &&
                                      streaming->SetPinned(residency, true) && streaming->Touch(residency) &&
                                      streaming->SetPinned(residency, false);
        });
    observer.join();
    CHECK(workerControlsSucceeded);
    std::thread retirementReporter([&] { streaming->ReportRetired(Keire::StreamingClass::Texture, 128U, 512U); });
    retirementReporter.join();
    auto retirementStatistics = streaming->Statistics();
    CHECK(retirementStatistics[1].RetiredCpuBytes == 128U);
    CHECK(retirementStatistics[1].RetiredGpuBytes == 512U);
    std::thread retirementReleaser([&] { streaming->ReleaseRetired(Keire::StreamingClass::Texture, 128U, 512U); });
    retirementReleaser.join();
    retirementStatistics = streaming->Statistics();
    CHECK(retirementStatistics[1].RetiredCpuBytes == 0U);
    CHECK(retirementStatistics[1].RetiredGpuBytes == 0U);
    const auto cancelledResidency = streaming->Request(
        {.Asset = record->Id, .Range = {.Offset = 0, .Bytes = 128}, .Priority = Keire::AssetPriority::Normal});
    bool workerCancellationSucceeded = false;
    std::thread canceller([&] { workerCancellationSucceeded = streaming->Cancel(cancelledResidency); });
    canceller.join();
    CHECK(workerCancellationSucceeded);
    CHECK(streaming->Snapshot(cancelledResidency).State == Keire::ResidencyState::Cancelled);
    CHECK(streaming->Statistics().front().CancelledRequests == 1U);
    CHECK(streaming->Release(cancelledResidency));
    bool workerReleaseSucceeded = false;
    std::thread releaser([&] { workerReleaseSucceeded = streaming->Release(residency); });
    releaser.join();
    CHECK(workerReleaseSucceeded);
    CHECK_THROWS_AS((void)streaming->Snapshot(residency), std::invalid_argument);
    const auto closeCancelledResidency = streaming->Request(
        {.Asset = record->Id, .Range = {.Offset = 0, .Bytes = 128}, .Priority = Keire::AssetPriority::Normal});
    CHECK(closeCancelledResidency.IsValid());
    std::thread closer([&] { streaming->Close(); });
    closer.join();
    CHECK_FALSE(streaming->IsOpen());
    CHECK(streaming->Statistics().front().CancelledRequests == 2U);
    CHECK(streaming->Statistics().front().RequestedBytes == 0U);
    CHECK(streaming->Statistics().front().InFlightBytes == 0U);

    CHECK_THROWS_AS((void)assets->ReadRangeAsync(record->Id, 0, 8193), std::invalid_argument);
    CHECK_THROWS_AS((void)assets->ReadRangeAsync(record->Id, payload.size() - 10U, 20), std::out_of_range);
    assets->Close();
    CHECK_THROWS_AS((void)assets->ReadRangeAsync(record->Id, 0, 1), std::logic_error);
}

TEST_CASE("Cooked stream layouts expose semantic segments and preserve monolithic catalogs")
{
    TemporaryAssetProject project;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.StreamTexture";
    importer.Type = Keire::Texture2DAsset::StaticType();
    importer.Extensions = {".streamtexture"};
    importer.Import = [](std::span<const std::byte>)
    {
        Keire::TextureImportSettings settings;
        std::vector<Keire::TextureMipLevel> mips;
        for (std::uint32_t dimension = 64U; dimension != 0U; dimension /= 2U)
        {
            Keire::TextureMipLevel mip;
            mip.Width = dimension;
            mip.Height = dimension;
            mip.Pixels.resize(static_cast<std::size_t>(dimension) * dimension * 4U, std::byte{0x5a});
            mips.push_back(std::move(mip));
            if (dimension == 1U)
                break;
        }
        return Keire::Texture2DAsset::Encode(settings, mips);
    };
    Keire::JobSystemSpecification jobSpecification;
    jobSpecification.WorkerCount = 2;
    jobSpecification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(jobSpecification);
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .Importers = {importer, Keire::CreateTexture2DAssetImporter()}, .Jobs = jobs});
    const std::string source = "semantic texture";
    const auto texture = database->CreateAsset("Textures/Test.streamtexture", importer,
                                               std::as_bytes(std::span(source.data(), source.size())));
    Keire::AssetBuildProfile profile;
    profile.StreamPageBytes = 4096U;
    const auto submittedBeforeCook = jobs->Statistics().SubmittedJobs;
    const auto cooked = Keire::AssetCooker::Cook(*database, profile, project.Root / "SemanticCook");
    CHECK(jobs->Statistics().SubmittedJobs > submittedBeforeCook);

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Cooked;
    specification.Mounts.push_back({cooked.CatalogPath});
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const auto layout = assets->TryGetStreamLayout(texture);
    REQUIRE(layout);
    CHECK(layout->Version == 1U);
    CHECK_FALSE(layout->MonolithicCompatibility);
    CHECK(std::ranges::any_of(layout->Segments, [](const Keire::AssetStreamSegment& segment)
                              { return segment.Kind == Keire::AssetStreamSegmentKind::Metadata; }));
    const auto mip = std::ranges::find_if(
        layout->Segments, [](const Keire::AssetStreamSegment& segment)
        { return segment.Kind == Keire::AssetStreamSegmentKind::TextureMip && segment.Segment == 2U; });
    REQUIRE(mip != layout->Segments.end());

    auto streaming = Keire::CreateRef<Keire::StreamingSystem>(Keire::StreamingBudgetSpecification{}, assets);
    const auto request = streaming->RequestTextureMip(texture, 2U);
    for (std::size_t attempt = 0; attempt < 500 && streaming->Snapshot(request).State == Keire::ResidencyState::Loading;
         ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        (void)streaming->Pump();
    }
    CHECK(streaming->Snapshot(request).State == Keire::ResidencyState::Resident);
    CHECK(streaming->ResidentData(request).size() == mip->Bytes);
    streaming->Close();
    assets->Close();

    auto legacyDocument = nlohmann::json::parse(ReadAll(cooked.CatalogPath));
    legacyDocument["schemaVersion"] = 2;
    for (auto& entry : legacyDocument["assets"])
        entry.erase("segments");
    const auto legacyCatalog = cooked.CatalogPath.parent_path() / "legacy-catalog.json";
    {
        std::ofstream output(legacyCatalog, std::ios::binary | std::ios::trunc);
        output << legacyDocument.dump(2) << '\n';
        REQUIRE(output.good());
    }
    specification.Mounts = {{legacyCatalog}};
    auto legacyAssets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const auto legacyLayout = legacyAssets->TryGetStreamLayout(texture);
    REQUIRE(legacyLayout);
    CHECK(legacyLayout->Version == 0U);
    CHECK(legacyLayout->MonolithicCompatibility);
    REQUIRE(legacyLayout->Segments.size() == 1U);
    CHECK(legacyLayout->Segments.front().Kind == Keire::AssetStreamSegmentKind::Data);
    legacyAssets->Close();
    database.Reset();
    jobs->Close();
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

    const auto catalog = Keire::Detail::LoadCatalog(imported.CatalogPath);
    REQUIRE(!catalog.Entries.empty());
    const auto pack = catalog.Entries.front().PackPath;
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
