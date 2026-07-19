#pragma once

namespace Keire
{
    class UiFrame;
}

class EditorWorkspaceLayer;

namespace KeireEditor
{
    class ConsolePanel final
    {
      public:
        void Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor);
    };
} // namespace KeireEditor
