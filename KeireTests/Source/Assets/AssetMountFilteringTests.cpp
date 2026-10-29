#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct FilteredMountProject final
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("FilteredMount-" + Keire::AssetId::Generate().ToString()));

        FilteredMountProject()
        {
            std::filesystem::create_directories(Root / "Assets");
            for (const auto name : {"leaf", "parent", "root", "independent"})
            {
                std::ofstream(Root / "Assets" / (std::string(name) + ".txt")) << name;
            }
        }

        ~FilteredMountProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };

    void FinishLoad(Keire::AssetSystem& assets, const Keire::AssetHandle<Keire::TextAsset>& handle)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (handle.State() != Keire::AssetState::Ready && handle.State() != Keire::AssetState::Failed &&
               std::chrono::steady_clock::now() < deadline)
        {
            (void)assets.PumpCompletions();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
} // namespace

TEST_CASE("Filtered catalog mounts withhold incompatible dependency closures and recover pending handles")
{
    FilteredMountProject project;
    auto database =
        Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root});
    const auto imported = database->ImportAll();
    auto catalog = Keire::Detail::LoadCatalog(imported.CatalogPath);
    REQUIRE(catalog.Entries.size() == 4);
    const auto leaf = catalog.Entries[0].Id;
    const auto parent = catalog.Entries[1].Id;
    const auto root = catalog.Entries[2].Id;
    const auto independent = catalog.Entries[3].Id;
    catalog.Entries[1].Dependencies = {leaf};
    catalog.Entries[2].Dependencies = {parent};
    // Loaded entries resolve pack paths; the catalog writer expects portable relative paths.
    for (auto& entry : catalog.Entries)
    {
        entry.PackPath = entry.PackPath.lexically_relative(imported.CatalogPath.parent_path());
    }
    Keire::Detail::WriteCatalog(imported.CatalogPath, catalog.Entries);

    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog = project.Root / "absent.json";
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification, Keire::CreateRef<Keire::EventBus>());
    Keire::AssetMountSpecification mount{imported.CatalogPath};
    mount.ExcludedAssets = {leaf};
    SUBCASE("an explicit empty allowlist exposes no cooked assets")
    {
        mount.AllowedAssets.emplace();
        assets->Mount(mount);
        CHECK_FALSE(assets->TryGetType(leaf));
        CHECK_FALSE(assets->TryGetType(independent));
        assets->Close();
        return;
    }
    SUBCASE("an allowlist also excludes unknown source-less entries")
    {
        mount.ExcludedAssets.clear();
        mount.AllowedAssets = std::vector{parent, root, independent};
    }
    assets->Mount(mount);
    CHECK_FALSE(assets->TryGetType(leaf));
    CHECK_FALSE(assets->TryGetType(parent));
    CHECK_FALSE(assets->TryGetType(root));
    CHECK(assets->TryGetType(independent));
    const auto blocked = assets->Load<Keire::TextAsset>(root);
    FinishLoad(*assets, blocked);
    CHECK(blocked.State() == Keire::AssetState::Failed);
    CHECK(blocked.Diagnostic().Message.find("compatible catalog refresh") != std::string::npos);
    const auto ready = assets->Load<Keire::TextAsset>(independent);
    FinishLoad(*assets, ready);
    REQUIRE(ready.State() == Keire::AssetState::Ready);
    const auto original = ready.Get();

    REQUIRE(assets->Unmount(imported.CatalogPath));
    assets->Mount({imported.CatalogPath});
    FinishLoad(*assets, blocked);
    CHECK(blocked.State() == Keire::AssetState::Ready);
    CHECK(ready.Get() == original);

    REQUIRE(assets->Unmount(imported.CatalogPath));
    assets->Mount(mount);
    CHECK_FALSE(assets->TryGetType(root));
    CHECK(blocked.State() == Keire::AssetState::Ready);
    assets->Close();
}
