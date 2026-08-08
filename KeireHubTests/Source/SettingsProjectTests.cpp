#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/HubProjectCatalog.h"
#include "KeireHubRuntime/HubSettingsStore.h"

#include <doctest/doctest.h>

#include <array>
#include <ranges>

using namespace KeireHub;

TEST_CASE("Hub settings migrate legacy view and sort atomically")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto settingsPath = temporary.Path() / "settings.json";
    const auto legacyPath = temporary.Path() / "HubUi.settings";
    KeireHubTests::WriteText(legacyPath, "view=cards\nsort=name\n");

    HubSettingsStore store(settingsPath, legacyPath);
    REQUIRE(store.Load());
    CHECK(store.MigratedLegacySettings());
    CHECK(store.Snapshot()->ProjectsView == ProjectView::Cards);
    CHECK(store.Snapshot()->ProjectsSort == ProjectSort::Name);
    CHECK(std::filesystem::exists(settingsPath));
    CHECK(std::filesystem::exists(legacyPath));
    CHECK(KeireHubTests::ReadText(settingsPath).find("\"schemaVersion\": 1") != std::string::npos);

    HubSettingsStore reloaded(settingsPath, legacyPath);
    REQUIRE(reloaded.Load());
    CHECK_FALSE(reloaded.MigratedLegacySettings());
    CHECK(reloaded.Snapshot()->ProjectsView == ProjectView::Cards);
}

TEST_CASE("Hub settings migrate the legacy last-opened spelling written by the shipped Hub")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto settingsPath = temporary.Path() / "settings.json";
    const auto legacyPath = temporary.Path() / "HubUi.settings";
    KeireHubTests::WriteText(legacyPath, "view=list\nsort=last-opened\n");

    HubSettingsStore store(settingsPath, legacyPath);
    REQUIRE(store.Load());
    CHECK(store.MigratedLegacySettings());
    CHECK(store.Snapshot()->ProjectsView == ProjectView::Table);
    CHECK(store.Snapshot()->ProjectsSort == ProjectSort::LastOpened);
    CHECK(std::filesystem::exists(settingsPath));
    CHECK(std::filesystem::exists(legacyPath));
}

TEST_CASE("Invalid Hub settings do not replace the committed snapshot")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubSettingsStore store(temporary.Path() / "settings.json");
    HubSettings settings;
    settings.Appearance = HubAppearance::Dark;
    REQUIRE(store.Save(settings));
    const auto original = KeireHubTests::ReadText(store.Path());

    settings.Appearance = HubAppearance::Light;
    settings.ConcurrentDownloads = 0;
    const auto status = store.Save(settings);
    REQUIRE_FALSE(status);
    CHECK(status.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(store.Snapshot()->Appearance == HubAppearance::Dark);
    CHECK(KeireHubTests::ReadText(store.Path()) == original);
}

TEST_CASE("Hub settings require absolute roots and bounded network configuration")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubSettingsStore store(temporary.Path() / "settings.json");
    HubSettings settings;
    settings.CacheRoot = "relative-cache";
    CHECK_FALSE(store.Save(settings));

    settings.CacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");
    settings.NetworkProxyMode = ProxyMode::Custom;
    settings.CustomProxyUrl = "file:///unsafe";
    CHECK_FALSE(store.Save(settings));
    settings.CustomProxyUrl = "https://proxy.example:8443";
    REQUIRE(store.Save(settings));
    CHECK(store.Snapshot()->CustomProxyUrl == "https://proxy.example:8443");

    settings.DevelopmentServiceUrl = "https://distribution.example";
    CHECK_FALSE(store.Save(settings));
    settings.DevelopmentTrustedKey = "development-key";
    CHECK(store.Save(settings));
}

TEST_CASE("Malformed settings are quarantined while future schemas are preserved")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto malformed = temporary.Path() / "malformed.json";
    KeireHubTests::WriteText(malformed, "{not-json");
    HubSettingsStore malformedStore(malformed);
    CHECK_FALSE(malformedStore.Load());
    CHECK_FALSE(std::filesystem::exists(malformed));
    CHECK(std::filesystem::exists(malformed.string() + ".corrupt"));

    const auto future = temporary.Path() / "future.json";
    KeireHubTests::WriteText(future, R"({"schemaVersion":99})");
    HubSettingsStore futureStore(future);
    const auto status = futureStore.Load();
    REQUIRE_FALSE(status);
    CHECK(status.Error().Code == HubErrorCode::UnsupportedSchema);
    CHECK(std::filesystem::exists(future));
    CHECK_FALSE(std::filesystem::exists(future.string() + ".corrupt"));
}

TEST_CASE("Recent project registry schema one migrates without losing identity or UTF-8 paths")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "projects.json";
    KeireHubTests::WriteText(path, R"({
  "schemaVersion": 1,
  "projects": [
    {"id":"01234567-89ab-cdef-0123-456789abcdef","root":"D:/Prójèct","name":"Tést","lastOpened":42,"pinned":true}
  ]
})");

    HubProjectCatalog catalog(path);
    REQUIRE(catalog.Load());
    CHECK(catalog.MigratedSchemaOne());
    REQUIRE(catalog.Snapshot()->size() == 1);
    CHECK(catalog.Snapshot()->front().Id == "01234567-89ab-cdef-0123-456789abcdef");
    CHECK(catalog.Snapshot()->front().LastOpenedUnixSeconds == 42);
    CHECK(catalog.Snapshot()->front().Pinned);
    const auto persisted = KeireHubTests::ReadText(path);
    CHECK(persisted.find("\"schemaVersion\": 2") != std::string::npos);
    CHECK(persisted.find("Prójèct") != std::string::npos);
}

TEST_CASE("Hub project catalog accepts Core-flat schema two aliases")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "projects.json";
    KeireHubTests::WriteText(path, R"({
      "schemaVersion":2,
      "projects":[{
        "id":"project-a","root":"D:/Project","name":"Project","lastOpened":50,"created":20,
        "lastSavedWithEngineVersion":"2.1.0","preferredEditorInstallation":"editor-a","pinned":false
      }]
    })");
    HubProjectCatalog catalog(path);
    REQUIRE(catalog.Load());
    const auto& project = catalog.Snapshot()->front();
    REQUIRE(project.CachedMetadata.CreatedUnixSeconds);
    CHECK(*project.CachedMetadata.CreatedUnixSeconds == 20);
    REQUIRE(project.CachedMetadata.LastSavedWithEngineVersion);
    CHECK(*project.CachedMetadata.LastSavedWithEngineVersion == "2.1.0");
    REQUIRE(project.PreferredEditorInstallationId);
    CHECK(*project.PreferredEditorInstallationId == "editor-a");
}

TEST_CASE("Project catalog validates locate identity and publishes immutable replacements")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    HubRecentProject project{
        .Id = "project-a", .Root = "D:/Old", .Name = "Project", .AddedUnixSeconds = 10, .LastOpenedUnixSeconds = 11};
    REQUIRE(catalog.Upsert(project));
    const auto before = catalog.Snapshot();
    const auto rejected = catalog.Locate("project-a", "D:/Wrong", "project-b");
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(catalog.Snapshot() == before);

    REQUIRE(catalog.Locate("project-a", "D:/Moved", "project-a"));
    CHECK(catalog.Snapshot() != before);
    CHECK(catalog.Snapshot()->front().Root == std::filesystem::path("D:/Moved"));
    CHECK(before->front().Root == std::filesystem::path("D:/Old"));
}

TEST_CASE("Project catalog batch import is atomic and preserves existing user state")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "projects.json";
    HubProjectCatalog catalog(path);
    REQUIRE(catalog.Upsert({.Id = "project-a",
                            .Root = temporary.Path() / "Existing",
                            .Name = "Old name",
                            .AddedUnixSeconds = 5,
                            .LastOpenedUnixSeconds = 40,
                            .Pinned = true,
                            .PreferredEditorInstallationId = "editor-a"}));

    const std::array batch{HubRecentProject{.Id = "project-a",
                                            .Root = temporary.Path() / "Existing",
                                            .Name = "Updated name",
                                            .AddedUnixSeconds = 100,
                                            .CachedMetadata = {.Status = HubProjectStatus::Ready}},
                           HubRecentProject{.Id = "project-b",
                                            .Root = temporary.Path() / "New",
                                            .Name = "New project",
                                            .AddedUnixSeconds = 100,
                                            .CachedMetadata = {.Status = HubProjectStatus::Ready}}};
    REQUIRE(catalog.UpsertMany(batch));
    REQUIRE(catalog.Snapshot()->size() == 2);
    const auto existing = std::ranges::find(*catalog.Snapshot(), "project-a", &HubRecentProject::Id);
    REQUIRE(existing != catalog.Snapshot()->end());
    CHECK(existing->Root == temporary.Path() / "Existing");
    CHECK(existing->Name == "Updated name");
    CHECK(existing->AddedUnixSeconds == 5);
    CHECK(existing->LastOpenedUnixSeconds == 40);
    CHECK(existing->Pinned);
    CHECK(existing->PreferredEditorInstallationId == "editor-a");

    const auto beforeSnapshot = catalog.Snapshot();
    const auto beforeDocument = KeireHubTests::ReadText(path);
    const std::array duplicateIds{HubRecentProject{.Id = "duplicate", .Root = temporary.Path() / "One", .Name = "One"},
                                  HubRecentProject{.Id = "duplicate", .Root = temporary.Path() / "Two", .Name = "Two"}};
    const auto rejectedIds = catalog.UpsertMany(duplicateIds);
    REQUIRE_FALSE(rejectedIds);
    CHECK(rejectedIds.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(catalog.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);

    const std::array duplicateRoots{
        HubRecentProject{.Id = "root-a", .Root = temporary.Path() / "Shared", .Name = "One"},
        HubRecentProject{.Id = "root-b", .Root = temporary.Path() / "Shared", .Name = "Two"}};
    const auto rejectedRoots = catalog.UpsertMany(duplicateRoots);
    REQUIRE_FALSE(rejectedRoots);
    CHECK(rejectedRoots.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(catalog.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);
}

TEST_CASE("Project catalog cached metadata batch commits once or leaves the registry unchanged")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "projects.json";
    HubProjectCatalog catalog(path);
    REQUIRE(catalog.Upsert({.Id = "project-a", .Root = temporary.Path() / "A", .Name = "A"}));
    REQUIRE(catalog.Upsert({.Id = "project-b", .Root = temporary.Path() / "B", .Name = "B"}));

    const std::array updates{
        HubProjectMetadataUpdate{.ProjectId = "project-a",
                                 .Metadata = {.SizeBytes = 10, .Status = HubProjectStatus::Ready}},
        HubProjectMetadataUpdate{.ProjectId = "project-b",
                                 .Metadata = {.SizeBytes = 20, .Status = HubProjectStatus::Missing}}};
    REQUIRE(catalog.UpdateCachedMetadataMany(updates));
    const auto projectA = std::ranges::find(*catalog.Snapshot(), "project-a", &HubRecentProject::Id);
    const auto projectB = std::ranges::find(*catalog.Snapshot(), "project-b", &HubRecentProject::Id);
    REQUIRE(projectA != catalog.Snapshot()->end());
    REQUIRE(projectB != catalog.Snapshot()->end());
    CHECK(projectA->CachedMetadata.SizeBytes == 10);
    CHECK(projectA->CachedMetadata.Status == HubProjectStatus::Ready);
    CHECK(projectB->CachedMetadata.SizeBytes == 20);
    CHECK(projectB->CachedMetadata.Status == HubProjectStatus::Missing);

    const auto beforeSnapshot = catalog.Snapshot();
    const auto beforeDocument = KeireHubTests::ReadText(path);
    const std::array missingProject{
        HubProjectMetadataUpdate{.ProjectId = "project-a",
                                 .Metadata = {.SizeBytes = 99, .Status = HubProjectStatus::Invalid}},
        HubProjectMetadataUpdate{.ProjectId = "missing-project",
                                 .Metadata = {.SizeBytes = 100, .Status = HubProjectStatus::Missing}}};
    const auto missingRejected = catalog.UpdateCachedMetadataMany(missingProject);
    REQUIRE_FALSE(missingRejected);
    CHECK(missingRejected.Error().Code == HubErrorCode::NotFound);
    CHECK(catalog.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);

    const std::array duplicateProject{
        HubProjectMetadataUpdate{.ProjectId = "project-a",
                                 .Metadata = {.SizeBytes = 30, .Status = HubProjectStatus::Ready}},
        HubProjectMetadataUpdate{.ProjectId = "project-a",
                                 .Metadata = {.SizeBytes = 40, .Status = HubProjectStatus::Ready}}};
    const auto duplicateRejected = catalog.UpdateCachedMetadataMany(duplicateProject);
    REQUIRE_FALSE(duplicateRejected);
    CHECK(duplicateRejected.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(catalog.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);
}

TEST_CASE("Project catalog retains pinned entries while bounding recent unpinned entries")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    for (std::uint64_t index = 0; index < 52; ++index)
    {
        HubRecentProject project{.Id = "project-" + std::to_string(index),
                                 .Root = temporary.Path() / std::to_string(index),
                                 .Name = "Project " + std::to_string(index),
                                 .AddedUnixSeconds = index,
                                 .LastOpenedUnixSeconds = index,
                                 .Pinned = index == 0};
        REQUIRE(catalog.Upsert(std::move(project)));
    }
    CHECK(catalog.Snapshot()->size() == 51);
    CHECK(std::ranges::any_of(*catalog.Snapshot(), [](const auto& value) { return value.Id == "project-0"; }));
    CHECK_FALSE(std::ranges::any_of(*catalog.Snapshot(), [](const auto& value) { return value.Id == "project-1"; }));
}

TEST_CASE("Project catalog removes only entries confirmed missing by metadata discovery")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert({.Id = "missing",
                            .Root = temporary.Path() / "Missing",
                            .Name = "Missing",
                            .CachedMetadata = {.Status = HubProjectStatus::Missing}}));
    REQUIRE(catalog.Upsert({.Id = "invalid",
                            .Root = temporary.Path() / "Invalid",
                            .Name = "Invalid",
                            .CachedMetadata = {.Status = HubProjectStatus::Invalid}}));
    REQUIRE(catalog.Upsert({.Id = "ready",
                            .Root = temporary.Path() / "Ready",
                            .Name = "Ready",
                            .CachedMetadata = {.Status = HubProjectStatus::Ready}}));

    REQUIRE(catalog.RemoveMissing());
    REQUIRE(catalog.Snapshot()->size() == 2);
    CHECK_FALSE(std::ranges::any_of(*catalog.Snapshot(), [](const auto& project) { return project.Id == "missing"; }));
    CHECK(std::ranges::any_of(*catalog.Snapshot(), [](const auto& project) { return project.Id == "invalid"; }));
    CHECK(std::ranges::any_of(*catalog.Snapshot(), [](const auto& project) { return project.Id == "ready"; }));

    const auto before = catalog.Snapshot();
    REQUIRE(catalog.RemoveMissing());
    CHECK(catalog.Snapshot() == before);
}
