#include "KeireClient/Editor/EditorAssetOperationCoordinator.h"
#include "KeireClient/Editor/EditorBuildCookCoordinator.h"
#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"
#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"
#include "KeireClient/Editor/EditorPackageCoordinator.h"
#include "KeireClient/Editor/EditorPlayModeCoordinator.h"
#include "KeireClient/Editor/EditorReplayProfilingCoordinator.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <doctest/doctest.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] KeireEditor::EditorDocumentWorkspaceDocuments CreateTestDocuments()
    {
        KeireEditor::EditorDocumentWorkspaceDocuments documents;
        documents.Scene = std::make_unique<KeireEditor::SceneDocument>();
        documents.InputActions = std::make_unique<KeireEditor::InputActionsDocument>();
        documents.AnimatorController = std::make_unique<KeireEditor::AnimatorControllerDocument>();
        documents.AudioMixer = std::make_unique<KeireEditor::AudioMixerDocument>(
            KeireEditor::AudioMixerDocumentSpecification{.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
        documents.VfxEffect = std::make_unique<KeireEditor::VfxEffectDocument>(
            KeireEditor::VfxEffectDocumentSpecification{.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
        documents.ShaderGraph =
            std::make_unique<KeireEditor::ShaderGraphDocument>(KeireEditor::ShaderGraphDocumentSpecification{
                .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
        documents.MaterialGraph =
            std::make_unique<KeireEditor::MaterialGraphDocument>(KeireEditor::MaterialGraphDocumentSpecification{
                .ResolveInterface = [](const Keire::MaterialShaderReference&)
                { return std::optional<Keire::ShaderInterfaceDefinition>{Keire::ShaderInterfaceDefinition{}}; },
                .ResolveShader = [](const Keire::MaterialShaderReference&) { return Keire::AssetId{}; },
                .Persist = [](Keire::AssetId, std::span<const std::byte>) {},
            });
        documents.Material = std::make_unique<KeireEditor::MaterialDocument>();
        documents.ProjectSettings = std::make_unique<KeireEditor::ProjectSettingsDocument>();
        return documents;
    }
} // namespace

TEST_CASE("document workspace facade delegates maintenance and tears down exactly once")
{
    auto documents = CreateTestDocuments();
    auto* const initialScene = documents.Scene.get();
    unsigned int commits = 0;
    unsigned int autosaves = 0;
    unsigned int catalogUpdates = 0;
    unsigned int workspaceUndoCloses = 0;
    unsigned int supplementalCloses = 0;
    std::vector<std::string> shutdownFailures;

    KeireEditor::EditorDocumentWorkspaceCoordinator coordinator(
        std::move(documents),
        {
            .CommitMaterialDraft = [&] { ++commits; },
            .CancelMaterialCatalog = [] {},
            .UpdateMaterialGraphAutosave = [&](const double) { ++autosaves; },
            .UpdateMaterialCatalog = [&](const double) { ++catalogUpdates; },
            .WriteSceneRecovery = [] {},
            .ReportSceneError = [](std::string) {},
            .CloseWorkspaceUndo = [&] { ++workspaceUndoCloses; },
            .CloseSupplementalDocuments =
                [&]
            {
                ++supplementalCloses;
                throw std::runtime_error("supplemental close failed");
            },
            .ProjectWritable = [] { return false; },
            .ReportShutdownFailure = [&](const std::string_view operation, const std::exception_ptr&)
            { shutdownFailures.emplace_back(operation); },
        });

    const auto callback = coordinator.CaptureCallbackToken();
    REQUIRE(callback.Current());
    coordinator.UpdateMaintenance({}, 0.016);
    CHECK(autosaves == 1);
    CHECK(catalogUpdates == 1);
    coordinator.CommitMaterialDraft();
    CHECK(commits == 1);
    coordinator.CloseUndoContexts();
    CHECK(workspaceUndoCloses == 1);

    const Keire::AssetId target(0x444f43554d454e54ULL, 1);
    coordinator.SetPendingTransition(KeireEditor::EditorDocumentTransitionAction::Open, target);
    CHECK(coordinator.PendingTransition() == KeireEditor::EditorDocumentTransitionAction::Open);
    CHECK(coordinator.PendingTransitionAsset() == target);
    const auto [action, asset] = coordinator.TakePendingTransition();
    CHECK(action == KeireEditor::EditorDocumentTransitionAction::Open);
    CHECK(asset == target);
    CHECK(coordinator.PendingTransition() == KeireEditor::EditorDocumentTransitionAction::None);

    auto replacement = std::make_unique<KeireEditor::SceneDocument>();
    auto* const replacementScene = replacement.get();
    auto previous = coordinator.ReplaceScene(std::move(replacement));
    CHECK(previous.get() == initialScene);
    CHECK(&coordinator.Scene() == replacementScene);
    previous.reset();

    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK(supplementalCloses == 1);
    CHECK(shutdownFailures == std::vector<std::string>{"close-supplemental-documents"});
}

TEST_CASE("document workspace facade rejects off-owner updates without running callbacks")
{
    auto documents = CreateTestDocuments();
    auto* const originalScene = documents.Scene.get();
    std::atomic_bool callbackInvoked = false;
    std::atomic_uint rejections = 0;
    KeireEditor::EditorDocumentWorkspaceCoordinator coordinator(
        std::move(documents),
        {
            .CommitMaterialDraft = [] {},
            .CancelMaterialCatalog = [] {},
            .UpdateMaterialGraphAutosave = [&](const double) { callbackInvoked = true; },
            .UpdateMaterialCatalog = [](const double) {},
            .WriteSceneRecovery = [] {},
            .ReportSceneError = [](std::string) {},
            .CloseWorkspaceUndo = [] {},
            .CloseSupplementalDocuments = [] {},
            .ProjectWritable = [] { return false; },
        });
    const auto callback = coordinator.CaptureCallbackToken();
    coordinator.SetPendingTransition(KeireEditor::EditorDocumentTransitionAction::Close);

    std::thread worker(
        [&]
        {
            const auto reject = [&](const auto& action)
            {
                try
                {
                    action();
                }
                catch (const std::logic_error&)
                {
                    ++rejections;
                }
            };
            reject([&] { coordinator.UpdateMaintenance({}, 0.016); });
            reject([&] { static_cast<void>(coordinator.Scene()); });
            reject([&] { static_cast<void>(coordinator.ShutdownComplete()); });
            coordinator.Shutdown();
        });
    worker.join();
    CHECK(rejections == 3);
    CHECK_FALSE(callbackInvoked);
    CHECK(callback.Current());
    CHECK_FALSE(coordinator.ShutdownComplete());
    CHECK(&coordinator.Scene() == originalScene);
    CHECK(coordinator.PendingTransition() == KeireEditor::EditorDocumentTransitionAction::Close);

    coordinator.Shutdown();
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(static_cast<void>(coordinator.Scene()), std::logic_error);
}

TEST_CASE("document workspace facade closes project settings after a save failure")
{
    auto documents = CreateTestDocuments();
    auto* projectSettings = documents.ProjectSettings.get();
    const auto invalidRoot = std::filesystem::temp_directory_path() /
                             ("Keire-Coordinator-ProjectSettings-" + Keire::AssetId::Generate().ToString());
    std::filesystem::remove_all(invalidRoot);
    {
        std::ofstream file(invalidRoot, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file << "not a project directory";
    }
    projectSettings->Open(invalidRoot, {}, Keire::DefaultProjectAuthoringSettings());
    auto edited = projectSettings->Settings();
    edited.Exposure = 1.5F;
    projectSettings->Update(edited);
    REQUIRE(projectSettings->Dirty());
    std::vector<std::string> failures;

    KeireEditor::EditorDocumentWorkspaceCoordinator coordinator(
        std::move(documents),
        {
            .CommitMaterialDraft = [] {},
            .CancelMaterialCatalog = [] {},
            .UpdateMaterialGraphAutosave = [](double) {},
            .UpdateMaterialCatalog = [](double) {},
            .WriteSceneRecovery = [] {},
            .ReportSceneError = [](std::string) {},
            .CloseWorkspaceUndo = [] {},
            .CloseSupplementalDocuments = [] {},
            .ProjectWritable = [] { return true; },
            .ReportShutdownFailure = [&](const std::string_view operation, const std::exception_ptr&)
            { failures.emplace_back(operation); },
        });

    coordinator.CloseProjectSettings();
    CHECK_FALSE(projectSettings->Opened());
    CHECK_FALSE(projectSettings->Dirty());
    CHECK(failures == std::vector<std::string>{"save-project-settings"});
    coordinator.CloseProjectSettings();
    CHECK(failures.size() == 1);
    std::filesystem::remove(invalidRoot);
}

TEST_CASE("package coordinator owns panel and export lifecycle idempotently")
{
    unsigned int panelShutdowns = 0;
    KeireEditor::EditorPackageCoordinator coordinator({
        .ShutdownPanel = [&] { ++panelShutdowns; },
        .AssetDatabase = [] { return Keire::Ref<Keire::AssetDatabase>{}; },
        .AssetRecords = [] { return std::span<const Keire::AssetSourceRecord>{}; },
        .Windows = [] { return Keire::Ref<Keire::WindowSystem>{}; },
        .MainWindow = [] { return Keire::WindowId{}; },
        .SetStatus = [](std::string) {},
        .SetError = [](std::string) {},
    });
    const auto callback = coordinator.CaptureCallbackToken();
    CHECK(callback.Current());
    CHECK_FALSE(coordinator.Busy());

    std::atomic_bool busyRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                static_cast<void>(coordinator.Busy());
            }
            catch (const std::logic_error&)
            {
                busyRejected = true;
            }
            coordinator.Shutdown();
        });
    worker.join();
    CHECK(busyRejected);
    CHECK(callback.Current());
    CHECK_FALSE(coordinator.ShutdownComplete());
    CHECK(panelShutdowns == 0);

    coordinator.ShutdownPanel();
    coordinator.ShutdownPanel();
    CHECK(panelShutdowns == 1);
    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(panelShutdowns == 1);
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_FALSE(coordinator.CaptureCallbackToken().Current());
    CHECK_THROWS_AS(static_cast<void>(coordinator.Busy()), std::logic_error);
}

TEST_CASE("replay profiling coordinator owns presentation state and invalidates late callbacks")
{
    KeireEditor::EditorReplayProfilingCoordinator coordinator;
    coordinator.Replay().Path = "Library/Replays/test.keirereplay";
    coordinator.Profiler().Paused = true;
    coordinator.Profiler().Presentation.FrameSequence = 42;
    CHECK(coordinator.Replay().Path == "Library/Replays/test.keirereplay");
    CHECK(coordinator.Profiler().Presentation.FrameSequence == 42);
    const auto callback = coordinator.CaptureCallbackToken();
    REQUIRE(callback.Current());

    std::atomic_bool accessRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                static_cast<void>(coordinator.Replay());
            }
            catch (const std::logic_error&)
            {
                accessRejected = true;
            }
            coordinator.Shutdown();
        });
    worker.join();
    CHECK(accessRejected);
    CHECK(callback.Current());
    CHECK_FALSE(coordinator.ShutdownComplete());
    CHECK(coordinator.Replay().Path == "Library/Replays/test.keirereplay");

    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(static_cast<void>(coordinator.Replay()), std::logic_error);
}

TEST_CASE("managed-runtime coordinator schedules builds and tears down phased state exactly once")
{
    unsigned int starts = 0;
    unsigned int polls = 0;
    unsigned int detaches = 0;
    unsigned int resets = 0;
    std::vector<std::string> errors;
    KeireEditor::EditorManagedRuntimeCoordinator coordinator({
        .StartBuild = [&] { ++starts; },
        .PollBuild = [&] { ++polls; },
        .ReportBuildError = [&](std::string message) { errors.push_back(std::move(message)); },
        .DetachRuntimeServices = [&] { ++detaches; },
        .ResetRuntimeInput = [&] { ++resets; },
    });
    const auto callback = coordinator.CaptureCallbackToken();
    REQUIRE(callback.Current());

    coordinator.ScheduleBuild(0.1);
    coordinator.Update(0.04);
    CHECK(starts == 0);
    CHECK(polls == 1);
    coordinator.Update(0.06);
    CHECK(starts == 1);
    CHECK(polls == 2);
    CHECK(errors.empty());

    coordinator.ScheduleBuild(0.1);
    coordinator.Update(0.04);
    coordinator.ScheduleBuild(0.1);
    coordinator.Update(0.06);
    CHECK(starts == 1);
    coordinator.Update(0.04);
    CHECK(starts == 2);
    CHECK(polls == 5);

    coordinator.DetachRuntimeServices();
    coordinator.DetachRuntimeServices();
    CHECK(detaches == 1);
    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(detaches == 1);
    CHECK(resets == 1);
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(coordinator.Update(0.0), std::logic_error);
}

TEST_CASE("play-mode coordinator preserves transition order and invalidates late callbacks")
{
    std::vector<std::string> trace;
    double observedDelta = 0.0;
    double observedAlpha = 0.0;
    unsigned int closes = 0;
    KeireEditor::EditorPlayModeCoordinator coordinator({
        .ProcessSceneTransition = [&] { trace.emplace_back("transition"); },
        .FinalizeEditorMutation = [&] { trace.emplace_back("mutation"); },
        .CompletePendingTransition = [&] { trace.emplace_back("completion"); },
        .UpdateRuntime =
            [&](const double delta, const double alpha)
        {
            observedDelta = delta;
            observedAlpha = alpha;
        },
        .ContinuePendingPlay = [&] { trace.emplace_back("continue"); },
        .CloseRuntime = [&] { ++closes; },
    });
    const auto callback = coordinator.CaptureCallbackToken();

    coordinator.UpdateTransitions();
    coordinator.UpdateRuntime(0.016, 0.5);
    coordinator.ContinuePendingPlay();
    const std::vector<std::string> expectedTrace{"transition", "mutation", "completion", "continue"};
    CHECK(trace == expectedTrace);
    CHECK(observedDelta == doctest::Approx(0.016));
    CHECK(observedAlpha == doctest::Approx(0.5));

    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(closes == 1);
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(coordinator.UpdateTransitions(), std::logic_error);
}

TEST_CASE("asset-operation coordinator gates polling and closes owned authorities exactly once")
{
    unsigned int updates = 0;
    unsigned int mutations = 0;
    unsigned int prefabs = 0;
    unsigned int polls = 0;
    unsigned int operationShutdowns = 0;
    unsigned int workspaceCloses = 0;
    bool busy = true;
    KeireEditor::EditorAssetOperationCoordinator coordinator({
        .UpdateOperations = [&] { ++updates; },
        .DrainQueuedMutation = [&] { ++mutations; },
        .DrainQueuedPrefab = [&] { ++prefabs; },
        .BusyOrPending = [&] { return busy; },
        .PollHotReload = [&] { ++polls; },
        .ShutdownOperations = [&] { ++operationShutdowns; },
        .CloseAssetWorkspace = [&] { ++workspaceCloses; },
    });
    const auto callback = coordinator.CaptureCallbackToken();

    coordinator.UpdateOperations();
    coordinator.DrainQueuedMutation();
    coordinator.DrainQueuedPrefab();
    CHECK_FALSE(coordinator.AdmitPolling(0.2));
    busy = false;
    CHECK_FALSE(coordinator.AdmitPolling(0.04));
    CHECK(coordinator.AdmitPolling(0.06));
    coordinator.PollHotReload();
    CHECK(updates == 1);
    CHECK(mutations == 1);
    CHECK(prefabs == 1);
    CHECK(polls == 1);

    coordinator.ShutdownOperations();
    coordinator.ShutdownOperations();
    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(operationShutdowns == 1);
    CHECK(workspaceCloses == 1);
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(coordinator.UpdateOperations(), std::logic_error);
}

TEST_CASE("build-cook coordinator preserves admission and repeated shutdown behavior")
{
    unsigned int updates = 0;
    unsigned int shutdowns = 0;
    bool assetsReady = false;
    KeireEditor::EditorBuildCookCoordinator coordinator({
        .UpdateBuild = [&] { ++updates; },
        .AssetDatabaseReady = [&] { return assetsReady; },
        .ShutdownBuild = [&] { ++shutdowns; },
    });
    const auto callback = coordinator.CaptureCallbackToken();

    CHECK_FALSE(coordinator.Update());
    assetsReady = true;
    CHECK(coordinator.Update());
    CHECK(updates == 2);
    coordinator.Shutdown();
    coordinator.Shutdown();
    CHECK(shutdowns == 1);
    CHECK(coordinator.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    CHECK_THROWS_AS(static_cast<void>(coordinator.Update()), std::logic_error);
}

TEST_CASE("extracted runtime coordinators reject off-owner updates before callbacks")
{
    std::atomic_uint callbacks = 0;
    std::atomic_uint rejections = 0;
    KeireEditor::EditorManagedRuntimeCoordinator managed({
        .StartBuild = [&] { ++callbacks; },
        .PollBuild = [&] { ++callbacks; },
        .ReportBuildError = [](std::string) {},
        .DetachRuntimeServices = [] {},
        .ResetRuntimeInput = [] {},
    });
    KeireEditor::EditorPlayModeCoordinator play({
        .ProcessSceneTransition = [&] { ++callbacks; },
        .FinalizeEditorMutation = [] {},
        .CompletePendingTransition = [] {},
        .UpdateRuntime = [](double, double) {},
        .ContinuePendingPlay = [] {},
        .CloseRuntime = [] {},
    });
    KeireEditor::EditorAssetOperationCoordinator assets({
        .UpdateOperations = [&] { ++callbacks; },
        .DrainQueuedMutation = [] {},
        .DrainQueuedPrefab = [] {},
        .BusyOrPending = [] { return false; },
        .PollHotReload = [] {},
        .ShutdownOperations = [] {},
        .CloseAssetWorkspace = [] {},
    });
    KeireEditor::EditorBuildCookCoordinator build({
        .UpdateBuild = [&] { ++callbacks; },
        .AssetDatabaseReady = [] { return true; },
        .ShutdownBuild = [] {},
    });
    const auto managedToken = managed.CaptureCallbackToken();
    const auto playToken = play.CaptureCallbackToken();
    const auto assetToken = assets.CaptureCallbackToken();
    const auto buildToken = build.CaptureCallbackToken();
    managed.ScheduleBuild(0.0);

    std::thread worker(
        [&]
        {
            const auto reject = [&](const auto& action)
            {
                try
                {
                    action();
                }
                catch (const std::logic_error&)
                {
                    ++rejections;
                }
            };
            reject([&] { managed.Update(0.0); });
            reject([&] { play.UpdateTransitions(); });
            reject([&] { assets.UpdateOperations(); });
            reject([&] { static_cast<void>(build.Update()); });
            reject([&] { static_cast<void>(managed.ShutdownComplete()); });
            reject([&] { static_cast<void>(play.ShutdownComplete()); });
            reject([&] { static_cast<void>(assets.ShutdownComplete()); });
            reject([&] { static_cast<void>(build.ShutdownComplete()); });
            managed.Shutdown();
            play.Shutdown();
            assets.Shutdown();
            build.Shutdown();
        });
    worker.join();

    CHECK(rejections == 8);
    CHECK(callbacks == 0);
    CHECK(managedToken.Current());
    CHECK(playToken.Current());
    CHECK(assetToken.Current());
    CHECK(buildToken.Current());
    CHECK_FALSE(managed.ShutdownComplete());
    CHECK_FALSE(play.ShutdownComplete());
    CHECK_FALSE(assets.ShutdownComplete());
    CHECK_FALSE(build.ShutdownComplete());
    managed.Update(0.0);
    CHECK(callbacks == 2);
    play.UpdateTransitions();
    CHECK(callbacks == 3);
    assets.UpdateOperations();
    CHECK(callbacks == 4);
    CHECK(build.Update());
    CHECK(callbacks == 5);

    managed.Shutdown();
    play.Shutdown();
    assets.Shutdown();
    build.Shutdown();
    CHECK_FALSE(managedToken.Current());
    CHECK_FALSE(playToken.Current());
    CHECK_FALSE(assetToken.Current());
    CHECK_FALSE(buildToken.Current());
}
