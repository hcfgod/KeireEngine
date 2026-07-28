#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace KeireEditor
{
    class AnimatorControllerDocument final
    {
      public:
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return m_Source; }
        [[nodiscard]] const Keire::AnimationGraphDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        [[nodiscard]] std::string_view SelectedParameter() const noexcept { return m_SelectedParameter; }
        [[nodiscard]] std::string_view SelectedLayer() const noexcept { return m_SelectedLayer; }
        [[nodiscard]] std::string_view SelectedState() const noexcept { return m_SelectedState; }

        void Open(Keire::AssetId asset, Keire::AnimationGraphDefinition definition, Keire::Ref<Keire::UndoContext> undo,
                  std::filesystem::path source);
        void Save();
        void ReplaceDefinition(Keire::AnimationGraphDefinition definition, bool dirty = true);
        void SelectParameter(std::string id);
        void SelectLayer(std::string id);
        void SelectState(std::string layer, std::string state);
        void ClearSelection() noexcept;
        void RecordApplied(std::string_view name, Keire::AnimationGraphDefinition before);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Close() noexcept;

      private:
        Keire::AssetId m_Asset;
        Keire::AnimationGraphDefinition m_Definition;
        std::filesystem::path m_Source;
        Keire::Ref<Keire::UndoContext> m_Undo;
        std::string m_SelectedParameter;
        std::string m_SelectedLayer;
        std::string m_SelectedState;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
