#include "Keire/Ui/UiStyleProperties.h"
#include "KeireClient/Editor/UiBuilderPanel.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

namespace KeireEditor
{
    void UiBuilderPanel::DrawStyleSource(Keire::UiFrame& ui)
    {
        auto& styleDocument = m_Controller.UiBuilderStyleSheetState();
        const auto& theme = m_Controller.UiBuilderTheme();
        if (!styleDocument.Asset())
        {
            ui.TextColored(theme.MutedText, "Open a linked style sheet to edit source.");
            return;
        }
        if (m_StyleSourceGeneration != styleDocument.Generation())
        {
            m_StyleSourceGeneration = styleDocument.Generation();
            m_StyleSourceDraft = styleDocument.SourceText();
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            m_StyleSourceEditorState.CursorOffset =
                std::min(m_StyleSourceEditorState.CursorOffset, m_StyleSourceDraft.size());
        }
        ui.TextColored(theme.Accent, "SOURCE");
        ui.TextColoredWrapped(theme.MutedText,
                              "Completion, brace matching, property documentation, search, and formatting use the "
                              "same property registry as the runtime. Invalid drafts preserve the last valid preview.");

        (void)ui.InputTextWithHint("##UiStyleFind", "Find", m_StyleSourceFind);
        ui.SameLine();
        (void)ui.InputTextWithHint("##UiStyleReplace", "Replace", m_StyleSourceReplace);
        ui.SameLine();
        (void)ui.Checkbox("Case", m_StyleSourceFindCaseSensitive);
        ui.SameLine();
        if (ui.Button("Find All"))
        {
            m_StyleSourceMatches = m_StyleSourceEditor.Find(m_StyleSourceFind, m_StyleSourceFindCaseSensitive);
            m_StyleSourceMatch = 0U;
            if (!m_StyleSourceMatches.empty())
            {
                const auto& match = m_StyleSourceMatches.front();
                m_StyleSourceEditorState.CursorOffset = match.Offset;
                m_StyleSourceEditorState.SelectionBegin = match.Offset;
                m_StyleSourceEditorState.SelectionEnd = match.Offset + match.Length;
                m_StyleSourceEditorState.RequestCursor = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_StyleSourceMatches.empty()); disabled)
        {
            if (ui.Button("Next"))
            {
                m_StyleSourceMatch = (m_StyleSourceMatch + 1U) % m_StyleSourceMatches.size();
                const auto& match = m_StyleSourceMatches[m_StyleSourceMatch];
                m_StyleSourceEditorState.CursorOffset = match.Offset;
                m_StyleSourceEditorState.SelectionBegin = match.Offset;
                m_StyleSourceEditorState.SelectionEnd = match.Offset + match.Length;
                m_StyleSourceEditorState.RequestCursor = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_StyleSourceFind.empty()); disabled)
        {
            if (ui.Button("Replace All"))
            {
                m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
                const auto replacements = m_StyleSourceEditor.ReplaceAll(m_StyleSourceFind, m_StyleSourceReplace,
                                                                         m_StyleSourceFindCaseSensitive);
                if (replacements > 0U)
                {
                    m_StyleSourceDraft = m_StyleSourceEditor.Source();
                    (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
                    m_StyleSourceGeneration = styleDocument.Generation();
                    m_Message = "Replaced " + std::to_string(replacements) + " source occurrence(s).";
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Format"))
        {
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            if (m_StyleSourceEditor.Format())
            {
                m_StyleSourceDraft = m_StyleSourceEditor.Source();
                (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
                m_StyleSourceGeneration = styleDocument.Generation();
            }
        }

        if (!m_StyleSourceEditor.Rules().empty())
        {
            if (auto rules = ui.BeginCombo("Go to rule", "Select a selector"); rules)
            {
                for (const auto& rule : m_StyleSourceEditor.Rules())
                {
                    if (ui.Selectable(rule.Selector + "  (line " + std::to_string(rule.Line) + ")"))
                    {
                        m_StyleSourceEditorState.CursorOffset = rule.Offset;
                        m_StyleSourceEditorState.SelectionBegin = rule.Offset;
                        m_StyleSourceEditorState.SelectionEnd = rule.Offset + rule.Selector.size();
                        m_StyleSourceEditorState.RequestCursor = true;
                    }
                }
            }
        }

        m_StyleSourceEditorState.Highlights.clear();
        m_StyleSourceEditorState.Highlights.reserve(m_StyleSourceEditor.Tokens().size());
        for (const auto& token : m_StyleSourceEditor.Tokens())
        {
            Keire::UiColor color;
            switch (token.Kind)
            {
            case UiStyleSourceTokenKind::Header:
                color = {0.72F, 0.48F, 0.95F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Selector:
                color = {0.28F, 0.78F, 1.0F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Property:
                color = {0.44F, 0.68F, 1.0F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Value:
                color = {0.88F, 0.76F, 0.48F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Variable:
                color = {0.88F, 0.48F, 0.86F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Number:
                color = {0.98F, 0.62F, 0.30F, 1.0F};
                break;
            case UiStyleSourceTokenKind::String:
                color = {0.36F, 0.86F, 0.58F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Comment:
                color = {0.48F, 0.56F, 0.62F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Punctuation:
                color = {0.72F, 0.76F, 0.82F, 1.0F};
                break;
            case UiStyleSourceTokenKind::Invalid:
                color = theme.Error;
                break;
            }
            m_StyleSourceEditorState.Highlights.push_back({token.Offset, token.Length, color});
        }
        const auto sourceAvailable = ui.ContentAvailable();
        float editorHeight =
            std::clamp(m_StyleSourceEditorHeight, 180.0F, std::max(180.0F, sourceAvailable.Height - 120.0F));
        float detailsHeight = std::max(90.0F, sourceAvailable.Height - editorHeight - 4.0F);
        (void)ui.InputCodeEditor("CSS style source", m_StyleSourceDraft, m_StyleSourceEditorState,
                                 Keire::UiSize{0.0F, editorHeight});
        const auto sourceState = ui.LastItemState();
        const auto sourceRect = ui.LastItemRect();
        if (ui.Splitter(Keire::UiAxis::Vertical, "UiStyleSourceHeight", editorHeight, detailsHeight, 180.0F, 90.0F))
            m_StyleSourceEditorHeight = editorHeight;
        m_StyleSourceEditor.SetCursor(m_StyleSourceEditorState.CursorOffset);
        if (sourceState.Edited)
        {
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            m_StyleSourceEditor.SetCursor(m_StyleSourceEditorState.CursorOffset);
            m_StyleSourceParsePending = true;
            m_StyleSourceEditTime = std::chrono::steady_clock::now();
        }
        const auto cursor = m_StyleSourceEditor.CursorLocation();
        ui.TextColored(theme.MutedText, "Ln " + std::to_string(cursor.Line) + ", Col " + std::to_string(cursor.Column) +
                                            " | " + std::to_string(m_StyleSourceEditor.LineCount()) + " lines | " +
                                            std::to_string(m_StyleSourceEditor.Tokens().size()) + " syntax tokens");
        if (const auto brace = m_StyleSourceEditor.MatchingBrace(m_StyleSourceEditor.Cursor()))
        {
            ui.SameLine();
            ui.TextColored(theme.Accent, "matching brace at byte " + std::to_string(*brace));
        }
        if (const auto documentation = m_StyleSourceEditor.HoverDocumentation(m_StyleSourceEditor.Cursor()))
        {
            ui.TextColoredWrapped(theme.MutedText, *documentation);
        }
        auto completions = m_StyleSourceEditor.Completions(m_StyleSourceEditor.Cursor(), 12U);
        if (m_StyleCompletionCursor != m_StyleSourceEditor.Cursor())
        {
            m_StyleCompletionCursor = m_StyleSourceEditor.Cursor();
            m_StyleCompletionSelection = 0;
        }
        if (!completions.empty())
            m_StyleCompletionSelection = std::min(m_StyleCompletionSelection, completions.size() - 1U);

        const auto applyCompletion = [&](const UiStyleSourceCompletion& completion)
        {
            if (!m_StyleSourceEditor.ApplyCompletion(m_StyleSourceEditor.Cursor(), completion))
                return false;
            m_StyleSourceDraft = m_StyleSourceEditor.Source();
            m_StyleSourceEditorState.CursorOffset = m_StyleSourceEditor.Cursor();
            m_StyleSourceEditorState.SelectionBegin = m_StyleSourceEditor.Cursor();
            m_StyleSourceEditorState.SelectionEnd = m_StyleSourceEditor.Cursor();
            m_StyleSourceEditorState.RequestCursor = true;
            m_StyleCompletionCursor = m_StyleSourceEditor.Cursor();
            (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
            m_StyleSourceGeneration = styleDocument.Generation();
            return true;
        };

        bool completionApplied = false;
        if (sourceState.Active && !completions.empty())
        {
            if (ui.KeyPressed(Keire::UiKey::Down))
                m_StyleCompletionSelection = (m_StyleCompletionSelection + 1U) % completions.size();
            if (ui.KeyPressed(Keire::UiKey::Up))
                m_StyleCompletionSelection =
                    (m_StyleCompletionSelection + completions.size() - 1U) % completions.size();
            if (ui.KeyPressed(Keire::UiKey::Tab) || ui.KeyPressed(Keire::UiKey::Enter))
                completionApplied = applyCompletion(completions[m_StyleCompletionSelection]);
            if (!completionApplied)
                ui.OpenPopup("UiStyleSourceCompletions");
        }

        constexpr Keire::UiSize completionSize{440.0F, 250.0F};
        auto completionPosition =
            Keire::UiPosition{m_StyleSourceEditorState.CaretScreenPosition.X,
                              m_StyleSourceEditorState.CaretScreenPosition.Y + m_StyleSourceEditorState.CaretHeight};
        completionPosition.X = std::clamp(completionPosition.X, sourceRect.Minimum.X,
                                          std::max(sourceRect.Minimum.X, sourceRect.Maximum.X - completionSize.Width));
        if (completionPosition.Y + completionSize.Height > sourceRect.Maximum.Y)
            completionPosition.Y =
                std::max(sourceRect.Minimum.Y, m_StyleSourceEditorState.CaretScreenPosition.Y - completionSize.Height);
        ui.SetNextWindowPosition(completionPosition, false);
        ui.SetNextWindowSize(completionSize, false);
        if (auto suggestions =
                ui.BeginPopup("UiStyleSourceCompletions",
                              {.NoResize = true, .NoMove = true, .NoSavedSettings = true, .NoFocusOnAppearing = true});
            suggestions)
        {
            if (completionApplied || completions.empty())
            {
                ui.CloseCurrentPopup();
            }
            else
            {
                for (std::size_t index = 0; index < completions.size(); ++index)
                {
                    const auto& completion = completions[index];
                    if (ui.Selectable(completion.Label + "##UiStyleCompletion" + completion.Insertion,
                                      index == m_StyleCompletionSelection))
                    {
                        m_StyleCompletionSelection = index;
                        (void)applyCompletion(completion);
                        ui.CloseCurrentPopup();
                    }
                }
                ui.Separator();
                ui.TextColoredWrapped(theme.MutedText, completions[m_StyleCompletionSelection].Documentation);
                ui.TextColored(theme.MutedText, "Up/Down to navigate  |  Tab/Enter to accept  |  Esc to close");
            }
        }
        const bool parseNow = m_StyleSourceParsePending && (sourceState.DeactivatedAfterEdit ||
                                                            std::chrono::steady_clock::now() - m_StyleSourceEditTime >=
                                                                std::chrono::milliseconds(150));
        if (parseNow)
        {
            (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
            m_StyleSourceGeneration = styleDocument.Generation();
            m_StyleSourceParsePending = false;
        }
        if (const auto& diagnostic = styleDocument.SourceDiagnostic())
            ui.TextColoredWrapped(theme.Error, "Line " + std::to_string(diagnostic->Line) + ", column " +
                                                   std::to_string(diagnostic->Column) + ": " + diagnostic->Message);
        else
            ui.TextColored(theme.Success, "Valid draft | preview published");
        const bool externalConflict = styleDocument.ExternalConflict();
        if (externalConflict)
        {
            ui.TextColoredWrapped(theme.Warning, "The file changed outside Kéire. Compare or reload it before saving.");
            if (ui.Button(m_StyleExternalComparison.empty() ? "Compare External" : "Hide Comparison"))
            {
                m_StyleExternalComparison =
                    m_StyleExternalComparison.empty() ? styleDocument.ExternalComparison() : std::string{};
            }
            ui.SameLine();
            if (ui.Button("Reload External"))
            {
                try
                {
                    m_Controller.ReloadUiBuilderStyleSheet();
                    m_StyleSourceGeneration = 0;
                    m_StyleExternalComparison.clear();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
            if (!m_StyleExternalComparison.empty())
                if (auto comparison = ui.BeginChild("UiStyleExternalComparison", {0.0F, 180.0F}, true); comparison)
                    ui.TextColoredWrapped(theme.MutedText, m_StyleExternalComparison);
            if (ui.Button("Save Draft As..."))
                m_Controller.RequestSaveUiBuilderStyleSheetAs();
        }
        if (auto disabled = ui.BeginDisabled(!styleDocument.SourceValid() || externalConflict); disabled)
        {
            if (ui.Button("Save Style Sheet"))
            {
                try
                {
                    m_Controller.SaveUiBuilderStyleSheet();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Save As..."))
            m_Controller.RequestSaveUiBuilderStyleSheetAs();
        ui.SameLine();
        if (ui.Button("Reload"))
        {
            try
            {
                m_Controller.ReloadUiBuilderStyleSheet();
                m_StyleSourceGeneration = 0;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanUndo());
            disabled)
            if (ui.Button("Undo"))
                (void)styleDocument.Undo();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanRedo());
            disabled)
            if (ui.Button("Redo"))
                (void)styleDocument.Redo();
    }
} // namespace KeireEditor
