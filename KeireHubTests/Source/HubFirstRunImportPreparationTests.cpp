#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubFirstRunIntegration.h"
#include "KeireHub/HubFirstRunWorkflow.h"

#include "KeireHubRuntime/EditorInstallationManager.h"

#include "Keire/BuildInfo.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using namespace KeireHub;
using namespace std::chrono_literals;

namespace
{
    constexpr std::string_view EmptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    constexpr std::string_view ProjectId = "11111111-1111-4111-8111-111111111111";

    void WriteProject(const std::filesystem::path& root)
    {
        const nlohmann::json descriptor{{"schemaVersion", 3},
                                        {"id", std::string(ProjectId)},
                                        {"name", "Prepared Project"},
                                        {"createdWithEngineVersion", "0.1.0"},
                                        {"minimumEngineVersion", "0.1.0"},
                                        {"lastSavedWithEngineVersion", "0.1.0"}};
        KeireHubTests::WriteText(root / "ProjectSettings/Project.keireproject", descriptor.dump(2) + '\n');
    }

    void WriteEditor(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "bin");
        KeireHubTests::WriteText(root / "bin/Editor", {});
        nlohmann::json manifest{
            {"schemaVersion", 2},
            {"artifact", "editor"},
            {"packageId", "keire.editor"},
            {"version", "2.1.0"},
            {"channel", "Stable"},
            {"platform", std::string(Keire::GetBuildInfo().Platform)},
            {"architecture", std::string(Keire::GetBuildInfo().Architecture)},
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
        std::string document;
        for (std::size_t iteration = 0; iteration < 16; ++iteration)
        {
            document = manifest.dump(2) + '\n';
            if (manifest["installedSizeBytes"].get<std::uint64_t>() == document.size())
                break;
            manifest["installedSizeBytes"] = document.size();
        }
        document = manifest.dump(2) + '\n';
        if (manifest["installedSizeBytes"].get<std::uint64_t>() != document.size())
            throw std::runtime_error("The editor manifest fixture size did not converge.");
        KeireHubTests::WriteText(root / "editor-package.json", document);
    }

    [[nodiscard]] bool WaitForTerminal(HubFirstRunWorkflow& workflow)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto state = workflow.Snapshot()->State;
            if (state == HubFirstRunWorkflowState::Completed || state == HubFirstRunWorkflowState::Cancelled ||
                state == HubFirstRunWorkflowState::Failed)
            {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }

    struct SemaphoreReleaseGuard final
    {
        std::counting_semaphore<4>& Project;
        std::counting_semaphore<4>& Editor;

        ~SemaphoreReleaseGuard()
        {
            Project.release();
            Editor.release();
        }
    };
} // namespace

TEST_CASE("First-run workflow prepares project and editor imports away from the owner thread")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto project = temporary.Path() / "Project";
    const auto editor = temporary.Path() / "Editor";
    WriteProject(project);
    WriteEditor(editor);

    std::counting_semaphore<4> projectEntered(0);
    std::counting_semaphore<4> releaseProject(0);
    std::counting_semaphore<4> editorEntered(0);
    std::counting_semaphore<4> releaseEditor(0);
    std::thread::id projectThread;
    std::thread::id editorThread;
    HubFirstRunWorkflow workflow({.BeforeProjectInspection =
                                      [&](const std::filesystem::path&)
                                  {
                                      projectThread = std::this_thread::get_id();
                                      projectEntered.release();
                                      releaseProject.acquire();
                                  },
                                  .BeforeEditorInspection =
                                      [&](const std::filesystem::path&)
                                  {
                                      editorThread = std::this_thread::get_id();
                                      editorEntered.release();
                                      releaseEditor.acquire();
                                  }});
    [[maybe_unused]] SemaphoreReleaseGuard releaseOnFailure{releaseProject, releaseEditor};
    HubFirstRunDiscoveryRequest request{.ProjectRoots = {project},
                                        .EditorRoots = {editor},
                                        .HostPlatform = std::string(Keire::GetBuildInfo().Platform),
                                        .HostArchitecture = std::string(Keire::GetBuildInfo().Architecture)};
    const auto ownerThread = std::this_thread::get_id();
    REQUIRE(workflow.Start(std::move(request), 123));

    const bool reachedProject = projectEntered.try_acquire_for(2s);
    if (!reachedProject)
        releaseProject.release();
    REQUIRE(reachedProject);
    CHECK(projectThread != ownerThread);
    CHECK(workflow.Snapshot()->State == HubFirstRunWorkflowState::Running);
    releaseProject.release();

    const bool reachedEditor = editorEntered.try_acquire_for(2s);
    if (!reachedEditor)
        releaseEditor.release();
    REQUIRE(reachedEditor);
    CHECK(editorThread != ownerThread);
    releaseEditor.release();
    REQUIRE(WaitForTerminal(workflow));

    const auto snapshot = workflow.Snapshot();
    REQUIRE(snapshot->State == HubFirstRunWorkflowState::Completed);
    REQUIRE(snapshot->PreparedImport);
    REQUIRE(snapshot->PreparedImport->Projects.size() == 1);
    REQUIRE(snapshot->PreparedImport->Editors.size() == 1);
    CHECK(snapshot->PreparedImport->Projects.front().Id == ProjectId);
    CHECK(snapshot->PreparedImport->Editors.front().Ownership == InstallationOwnership::External);

    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(123));
    REQUIRE(ImportHubFirstRunSnapshot(*snapshot, controller));
    CHECK(controller.Projects().Snapshot()->size() == 1);
    CHECK(controller.Installations().Snapshot()->size() == 1);
}

TEST_CASE("First-run preparation revalidates discovery and publishes no import after failure")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto project = temporary.Path() / "Project";
    WriteProject(project);
    HubFirstRunWorkflow workflow({.BeforeProjectInspection = [](const std::filesystem::path& root)
                                  {
                                      std::error_code error;
                                      std::filesystem::remove(root / "ProjectSettings/Project.keireproject", error);
                                  }});
    HubFirstRunDiscoveryRequest request{.ProjectRoots = {project},
                                        .HostPlatform = std::string(Keire::GetBuildInfo().Platform),
                                        .HostArchitecture = std::string(Keire::GetBuildInfo().Architecture)};
    REQUIRE(workflow.Start(std::move(request), 123));
    REQUIRE(WaitForTerminal(workflow));
    const auto snapshot = workflow.Snapshot();
    CHECK(snapshot->State == HubFirstRunWorkflowState::Failed);
    CHECK_FALSE(snapshot->PreparedImport);

    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(123));
    const auto projectsBefore = controller.Projects().Snapshot();
    const auto editorsBefore = controller.Installations().Snapshot();
    const auto imported = ImportHubFirstRunSnapshot(*snapshot, controller);
    REQUIRE_FALSE(imported);
    CHECK(imported.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(controller.Projects().Snapshot() == projectsBefore);
    CHECK(controller.Installations().Snapshot() == editorsBefore);
}

TEST_CASE("First-run import rolls back the project registry when the editor registry write fails")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto project = temporary.Path() / "Project";
    const auto editor = temporary.Path() / "Editor";
    WriteProject(project);
    WriteEditor(editor);

    HubFirstRunWorkflow workflow;
    HubFirstRunDiscoveryRequest request{.ProjectRoots = {project},
                                        .EditorRoots = {editor},
                                        .HostPlatform = std::string(Keire::GetBuildInfo().Platform),
                                        .HostArchitecture = std::string(Keire::GetBuildInfo().Architecture)};
    REQUIRE(workflow.Start(std::move(request), 123));
    REQUIRE(WaitForTerminal(workflow));
    const auto prepared = workflow.Snapshot();
    REQUIRE(prepared->State == HubFirstRunWorkflowState::Completed);
    REQUIRE(prepared->PreparedImport);

    const auto preferences = temporary.Path() / "Preferences";
    HubController controller({.PreferenceRoot = preferences});
    REQUIRE(controller.Load(123));
    REQUIRE(controller.Projects().Upsert({.Id = "existing-project",
                                          .Root = temporary.Path() / "ExistingProject",
                                          .Name = "Existing Project",
                                          .AddedUnixSeconds = 10,
                                          .LastOpenedUnixSeconds = 11,
                                          .Pinned = true}));
    const auto projectsBefore = controller.Projects().Snapshot();
    const auto editorsBefore = controller.Installations().Snapshot();
    const auto projectRegistryBefore = KeireHubTests::ReadText(controller.Projects().Path());
    REQUIRE(std::filesystem::create_directories(controller.Installations().Path()));

    const auto imported = ImportHubFirstRunSnapshot(*prepared, controller);
    REQUIRE_FALSE(imported);
    CHECK(imported.Error().Code == HubErrorCode::IoWrite);
    CHECK(controller.Projects().Snapshot() == projectsBefore);
    CHECK(controller.Installations().Snapshot() == editorsBefore);
    CHECK(std::filesystem::is_regular_file(controller.Projects().Path()));
    CHECK(KeireHubTests::ReadText(controller.Projects().Path()) == projectRegistryBefore);
    CHECK(std::filesystem::is_directory(controller.Installations().Path()));
    auto temporaryInstallationRegistry = controller.Installations().Path();
    temporaryInstallationRegistry += ".tmp";
    CHECK_FALSE(std::filesystem::exists(temporaryInstallationRegistry));
}
