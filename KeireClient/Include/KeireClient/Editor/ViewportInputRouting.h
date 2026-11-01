#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <cstdint>
#include <optional>

namespace KeireEditor
{
    class EditorTextInputGate final
    {
      public:
        void Observe(const std::uint64_t frame, const bool textEditing) noexcept
        {
            if (!m_Frame || *m_Frame != frame)
            {
                // Keep commit/cancel key edges out of gameplay on the first frame after editing ends.
                m_Blocked = textEditing || m_PreviousTextEditing;
                m_PreviousTextEditing = textEditing;
                m_Frame = frame;
            }
            else if (textEditing)
            {
                m_Blocked = true;
                m_PreviousTextEditing = true;
            }
        }

        [[nodiscard]] bool AllowsGameplay() const noexcept { return !m_Blocked; }

      private:
        std::optional<std::uint64_t> m_Frame;
        bool m_PreviousTextEditing = false;
        bool m_Blocked = false;
    };

    [[nodiscard]] inline std::optional<Keire::InputControlSnapshot>
    RouteGameViewportControl(std::optional<Keire::InputControlSnapshot> snapshot, const bool inputActive) noexcept
    {
        if (snapshot && !inputActive)
        {
            snapshot->Value.X = 0.0F;
            snapshot->Value.Y = 0.0F;
            snapshot->Pressed = false;
            snapshot->Released = false;
        }
        return snapshot;
    }

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
                                                              const bool /*panelFocused*/,
                                                              const bool /*captureRequested*/,
                                                              const bool captureSuspended,
                                                              const bool playReviewActive = false) noexcept
    {
        if (!playActive || !applicationFocused || captureSuspended || playReviewActive)
            return false;
        // Play owns input until explicitly released, including unlocked-cursor controllers and panel/tab changes.
        return true;
    }

    class SceneViewportNavigationState final
    {
      public:
        [[nodiscard]] bool Update(const bool playActive, const bool available, const bool panelFocused,
                                  const bool canvasClicked) noexcept
        {
            if (!available || (playActive && !panelFocused && !canvasClicked))
            {
                Reset();
                return false;
            }
            if (!playActive)
            {
                Reset();
                return true;
            }
            m_Activated = m_Activated || canvasClicked;
            return m_Activated;
        }

        void Reset() noexcept { m_Activated = false; }

      private:
        bool m_Activated = false;
    };
} // namespace KeireEditor
