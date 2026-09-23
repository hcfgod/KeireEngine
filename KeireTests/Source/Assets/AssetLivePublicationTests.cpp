#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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

} // namespace

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

    SUBCASE("a live edit supersedes an in-flight catalog reload")
    {
        REQUIRE(assets->Reload(record.Id));
        REQUIRE(assets->PublishDevelopmentAsset(record.Id, Keire::CreateRef<Keire::TextAsset>("newer live edit")));
        const auto publishedRevision = handle.Revision();
        std::size_t drained = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!drained && std::chrono::steady_clock::now() < deadline)
        {
            drained += assets->PumpCompletions();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(drained == 1);
        CHECK(handle.State() == Keire::AssetState::Ready);
        CHECK(handle.Get()->Text() == "newer live edit");
        CHECK(handle.Revision() == publishedRevision);
        CHECK(loaded.back().Revision == publishedRevision);
        REQUIRE(assets->Reload(record.Id));
        WaitFor(*assets, [&handle] { return handle.State() == Keire::AssetState::Ready; });
        CHECK(handle.Get()->Text() == "catalog value");
        CHECK(handle.Revision() == publishedRevision + 1);
    }

    assets->Close();
    events->Close();
}
