#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/HubFirstRunDiscovery.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view EmptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    constexpr std::string_view ProjectA = "11111111-1111-4111-8111-111111111111";
    constexpr std::string_view ProjectB = "22222222-2222-4222-8222-222222222222";

    void WriteProject(const std::filesystem::path& root, const std::string_view id = ProjectA,
                      const std::string_view name = "Project")
    {
        const nlohmann::json descriptor{{"schemaVersion", 3},
                                        {"id", std::string(id)},
                                        {"name", std::string(name)},
                                        {"createdWithEngineVersion", "0.1.0"},
                                        {"minimumEngineVersion", "0.1.0"},
                                        {"lastSavedWithEngineVersion", "0.1.0"}};
        KeireHubTests::WriteText(root / "ProjectSettings/Project.keireproject", descriptor.dump(2) + '\n');
    }

    void WriteEditor(const std::filesystem::path& root, const std::string& platform = "Windows")
    {
        std::filesystem::create_directories(root / "bin");
        KeireHubTests::WriteText(root / "bin/Editor", {});
        nlohmann::json manifest{
            {"schemaVersion", 2},
            {"artifact", "editor"},
            {"packageId", "keire.editor"},
            {"version", "2.1.0"},
            {"channel", "Stable"},
            {"platform", platform},
            {"architecture", "x86_64"},
            {"entrypoints", {{"editor", "bin/Editor"}}},
            {"projectSchema", {{"minimum", 1}, {"maximum", 3}}},
            {"inventoryExcludes", {"editor-package.json"}},
            {"files", {{{"path", "bin/Editor"}, {"sizeBytes", 0}, {"sha256", std::string(EmptySha256)}}}},
            {"installedSizeBytes", 1},
            {"manifestFingerprint", std::string(64, '0')}};
        auto fingerprint = ComputeEditorPackageManifestFingerprint(manifest.dump());
        if (!fingerprint)
            throw std::runtime_error(fingerprint.Error().Message);
        manifest["manifestFingerprint"] = fingerprint.Value();
        std::string bytes;
        for (std::size_t iteration = 0; iteration < 16; ++iteration)
        {
            bytes = manifest.dump(2) + '\n';
            if (manifest["installedSizeBytes"].get<std::uint64_t>() == bytes.size())
                break;
            manifest["installedSizeBytes"] = bytes.size();
        }
        bytes = manifest.dump(2) + '\n';
        if (manifest["installedSizeBytes"].get<std::uint64_t>() != bytes.size())
            throw std::runtime_error("The test editor manifest size did not converge.");
        KeireHubTests::WriteText(root / "editor-package.json", bytes);
    }

    [[nodiscard]] HubFirstRunDiscoveryRequest Request()
    {
        return {.HostPlatform = "windows", .HostArchitecture = "x86_64"};
    }
} // namespace

TEST_CASE("First-run discovery rejects broad traversal and excessive scan requests")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto scan = temporary.Path() / "Scan";
    std::filesystem::create_directories(scan / "One");
    std::filesystem::create_directories(scan / "Two");
    HubFirstRunDiscovery discovery;
    const auto initial = discovery.Snapshot();

    auto broad = Request();
    broad.ProjectRoots = {std::filesystem::current_path().root_path()};
    CHECK_FALSE(discovery.Discover(broad));
    CHECK(discovery.Snapshot() == initial);

    auto traversal = Request();
    traversal.ProjectRoots = {scan / ".." / "Scan"};
    CHECK_FALSE(discovery.Discover(traversal));
    CHECK(discovery.Snapshot() == initial);

    auto tooManyRoots = Request();
    tooManyRoots.ProjectRoots = {scan, scan};
    tooManyRoots.Limits.MaximumRoots = 1;
    CHECK_FALSE(discovery.Discover(tooManyRoots));
    CHECK(discovery.Snapshot() == initial);

    auto tooManyEntries = Request();
    tooManyEntries.ProjectRoots = {scan};
    tooManyEntries.Limits.MaximumEntries = 1;
    const auto exceeded = discovery.Discover(tooManyEntries);
    REQUIRE_FALSE(exceeded);
    CHECK(exceeded.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(discovery.Snapshot() == initial);
}

TEST_CASE("First-run discovery respects depth and preserves UTF-8 project identity paths")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto scan = temporary.Path() / std::filesystem::path(std::u8string(u8"Découverte-世界"));
    const auto shallow = scan / std::filesystem::path(std::u8string(u8"Kéire-游戏"));
    const auto deep = scan / "LevelOne/LevelTwo";
    WriteProject(shallow, ProjectA, "Kéire World");
    WriteProject(deep, ProjectB, "Too Deep");

    auto request = Request();
    request.ProjectRoots = {scan};
    request.Limits.MaximumDepth = 1;
    HubFirstRunDiscovery discovery;
    REQUIRE(discovery.Discover(request));
    REQUIRE(discovery.Snapshot()->State == HubFirstRunDiscoveryState::Completed);
    REQUIRE(discovery.Snapshot()->Projects.size() == 1);
    CHECK(discovery.Snapshot()->Projects.front().Id == ProjectA);
    CHECK(discovery.Snapshot()->Projects.front().Name == "Kéire World");
    CHECK(discovery.Snapshot()->Projects.front().Root == std::filesystem::weakly_canonical(shallow));
    CHECK(discovery.Snapshot()->Projects.front().DescriptorPath ==
          std::filesystem::weakly_canonical(shallow) / "ProjectSettings/Project.keireproject");
}

TEST_CASE("First-run discovery ignores malformed and host-incompatible candidates")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto projects = temporary.Path() / "Projects";
    WriteProject(projects / "Valid", ProjectA, "Valid");
    KeireHubTests::WriteText(projects / "Malformed/ProjectSettings/Project.keireproject", "{not json");
    WriteProject(projects / "BadIdentity", "not-a-project-id", "Bad");

    const auto editors = temporary.Path() / "Editors";
    WriteEditor(editors / "Valid");
    WriteEditor(editors / "WrongHost", "Linux");
    KeireHubTests::WriteText(editors / "Malformed/editor-package.json", "{}");

    auto request = Request();
    request.ProjectRoots = {projects};
    request.EditorRoots = {editors};
    HubFirstRunDiscovery discovery;
    REQUIRE(discovery.Discover(request));
    REQUIRE(discovery.Snapshot()->Projects.size() == 1);
    CHECK(discovery.Snapshot()->Projects.front().Name == "Valid");
    REQUIRE(discovery.Snapshot()->Editors.size() == 1);
    CHECK(discovery.Snapshot()->Editors.front().Root == std::filesystem::weakly_canonical(editors / "Valid"));
}

TEST_CASE("First-run discovery never follows symbolic links outside an explicit root")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto scan = temporary.Path() / "Scan";
    const auto visible = scan / "Visible";
    const auto outside = temporary.Path() / "Outside";
    WriteProject(visible, ProjectA, "Visible");
    WriteProject(outside, ProjectB, "Outside");
    std::error_code linkError;
    const auto link = scan / "OutsideLink";
    std::filesystem::create_directory_symlink(outside, link, linkError);

    auto request = Request();
    request.ProjectRoots = {scan};
    HubFirstRunDiscovery discovery;
    REQUIRE(discovery.Discover(request));
    REQUIRE(discovery.Snapshot()->Projects.size() == 1);
    CHECK(discovery.Snapshot()->Projects.front().Id == ProjectA);

    if (linkError)
    {
        MESSAGE("Directory symlinks are unavailable in this test environment: " << linkError.message());
        if (KeireHubTests::RunningInCi())
            FAIL_CHECK("CI must provide symbolic-link capability for first-run discovery confinement tests.");
        return;
    }
    const auto before = discovery.Snapshot();
    auto linkedRoot = Request();
    linkedRoot.ProjectRoots = {link};
    CHECK_FALSE(discovery.Discover(linkedRoot));
    CHECK(discovery.Snapshot() == before);
}

TEST_CASE("First-run discovery cancellation publishes a deterministic partial snapshot and progress")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto scan = temporary.Path() / "Scan";
    for (std::size_t index = 0; index < 12; ++index)
        std::filesystem::create_directories(scan / ("Folder-" + std::to_string(index)));
    auto request = Request();
    request.ProjectRoots = {scan};
    bool cancel = false;
    std::vector<HubFirstRunDiscoveryPhase> phases;
    HubFirstRunDiscovery discovery;
    REQUIRE(discovery.Discover(request, {.IsCancelled = [&] { return cancel; },
                                         .ReportProgress =
                                             [&](const HubFirstRunDiscoveryProgress& progress)
                                         {
                                             phases.push_back(progress.Phase);
                                             if (progress.EntriesVisited >= 2)
                                                 cancel = true;
                                         }}));
    CHECK(discovery.Snapshot()->State == HubFirstRunDiscoveryState::Cancelled);
    CHECK(discovery.Snapshot()->EntriesVisited == 2);
    CHECK(discovery.Snapshot()->Projects.empty());
    REQUIRE_FALSE(phases.empty());
    CHECK(phases.back() == HubFirstRunDiscoveryPhase::Cancelled);
}

TEST_CASE("First-run discovery deterministically deduplicates overlapping roots and ancestry")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto scan = temporary.Path() / "Scan";
    const auto projectA = scan / "A";
    const auto projectB = scan / "B";
    const auto editor = scan / "Editor";
    WriteProject(projectA, ProjectA, "First Copy");
    WriteProject(projectB, ProjectA, "Second Copy");
    WriteEditor(editor);

    auto firstRequest = Request();
    firstRequest.ProjectRoots = {projectB, scan};
    firstRequest.EditorRoots = {editor, scan};
    firstRequest.PackagedOrCombinedAncestry = editor / "bin/Editor";
    auto secondRequest = Request();
    secondRequest.ProjectRoots = {scan, projectB};
    secondRequest.EditorRoots = {scan, editor};
    secondRequest.PackagedOrCombinedAncestry = editor / "bin/Editor";

    HubFirstRunDiscovery first;
    HubFirstRunDiscovery second;
    REQUIRE(first.Discover(firstRequest));
    REQUIRE(second.Discover(secondRequest));
    CHECK(*first.Snapshot() == *second.Snapshot());
    REQUIRE(first.Snapshot()->Projects.size() == 1);
    CHECK(first.Snapshot()->Projects.front().Root == std::filesystem::weakly_canonical(projectA));
    REQUIRE(first.Snapshot()->Editors.size() == 1);
    CHECK(first.Snapshot()->Editors.front().Root == std::filesystem::weakly_canonical(editor));

    auto ancestryRequest = Request();
    ancestryRequest.PackagedOrCombinedAncestry = editor / "bin/Editor";
    HubFirstRunDiscovery ancestryOnly;
    REQUIRE(ancestryOnly.Discover(ancestryRequest));
    REQUIRE(ancestryOnly.Snapshot()->Editors.size() == 1);
    CHECK(ancestryOnly.Snapshot()->Editors.front().Root == std::filesystem::weakly_canonical(editor));
}
