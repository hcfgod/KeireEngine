#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubProjectWorkflow.h"
#include "KeireHub/HubProjectsUi.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <ranges>
#include <string>

using namespace KeireHub;

TEST_CASE("Hub duplicate submission keeps rejected dialogs open and closes only accepted requests")
{
    HubDuplicateSubmission submission;
    submission.Resolve({});
    CHECK_FALSE(submission.CloseRequested);
    REQUIRE(submission.Begin());
    CHECK(submission.Waiting);
    CHECK_FALSE(submission.Begin());
    submission.Resolve("The duplicate destination already exists.");
    CHECK_FALSE(submission.Waiting);
    CHECK_FALSE(submission.CloseRequested);
    CHECK(submission.Error == "The duplicate destination already exists.");
    submission.Resolve({});
    CHECK_FALSE(submission.CloseRequested);
    REQUIRE(submission.Begin());
    CHECK(submission.Error.empty());
    submission.Resolve({});
    CHECK_FALSE(submission.Waiting);
    CHECK(submission.CloseRequested);
    CHECK_FALSE(submission.Begin());
    submission = {};
    REQUIRE(submission.Begin());
    submission = {};
    submission.Resolve("Late result after cancellation");
    CHECK(submission.Error.empty());
    CHECK_FALSE(submission.CloseRequested);
    CHECK_FALSE(submission.Waiting);
}

TEST_CASE("Hub creation reveals the new project despite every previous filter")
{
    HubProjectsUi projects;
    projects.SetPreferences(HubProjectsView::Cards, HubProjectsSort::Name);
    projects.SetFilters({.Search = "Unrelated", .Version = "0.1.0", .FavoritesOnly = true, .Status = 5, .Editor = 2});
    projects.RevealCreatedProject("New Exploration");
    CHECK(projects.Filters().Search == "New Exploration");
    CHECK(projects.Filters().Version.empty());
    CHECK_FALSE(projects.Filters().FavoritesOnly);
    CHECK(projects.Filters().Status == 0);
    CHECK(projects.Filters().Editor == 0);
    CHECK(projects.View() == HubProjectsView::Cards);
    CHECK(projects.Sort() == HubProjectsSort::Name);
    projects.RevealCreatedProject({});
    CHECK(projects.Filters().Search == "New Exploration");
    projects.SetFilters({.Status = 255, .Editor = 255});
    CHECK(projects.Filters().Status == 0);
    CHECK(projects.Filters().Editor == 0);
}

namespace
{
    constexpr std::string_view ReadyId = "01234567-89ab-cdef-0123-456789abcdef";
    constexpr std::string_view NewerId = "11234567-89ab-cdef-0123-456789abcdef";
    constexpr std::string_view UnsupportedId = "21234567-89ab-cdef-0123-456789abcdef";

    [[nodiscard]] Keire::ProjectInspectionResult Inspection(const std::filesystem::path& root,
                                                            const std::string_view id, const std::string_view name,
                                                            const std::uint32_t schemaVersion,
                                                            const Keire::ProjectStatus status,
                                                            const std::string_view minimumVersion = "0.0.0")
    {
        std::filesystem::create_directories(root);
        return {.Root = std::filesystem::canonical(root),
                .Status = status,
                .SchemaVersion = schemaVersion,
                .Id = Keire::ProjectId::Parse(id),
                .Name = std::string(name),
                .CreatedWithEngineVersion = "0.1.0",
                .MinimumEngineVersion = std::string(minimumVersion),
                .CreatedAt = "2026-08-07T00:00:00Z",
                .LastSavedWithEngineVersion = "0.1.0"};
    }

    [[nodiscard]] const HubRecentProject& FindProject(const HubController& controller, const std::string_view id)
    {
        const auto projects = controller.Snapshot().Projects;
        const auto found = std::ranges::find(*projects, id, &HubRecentProject::Id);
        REQUIRE(found != projects->end());
        return *found;
    }
} // namespace

TEST_CASE("Hub project add registers version-neutral project identities without opening them")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    HubProjectWorkflow workflow(controller);

    const auto readyRoot = temporary.Path() / "Ready";
    const auto newerRoot = temporary.Path() / "Newer";
    const auto unsupportedRoot = temporary.Path() / "Unsupported";
    REQUIRE(workflow.Add(Inspection(readyRoot, ReadyId, "Ready Project", Keire::CurrentProjectSchemaVersion,
                                    Keire::ProjectStatus::Ready),
                         10));
    REQUIRE(workflow.Add(Inspection(newerRoot, NewerId, "Newer Project", Keire::CurrentProjectSchemaVersion,
                                    Keire::ProjectStatus::RequiresNewerEngine, "9999.0.0"),
                         11));
    REQUIRE(workflow.Add(Inspection(unsupportedRoot, UnsupportedId, "Unsupported Project",
                                    Keire::CurrentProjectSchemaVersion + 1, Keire::ProjectStatus::UnsupportedSchema),
                         12));

    CHECK(FindProject(controller, ReadyId).CachedMetadata.Status == HubProjectStatus::Ready);
    CHECK(FindProject(controller, NewerId).CachedMetadata.Status == HubProjectStatus::MissingEditor);
    CHECK(FindProject(controller, UnsupportedId).CachedMetadata.Status == HubProjectStatus::UnsupportedSchema);
    CHECK(FindProject(controller, ReadyId).LastOpenedUnixSeconds == 0);
    CHECK(FindProject(controller, NewerId).LastOpenedUnixSeconds == 0);
    CHECK(FindProject(controller, UnsupportedId).LastOpenedUnixSeconds == 0);
}

TEST_CASE("Hub project add rejects a duplicate identity at another root without mutating the registration")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    HubProjectWorkflow workflow(controller);

    const auto registeredRoot = temporary.Path() / "Registered";
    const auto duplicateRoot = temporary.Path() / "Duplicate";
    const auto registered = Inspection(registeredRoot, ReadyId, "Registered Project",
                                       Keire::CurrentProjectSchemaVersion, Keire::ProjectStatus::Ready);
    const auto duplicate = Inspection(duplicateRoot, ReadyId, "Duplicate Project", Keire::CurrentProjectSchemaVersion,
                                      Keire::ProjectStatus::Ready);
    REQUIRE(workflow.Add(registered, 10));
    REQUIRE(workflow.SetPinned(std::string(ReadyId), true));
    const auto registryPath = controller.Projects().Path();
    const auto before = KeireHubTests::ReadText(registryPath);

    const auto rejected = workflow.Add(duplicate, 20);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(KeireHubTests::ReadText(registryPath) == before);
    const auto& preserved = FindProject(controller, ReadyId);
    CHECK(preserved.Root == std::filesystem::canonical(registeredRoot));
    CHECK(preserved.Name == "Registered Project");
    CHECK(preserved.Pinned);
    CHECK(preserved.AddedUnixSeconds == 10);
    CHECK(preserved.LastOpenedUnixSeconds == 0);
}

TEST_CASE("Open-with editor selection consumes pending state while retaining a stable command payload")
{
    std::optional<HubProjectUiPendingSelection> pending{
        HubProjectUiPendingSelection{.Id = std::string(ReadyId), .Root = "D:/Projects/Ready", .Name = "Ready"}};

    const auto command = TakeOpenWithEditorCommand(pending, "editor-1");

    CHECK_FALSE(pending);
    CHECK(command.Type == HubProjectUiCommandType::OpenWithEditor);
    CHECK(command.ProjectId == ReadyId);
    CHECK(command.EditorId == "editor-1");
    CHECK(command.Path == std::filesystem::path("D:/Projects/Ready"));
    CHECK_FALSE(TakeOpenWithEditorCommand(pending, "editor-2"));
}
