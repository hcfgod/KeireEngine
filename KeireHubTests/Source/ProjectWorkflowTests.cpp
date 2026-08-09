#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/ProjectWorkflowManager.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <ranges>

using namespace KeireHub;

namespace
{
    constexpr auto SourceId = "11111111-1111-4111-8111-111111111111";
    constexpr auto DuplicateId = "22222222-2222-4222-8222-222222222222";
    constexpr auto OtherId = "33333333-3333-4333-8333-333333333333";
    constexpr auto InitialTimestamp = "2026-01-02T03:04:05Z";
    constexpr auto DuplicateTimestamp = "2026-08-06T12:34:56Z";

    void WriteProject(const std::filesystem::path& root, const std::string_view id = SourceId,
                      const std::string_view name = "Source Project")
    {
        const nlohmann::json descriptor{{"schemaVersion", 3},
                                        {"id", id},
                                        {"name", name},
                                        {"createdWithEngineVersion", "0.1.0"},
                                        {"minimumEngineVersion", "0.1.0"},
                                        {"createdAt", InitialTimestamp},
                                        {"lastSavedWithEngineVersion", "0.1.0"},
                                        {"startupScene", nullptr},
                                        {"defaultInput", nullptr},
                                        {"requiredModules", nlohmann::json::array()}};
        KeireHubTests::WriteText(root / "ProjectSettings" / "Project.keireproject", descriptor.dump(2) + '\n');
        KeireHubTests::WriteText(root / "Assets" / "hello.txt", "hello");
    }

    [[nodiscard]] nlohmann::json ReadDescriptor(const std::filesystem::path& root)
    {
        return nlohmann::json::parse(KeireHubTests::ReadText(root / "ProjectSettings" / "Project.keireproject"));
    }

    [[nodiscard]] HubRecentProject RecentProject(const std::filesystem::path& root, const std::string& id = SourceId,
                                                 const std::string& name = "Source Project")
    {
        return {.Id = id,
                .Root = root,
                .Name = name,
                .AddedUnixSeconds = 10,
                .LastOpenedUnixSeconds = 20,
                .Pinned = true,
                .PreferredEditorInstallationId = "editor-stable"};
    }

    [[nodiscard]] ProjectWorkflowServices Services(bool& locked, std::string generatedId = DuplicateId)
    {
        return {.GenerateProjectId = [id = std::move(generatedId)] { return HubResult<std::string>::Success(id); },
                .CurrentUtcTimestamp = [] { return HubResult<std::string>::Success(std::string(DuplicateTimestamp)); },
                .CurrentUnixSeconds = [] { return 42ULL; },
                .IsProjectLocked = [&locked](const std::filesystem::path&)
                { return HubResult<bool>::Success(locked); }};
    }

    [[nodiscard]] ProjectDuplicateRequest DuplicateRequest(const std::filesystem::path& destination)
    {
        return {.SourceProjectId = SourceId, .Destination = destination, .DisplayName = "Duplicated Project"};
    }

    [[nodiscard]] bool HasDuplicateStaging(const std::filesystem::path& parent)
    {
        return std::ranges::any_of(std::filesystem::directory_iterator(parent), [](const auto& entry)
                                   { return entry.path().filename().string().starts_with(".keire-duplicate-"); });
    }
} // namespace

TEST_CASE("Project workflow duplicates a clean staged project with a new identity")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Duplicate";
    WriteProject(source);
    KeireHubTests::WriteText(source / "Library" / "generated.bin", "generated");
    KeireHubTests::WriteText(source / "Build" / "Player" / "output.bin", "output");
    KeireHubTests::WriteText(source / ".git" / "config", "private repository state");

    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    bool locked = false;
    ProjectWorkflowManager workflows(catalog, Services(locked));
    auto result = workflows.Duplicate(DuplicateRequest(destination));
    REQUIRE(result);

    CHECK(result.Value().ProjectId == DuplicateId);
    CHECK(result.Value().Root == destination);
    CHECK(result.Value().DisplayName == "Duplicated Project");
    CHECK(result.Value().CopiedBytes > 0);
    CHECK(result.Value().CopiedEntries > 0);
    CHECK(std::filesystem::is_regular_file(destination / "Assets" / "hello.txt"));
    CHECK_FALSE(std::filesystem::exists(destination / "Library"));
    CHECK_FALSE(std::filesystem::exists(destination / "Build"));
    CHECK_FALSE(std::filesystem::exists(destination / ".git"));
    CHECK_FALSE(HasDuplicateStaging(temporary.Path()));

    const auto original = ReadDescriptor(source);
    const auto duplicated = ReadDescriptor(destination);
    CHECK(original.at("id") == SourceId);
    CHECK(original.at("name") == "Source Project");
    CHECK(original.at("createdAt") == InitialTimestamp);
    CHECK(duplicated.at("id") == DuplicateId);
    CHECK(duplicated.at("name") == "Duplicated Project");
    CHECK(duplicated.at("createdAt") == DuplicateTimestamp);
    CHECK(duplicated.at("lastSavedWithEngineVersion") == original.at("lastSavedWithEngineVersion"));

    const auto snapshot = catalog.Snapshot();
    REQUIRE(snapshot->size() == 2);
    const auto registered = std::ranges::find(*snapshot, std::string(DuplicateId), &HubRecentProject::Id);
    REQUIRE(registered != snapshot->end());
    CHECK(registered->Root == destination);
    CHECK(registered->Name == "Duplicated Project");
    CHECK_FALSE(registered->Pinned);
    REQUIRE(registered->PreferredEditorInstallationId);
    CHECK(*registered->PreferredEditorInstallationId == "editor-stable");
}

TEST_CASE("Project duplicate collisions preserve existing paths and catalog state")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Existing";
    WriteProject(source);
    KeireHubTests::WriteText(destination / "sentinel.txt", "keep");
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    const auto before = catalog.Snapshot();
    bool locked = false;
    ProjectWorkflowManager workflows(catalog, Services(locked));

    const auto existing = workflows.Duplicate(DuplicateRequest(destination));
    REQUIRE_FALSE(existing);
    CHECK(existing.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(destination / "sentinel.txt") == "keep");
    CHECK(catalog.Snapshot() == before);

    const auto staging = temporary.Path() / (std::string(".keire-duplicate-") + DuplicateId);
    KeireHubTests::WriteText(staging / "sentinel.txt", "keep staging collision");
    const auto stagingCollision = workflows.Duplicate(DuplicateRequest(temporary.Path() / "StagingCollision"));
    REQUIRE_FALSE(stagingCollision);
    CHECK(stagingCollision.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(staging / "sentinel.txt") == "keep staging collision");
    std::filesystem::remove_all(staging);

    ProjectWorkflowManager identityCollision(catalog, Services(locked, SourceId));
    const auto duplicateIdentity = identityCollision.Duplicate(DuplicateRequest(temporary.Path() / "Identity"));
    REQUIRE_FALSE(duplicateIdentity);
    CHECK(duplicateIdentity.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / "Identity"));
    CHECK_FALSE(HasDuplicateStaging(temporary.Path()));
}

TEST_CASE("Project duplicate rejects locked sources and destinations inside the source")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    WriteProject(source);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    bool locked = true;
    ProjectWorkflowManager workflows(catalog, Services(locked));

    const auto lockedResult = workflows.Duplicate(DuplicateRequest(temporary.Path() / "Locked"));
    REQUIRE_FALSE(lockedResult);
    CHECK(lockedResult.Error().Code == HubErrorCode::InvalidTransition);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / "Locked"));

    locked = false;
    const auto confined = workflows.Duplicate(DuplicateRequest(source / "NestedDuplicate"));
    REQUIRE_FALSE(confined);
    CHECK(confined.Error().Code == HubErrorCode::InvalidArgument);
    CHECK_FALSE(std::filesystem::exists(source / "NestedDuplicate"));
    CHECK_FALSE(HasDuplicateStaging(temporary.Path()));
}

TEST_CASE("Failed staged duplicate validation removes only workflow-owned staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Rejected";
    WriteProject(source);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    const auto before = catalog.Snapshot();
    bool locked = false;
    ProjectWorkflowManager workflows(catalog, Services(locked));
    auto request = DuplicateRequest(destination);
    request.ValidateStagedProject = [](const std::filesystem::path& staging)
    {
        KeireHubTests::WriteText(staging / "validator-output.txt", "temporary");
        return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                   .Message = "Fixture validation failed.",
                                   .TechnicalDetails = "intentional"});
    };

    const auto result = workflows.Duplicate(request);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::ProjectValidationFailed);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(HasDuplicateStaging(temporary.Path()));
    CHECK(catalog.Snapshot() == before);
    CHECK(ReadDescriptor(source).at("id") == SourceId);
}

TEST_CASE("Project duplicate rolls back publication when catalog persistence fails")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "RolledBack";
    const auto catalogDirectory = temporary.Path() / "Catalog";
    WriteProject(source);
    HubProjectCatalog catalog(catalogDirectory / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    const auto before = catalog.Snapshot();
    REQUIRE(std::filesystem::remove(catalogDirectory / "projects.json"));
    REQUIRE(std::filesystem::remove(catalogDirectory));
    KeireHubTests::WriteText(catalogDirectory, "block the catalog directory");
    bool locked = false;
    ProjectWorkflowManager workflows(catalog, Services(locked));

    const auto result = workflows.Duplicate(DuplicateRequest(destination));
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::IoWrite);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(HasDuplicateStaging(temporary.Path()));
    CHECK(catalog.Snapshot() == before);
    CHECK(ReadDescriptor(source).at("id") == SourceId);
}

TEST_CASE("Locate moved project requires the registered identity")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto original = temporary.Path() / "MissingOriginal";
    const auto moved = temporary.Path() / "Moved";
    const auto wrong = temporary.Path() / "Wrong";
    WriteProject(moved);
    WriteProject(wrong, OtherId, "Other Project");
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(original)));
    ProjectWorkflowManager workflows(catalog);
    const auto before = catalog.Snapshot();

    const auto rejected = workflows.LocateMovedProject(SourceId, wrong);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::ProjectValidationFailed);
    CHECK(catalog.Snapshot() == before);
    CHECK(catalog.Snapshot()->front().Root == original);

    REQUIRE(workflows.LocateMovedProject(SourceId, moved));
    CHECK(catalog.Snapshot() != before);
    CHECK(catalog.Snapshot()->front().Id == SourceId);
    CHECK(catalog.Snapshot()->front().Root == std::filesystem::weakly_canonical(moved));
    CHECK(ReadDescriptor(moved).at("id") == SourceId);
}

TEST_CASE("Locate moved project refuses to repoint an available registration")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto original = temporary.Path() / "Original";
    const auto copied = temporary.Path() / "Copied";
    WriteProject(original);
    WriteProject(copied);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(original)));
    ProjectWorkflowManager workflows(catalog);
    const auto before = catalog.Snapshot();

    const auto rejected = workflows.LocateMovedProject(SourceId, copied);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(catalog.Snapshot() == before);
    CHECK(catalog.Snapshot()->front().Root == std::filesystem::weakly_canonical(original));
}

TEST_CASE("Rename display name requires a closed compatible project and preserves identity")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(root)));
    bool locked = true;
    ProjectWorkflowManager workflows(catalog, Services(locked));
    const auto before = catalog.Snapshot();

    const auto rejected = workflows.RenameDisplayName(SourceId, "Renamed Project");
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(catalog.Snapshot() == before);
    CHECK(ReadDescriptor(root).at("name") == "Source Project");

    locked = false;
    REQUIRE(workflows.RenameDisplayName(SourceId, "Renamed Project"));
    const auto descriptor = ReadDescriptor(root);
    CHECK(descriptor.at("id") == SourceId);
    CHECK(descriptor.at("name") == "Renamed Project");
    CHECK(catalog.Snapshot()->front().Id == SourceId);
    CHECK(catalog.Snapshot()->front().Name == "Renamed Project");
}

TEST_CASE("Remove from Hub never deletes project files")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(root)));
    ProjectWorkflowManager workflows(catalog);

    REQUIRE(workflows.RemoveFromHub(SourceId));
    CHECK(catalog.Snapshot()->empty());
    CHECK(std::filesystem::is_regular_file(root / "ProjectSettings" / "Project.keireproject"));
    CHECK(std::filesystem::is_regular_file(root / "Assets" / "hello.txt"));
}
