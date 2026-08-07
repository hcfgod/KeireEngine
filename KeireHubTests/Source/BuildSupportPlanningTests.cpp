#include "TestSupport.h"

#include "KeireHubRuntime/BuildSupportPlanning.h"

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] BuildSupportEditorTarget Editor(const std::filesystem::path& root, std::string id,
                                                  std::string version, const bool healthy = true)
    {
        return {.InstallationId = std::move(id),
                .EngineVersion = std::move(version),
                .Root = root,
                .AssetToolEntrypoint = root / "bin" / "KeireAssetTool",
                .Healthy = healthy};
    }

    [[nodiscard]] BuildSupportComponent Component(std::string id, std::string version, std::string platform,
                                                  std::string architecture, const bool healthy = true)
    {
        return {.Id = std::move(id),
                .EngineVersion = std::move(version),
                .Platform = std::move(platform),
                .Architecture = std::move(architecture),
                .ArchiveSizeBytes = 4096,
                .Healthy = healthy};
    }
} // namespace

TEST_CASE("Build Support selection requires an explicit healthy editor and chooses targets deterministically")
{
    KeireHubTests::TemporaryDirectory directory;
    const std::array editors{Editor(directory.Path() / "stable", "stable", "2.1.0"),
                             Editor(directory.Path() / "new-b", "new-b", "2.2.0"),
                             Editor(directory.Path() / "new-a", "new-a", "2.2.0"),
                             Editor(directory.Path() / "broken", "broken", "3.0.0", false)};

    const auto explicitSelection = SelectBuildSupportEditor(editors, "stable");
    REQUIRE(explicitSelection);
    CHECK(explicitSelection.Value().Editor.InstallationId == "stable");
    CHECK_FALSE(SelectBuildSupportEditor(editors, "missing"));
    CHECK_FALSE(SelectBuildSupportEditor(editors, "broken"));

    auto running = Editor(directory.Path() / "running", "running", "2.1.0");
    running.Running = true;
    const auto runningSelection = SelectBuildSupportEditor(std::span(&running, 1), "running");
    REQUIRE_FALSE(runningSelection);
    CHECK(runningSelection.Error().Code == HubErrorCode::InstallationBusy);

    auto activeTask = Editor(directory.Path() / "busy", "busy", "2.1.0");
    activeTask.HasActiveTask = true;
    const auto busySelection = SelectBuildSupportEditor(std::span(&activeTask, 1), "busy");
    REQUIRE_FALSE(busySelection);
    CHECK(busySelection.Error().Code == HubErrorCode::InstallationBusy);

    const auto target = SelectBuildSupportEditorForTarget(editors, "windows", "x86_64");
    REQUIRE(target);
    CHECK(target.Value().Editor.InstallationId == "new-a");
    REQUIRE(target.Value().Filter.Platform);
    REQUIRE(target.Value().Filter.Architecture);
    CHECK(*target.Value().Filter.Platform == "windows");
    CHECK(*target.Value().Filter.Architecture == "x86_64");
    CHECK_FALSE(SelectBuildSupportEditorForTarget(editors, "android", "arm64"));

    auto escaped = Editor(directory.Path() / "confined", "escaped", "2.1.0");
    escaped.AssetToolEntrypoint = directory.Path() / "outside" / "KeireAssetTool";
    CHECK_FALSE(SelectBuildSupportEditor(std::span(&escaped, 1), "escaped"));
}

TEST_CASE("Build Support inventory is filtered by selected editor version and optional target")
{
    KeireHubTests::TemporaryDirectory directory;
    const auto editor = Editor(directory.Path() / "editor", "editor", "2.1.0");
    const auto selection =
        SelectBuildSupportEditor(std::span(&editor, 1), "editor", {.Platform = "windows", .Architecture = "x86_64"});
    REQUIRE(selection);

    auto controlId = std::string("bad");
    controlId.push_back('\0');
    controlId += "id";
    const std::array components{Component("windows-x86_64", "2.1.0", "windows", "x86_64"),
                                Component("windows-arm64", "2.1.0", "windows", "arm64"),
                                Component("linux-x86_64", "2.1.0", "linux", "x86_64"),
                                Component("other-version", "2.2.0", "windows", "x86_64"),
                                Component(std::move(controlId), "2.1.0", "windows", "x86_64")};

    const auto filtered = FilterBuildSupportComponents(components, selection.Value());
    REQUIRE(filtered.size() == 1);
    CHECK(filtered.front().Id == "windows-x86_64");
    CHECK(CountBuildSupportComponents(components, "2.1.0") == 3);
    CHECK(CountBuildSupportComponents(components, "2.2.0") == 1);
}

TEST_CASE("Build Support inventory failures keep technical validation detail out of product messages")
{
    constexpr std::string_view raw =
        R"(filesystem error: cannot open C:\Users\person\BuildSupport\manifest.json: JSON parse error 101)";
    const auto failure = BuildSupportInventoryFailure("windows-x86_64", std::string(raw));

    CHECK(failure.Code == HubErrorCode::PackageManifestInvalid);
    CHECK(failure.Message == "Installed Build Support files are missing or corrupt.");
    CHECK(failure.Message.find("C:\\Users") == std::string::npos);
    CHECK(failure.Message.find("JSON") == std::string::npos);
    CHECK(failure.AffectedItem == "windows-x86_64");
    CHECK(failure.TechnicalDetails == raw);
    CHECK(failure.LogReference == "build-support.inventory");
}

TEST_CASE("Build Support live status text ignores untrusted Asset Tool status strings")
{
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Import, "running", "verify") ==
          "Verifying the Build Support package.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Repair, "running", "install") ==
          "Repairing Build Support.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Remove, "running", "remove") ==
          "Removing Build Support.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Remove, "succeeded", "complete") ==
          "Build Support removal completed.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Import, "failed", "C:\\private\\status.json",
                                     "build_support.install_failed") ==
          "Build Support could not be installed. Verify the package and try again.");
    CHECK(
        BuildSupportLiveStatusText(BuildSupportOperationKind::Import, "failed", "failed", "C:\\private\\status.json") ==
        "The Build Support operation failed. See Hub logs for details.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Import, "running", "JSON parse error at /private") ==
          "Build Support operation is running.");
    CHECK(BuildSupportLiveStatusText(BuildSupportOperationKind::Import, "filesystem error", "verify") ==
          "Build Support status is unavailable.");
}

TEST_CASE("Build Support import and repair plans target the selected typed Asset Tool")
{
    KeireHubTests::TemporaryDirectory directory;
    const auto editor = Editor(directory.Path() / "editor", "editor", "2.1.0");
    const auto selection =
        SelectBuildSupportEditor(std::span(&editor, 1), "editor", {.Platform = "windows", .Architecture = "x86_64"});
    REQUIRE(selection);
    const auto package = directory.Path() / "windows.keireplayersupport";
    const auto operation = directory.Path() / "operation";

    const auto import = PlanBuildSupportImport(selection.Value(), package, operation);
    REQUIRE(import);
    CHECK(import.Value().Kind == BuildSupportOperationKind::Import);
    CHECK(import.Value().Executable == editor.AssetToolEntrypoint);
    CHECK(import.Value().StatusPath == operation / "status.json");
    CHECK(import.Value().CancelPath == operation / "cancel");
    CHECK(import.Value().Arguments ==
          std::vector<std::string>{"install-player-support", "--input", package.generic_string(), "--status",
                                   (operation / "status.json").generic_string(), "--cancel",
                                   (operation / "cancel").generic_string(), "--expected-platform", "windows",
                                   "--expected-architecture", "x86_64"});

    const auto component = Component("windows-x86_64", "2.1.0", "windows", "x86_64", false);
    const auto repair = PlanBuildSupportImport(selection.Value(), package, operation, &component);
    REQUIRE(repair);
    CHECK(repair.Value().Kind == BuildSupportOperationKind::Repair);
    CHECK(repair.Value().ComponentId == component.Id);
    CHECK(repair.Value().Arguments[7] == "--pack-id");
    CHECK(repair.Value().Arguments[8] == component.Id);

    CHECK_FALSE(PlanBuildSupportImport(selection.Value(), directory.Path() / "generic.keirepackage", operation));
    const auto wrongVersion = Component("windows-x86_64", "2.2.0", "windows", "x86_64", false);
    CHECK_FALSE(PlanBuildSupportImport(selection.Value(), package, operation, &wrongVersion));

    auto forgedBusySelection = selection.Value();
    forgedBusySelection.Editor.HasActiveTask = true;
    const auto busyPlan = PlanBuildSupportImport(forgedBusySelection, package, operation);
    REQUIRE_FALSE(busyPlan);
    CHECK(busyPlan.Error().Code == HubErrorCode::InstallationBusy);
}

TEST_CASE("Build Support removal plans remain scoped to the selected editor and component")
{
    KeireHubTests::TemporaryDirectory directory;
    const auto editor = Editor(directory.Path() / "editor", "editor", "2.1.0");
    const auto selection =
        SelectBuildSupportEditor(std::span(&editor, 1), "editor", {.Platform = "linux", .Architecture = "x86_64"});
    REQUIRE(selection);
    const auto component = Component("linux-x86_64", "2.1.0", "linux", "x86_64");

    const auto removal = PlanBuildSupportRemoval(selection.Value(), component);
    REQUIRE(removal);
    CHECK(removal.Value().Kind == BuildSupportOperationKind::Remove);
    CHECK(removal.Value().Executable == editor.AssetToolEntrypoint);
    CHECK(removal.Value().Arguments ==
          std::vector<std::string>{"remove-player-support", "--engine-version", "2.1.0", "--pack-id", "linux-x86_64"});

    const auto wrongTarget = Component("windows-x86_64", "2.1.0", "windows", "x86_64");
    CHECK_FALSE(PlanBuildSupportRemoval(selection.Value(), wrongTarget));

    auto forgedRunningSelection = selection.Value();
    forgedRunningSelection.Editor.Running = true;
    const auto runningPlan = PlanBuildSupportRemoval(forgedRunningSelection, component);
    REQUIRE_FALSE(runningPlan);
    CHECK(runningPlan.Error().Code == HubErrorCode::InstallationBusy);
}
