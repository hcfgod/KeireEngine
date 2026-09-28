#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <optional>

namespace KeireEditor
{
    // A valid action stays valid while the editor owns focus. Only its sampled input is suppressed.
    [[nodiscard]] inline std::optional<Keire::ManagedInputActionSnapshot>
    RouteGameViewportAction(std::optional<Keire::ManagedInputActionSnapshot> snapshot, const bool inputActive) noexcept
    {
        if (snapshot && !inputActive)
        {
            snapshot->Value.X = 0.0F;
            snapshot->Value.Y = 0.0F;
            snapshot->Phase = snapshot->Enabled ? Keire::InputActionPhase::Waiting : Keire::InputActionPhase::Disabled;
            snapshot->Started = false;
            snapshot->Performed = false;
            snapshot->Canceled = false;
        }
        return snapshot;
    }

    [[nodiscard]] constexpr bool GameViewportOwnsRuntimeInput(const bool playActive, const bool applicationFocused,
                                                              const bool panelFocused, const bool captureRequested,
                                                              const bool captureSuspended,
                                                              const bool playReviewActive = false) noexcept
    {
        if (!playActive || !applicationFocused || captureSuspended || playReviewActive)
            return false;
        if (captureRequested)
            return true;
        // Requesting focus when Play starts must be enough to route keyboard/gamepad input even when the pointer is
        // still over another panel. A click on the viewport focuses its panel and restores ownership after Escape.
        return panelFocused;
    }
} // namespace KeireEditor
