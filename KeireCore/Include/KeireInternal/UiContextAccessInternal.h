#pragma once

#include <imgui.h>

#include <memory>
#include <mutex>
#include <stdexcept>

namespace Keire::Detail
{
    // Dear ImGui's current context and backend registration are process-global. The owner thread retains this guard
    // while authoring a frame; packet capture, render-backend recording, and device recovery acquire the same guard.
    class UiContextAccess final
    {
      public:
        explicit UiContextAccess(ImGuiContext* context) : m_Context(context)
        {
            if (!m_Context)
                throw std::invalid_argument("UI context access requires a live Dear ImGui context.");
        }

        UiContextAccess(const UiContextAccess&) = delete;
        UiContextAccess& operator=(const UiContextAccess&) = delete;

        [[nodiscard]] std::unique_lock<std::recursive_mutex> Acquire() const
        {
            std::unique_lock lock(s_ProcessMutex);
            if (!m_Context)
                throw std::logic_error("The Dear ImGui context is no longer available.");
            ImGui::SetCurrentContext(m_Context);
            return lock;
        }

        void Invalidate(ImGuiContext* replacement = nullptr) noexcept
        {
            std::scoped_lock lock(s_ProcessMutex);
            m_Context = nullptr;
            ImGui::SetCurrentContext(replacement);
        }

      private:
        inline static std::recursive_mutex s_ProcessMutex;
        ImGuiContext* m_Context = nullptr;
    };

    [[nodiscard]] inline std::unique_lock<std::recursive_mutex>
    AcquireRequiredUiContext(const std::shared_ptr<UiContextAccess>& contextAccess, const char* missingDiagnostic)
    {
        if (!contextAccess)
            throw std::logic_error(missingDiagnostic);
        return contextAccess->Acquire();
    }
} // namespace Keire::Detail
