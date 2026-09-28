#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <cstdint>

namespace KeireEditor
{
    enum class PlayModeReadiness : std::uint8_t
    {
        Ready,
        WaitingForManagedRuntime,
        ManagedRuntimeUnavailable
    };

    [[nodiscard]] constexpr PlayModeReadiness
    EvaluatePlayModeReadiness(const bool requiresManagedRuntime, const bool runtimeHostAvailable,
                              const Keire::ManagedBuildState buildState, const Keire::ManagedReloadState reloadState,
                              const bool latestBuildReloadRequested = true) noexcept
    {
        if (!requiresManagedRuntime)
            return PlayModeReadiness::Ready;
        if (buildState == Keire::ManagedBuildState::Generating || buildState == Keire::ManagedBuildState::Compiling ||
            buildState == Keire::ManagedBuildState::Publishing)
            return PlayModeReadiness::WaitingForManagedRuntime;
        if (buildState == Keire::ManagedBuildState::Succeeded && !latestBuildReloadRequested)
            return PlayModeReadiness::WaitingForManagedRuntime;
        if (reloadState == Keire::ManagedReloadState::Active)
            return PlayModeReadiness::Ready;
        if (!runtimeHostAvailable || buildState == Keire::ManagedBuildState::Failed ||
            buildState == Keire::ManagedBuildState::Cancelled || reloadState == Keire::ManagedReloadState::Failed ||
            reloadState == Keire::ManagedReloadState::Cancelled)
        {
            return PlayModeReadiness::ManagedRuntimeUnavailable;
        }
        return PlayModeReadiness::WaitingForManagedRuntime;
    }
} // namespace KeireEditor
