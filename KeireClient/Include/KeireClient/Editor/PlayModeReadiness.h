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
                              const Keire::ManagedBuildState buildState,
                              const Keire::ManagedReloadState reloadState) noexcept
    {
        if (!requiresManagedRuntime || reloadState == Keire::ManagedReloadState::Active)
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
