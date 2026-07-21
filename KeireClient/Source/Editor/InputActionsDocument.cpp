#include "KeireClient/Editor/InputActionsDocument.h"

#include <utility>

namespace KeireEditor
{
    void InputActionsDocument::Open(const Keire::AssetId asset, Keire::InputActionAssetDefinition definition,
                                    Keire::Ref<Keire::UndoContext> undo)
    {
        Close();
        m_Asset = asset;
        m_Definition = std::move(definition);
        m_Undo = std::move(undo);
    }

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
