#include "KeireClient/Editor/SceneTransitionCoordinator.h"

#include <doctest/doctest.h>

#include <initializer_list>

TEST_CASE("Deferred startup scene is consumed once when no user transition intervenes")
{
    KeireEditor::SceneTransitionCoordinator transitions;
    constexpr Keire::AssetId startup{1U, 2U};
    transitions.DeferStartupScene(startup);
    CHECK(transitions.PendingStartupScene() == startup);
    CHECK(transitions.TakeStartupScene() == startup);
    CHECK_FALSE(transitions.TakeStartupScene());
    CHECK(transitions.Request({KeireEditor::SceneTransitionKind::Open, startup}));
    const auto request = transitions.BeginCommit();
    REQUIRE(request);
    CHECK(request->Asset == startup);
}

TEST_CASE("User scene transitions supersede startup even after cancellation or failure")
{
    using enum KeireEditor::SceneTransitionKind;
    for (const auto kind : {Open, Create, Close, Exit})
    {
        for (const bool fail : {false, true})
        {
            KeireEditor::SceneTransitionCoordinator transitions;
            transitions.DeferStartupScene({1U, 2U});
            REQUIRE(transitions.Request({kind, {3U, 4U}}));
            CHECK_FALSE(transitions.PendingStartupScene());
            CHECK_FALSE(transitions.TakeStartupScene());
            transitions.DeferStartupScene({5U, 6U});
            CHECK_FALSE(transitions.PendingStartupScene());
            if (fail)
            {
                REQUIRE(transitions.BeginCommit());
                transitions.DeferStartupScene({5U, 6U});
                transitions.Fail("Scene could not be loaded.");
            }
            else
            {
                transitions.Cancel();
            }
            CHECK_FALSE(transitions.TakeStartupScene());
        }
    }
}

TEST_CASE("Completed explicit scene open cannot be replaced by later import completion")
{
    KeireEditor::SceneTransitionCoordinator transitions;
    transitions.DeferStartupScene({1U, 2U});
    REQUIRE(transitions.Request({KeireEditor::SceneTransitionKind::Open, {3U, 4U}}));
    REQUIRE(transitions.BeginCommit());
    transitions.Complete();
    CHECK_FALSE(transitions.TakeStartupScene());
    CHECK_FALSE(transitions.Pending());
}
