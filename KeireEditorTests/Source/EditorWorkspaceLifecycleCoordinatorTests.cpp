#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    constexpr std::array ExpectedUpdateTrace{
        KeireEditor::EditorWorkspaceUpdatePhase::CaptureConsole,
        KeireEditor::EditorWorkspaceUpdatePhase::SceneTransitions,
        KeireEditor::EditorWorkspaceUpdatePhase::SmokeAutomation,
        KeireEditor::EditorWorkspaceUpdatePhase::PlayRuntime,
        KeireEditor::EditorWorkspaceUpdatePhase::EditModePreview,
        KeireEditor::EditorWorkspaceUpdatePhase::PendingFileDialogs,
        KeireEditor::EditorWorkspaceUpdatePhase::ManagedRuntime,
        KeireEditor::EditorWorkspaceUpdatePhase::BuildAndCook,
        KeireEditor::EditorWorkspaceUpdatePhase::AssetOperations,
        KeireEditor::EditorWorkspaceUpdatePhase::QueuedAssetMutations,
        KeireEditor::EditorWorkspaceUpdatePhase::QueuedPrefabCreations,
        KeireEditor::EditorWorkspaceUpdatePhase::DocumentMaintenance,
        KeireEditor::EditorWorkspaceUpdatePhase::SceneLoad,
        KeireEditor::EditorWorkspaceUpdatePhase::SceneRecovery,
        KeireEditor::EditorWorkspaceUpdatePhase::AssetAdmission,
        KeireEditor::EditorWorkspaceUpdatePhase::AssetHotReload,
    };

    constexpr std::array ExpectedShutdownTrace{
        KeireEditor::EditorWorkspaceShutdownPhase::DiagnosticBundle,
        KeireEditor::EditorWorkspaceShutdownPhase::SessionPreferences,
        KeireEditor::EditorWorkspaceShutdownPhase::BuildAndCook,
        KeireEditor::EditorWorkspaceShutdownPhase::Packages,
        KeireEditor::EditorWorkspaceShutdownPhase::AssetOperations,
        KeireEditor::EditorWorkspaceShutdownPhase::ManagedRuntime,
        KeireEditor::EditorWorkspaceShutdownPhase::MaterialDraft,
        KeireEditor::EditorWorkspaceShutdownPhase::MaterialCatalog,
        KeireEditor::EditorWorkspaceShutdownPhase::SceneViewport,
        KeireEditor::EditorWorkspaceShutdownPhase::ProjectSettings,
        KeireEditor::EditorWorkspaceShutdownPhase::Input,
        KeireEditor::EditorWorkspaceShutdownPhase::PlayMode,
        KeireEditor::EditorWorkspaceShutdownPhase::TransientPanels,
        KeireEditor::EditorWorkspaceShutdownPhase::Undo,
        KeireEditor::EditorWorkspaceShutdownPhase::SceneRecovery,
        KeireEditor::EditorWorkspaceShutdownPhase::Documents,
        KeireEditor::EditorWorkspaceShutdownPhase::AssetPackage,
        KeireEditor::EditorWorkspaceShutdownPhase::AssetBrowser,
    };
} // namespace

TEST_CASE("Editor workspace update sequence preserves the characterized phase trace")
{
    KeireEditor::EditorWorkspaceLifecycleCoordinator lifecycle;
    std::vector<KeireEditor::EditorWorkspaceUpdatePhase> invoked;
    lifecycle.Update(
        [&](const auto phase)
        {
            invoked.push_back(phase);
            return KeireEditor::EditorWorkspaceUpdateDisposition::Continue;
        });

    CHECK(std::ranges::equal(invoked, ExpectedUpdateTrace));
    CHECK(std::ranges::equal(lifecycle.LastUpdateTrace(), ExpectedUpdateTrace));
    CHECK(std::ranges::equal(KeireEditor::EditorWorkspaceLifecycleCoordinator::UpdatePhases(), ExpectedUpdateTrace));
    CHECK(KeireEditor::ToString(ExpectedUpdateTrace.front()) == std::string_view("capture-console"));
    CHECK(KeireEditor::ToString(ExpectedUpdateTrace.back()) == std::string_view("asset-hot-reload"));
}

TEST_CASE("Editor workspace update trace records exact stop and failure prefixes")
{
    using Phase = KeireEditor::EditorWorkspaceUpdatePhase;
    using Disposition = KeireEditor::EditorWorkspaceUpdateDisposition;

    KeireEditor::EditorWorkspaceLifecycleCoordinator lifecycle;
    lifecycle.Update([](const Phase phase)
                     { return phase == Phase::BuildAndCook ? Disposition::Stop : Disposition::Continue; });
    CHECK(lifecycle.LastUpdateTrace().size() == 8);
    CHECK(lifecycle.LastUpdateTrace().back() == Phase::BuildAndCook);
    CHECK(std::ranges::equal(lifecycle.LastUpdateTrace(), ExpectedUpdateTrace | std::views::take(8)));

    CHECK_THROWS_WITH_AS(lifecycle.Update(
                             [](const Phase phase)
                             {
                                 if (phase == Phase::PlayRuntime)
                                     throw std::runtime_error("play update failed");
                                 return Disposition::Continue;
                             }),
                         "play update failed", std::runtime_error);
    CHECK(lifecycle.LastUpdateTrace().size() == 4);
    CHECK(lifecycle.LastUpdateTrace().back() == Phase::PlayRuntime);
    CHECK(std::ranges::equal(lifecycle.LastUpdateTrace(), ExpectedUpdateTrace | std::views::take(4)));
}

TEST_CASE("Editor workspace shutdown is ordered failure-resilient and idempotent")
{
    using Phase = KeireEditor::EditorWorkspaceShutdownPhase;

    KeireEditor::EditorWorkspaceLifecycleCoordinator lifecycle;
    const auto callback = lifecycle.CaptureCallbackToken();
    REQUIRE(callback.Current());

    std::vector<Phase> invoked;
    std::vector<Phase> reported;
    lifecycle.Shutdown(
        [&](const Phase phase)
        {
            invoked.push_back(phase);
            if (phase == Phase::Packages)
                throw std::runtime_error("package shutdown failed");
            if (phase == Phase::Undo)
                throw std::logic_error("undo shutdown failed");
        },
        [&](const Phase phase, const std::exception_ptr&) { reported.push_back(phase); });

    CHECK(std::ranges::equal(invoked, ExpectedShutdownTrace));
    CHECK(std::ranges::equal(lifecycle.ShutdownTrace(), ExpectedShutdownTrace));
    CHECK(
        std::ranges::equal(KeireEditor::EditorWorkspaceLifecycleCoordinator::ShutdownPhases(), ExpectedShutdownTrace));
    CHECK(reported == std::vector<Phase>{Phase::Packages, Phase::Undo});
    CHECK(lifecycle.ShutdownComplete());
    CHECK_FALSE(callback.Current());
    REQUIRE(lifecycle.FirstShutdownFailure());
    CHECK_THROWS_WITH_AS(std::rethrow_exception(lifecycle.FirstShutdownFailure()), "package shutdown failed",
                         std::runtime_error);

    lifecycle.Shutdown([&](const Phase phase) { invoked.push_back(phase); });
    CHECK(invoked.size() == ExpectedShutdownTrace.size());
}

TEST_CASE("Editor workspace callback generations reject late and destroyed-owner callbacks")
{
    KeireEditor::EditorWorkspaceCallbackToken destroyedOwner;
    {
        KeireEditor::EditorWorkspaceLifecycleCoordinator lifecycle;
        destroyedOwner = lifecycle.CaptureCallbackToken();
        CHECK(destroyedOwner.Current());
    }
    CHECK_FALSE(destroyedOwner.Current());
}

TEST_CASE("Editor workspace lifecycle rejects off-owner work without mutating the healthy sequence")
{
    using Disposition = KeireEditor::EditorWorkspaceUpdateDisposition;

    KeireEditor::EditorWorkspaceLifecycleCoordinator lifecycle;
    std::atomic_bool updateRejected = false;
    std::atomic_bool actionInvoked = false;
    std::atomic_uint queryRejections = 0;
    std::thread updateThread(
        [&]
        {
            try
            {
                lifecycle.Update(
                    [&](const auto)
                    {
                        actionInvoked = true;
                        return Disposition::Continue;
                    });
            }
            catch (const std::logic_error&)
            {
                updateRejected = true;
            }
            try
            {
                static_cast<void>(lifecycle.LastUpdateTrace());
            }
            catch (const std::logic_error&)
            {
                ++queryRejections;
            }
            try
            {
                static_cast<void>(lifecycle.CaptureCallbackToken());
            }
            catch (const std::logic_error&)
            {
                ++queryRejections;
            }
        });
    updateThread.join();
    CHECK(updateRejected);
    CHECK_FALSE(actionInvoked);
    CHECK(queryRejections == 2);
    CHECK(lifecycle.LastUpdateTrace().empty());

    std::atomic_bool shutdownInvoked = false;
    std::thread shutdownThread(
        [&]
        {
            lifecycle.Shutdown([&](const auto) { shutdownInvoked = true; });
            try
            {
                static_cast<void>(lifecycle.ShutdownComplete());
            }
            catch (const std::logic_error&)
            {
                ++queryRejections;
            }
        });
    shutdownThread.join();
    CHECK_FALSE(shutdownInvoked);
    CHECK(queryRejections == 3);
    CHECK(lifecycle.ShutdownOwnerViolation());
    CHECK_FALSE(lifecycle.ShutdownComplete());

    lifecycle.Shutdown([](const auto) {});
    CHECK(lifecycle.ShutdownComplete());
    CHECK_FALSE(lifecycle.CaptureCallbackToken().Current());
    CHECK_THROWS_AS(lifecycle.Update([](const auto) { return Disposition::Continue; }), std::logic_error);
}
