#include "KeireClient/Editor/ReplayPanelState.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Replay dashboard exposes controls only in valid session states")
{
    using Keire::ReplaySessionState;
    using namespace KeireEditor::Detail;

    constexpr std::array activeStates{ReplaySessionState::Playing, ReplaySessionState::Verifying,
                                      ReplaySessionState::Paused};
    for (const auto state : activeStates)
    {
        CHECK(CanToggleReplayPause(state));
        CHECK(CanSeekReplay(state));
    }

    constexpr std::array inactiveStates{ReplaySessionState::Idle, ReplaySessionState::Recording,
                                        ReplaySessionState::Completed, ReplaySessionState::Diverged,
                                        ReplaySessionState::Failed};
    for (const auto state : inactiveStates)
    {
        CHECK_FALSE(CanToggleReplayPause(state));
        CHECK_FALSE(CanSeekReplay(state));
        CHECK_FALSE(CanStepReplay(state));
    }

    CHECK(CanStepReplay(ReplaySessionState::Paused));
    CHECK_FALSE(CanStepReplay(ReplaySessionState::Playing));
    CHECK_FALSE(CanStepReplay(ReplaySessionState::Verifying));
}
