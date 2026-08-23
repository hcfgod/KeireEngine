#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    class InputActionsDocument final
    {
      public:
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] const Keire::InputActionAssetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] Keire::AssetId SelectedMap() const noexcept { return m_Map; }
        [[nodiscard]] Keire::AssetId SelectedScheme() const noexcept { return m_Scheme; }
        [[nodiscard]] Keire::AssetId SelectedAction() const noexcept { return m_Action; }
        [[nodiscard]] Keire::AssetId SelectedBinding() const noexcept { return m_Binding; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        void Open(Keire::AssetId asset, Keire::InputActionAssetDefinition definition,
                  Keire::Ref<Keire::UndoContext> undo = {}, std::filesystem::path source = {});
        void Save();
        void ReplaceDefinition(Keire::InputActionAssetDefinition definition, bool dirty = true);
        [[nodiscard]] bool TryReplaceDefinition(Keire::InputActionAssetDefinition definition, std::string& diagnostic,
                                                bool dirty = true);
        void SelectMap(Keire::AssetId map) noexcept;
        void SelectScheme(Keire::AssetId scheme) noexcept;
        void SelectAction(Keire::AssetId action) noexcept;
        void SelectBinding(Keire::AssetId binding) noexcept;
        void SetSelection(Keire::AssetId map, Keire::AssetId scheme, Keire::AssetId action,
                          Keire::AssetId binding) noexcept;
        void ClearSelection() noexcept;
        void RecordApplied(std::string_view name, Keire::InputActionAssetDefinition before);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Close() noexcept;

      private:
        Keire::AssetId m_Asset;
        Keire::AssetId m_Map;
        Keire::AssetId m_Scheme;
        Keire::AssetId m_Action;
        Keire::AssetId m_Binding;
        Keire::InputActionAssetDefinition m_Definition;
        std::filesystem::path m_Source;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
