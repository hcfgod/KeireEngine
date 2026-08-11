#pragma once

#include "Keire/Replay/ReplaySystem.h"

#include <string_view>

namespace KeireEditor::Detail
{
    [[nodiscard]] constexpr std::string_view ReplayStateName(const Keire::ReplaySessionState value) noexcept
    {
        switch (value)
        {
        case Keire::ReplaySessionState::Idle:
            return "Idle";
        case Keire::ReplaySessionState::Recording:
            return "Recording";
        case Keire::ReplaySessionState::Playing:
            return "Playing";
        case Keire::ReplaySessionState::Verifying:
            return "Verifying";
        case Keire::ReplaySessionState::Paused:
            return "Paused";
        case Keire::ReplaySessionState::Completed:
            return "Completed";
        case Keire::ReplaySessionState::Diverged:
            return "Diverged";
        case Keire::ReplaySessionState::Failed:
            return "Failed";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr bool CanToggleReplayPause(const Keire::ReplaySessionState state) noexcept
    {
        return state == Keire::ReplaySessionState::Playing || state == Keire::ReplaySessionState::Verifying ||
               state == Keire::ReplaySessionState::Paused;
    }

    [[nodiscard]] constexpr bool CanStepReplay(const Keire::ReplaySessionState state) noexcept
    {
        return state == Keire::ReplaySessionState::Paused;
    }

    [[nodiscard]] constexpr bool CanSeekReplay(const Keire::ReplaySessionState state) noexcept
    {
        return state == Keire::ReplaySessionState::Playing || state == Keire::ReplaySessionState::Verifying ||
               state == Keire::ReplaySessionState::Paused;
    }
} // namespace KeireEditor::Detail
