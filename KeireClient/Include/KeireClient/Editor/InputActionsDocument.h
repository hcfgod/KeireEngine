#pragma once

#include "Keire/Core.h"

namespace KeireEditor
{
    class InputActionsDocument final
    {
      public:
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] const Keire::InputActionAssetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        void MarkDirty() noexcept { m_Dirty = true; }
        void MarkSaved() noexcept { m_Dirty = false; }
        void Close() noexcept;

        [[nodiscard]] Keire::AssetId& AssetStorage() noexcept { return m_Asset; }
        [[nodiscard]] Keire::AssetId& MapStorage() noexcept { return m_Map; }
        [[nodiscard]] Keire::AssetId& SchemeStorage() noexcept { return m_Scheme; }
        [[nodiscard]] Keire::AssetId& ActionStorage() noexcept { return m_Action; }
        [[nodiscard]] Keire::AssetId& BindingStorage() noexcept { return m_Binding; }
        [[nodiscard]] Keire::InputActionAssetDefinition& DefinitionStorage() noexcept { return m_Definition; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext>& UndoStorage() noexcept { return m_Undo; }
        [[nodiscard]] bool& DirtyStorage() noexcept { return m_Dirty; }

      private:
        Keire::AssetId m_Asset;
        Keire::AssetId m_Map;
        Keire::AssetId m_Scheme;
        Keire::AssetId m_Action;
        Keire::AssetId m_Binding;
        Keire::InputActionAssetDefinition m_Definition;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
