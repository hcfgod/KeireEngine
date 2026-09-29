#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "KeireInternal/Assets/AssetImportDrafts.h"
#include "KeireInternal/Assets/AssetSourceDiscovery.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>

TEST_CASE("asset import drafts preserve pending edits across selection and external import changes")
{
    Keire::Internal::AssetImportDrafts drafts;
    const auto fox = Keire::AssetId::Generate();
    const auto human = Keire::AssetId::Generate();
    const Keire::AssetImportSettings original{{"rigSource", std::string("embedded")}};
    auto& edited = drafts.Select(fox, original);
    edited.Values["rigSource"] = std::string("generate");
    edited.Dirty = true;
    CHECK(drafts.Select(human, original).Values == original);
    const Keire::AssetImportSettings external{{"rigSource", std::string("none")}};
    CHECK(std::get<std::string>(drafts.Select(fox, external).Values.at("rigSource")) == "generate");
    CHECK(drafts.Select(fox, external).Dirty);
    CHECK(drafts.Select(fox, external).Baseline == original);
    drafts.Revert(fox, external);
    CHECK_FALSE(drafts.Select(fox, external).Dirty);
    CHECK(drafts.Select(fox, external).Values == external);
    CHECK(drafts.Select(fox, external).Baseline == external);
    CHECK(drafts.Select(human, external).Values == external);
}

TEST_CASE("import settings refresh clean inspectors and preserve conflicting drafts")
{
    using Keire::Internal::SynchronizeImportSettingsDraft;
    const Keire::AssetImportSettings original{{"rigProfile", std::string("humanoid")}};
    const Keire::AssetImportSettings external{{"rigProfile", std::string("quadruped")}};
    auto baseline = original;
    auto draft = original;
    CHECK_FALSE(SynchronizeImportSettingsDraft(external, baseline, draft));
    CHECK(baseline == external);
    CHECK(draft == external);
    draft["maximumInfluences"] = std::string("8");
    const auto pending = draft;
    CHECK(SynchronizeImportSettingsDraft(original, baseline, draft));
    CHECK(baseline == external);
    CHECK(draft == pending);
    CHECK_FALSE(SynchronizeImportSettingsDraft(external, baseline, draft));
    CHECK(draft == pending);
}

TEST_CASE("asset source discovery waits for stable files and forgets removed candidates")
{
    using namespace std::chrono_literals;
    Keire::Detail::AssetSourceDiscovery discovery;
    const auto start = Keire::Detail::AssetSourceDiscovery::Clock::time_point{};
    const Keire::Detail::AnchoredFileSignature initial{1, 0};
    const Keire::Detail::AnchoredFileSignature changed{2, 12};
    const auto observe = [&](const auto signature, const auto elapsed)
    {
        discovery.BeginScan();
        const auto ready = discovery.Ready("Scene.keirescene", signature, start + elapsed, 250ms);
        discovery.EndScan();
        return ready;
    };
    CHECK_FALSE(observe(initial, 0ms));
    CHECK_FALSE(observe(initial, 249ms));
    CHECK(observe(initial, 250ms));
    CHECK_FALSE(observe(changed, 251ms));
    CHECK_FALSE(observe(changed, 500ms));
    CHECK(observe(changed, 501ms));

    discovery.BeginScan();
    discovery.EndScan();
    CHECK_FALSE(observe(changed, 1000ms));
    CHECK(observe(changed, 1250ms));

    discovery.BeginScan();
    CHECK(discovery.Ready("Immediate.txt", initial, start, 0ms));
    discovery.EndScan();
}

TEST_CASE("asset monitor does not reserve transient save dialog filenames")
{
    using namespace std::chrono_literals;
    struct Project final
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("DiscoveryTests-" + Keire::AssetId::Generate().ToString()));
        Project() { std::filesystem::create_directories(Root / "Assets"); }
        ~Project()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } project;
    auto importer = Keire::CreateTextAssetImporter();
    importer.Extensions = {".txt"};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .ChangeDebounce = 10s, .ChangeMonitorInterval = 1ms, .Importers = {importer}});
    const auto source = project.Root / "Assets/Workshop.txt";
    const auto metadata = project.Root / "Assets/Workshop.txt.keiremeta";
    std::ofstream(source).close();
    const auto scans = database->ChangeMonitorStatistics().PublishedScans;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (database->ChangeMonitorStatistics().PublishedScans < scans + 2 &&
           std::chrono::steady_clock::now() < deadline)
    {
        (void)database->PollChangedAssets();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(database->ChangeMonitorStatistics().PublishedScans >= scans + 2);
    CHECK_FALSE(std::filesystem::exists(metadata));
    CHECK_FALSE(database->Find("Workshop.txt"));
    std::filesystem::remove(source);

    const std::string text = "Saved workshop";
    const auto id = database->CreateAsset("Workshop.txt", importer, std::as_bytes(std::span(text)));
    REQUIRE(database->Find(id));
    CHECK(std::filesystem::exists(metadata));

    // Explicit refresh remains immediate even with a long monitor debounce.
    std::ofstream(project.Root / "Assets/External.txt") << "external";
    CHECK(database->Refresh() == 2);
    CHECK(database->Find("Workshop.txt")->Id == id);
    CHECK(database->Find("External.txt").has_value());
    database.Reset();
}
