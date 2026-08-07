#include "TestSupport.h"

#include "KeireHubRuntime/BuildSupportOperationStore.h"

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <string>

using namespace KeireHub;

namespace
{
    [[nodiscard]] std::string OperationId(const unsigned long long value)
    {
        char encoded[37]{};
        std::snprintf(encoded, sizeof(encoded), "00000000-0000-0000-0000-%012llx", value);
        return encoded;
    }

    [[nodiscard]] BuildSupportOperationRecord ImportRecord(const std::filesystem::path& root,
                                                           const unsigned long long identity = 1)
    {
        const auto id = OperationId(identity);
        const auto editorRoot = root.parent_path() / "Editors" / "2.0.0";
        const auto operationRoot = root / id;
        return {.Id = id,
                .Kind = BuildSupportOperationKind::Import,
                .State = BuildSupportOperationState::Launching,
                .TargetInstallationId = "editor-stable-2",
                .EngineVersion = "2.0.0",
                .EditorRoot = editorRoot,
                .AssetToolEntrypoint = editorRoot / "bin" / "KeireAssetTool",
                .OperationRoot = operationRoot,
                .StatusPath = operationRoot / "status.json",
                .CancelPath = operationRoot / "cancel",
                .CurrentPackage = "windows.keireplayersupport",
                .Phase = "Starting",
                .Message = "Starting Build Support import.",
                .CreatedUnixSeconds = identity + 10,
                .UpdatedUnixSeconds = identity + 10};
    }

    [[nodiscard]] BuildSupportOperationRecord RemovalRecord(const std::filesystem::path& root,
                                                            const unsigned long long identity = 2)
    {
        auto record = ImportRecord(root, identity);
        record.Kind = BuildSupportOperationKind::Remove;
        record.StatusPath.clear();
        record.CancelPath.clear();
        record.ComponentId = "linux-x86_64";
        record.CurrentPackage = record.ComponentId;
        record.Phase = "Installing";
        record.Message = "Removing Build Support.";
        return record;
    }
} // namespace

TEST_CASE("Build Support operation journal survives restart and preserves cancellation ownership")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "BuildSupportOperations";
    BuildSupportOperationStore store(root);
    auto record = ImportRecord(root);
    REQUIRE(store.Add(record));
    CHECK(IsActive(store.Snapshot()->front().State));
    CHECK(BuildSupportTaskId(record.Id) == "build-support-" + record.Id);

    REQUIRE(store.AttachProcess(record.Id, 4242, 12));
    REQUIRE(store.Update(record.Id, BuildSupportOperationState::Cancelling, "Cancelling",
                         "Cancelling the Build Support operation.", 0.4F, 13));

    BuildSupportOperationStore reloaded(root);
    REQUIRE(reloaded.Load());
    REQUIRE(reloaded.Snapshot()->size() == 1);
    const auto& recovered = reloaded.Snapshot()->front();
    CHECK(recovered.State == BuildSupportOperationState::Cancelling);
    REQUIRE(recovered.ChildProcessId);
    CHECK(*recovered.ChildProcessId == 4242);
    CHECK(recovered.CancelPath == recovered.OperationRoot / "cancel");
    CHECK(recovered.AssetToolEntrypoint.lexically_relative(recovered.EditorRoot).generic_string().starts_with("bin"));

    REQUIRE(reloaded.Finish(record.Id, BuildSupportOperationState::Cancelled, "Cancelled",
                            "Build Support operation cancelled.", 0.4F, 14));
    CHECK(IsTerminal(reloaded.Snapshot()->front().State));
    CHECK_FALSE(reloaded.Snapshot()->front().ChildProcessId.has_value());
}

TEST_CASE("Build Support operation journal fails atomically on unsafe identity and persistence errors")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "BuildSupportOperations";
    BuildSupportOperationStore store(root);
    auto unsafe = ImportRecord(root);
    unsafe.OperationRoot = temporary.Path() / "outside";
    unsafe.StatusPath = unsafe.OperationRoot / "status.json";
    unsafe.CancelPath = unsafe.OperationRoot / "cancel";
    const auto rejected = store.Add(unsafe);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(store.Snapshot()->empty());

    std::filesystem::create_directories(store.Path());
    const auto unwritable = store.Add(ImportRecord(root, 3));
    REQUIRE_FALSE(unwritable);
    CHECK(store.Snapshot()->empty());
}

TEST_CASE("Build Support operation journal rejects malformed persisted paths and multiple active tasks")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "BuildSupportOperations";
    BuildSupportOperationStore store(root);
    REQUIRE(store.Add(ImportRecord(root)));
    const auto duplicateActive = store.Add(RemovalRecord(root));
    REQUIRE_FALSE(duplicateActive);
    CHECK(duplicateActive.Error().Code == HubErrorCode::InstallationBusy);

    auto document = KeireHubTests::ReadText(store.Path());
    const auto confined = (root / OperationId(1) / "status.json").generic_string();
    const auto escaped = (temporary.Path() / "escaped-status.json").generic_string();
    const auto position = document.find(confined);
    REQUIRE(position != std::string::npos);
    document.replace(position, confined.size(), escaped);
    KeireHubTests::WriteText(store.Path(), document);

    BuildSupportOperationStore malformed(root);
    const auto loaded = malformed.Load();
    REQUIRE_FALSE(loaded);
    CHECK(loaded.Error().Code == HubErrorCode::InvalidData);
    CHECK(malformed.Snapshot()->empty());
}

TEST_CASE("Build Support operation history is bounded newest first")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "BuildSupportOperations";
    BuildSupportOperationStore store(root);
    constexpr unsigned long long count = BuildSupportOperationStore::MaximumTerminalHistory + 7;
    for (unsigned long long index = 1; index <= count; ++index)
    {
        auto record = ImportRecord(root, index);
        REQUIRE(store.Add(record));
        REQUIRE(store.AttachProcess(record.Id, index + 100, index + 20));
        REQUIRE(store.Finish(record.Id, BuildSupportOperationState::Completed, "Completed",
                             "Build Support import completed.", 1.0F, index + 30));
    }
    REQUIRE(store.Snapshot()->size() == BuildSupportOperationStore::MaximumTerminalHistory);
    CHECK(store.Snapshot()->front().Id == OperationId(count));
    CHECK(store.Snapshot()->back().Id == OperationId(count - BuildSupportOperationStore::MaximumTerminalHistory + 1));
}

TEST_CASE("Build Support removal recovery validates and bounds schema-one root journals")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto storage = temporary.Path() / "BuildSupport";
    REQUIRE_FALSE(HasPendingBuildSupportRemovalJournal(storage, "2.0.0", "linux-x86_64").Value());

    const auto versionRoot = storage / "2.0.0";
    const auto operation = OperationId(50);
    const auto tombstone = ".remove-" + operation;
    KeireHubTests::WriteText(versionRoot / (tombstone + ".json"), "{\"schemaVersion\":1,\"engineVersion\":\"2.0.0\","
                                                                  "\"packId\":\"linux-x86_64\",\"tombstone\":\"" +
                                                                      tombstone + "\"}\n");
    auto pending = HasPendingBuildSupportRemovalJournal(storage, "2.0.0", "linux-x86_64");
    REQUIRE(pending);
    CHECK(pending.Value());
    auto unrelated = HasPendingBuildSupportRemovalJournal(storage, "2.0.0", "windows-x86_64");
    REQUIRE(unrelated);
    CHECK_FALSE(unrelated.Value());

    KeireHubTests::WriteText(versionRoot / (tombstone + ".json"), "{\"schemaVersion\":2}\n");
    const auto malformed = HasPendingBuildSupportRemovalJournal(storage, "2.0.0", "linux-x86_64");
    REQUIRE_FALSE(malformed);
    CHECK(malformed.Error().Code == HubErrorCode::IoRead);
}

TEST_CASE("Build Support removal recovery requires a fresh inventory revision after an in-flight scan")
{
    BuildSupportRemovalInventoryGate inFlight{.BaselineRevision = 10, .RefreshAfterCurrentLoad = true};
    CHECK(EvaluateBuildSupportRemovalInventory(inFlight, 10, true) == BuildSupportRemovalInventoryAction::Wait);
    CHECK(EvaluateBuildSupportRemovalInventory(inFlight, 11, false) ==
          BuildSupportRemovalInventoryAction::StartFreshRefresh);

    BuildSupportRemovalInventoryGate fresh{.BaselineRevision = 11, .RefreshAfterCurrentLoad = false};
    CHECK(EvaluateBuildSupportRemovalInventory(fresh, 12, true) == BuildSupportRemovalInventoryAction::Wait);
    CHECK(EvaluateBuildSupportRemovalInventory(fresh, 13, false) == BuildSupportRemovalInventoryAction::Reconcile);
}
