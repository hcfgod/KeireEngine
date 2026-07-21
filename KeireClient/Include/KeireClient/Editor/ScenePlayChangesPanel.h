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
        void Open() noexcept { m_OpenRequested = true; }
        [[nodiscard]] ScenePlayDecision Draw(Keire::UiFrame& ui, ScenePlayChangeSet& changes);

      private:
        bool m_OpenRequested = false;
    };
} // namespace KeireEditor
