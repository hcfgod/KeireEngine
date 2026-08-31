#include "KeireClient/Editor/UiBuilderPanel.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace KeireEditor
{
    void UiBuilderPanel::DrawStyleSheets(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        auto& styleDocument = m_Controller.UiBuilderStyleSheetState();
        const auto& theme = m_Controller.UiBuilderTheme();

        ui.Text("Linked Style Sheets");
        ui.TextColored(theme.MutedText, "Open a linked .keirestyle asset to author selectors and declarations.");
        ui.Separator();
        const auto styleSheets = document.Definition().StyleSheets;
        for (const auto style : styleSheets)
        {
            ui.Text(style.ToString());
            ui.SameLine();
            if (ui.Button("Edit##UiBuilderStyleEdit" + style.ToString()))
            {
                try
                {
                    m_Controller.OpenUiBuilderStyleSheet(style);
                    m_StyleRuleAsset = {};
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
            ui.SameLine();
            if (ui.Button("Remove##UiBuilderStyleRemove" + style.ToString()))
            {
                auto candidate = document.Definition();
                std::erase(candidate.StyleSheets, style);
                (void)document.Edit("Unlink UI style sheet", std::move(candidate));
                break;
            }
        }
        (void)m_StyleSheetPicker.Draw(
            ui, m_Controller.UiBuilderAssetRecords(), m_StyleSheetDraft,
            {.Label = "Style Sheet",
             .EmptyLabel = "Choose a .keirestyle asset",
             .ExpectedType = Keire::UiStyleSheetAsset::StaticType(),
             .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealUiBuilderAsset(asset); },
             .AllowNone = true});
        if (ui.Button("Link Style Sheet"))
        {
            try
            {
                const auto style = m_StyleSheetDraft;
                if (!style)
                    throw std::invalid_argument("Choose a .keirestyle asset to link.");
                auto candidate = document.Definition();
                if (std::ranges::find(candidate.StyleSheets, style) == candidate.StyleSheets.end())
                    candidate.StyleSheets.push_back(style);
                (void)document.Edit("Link UI style sheet", std::move(candidate));
                m_StyleSheetDraft = {};
                m_StyleSheetPicker.Clear();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }

        ui.Separator();
        if (!styleDocument.Asset())
        {
            ui.TextColored(theme.MutedText, "Choose Edit beside a linked style sheet to begin authoring it.");
            return;
        }
        m_Controller.ActivateUiBuilderStyleSheetHistory();
        ui.Text(styleDocument.SourcePath().filename().string());
        ui.SameLine();
        ui.TextColored(styleDocument.Dirty() ? theme.Warning : theme.MutedText,
                       styleDocument.Dirty() ? "Unsaved style changes" : "Saved");
        if (ui.Button("Save Style Sheet"))
        {
            try
            {
                m_Controller.SaveUiBuilderStyleSheet();
                m_Message = "Saved and queued the UI style sheet for import.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        ui.SameLine();
        if (ui.Button("Reload Style Sheet"))
        {
            try
            {
                m_Controller.ReloadUiBuilderStyleSheet();
                m_StyleRuleAsset = {};
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanUndo());
            disabled)
        {
            if (ui.Button("Undo Style"))
                (void)styleDocument.Undo();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanRedo());
            disabled)
        {
            if (ui.Button("Redo Style"))
                (void)styleDocument.Redo();
        }

        ui.Separator();
        for (std::size_t index = 0; index < styleDocument.Definition().Rules.size(); ++index)
        {
            const auto& rule = styleDocument.Definition().Rules[index];
            if (ui.Selectable(rule.Selector + "##UiBuilderStyleRule" + std::to_string(index),
                              styleDocument.Selection() == index))
            {
                styleDocument.Select(index);
                m_StyleRuleAsset = {};
            }
        }

        if (m_StyleRuleAsset != styleDocument.Asset() || m_StyleRuleGeneration != styleDocument.Generation() ||
            m_StyleRuleSelection != styleDocument.Selection())
        {
            m_StyleRuleAsset = styleDocument.Asset();
            m_StyleRuleGeneration = styleDocument.Generation();
            m_StyleRuleSelection = styleDocument.Selection();
            if (m_StyleRuleSelection)
            {
                m_StyleSelectorDraft = styleDocument.Definition().Rules[*m_StyleRuleSelection].Selector;
                m_StyleDeclarationsDraft = styleDocument.RuleDeclarations(*m_StyleRuleSelection);
            }
            else
            {
                m_StyleSelectorDraft = ".new-class";
                m_StyleDeclarationsDraft = "color: #ffffffff;";
            }
        }

        ui.Separator();
        (void)ui.InputText("Selector", m_StyleSelectorDraft);
        (void)ui.InputTextMultiline("Declarations", m_StyleDeclarationsDraft, 8);
        if (styleDocument.Selection())
        {
            if (ui.Button("Apply Rule"))
            {
                try
                {
                    (void)styleDocument.EditRule(*styleDocument.Selection(), m_StyleSelectorDraft,
                                                 m_StyleDeclarationsDraft);
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
            ui.SameLine();
            if (ui.Button("Remove Rule"))
            {
                (void)styleDocument.RemoveRule(*styleDocument.Selection());
                m_StyleRuleAsset = {};
            }
        }
        if (ui.Button("Add Rule"))
        {
            try
            {
                (void)styleDocument.AddRule(m_StyleSelectorDraft, m_StyleDeclarationsDraft);
                m_StyleRuleAsset = {};
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        ui.TextColored(theme.MutedText,
                       "Selectors support type, #name, .class, child/descendant combinators, and pseudo-states.");
    }
} // namespace KeireEditor
