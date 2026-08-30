#pragma once

#include "Keire/Ui/UiToolkit.h"
#include "Keire/Undo.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor
{
    class UiBuilderStyleSheetDocument final
    {
      public:
        void Open(Keire::AssetId asset, Keire::UiStyleSheetDefinition definition, std::uint64_t revision,
                  std::filesystem::path source, Keire::Ref<Keire::UndoContext> undo = {});
        void Close() noexcept;

        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] std::uint64_t Generation() const noexcept { return m_Generation; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        [[nodiscard]] const Keire::UiStyleSheetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] std::optional<std::size_t> Selection() const noexcept { return m_Selection; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return m_Source; }

        void Select(std::optional<std::size_t> rule) noexcept;
        [[nodiscard]] bool AddRule(std::string_view selector, std::string_view declarations);
        [[nodiscard]] bool EditRule(std::size_t rule, std::string_view selector, std::string_view declarations);
        [[nodiscard]] bool RemoveRule(std::size_t rule);
        [[nodiscard]] std::string RuleDeclarations(std::size_t rule) const;
        [[nodiscard]] std::string SourcePreview() const;
        void Save();
        void ReloadFromSource(bool discardLocalChanges = false);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

      private:
        [[nodiscard]] static Keire::UiStyleRuleDefinition ParseRule(std::string_view selector,
                                                                    std::string_view declarations);
        [[nodiscard]] bool Edit(std::string_view name, Keire::UiStyleSheetDefinition candidate);
        void RecordApplied(std::string_view name, Keire::UiStyleSheetDefinition before);
        void AdvanceGeneration() noexcept;

        Keire::AssetId m_Asset;
        Keire::UiStyleSheetDefinition m_Definition;
        Keire::UiStyleSheetDefinition m_Baseline;
        std::optional<std::size_t> m_Selection;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_Generation = 0;
        std::filesystem::path m_Source;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
