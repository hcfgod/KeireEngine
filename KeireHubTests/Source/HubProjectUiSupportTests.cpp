#include "KeireHub/HubProjectUiSupport.h"
#include "KeireHubTests/TestSupport.h"

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <initializer_list>
#include <string>

TEST_CASE("Hub opens older-schema projects with a compatible installed editor")
{
    using KeireHub::HubProjectOpenAction;
    using KeireHub::ResolveProjectOpenAction;

    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UpgradeAvailable, true) ==
          HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UpgradeAvailable, false) ==
          HubProjectOpenAction::ReviewUpgrade);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::RecoveryRequired, true) ==
          HubProjectOpenAction::ReviewUpgrade);
}

TEST_CASE("Hub project opening distinguishes missing editors from unavailable projects")
{
    using KeireHub::HubProjectOpenAction;
    using KeireHub::ResolveProjectOpenAction;

    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::Ready, true) == HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::RequiresNewerEngine, true) ==
          HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UnsupportedSchema, false) ==
          HubProjectOpenAction::FindCompatibleEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::Missing, true) == HubProjectOpenAction::Unavailable);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::InUse, true) == HubProjectOpenAction::Unavailable);
}

TEST_CASE("Hub folder validation explains invalid selections without creating project files")
{
    KeireHubTests::TemporaryDirectory temporary;
    CHECK_FALSE(KeireHub::ValidateHubOpenProjectFolder({}).empty());
    const auto missing = temporary.Path() / "Missing";
    CHECK(KeireHub::ValidateHubOpenProjectFolder(missing).find("does not exist") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(missing));
    CHECK(KeireHub::ValidateHubOpenProjectFolder(temporary.Path()).find("ProjectSettings/Project.keireproject") !=
          std::string::npos);
    CHECK(std::filesystem::is_empty(temporary.Path()));
    const auto file = temporary.Path() / "readme.txt";
    KeireHubTests::WriteText(file, "preserve me");
    CHECK(KeireHub::ValidateHubOpenProjectFolder(file).find("not a file") != std::string::npos);
    CHECK(KeireHubTests::ReadText(file) == "preserve me");
    const auto marker = temporary.Path() / "ProjectSettings" / "Project.keireproject";
    KeireHubTests::WriteText(marker, "{broken");
    CHECK(KeireHub::ValidateHubOpenProjectFolder(temporary.Path()).find("invalid or unreadable") != std::string::npos);
    CHECK(KeireHubTests::ReadText(marker) == "{broken");
}

TEST_CASE("Hub folder validation accepts valid projects and explains an active editor lock")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto project = Keire::Project::Create(
        {.Location = temporary.Path(), .Name = "Open Check", .Template = Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    const auto marker = root / "ProjectSettings" / "Project.keireproject";
    const auto before = KeireHubTests::ReadText(marker);
    CHECK(KeireHub::ValidateHubOpenProjectFolder(root).empty());
    CHECK(KeireHubTests::ReadText(marker) == before);
    {
        const auto locked = Keire::Project::Open(root, Keire::ProjectOpenMode::Exclusive);
        CHECK(KeireHub::ValidateHubOpenProjectFolder(root).find("already open") != std::string::npos);
    }
    CHECK(KeireHub::ValidateHubOpenProjectFolder(root).empty());
    CHECK(KeireHubTests::ReadText(marker) == before);
}

TEST_CASE("Hub folder validation leaves future-version decisions to editor compatibility selection")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto marker = temporary.Path() / "ProjectSettings" / "Project.keireproject";
    const auto descriptor = [](const std::uint32_t schema)
    {
        return std::string("{\"schemaVersion\":") + std::to_string(schema) +
               R"(,"id":"11875bf1-e52b-43ac-893b-a62f57e4f275","name":"Future Project",)"
               R"("createdWithEngineVersion":"0.1.0","minimumEngineVersion":"999.0.0"})";
    };
    for (const auto schema : {Keire::CurrentProjectSchemaVersion, Keire::CurrentProjectSchemaVersion + 1})
    {
        const auto contents = descriptor(schema);
        KeireHubTests::WriteText(marker, contents);
        const auto expected = schema == Keire::CurrentProjectSchemaVersion ? Keire::ProjectStatus::RequiresNewerEngine
                                                                           : Keire::ProjectStatus::UnsupportedSchema;
        REQUIRE(Keire::Project::InspectMetadata(temporary.Path()).Status == expected);
        CHECK(KeireHub::ValidateHubOpenProjectFolder(temporary.Path()).empty());
        CHECK(KeireHubTests::ReadText(marker) == contents);
    }
}
