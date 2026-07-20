#include "KeireClient/Editor/InputActionsDocument.h"

namespace KeireEditor
{
    void InputActionsDocument::Close() noexcept
    {
        m_Asset = {};
        m_Map = {};
        m_Scheme = {};
        m_Action = {};
        m_Binding = {};
        m_Definition = {};
        m_Undo.Reset();
        m_Dirty = false;
    }
} // namespace KeireEditor
