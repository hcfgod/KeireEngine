#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/ScenePlayChanges.h"

namespace KeireEditor
{
    enum class ScenePlayDecision : std::uint8_t
    {
        None,
        Apply,
        Discard,
        Cancel
    };

    class ScenePlayChangesPanel final
    {
      public:
        void Open() noexcept
        {
            m_ReviewPending = true;
            m_OpenRequested = true;
        }
        void Close() noexcept
        {
            m_ReviewPending = false;
            m_OpenRequested = false;
        }
        [[nodiscard]] bool Pending() const noexcept { return m_ReviewPending; }
        [[nodiscard]] ScenePlayDecision Draw(Keire::UiFrame& ui, ScenePlayChangeSet& changes);

      private:
        bool m_OpenRequested = false;
        bool m_ReviewPending = false;
    };
} // namespace KeireEditor
