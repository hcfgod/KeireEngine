#include "KeireClient/Editor/AssetOperationService.h"

#include <doctest/doctest.h>

#include <KeireEditorTests/EditorTestSupport.h>

#include "KeireInternal/FileSystem.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    void SetTestWorkerMode(const char* value)
    {
#if defined(_WIN32)
        if (_putenv_s("KEIRE_EDITOR_TEST_WORKER_MODE", value ? value : "") != 0)
            throw std::runtime_error("Could not configure the editor-test worker mode.");
#else
        const auto result =
            value ? setenv("KEIRE_EDITOR_TEST_WORKER_MODE", value, 1) : unsetenv("KEIRE_EDITOR_TEST_WORKER_MODE");
        if (result != 0)
            throw std::runtime_error("Could not configure the editor-test worker mode.");
#endif
    }
} // namespace

TEST_CASE("Asset operation service runs the isolated worker and publishes a source index")
{
    const auto location = std::filesystem::temp_directory_path() / std::filesystem::path(u8"Kéire-资产-Worker-Test");
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    const auto worker = KeireEditor::AssetOperationService::ResolveWorkerExecutable(KeireEditorTests::ExecutablePath);
    REQUIRE(std::filesystem::is_regular_file(worker));
    {
        auto project = Keire::Project::Create(
            {.Location = location, .Name = "Worker Project", .Template = Keire::ProjectTemplate::Empty});
        REQUIRE(project);
        const auto interrupted = project->Root() / "Library/AssetOperations" / Keire::AssetId::Generate().ToString();
        std::filesystem::create_directories(interrupted);
        std::filesystem::create_directories(project->Root() / "Assets/Shaders");
        Keire::Detail::WriteTextFileAtomically(project->Root() / "Assets/Shaders/StaleAuxiliary.hlsl", "stale");
        Keire::Detail::WriteTextFileAtomically(interrupted / "create-auxiliary.journal",
                                               "Scenes/Missing.keirescene\nShaders/StaleAuxiliary.hlsl\n");
        KeireEditor::AssetOperationService operations(worker, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        CHECK_FALSE(operations.Busy());
        const auto completion = operations.TakeCompletion();
        REQUIRE(completion);
        INFO(completion->Result.Diagnostic);
        CHECK(completion->Result.Success);
        CHECK(std::filesystem::is_regular_file(completion->SourceIndexPath));
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Shaders/StaleAuxiliary.hlsl"));
        CHECK_FALSE(std::filesystem::exists(interrupted / "create-auxiliary.journal"));

        const std::string auxiliaryText = "worker auxiliary source";
        const auto auxiliaryBytes = std::as_bytes(std::span(auxiliaryText));
        operations.QueueCreateAssetWithAuxiliary(
            "Scenes/WorkerCreated.keirescene",
            Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Worker Created")), {}, {},
            {{"Shaders/WorkerAuxiliary.hlsl", std::vector<std::byte>(auxiliaryBytes.begin(), auxiliaryBytes.end())}});
        const auto createDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < createDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto created = operations.TakeCompletion();
        REQUIRE(created);
        INFO(created->Result.Diagnostic);
        CHECK(created->Result.Success);
        CHECK(created->Kind == Keire::Detail::AssetWorkerOperationKind::CreateAsset);
        CHECK(created->Result.CreatedAsset);
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerCreated.keirescene"));
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Shaders/WorkerAuxiliary.hlsl"));

        operations.QueueAssetImport(created->Result.CreatedAsset, KeireEditor::AssetOperationPriority::ExplicitAction,
                                    {.Reason = "isolated-worker-targeted-test"});
        const auto targetedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < targetedDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto targeted = operations.TakeCompletion();
        REQUIRE(targeted);
        INFO(targeted->Result.Diagnostic);
        CHECK(targeted->Result.Success);
        CHECK(targeted->Kind == Keire::Detail::AssetWorkerOperationKind::ImportAssets);
        CHECK(targeted->Result.Import.Statuses.size() == 1);
        CHECK(targeted->WorkerOutput.find("kind=import-assets") != std::string::npos);
        CHECK(targeted->WorkerOutput.find("reason='isolated-worker-targeted-test'") != std::string::npos);
        CHECK(targeted->WorkerOutput.find("targets=1") != std::string::npos);

        operations.QueueMutation({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                  .Asset = created->Result.CreatedAsset,
                                  .Destination = "Scenes/WorkerRenamed.keirescene"});
        const auto mutationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < mutationDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto mutated = operations.TakeCompletion();
        REQUIRE(mutated);
        INFO(mutated->Result.Diagnostic);
        CHECK(mutated->Result.Success);
        CHECK(mutated->Kind == Keire::Detail::AssetWorkerOperationKind::Mutate);
        CHECK(mutated->Result.MutatedAssets == std::vector{created->Result.CreatedAsset});
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/WorkerCreated.keirescene"));
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));

        operations.QueueMutation(
            {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = created->Result.CreatedAsset});
        const auto trashDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < trashDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto trashed = operations.TakeCompletion();
        REQUIRE(trashed);
        INFO(trashed->Result.Diagnostic);
        REQUIRE(trashed->Result.Success);
        REQUIRE(trashed->Result.Trash);
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));

        operations.QueueMutation(
            {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash, .Trash = trashed->Result.Trash});
        const auto restoreDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < restoreDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto restored = operations.TakeCompletion();
        REQUIRE(restored);
        INFO(restored->Result.Diagnostic);
        CHECK(restored->Result.Success);
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));
    }
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Asset operation service reports malformed worker completion and bounds forced shutdown")
{
    const auto location =
        std::filesystem::temp_directory_path() / ("Keire-Worker-Failure-" + Keire::AssetId::Generate().ToString());
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    auto project = Keire::Project::Create(
        {.Location = location, .Name = "Worker Failure Project", .Template = Keire::ProjectTemplate::Empty});

    SetTestWorkerMode("malformed");
    {
        KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        operations.Update();
        SetTestWorkerMode(nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto completion = operations.TakeCompletion();
        REQUIRE(completion);
        CHECK_FALSE(completion->Result.Success);
        CHECK_FALSE(completion->Result.Diagnostic.empty());
    }

    SetTestWorkerMode("hang");
    {
        KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        operations.Update();
        SetTestWorkerMode(nullptr);
        REQUIRE(operations.Busy());
        const auto started = std::chrono::steady_clock::now();
        operations.Shutdown();
        const auto shutdownDuration = std::chrono::steady_clock::now() - started;
        CAPTURE(std::chrono::duration_cast<std::chrono::milliseconds>(shutdownDuration).count());
        CHECK(shutdownDuration < std::chrono::seconds(2));
        CHECK_FALSE(operations.Busy());
    }
    SetTestWorkerMode(nullptr);
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Asset operation service coalesces material refresh generations before dispatch")
{
    const auto location =
        std::filesystem::temp_directory_path() / ("Keire-Worker-Queue-" + Keire::AssetId::Generate().ToString());
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    auto project = Keire::Project::Create(
        {.Location = location, .Name = "Worker Queue Project", .Template = Keire::ProjectTemplate::Empty});
    KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
    const auto first = Keire::AssetId::Generate();
    const auto second = Keire::AssetId::Generate();
    operations.QueueAssetImport(first, KeireEditor::AssetOperationPriority::MaterialRefresh,
                                {.ReloadAsset = first, .Generation = 1});
    operations.QueueAssetImport(second, KeireEditor::AssetOperationPriority::MaterialRefresh,
                                {.ReloadAsset = second, .Generation = 2});
    operations.QueueCook({.Name = "Test"}, project->Root() / "Build/Cooked");
    operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
    CHECK(operations.QueuedCount() == 3);
    operations.Shutdown();
    std::filesystem::remove_all(location, cleanupError);
}
