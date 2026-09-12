#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireTests/TestSupport.h"
#include <array>
#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <stdexcept>
#include <system_error>

TEST_CASE("worker import diagnostics reach the editor database transactionally")
{
    struct Directory final
    {
        std::filesystem::path Path = KeireTests::MakeTestDirectory("WorkerStatus");
        ~Directory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }
    } directory;
    auto importer = Keire::CreateTextAssetImporter();
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = directory.Path, .Importers = {importer}});
    const auto id = database->CreateAsset("Source.cs", importer, {});
    const std::array failed{
        Keire::AssetImportStatus{.Id = id,
                                 .State = Keire::AssetImportState::Failed,
                                 .Diagnostics = {{.RelativePath = "Source.cs", .Message = "Malformed source"}}}};
    Keire::Detail::AssetDatabaseWorkerAccess::ApplyImportStatuses(*database, failed);
    CHECK(database->ImportStatus(id).State == Keire::AssetImportState::Failed);
    REQUIRE(database->ImportStatus(id).Diagnostics.size() == 1);
    CHECK(database->ImportStatus(id).Diagnostics.front().Message == "Malformed source");
    const std::array invalid{
        Keire::AssetImportStatus{.Id = id, .State = Keire::AssetImportState::Imported},
        Keire::AssetImportStatus{.Id = Keire::AssetId::Generate(), .State = Keire::AssetImportState::Failed}};
    CHECK_THROWS_AS(Keire::Detail::AssetDatabaseWorkerAccess::ApplyImportStatuses(*database, invalid),
                    std::invalid_argument);
    CHECK(database->ImportStatus(id).State == Keire::AssetImportState::Failed);
    const std::array recovered{Keire::AssetImportStatus{.Id = id, .State = Keire::AssetImportState::Imported}};
    Keire::Detail::AssetDatabaseWorkerAccess::ApplyImportStatuses(*database, recovered);
    CHECK(database->ImportStatus(id).State == Keire::AssetImportState::Imported);
    CHECK(database->ImportStatus(id).Diagnostics.empty());
}

TEST_CASE("worker progress throttles intermediate disk writes but always publishes completion")
{
    using namespace std::chrono_literals;
    Keire::Detail::AssetWorkerProgressThrottle throttle;
    const auto start = Keire::Detail::AssetWorkerProgressThrottle::Clock::time_point{};
    CHECK(throttle.ShouldPublish({.Phase = Keire::AssetOperationPhase::Scanning}, start));
    CHECK_FALSE(throttle.ShouldPublish({.Phase = Keire::AssetOperationPhase::Importing}, start + 1ms));
    CHECK_FALSE(throttle.ShouldPublish({.Phase = Keire::AssetOperationPhase::Publishing}, start + 49ms));
    CHECK(throttle.ShouldPublish({.Phase = Keire::AssetOperationPhase::Publishing}, start + 50ms));
    CHECK(throttle.ShouldPublish({.Phase = Keire::AssetOperationPhase::Completed}, start + 51ms));
}
