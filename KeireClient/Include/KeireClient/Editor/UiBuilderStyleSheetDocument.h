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
#include <vector>

namespace KeireEditor
{
    struct UiBuilderStyleSourceDiagnostic final
    {
        std::size_t Line = 0;
        std::size_t Column = 0;
        std::string Message;

        [[nodiscard]] bool operator==(const UiBuilderStyleSourceDiagnostic&) const = default;
    };

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
        [[nodiscard]] const std::string& SourceText() const noexcept { return m_SourceText; }
        [[nodiscard]] bool SourceValid() const noexcept { return !m_SourceDiagnostic.has_value(); }
        [[nodiscard]] const std::optional<UiBuilderStyleSourceDiagnostic>& SourceDiagnostic() const noexcept
        {
            return m_SourceDiagnostic;
        }
        [[nodiscard]] bool ExternalConflict() const;
        [[nodiscard]] std::string ExternalComparison(std::size_t maximumLines = 256U) const;

        void Select(std::optional<std::size_t> rule) noexcept;
        [[nodiscard]] bool AddRule(std::string_view selector, std::string_view declarations);
        [[nodiscard]] bool EditRule(std::size_t rule, std::string_view selector, std::string_view declarations);
        [[nodiscard]] bool SetSelector(std::size_t rule, std::string_view selector);
        [[nodiscard]] bool SetMediaCondition(std::size_t rule, std::optional<Keire::UiStyleMediaCondition> condition);
        [[nodiscard]] bool SetProperty(std::size_t rule, std::string_view name, std::string_view value);
        [[nodiscard]] bool RemoveProperty(std::size_t rule, std::string_view name);
        [[nodiscard]] bool SetToken(std::string_view name, std::string_view value);
        [[nodiscard]] bool RenameToken(std::string_view currentName, std::string_view replacementName);
        [[nodiscard]] bool DuplicateRule(std::size_t rule);
        [[nodiscard]] bool MoveRule(std::size_t rule, std::size_t destination);
        [[nodiscard]] bool RemoveRule(std::size_t rule);
        [[nodiscard]] bool ApplySourceDraft(std::string source);
        [[nodiscard]] std::string RuleDeclarations(std::size_t rule) const;
        [[nodiscard]] std::string SourcePreview() const;
        void Save();
        void SaveAs(const std::filesystem::path& destination) const;
        void ReloadFromSource(bool discardLocalChanges = false);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

      private:
        struct DocumentState final
        {
            Keire::UiStyleSheetDefinition Definition;
            std::string Source;
            std::optional<UiBuilderStyleSourceDiagnostic> Diagnostic;
        };

        [[nodiscard]] static Keire::UiStyleRuleDefinition
        ParseRule(std::string_view selector, std::string_view declarations, std::uint32_t schemaVersion);
        [[nodiscard]] bool Edit(std::string_view name, Keire::UiStyleSheetDefinition candidate,
                                std::optional<std::string> source = std::nullopt);
        void RecordApplied(std::string_view name, DocumentState before, std::string mergeKey = {});
        [[nodiscard]] DocumentState State() const;
        void Restore(DocumentState state);
        [[nodiscard]] static UiBuilderStyleSourceDiagnostic Diagnose(std::string_view source,
                                                                     std::string_view message) noexcept;
        void AdvanceGeneration() noexcept;

        Keire::AssetId m_Asset;
        Keire::UiStyleSheetDefinition m_Definition;
        Keire::UiStyleSheetDefinition m_Baseline;
        std::optional<std::size_t> m_Selection;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_Generation = 0;
        std::filesystem::path m_Source;
        std::string m_SourceText;
        std::string m_BaselineSource;
        std::optional<UiBuilderStyleSourceDiagnostic> m_SourceDiagnostic;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
