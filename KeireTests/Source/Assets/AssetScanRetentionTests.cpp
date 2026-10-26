#include "Keire/Assets/AssetPipeline.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct ScanFixture final
    {
        ScanFixture()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-ScanRetention-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Root / "Assets");
            Keire::Detail::WriteTextFileAtomically(Root / "Assets/Main.scanretention", "initial source");
            Keire::AssetImporterRegistration importer;
            importer.Name = "Test.ScanRetention";
            importer.Type = Keire::TextAsset::StaticType();
            importer.Extensions = {".scanretention"};
            importer.ImportOptions.push_back({.Key = "mode", .DefaultValue = false});
            importer.ContextualImport =
                [](const Keire::AssetImportContext& context, const std::span<const std::byte> bytes)
            {
                Keire::AssetImportOutput output;
                output.Bytes.assign(bytes.begin(), bytes.end());
                const auto child = context.ResolveSubAssetId("child");
                output.SubAssets.push_back({child, Keire::TextAsset::StaticType(), "child", "Child", output.Bytes});
                output.AssetDependencies.push_back(child);
                output.Metadata.DisplayName = "Imported display metadata";
                (void)context.ReadProjectFile("Assets/Main.scanretention");
                return output;
            };
            Database = Keire::CreateRef<Keire::AssetDatabase>(
                Keire::AssetDatabaseSpecification{.ProjectRoot = Root,
                                                  .ChangeDebounce = std::chrono::milliseconds(0),
                                                  .ChangeMonitorInterval = std::chrono::milliseconds(1),
                                                  .Importers = {importer}});
            if (Database->ImportAll().Imported != 1U)
            {
                throw std::runtime_error("Scan retention fixture did not import its source.");
            }
            Imported = Record();
            WaitForScan();
            (void)Database->PollChangedAssets();
        }

        ~ScanFixture()
        {
            Database.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::AssetSourceRecord Record() const
        {
            const auto record = Database->Find("Main.scanretention");
            if (!record)
            {
                throw std::runtime_error("Scan retention fixture lost its source record.");
            }
            return *record;
        }

        void WaitForScan() const
        {
            const auto initial = Database->ChangeMonitorStatistics().PublishedScans;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline)
            {
                (void)Database->PollChangedAssets();
                if (Database->ChangeMonitorStatistics().PublishedScans > initial)
                {
                    (void)Database->PollChangedAssets();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("Scan retention fixture did not publish a background scan.");
        }

        template <typename Predicate> void WaitForChange(Predicate&& recordMatches) const
        {
            bool detectedChange = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline)
            {
                const auto changed = Database->PollChangedAssets();
                if (std::ranges::find(changed, Imported.Id) != changed.end())
                {
                    detectedChange = true;
                }
                // Import's earlier atomic metadata publication can still have a queued signature change.
                // Observe the requested contract too, rather than treating that older notification as this edit.
                if (detectedChange && recordMatches(Record()))
                {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("Scan retention fixture did not detect the edited source/metadata.");
        }

        void CheckRetained() const
        {
            const auto record = Record();
            CHECK(record.Dependencies == Imported.Dependencies);
            REQUIRE(record.SourceDependencies.size() == Imported.SourceDependencies.size());
            CHECK(record.SourceDependencies.front().RelativePath == Imported.SourceDependencies.front().RelativePath);
            CHECK(record.SourceDependencies.front().Digest == Imported.SourceDependencies.front().Digest);
            CHECK(record.Metadata == Imported.Metadata);
        }

        std::filesystem::path Root;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetSourceRecord Imported;
    };
} // namespace

TEST_CASE("Asset background scans retain imported dependencies and derived metadata")
{
    ScanFixture fixture;
    REQUIRE(fixture.Imported.SourceDependencies.size() == 1U);
    REQUIRE_FALSE(fixture.Imported.Metadata.DisplayName.empty());
    SUBCASE("ordinary signature scans") {}
    SUBCASE("digest verification after a worker source-index reload")
    {
        const auto index = fixture.Root / "Library/source-index.json";
        Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*fixture.Database, index);
        REQUIRE(Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*fixture.Database, index) == 1U);
    }
    for (std::size_t iteration = 0; iteration < 3U; ++iteration)
    {
        fixture.WaitForScan();
        fixture.CheckRetained();
    }
}

TEST_CASE("Asset source edits retain last-good dependency metadata until reimport")
{
    ScanFixture fixture;
    Keire::Detail::WriteTextFileAtomically(fixture.Root / "Assets/Main.scanretention", "edited longer source");
    fixture.WaitForChange([](const auto&) { return true; });
    fixture.CheckRetained();
    fixture.WaitForScan();
    fixture.CheckRetained();
}

TEST_CASE("Asset metadata contract changes do not retain the old imported dependency metadata")
{
    ScanFixture fixture;
    const auto path = fixture.Root / "Assets/Main.scanretention.keiremeta";
    nlohmann::json metadata;
    {
        std::ifstream input(path, std::ios::binary);
        input >> metadata;
    }
    metadata["importSettings"]["mode"] = true;
    Keire::Detail::WriteTextFileAtomically(path, metadata.dump(2) + '\n');
    fixture.WaitForChange(
        [](const auto& record)
        {
            const auto setting = record.ImportSettings.find("mode");
            return setting != record.ImportSettings.end() && std::get<bool>(setting->second);
        });
    CHECK(fixture.Record().SourceDependencies.empty());
    CHECK(fixture.Record().Metadata.DisplayName.empty());
    CHECK(std::get<bool>(fixture.Record().ImportSettings.at("mode")));
}
