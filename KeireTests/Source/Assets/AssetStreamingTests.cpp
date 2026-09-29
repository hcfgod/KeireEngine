#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Event.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Log.h"
#include "Keire/Streaming/StreamingSystem.h"
#include "KeireTests/AssetTestProject.h"
#include "KeireTests/TestSupport.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using KeireTests::ReadAll;
    using KeireTests::TemporaryAssetProject;
    using KeireTests::WaitFor;
} // namespace

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
    const auto runtimeHandle = assets->Load(record.Id, Keire::TextAsset::StaticType(), Keire::AssetPriority::High);
    REQUIRE(handle.Get());
    REQUIRE(runtimeHandle.Get());
    CHECK(handle.Get()->Text().empty());
    CHECK(runtimeHandle.Get()->Type() == Keire::TextAsset::StaticType());
    CHECK(handle.UsingFallback());
    CHECK(runtimeHandle.UsingFallback());

    WaitFor(*assets, [&handle] { return handle.State() == Keire::AssetState::Ready; });
    REQUIRE(handle.Get());
    CHECK(handle.Get()->Text() == "hello assets");
    CHECK_FALSE(handle.UsingFallback());
    CHECK(handle.Revision() == 1);
    CHECK(handle.Require()->Text() == "hello assets");
    CHECK(runtimeHandle.State() == Keire::AssetState::Ready);
    CHECK_FALSE(runtimeHandle.UsingFallback());
    CHECK(runtimeHandle.Revision() == 1);
    REQUIRE(Keire::DynamicRefCast<const Keire::TextAsset>(runtimeHandle.Require()));
    CHECK(Keire::DynamicRefCast<const Keire::TextAsset>(runtimeHandle.Require())->Text() == "hello assets");

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
    try
    {
        (void)assets->Load<Keire::BinaryAsset>(missing.Id());
        FAIL("A conflicting handle type must be rejected.");
    }
    catch (const std::invalid_argument& error)
    {
        const std::string diagnostic = error.what();
        CHECK(diagnostic.find(missing.Id().ToString()) != std::string::npos);
        CHECK(diagnostic.find("requested as type") != std::string::npos);
        CHECK(diagnostic.find("existing handle uses type") != std::string::npos);
    }
    CHECK(missing.State() == Keire::AssetState::Failed);
    CHECK(missing.Get()->Text().empty());
    assets->Close();
}

TEST_CASE("indexed source loading rejects missing files without changing the current database")
{
    TemporaryAssetProject project;
    auto importer = Keire::CreateTextAssetImporter();
    project.Write("Source.cs", "source");
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const auto index = project.Root / "Library/Assets/Runtime/source-index.json";
    Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, index);
    const auto before = database->Records();
    REQUIRE(before.size() == 1);
    std::filesystem::remove(project.Root / "Assets/Source.cs");
    CHECK_THROWS((void)Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*database, index));
    REQUIRE(database->Records().size() == 1);
    CHECK(database->Records().front().Id == before.front().Id);
}
