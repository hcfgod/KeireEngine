#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace KeireEditor
{
    enum class EditorCommand : std::uint8_t
    {
        NewScene,
        SaveScene,
        SaveSceneAs,
        CloseScene,
        CreateEntity,
        DeleteSelection,
        SelectAll,
        ClearSelection,
        Play,
        Pause,
        Stop,
        Exit,
        Undo,
        Redo
    };

    class EditorCommandRouter final
    {
      public:
        using Action = std::function<void()>;
        using Availability = std::function<bool()>;

        void Bind(EditorCommand command, Action action, Availability available = {});
        [[nodiscard]] bool Available(EditorCommand command) const;
        [[nodiscard]] bool Execute(EditorCommand command) const;
        void Clear() noexcept { m_Bindings.clear(); }

      private:
        struct Binding final
        {
            Action Execute;
            Availability IsAvailable;
        };
        std::unordered_map<EditorCommand, Binding> m_Bindings;
    };
} // namespace KeireEditor
