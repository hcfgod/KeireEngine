#include "KeireClient/Editor/AssetOperationService.h"

#include <doctest/doctest.h>

#include <KeireEditorTests/EditorTestSupport.h>

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("scene Save As adoption compares the original snapshot and preserves intervening edits")
{
    const auto sourceAsset = Keire::AssetId::Generate();
    Keire::SceneDefinition source;
    source.Name = "Original";
    auto copy = source;
    copy.Name = "Workshop Caf\xc3\xa9";
    KeireEditor::AssetOperationContext context{
        .FollowUp = KeireEditor::AssetOperationFollowUp::AdoptSceneCopy,
        .SceneSnapshot = copy,
        .SourceSceneSnapshot = source,
        .SourceSceneAsset = sourceAsset,
    };
    CHECK(context.CanAdoptSceneCopy(sourceAsset, source));
    CHECK_FALSE(context.CanAdoptSceneCopy(Keire::AssetId::Generate(), source));
    CHECK_FALSE(context.CanAdoptSceneCopy({}, source));
    auto edited = source;
    edited.Name = "Renamed while saving";
    CHECK_FALSE(context.CanAdoptSceneCopy(sourceAsset, edited));
    edited = source;
    edited.Objects.push_back({.Id = Keire::AssetId::Generate(), .Name = "Added while saving"});
    CHECK_FALSE(context.CanAdoptSceneCopy(sourceAsset, edited));
    context.SourceSceneSnapshot.reset();
    CHECK_FALSE(context.CanAdoptSceneCopy(sourceAsset, source));
    context.SourceSceneSnapshot = source;
    context.SceneSnapshot.reset();
    CHECK_FALSE(context.CanAdoptSceneCopy(sourceAsset, source));
}

namespace
{
    class AssetWorkerTestRuntime final
    {
      public:
        explicit AssetWorkerTestRuntime(const std::filesystem::path& builtWorker)
        {
#if defined(_WIN32)
            const auto executable = std::filesystem::absolute(KeireEditorTests::ExecutablePath).lexically_normal();
            auto repositoryRoot = executable.parent_path();
            while (!std::filesystem::is_regular_file(repositoryRoot / "Config/Dependencies.lock"))
            {
                const auto parent = repositoryRoot.parent_path();
                if (parent == repositoryRoot)
                    throw std::runtime_error("Could not locate the repository root for the asset-worker test runtime.");
                repositoryRoot = parent;
            }

            const auto configurationOutput = executable.parent_path().parent_path().filename().string();
            const auto separator = configurationOutput.find("-windows-");
            if (separator == std::string::npos)
                throw std::runtime_error("Could not identify the asset-worker test configuration.");
            const auto configuration = configurationOutput.substr(0, separator);
            const bool releaseRuntime =
                configuration == "Release" || configuration == "Profile" || configuration == "Dist";
            if (!releaseRuntime && configuration != "Debug" && configuration != "DebugASan" &&
                configuration != "DebugUBSan" && configuration != "DebugTSan" && configuration != "Coverage")
            {
                throw std::runtime_error("Unsupported asset-worker test configuration: " + configuration);
            }

            m_StagingRoot = std::filesystem::temp_directory_path() /
                            ("Keire-AssetWorker-Runtime-" + Keire::AssetId::Generate().ToString());
            try
            {
                std::filesystem::create_directory(m_StagingRoot);
                CopyRegularFile(builtWorker, m_StagingRoot / builtWorker.filename());
                constexpr std::array RuntimeFiles{"avformat-63.dll", "avcodec-63.dll", "swresample-7.dll",
                                                  "avutil-61.dll"};
                const auto runtimeDirectory = repositoryRoot / "Build/Dependencies/ffmpeg" /
                                              (releaseRuntime ? "Release" : "Debug") / "install/bin";
                for (const auto* runtime : RuntimeFiles)
                    CopyRegularFile(runtimeDirectory / runtime, m_StagingRoot / runtime);
                m_Executable = m_StagingRoot / builtWorker.filename();
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove_all(m_StagingRoot, ignored);
                throw;
            }
#else
            m_Executable = builtWorker;
#endif
        }

        ~AssetWorkerTestRuntime()
        {
            if (m_StagingRoot.empty())
                return;
            std::error_code ignored;
            std::filesystem::remove_all(m_StagingRoot, ignored);
        }

        AssetWorkerTestRuntime(const AssetWorkerTestRuntime&) = delete;
        AssetWorkerTestRuntime& operator=(const AssetWorkerTestRuntime&) = delete;

        [[nodiscard]] const std::filesystem::path& Executable() const noexcept { return m_Executable; }

      private:
        static void CopyRegularFile(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(source)))
                throw std::runtime_error("Required asset-worker test runtime file is missing: " + source.string());
            std::filesystem::copy_file(source, destination);
        }

        std::filesystem::path m_StagingRoot;
        std::filesystem::path m_Executable;
    };

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
    const auto location = std::filesystem::temp_directory_path() / std::filesystem::path(u8"Kéire-èµ„äº§-Worker-Test");
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    const auto builtWorker =
        KeireEditor::AssetOperationService::ResolveWorkerExecutable(KeireEditorTests::ExecutablePath);
    REQUIRE(std::filesystem::is_regular_file(builtWorker));
    const AssetWorkerTestRuntime worker(builtWorker);
    REQUIRE(std::filesystem::is_regular_file(worker.Executable()));
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
        KeireEditor::AssetOperationService operations(worker.Executable(), project->Root());
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
        auto bakeScene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                        Keire::SceneAsset::EmptyDefinition("Worker Created"));
        auto receiver = bakeScene->CreateEntity("Static Receiver").AddComponent<Keire::MeshRendererComponent>();
        receiver->SetStaticLighting(true);
        receiver->SetGIReceive(Keire::GIReceiveMode::Lightmaps);
        auto definition = bakeScene->Snapshot();
        definition.Lighting.LightmapResolution = 64;
        definition.Lighting.MaximumLightmapResolution = 64;
        definition.Lighting.SamplesPerTexel = 1;
        bakeScene->Close();
        operations.QueueCreateAssetWithAuxiliary(
            "Scenes/WorkerCreated.keirescene", Keire::SceneAsset::Encode(definition), {},
            {.UndoName = "Create Test Asset"},
            {{"Shaders/WorkerAuxiliary.hlsl", std::vector<std::byte>(auxiliaryBytes.begin(), auxiliaryBytes.end())}});
        CHECK(operations.ActivityLabel() == "Create Test Asset");
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

        operations.QueueLightingBake(created->Result.CreatedAsset, true,
                                     {.Reason = "isolated-worker-lighting-publication-test"});
        const auto bakeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < bakeDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto baked = operations.TakeCompletion();
        REQUIRE(baked);
        INFO(baked->Result.Diagnostic);
        CHECK(baked->Result.Success);
        CHECK(baked->Kind == Keire::Detail::AssetWorkerOperationKind::BakeLighting);
        REQUIRE(baked->Result.CreatedAsset);
        CHECK(std::ranges::find(baked->Result.MutatedAssets, created->Result.CreatedAsset) !=
              baked->Result.MutatedAssets.end());
        CHECK(std::ranges::find(baked->Result.MutatedAssets, baked->Result.CreatedAsset) !=
              baked->Result.MutatedAssets.end());
        const auto bakedCatalog = Keire::Detail::LoadCatalog(baked->Result.Import.CatalogPath);
        CHECK(baked->Result.MutatedAssets.size() == 4);
        for (const auto asset : baked->Result.MutatedAssets)
            CHECK(std::ranges::find(bakedCatalog.Entries, asset, &Keire::Detail::CatalogEntry::Id) !=
                  bakedCatalog.Entries.end());
        CHECK(std::ranges::find(bakedCatalog.Entries, baked->Result.CreatedAsset, &Keire::Detail::CatalogEntry::Id) !=
              bakedCatalog.Entries.end());

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

        const auto finishMutation = [&operations]
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
            {
                operations.Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            operations.Update();
            CHECK_FALSE(operations.Busy());
            return operations.TakeCompletion();
        };
        const Keire::Detail::AssetWorkerMutation duplicateMutation{
            .Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateAsset,
            .Asset = created->Result.CreatedAsset,
            .Destination = "Scenes/InspectorCopy.keirescene"};
        operations.QueueMutation(duplicateMutation, {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal,
                                                     .UndoName = "Duplicate Asset"});
        const auto duplicated = finishMutation();
        REQUIRE(duplicated);
        INFO(duplicated->Result.Diagnostic);
        REQUIRE(duplicated->Result.Success);
        REQUIRE(duplicated->Result.MutatedAssets.size() == 1);
        REQUIRE(duplicated->Context.MutationUndo);
        CHECK(duplicated->Context.MutationUndo->Name == "Duplicate Asset");
        CHECK(duplicated->Context.MutationUndo->RecordCommand);
        CHECK(duplicated->Context.MutationUndo->RevealResult);
        CHECK(duplicated->Context.MutationUndo->Forward.Asset == created->Result.CreatedAsset);
        CHECK(duplicated->Context.MutationUndo->Forward.Destination == duplicateMutation.Destination);
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/InspectorCopy.keirescene"));

        operations.QueueMutation(duplicateMutation, {.UndoName = "Duplicate Asset"});
        const auto failedDuplicate = finishMutation();
        REQUIRE(failedDuplicate);
        CHECK_FALSE(failedDuplicate->Result.Success);
        REQUIRE(failedDuplicate->Context.MutationUndo);
        CHECK(failedDuplicate->Context.MutationUndo->RecordCommand);
        CHECK(failedDuplicate->Context.MutationUndo->Name == "Duplicate Asset");
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/InspectorCopy.keirescene"));

        CHECK_THROWS_AS(operations.QueueMutation({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                                  .Asset = created->Result.CreatedAsset,
                                                  .Destination = "Scenes/UntrackedMove.keirescene"},
                                                 {.UndoName = "Move Asset"}),
                        std::invalid_argument);
        CHECK_THROWS_AS(
            operations.QueueMutation(duplicateMutation, {.UndoName = "Duplicate Asset",
                                                         .MutationPhase = KeireEditor::AssetMutationPhase::Undo}),
            std::invalid_argument);
        CHECK_FALSE(operations.Busy());
        CHECK_FALSE(operations.TakeCompletion());
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/UntrackedMove.keirescene"));

        operations.QueueMutation(
            {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = created->Result.CreatedAsset},
            {.UndoName = "Trash Asset"});
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
        REQUIRE(trashed->Context.MutationUndo);
        CHECK(trashed->Context.MutationUndo->Name == "Trash Asset");
        CHECK(trashed->Context.MutationUndo->RecordCommand);
        CHECK_FALSE(trashed->Context.MutationUndo->RevealResult);
        CHECK(trashed->Context.MutationUndo->Forward.Asset == created->Result.CreatedAsset);
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));

        auto undoState = trashed->Context.MutationUndo;
        undoState->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash,
                              .Trash = trashed->Result.Trash};
        undoState->RecordCommand = false;
        operations.QueueMutation(undoState->Reverse, {.UndoName = "Trash Asset",
                                                      .MutationUndo = undoState,
                                                      .MutationPhase = KeireEditor::AssetMutationPhase::Undo});
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
        CHECK(restored->Context.MutationUndo == undoState);
        CHECK(restored->Context.MutationPhase == KeireEditor::AssetMutationPhase::Undo);
        CHECK_FALSE(restored->Context.MutationUndo->RecordCommand);
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
        CHECK_THROWS_AS(KeireEditor::AssetOperationService(KeireEditorTests::ExecutablePath, project->Root(), {}),
                        std::invalid_argument);
        std::size_t clockSample = 0;
        const std::array clockMilliseconds{0, 125, 375};
        KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root(),
                                                      [&]
                                                      {
                                                          return std::chrono::steady_clock::time_point{} +
                                                                 std::chrono::milliseconds(
                                                                     clockMilliseconds.at(clockSample++));
                                                      });
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
        CHECK_FALSE(completion->OperationId.empty());
        CHECK(completion->QueueMilliseconds == 125.0);
        CHECK(completion->ExecutionMilliseconds == 250.0);
        CHECK(clockSample == 3);
        CHECK_FALSE(operations.TakeCompletion());
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

    SetTestWorkerMode("hang");
    {
        std::size_t clockSample = 0;
        const std::array clockMilliseconds{0, 80, 200};
        KeireEditor::AssetOperationService interrupted(KeireEditorTests::ExecutablePath, project->Root(),
                                                       [&]
                                                       {
                                                           return std::chrono::steady_clock::time_point{} +
                                                                  std::chrono::milliseconds(
                                                                      clockMilliseconds.at(clockSample++));
                                                       });
        interrupted.QueueAssetImport(first, KeireEditor::AssetOperationPriority::MaterialRefresh,
                                     {.ReloadAsset = first, .Generation = 1});
        interrupted.QueueAssetImport(second, KeireEditor::AssetOperationPriority::MaterialRefresh,
                                     {.ReloadAsset = second, .Generation = 2});
        CHECK(interrupted.QueuedCount() == 1);
        CHECK(clockSample == 1);
        interrupted.Update();
        SetTestWorkerMode(nullptr);
        REQUIRE(interrupted.PreemptBackgroundImports());
        CHECK_FALSE(interrupted.Busy());
        const auto completion = interrupted.TakeCompletion();
        REQUIRE(completion);
        CHECK(completion->Result.Cancelled);
        CHECK_FALSE(completion->Result.Success);
        CHECK(completion->Context.Generation == 2);
        CHECK_FALSE(completion->Context.ReloadAsset);
        CHECK_FALSE(completion->OperationId.empty());
        CHECK(completion->QueueMilliseconds == 80.0);
        CHECK(completion->ExecutionMilliseconds == 120.0);
        CHECK(clockSample == 3);
        CHECK_FALSE(interrupted.TakeCompletion());
        CHECK_FALSE(interrupted.PreemptBackgroundImports());
    }
    SetTestWorkerMode(nullptr);
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Asset worker timing benchmark" * doctest::skip())
{
    const auto location = std::filesystem::absolute(std::filesystem::path("Temp") /
                                                    ("AssetTiming-" + Keire::AssetId::Generate().ToString()));
    struct Cleanup
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{location};
    std::filesystem::create_directories(location);
    auto project =
        Keire::Project::Create({.Location = location, .Name = "Timing", .Template = Keire::ProjectTemplate::Empty});
    const AssetWorkerTestRuntime worker(
        KeireEditor::AssetOperationService::ResolveWorkerExecutable(KeireEditorTests::ExecutablePath));
    for (int i = 0; i < 200; ++i)
        Keire::Detail::WriteTextFileAtomically(project->Root() / "Assets" / ("Seed" + std::to_string(i) + ".cs"),
                                               "// asset benchmark source\n" + std::string(2048, ' '));
    KeireEditor::AssetOperationService operations(worker.Executable(), project->Root());
    const auto finish = [&]
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        operations.Update();
        const auto result = operations.TakeCompletion();
        REQUIRE(result);
        INFO(result->Result.Diagnostic);
        REQUIRE(result->Result.Success);
    };
    operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
    finish();
    const auto bytes = Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Created"));
    std::vector<double> creation;
    std::vector<double> importing;
    for (int sample = 0; sample < 5; ++sample)
    {
        const auto start = std::chrono::steady_clock::now();
        operations.QueueCreateAsset("Created" + std::to_string(sample) + ".keirescene", bytes, {}, {});
        finish();
        creation.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        std::vector<Keire::ExternalAssetImportItem> items;
        for (int item = 0; item < 20; ++item)
        {
            const auto name = "External" + std::to_string(sample) + "_" + std::to_string(item) + ".cs";
            const auto source = location / name;
            Keire::Detail::WriteTextFileAtomically(source, "// imported source\n" + std::string(2048, ' '));
            items.push_back({.SourcePath = source, .RelativeDestination = name});
        }
        const auto importStart = std::chrono::steady_clock::now();
        operations.QueueExternalImport(std::move(items), {});
        finish();
        importing.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - importStart).count());
    }
    std::ranges::sort(creation);
    std::ranges::sort(importing);
    std::cout << "ASSET_TIMING creation_median_ms=" << creation[2] << " import20_median_ms=" << importing[2] << '\n';
    operations.Shutdown();
}
