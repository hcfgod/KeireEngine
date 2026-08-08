#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubProjectMetadataWorkflow.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>

using namespace KeireHub;
using namespace std::chrono_literals;

namespace
{
    constexpr std::string_view ProjectA = "11111111-1111-4111-8111-111111111111";
    constexpr std::string_view ProjectB = "22222222-2222-4222-8222-222222222222";

    void WriteProject(const std::filesystem::path& root, const std::string_view id, const std::string_view name)
    {
        const nlohmann::json descriptor{{"schemaVersion", 3},
                                        {"id", std::string(id)},
                                        {"name", std::string(name)},
                                        {"createdWithEngineVersion", "0.1.0"},
                                        {"minimumEngineVersion", "0.1.0"},
                                        {"createdAt", "2026-08-06T12:34:56Z"},
                                        {"lastSavedWithEngineVersion", "0.2.0"}};
        KeireHubTests::WriteText(root / "ProjectSettings/Project.keireproject", descriptor.dump(2) + '\n');
    }

    [[nodiscard]] bool WaitForScan(HubProjectMetadataWorkflow& workflow, const std::size_t expected)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto snapshot = workflow.Snapshot();
            if (snapshot->Running && snapshot->Completed == expected && snapshot->Total == expected)
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
} // namespace

TEST_CASE("Project metadata workflow leaves every cached result unchanged when one project disappears before commit")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto rootA = temporary.Path() / "A";
    const auto rootB = temporary.Path() / "B";
    WriteProject(rootA, ProjectA, "A");
    WriteProject(rootB, ProjectB, "B");

    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(100));
    REQUIRE(controller.Projects().Upsert({.Id = std::string(ProjectA), .Root = rootA, .Name = "A"}));
    REQUIRE(controller.Projects().Upsert({.Id = std::string(ProjectB), .Root = rootB, .Name = "B"}));
    const auto scanOrder = controller.Projects().Snapshot();
    REQUIRE(scanOrder->size() == 2);
    const auto survivingId = scanOrder->front().Id;
    const auto removedId = scanOrder->back().Id;

    HubProjectMetadataWorkflow workflow;
    REQUIRE(workflow.Start(controller));
    REQUIRE(WaitForScan(workflow, scanOrder->size()));
    REQUIRE(controller.Projects().Remove(removedId));
    const auto beforeSnapshot = controller.Projects().Snapshot();
    const auto beforeDocument = KeireHubTests::ReadText(controller.Projects().Path());

    std::optional<HubError> failure;
    bool unexpectedlyCompleted = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto polled = workflow.Poll(controller);
        if (!polled)
        {
            failure = polled.Error();
            break;
        }
        if (polled.Value())
        {
            unexpectedlyCompleted = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }

    CHECK_FALSE(unexpectedlyCompleted);
    REQUIRE(failure.has_value());
    CHECK(failure->Code == HubErrorCode::NotFound);
    CHECK(controller.Projects().Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(controller.Projects().Path()) == beforeDocument);
    const auto afterSnapshot = controller.Projects().Snapshot();
    const auto surviving = std::ranges::find(*afterSnapshot, survivingId, &HubRecentProject::Id);
    REQUIRE(surviving != afterSnapshot->end());
    CHECK(surviving->CachedMetadata.Status == HubProjectStatus::Unknown);
    CHECK_FALSE(surviving->CachedMetadata.SizeBytes.has_value());
}
