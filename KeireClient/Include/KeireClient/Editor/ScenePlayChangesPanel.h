#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/ScenePlayChanges.h"

#include <cstddef>

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
        [[nodiscard]] static float ChangeListHeight(std::size_t changeCount) noexcept;
        [[nodiscard]] ScenePlayDecision Draw(Keire::UiFrame& ui, ScenePlayChangeSet& changes);

      private:
        bool m_OpenRequested = false;
        bool m_ReviewPending = false;
    };
} // namespace KeireEditor
