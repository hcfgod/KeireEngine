#include "KeireClient/Editor/UiBuilderPanel.h"

#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include "Keire/Ui/UiElements.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        struct ElementRow final
        {
            const Keire::UiVisualElementDefinition* Element = nullptr;
            Keire::AssetId Parent;
            std::size_t ChildIndex = 0;
            std::size_t Depth = 0;
        };

        void Flatten(const Keire::UiVisualElementDefinition& element, const std::size_t depth,
                     std::vector<ElementRow>& rows, const Keire::AssetId parent = {}, const std::size_t childIndex = 0)
        {
            rows.push_back({&element, parent, childIndex, depth});
            for (std::size_t index = 0; index < element.Children.size(); ++index)
                Flatten(element.Children[index], depth + 1, rows, element.StableId, index);
        }

        [[nodiscard]] std::string JoinClasses(const std::span<const std::string> classes)
        {
            std::string result;
            for (const auto& value : classes)
            {
                if (!result.empty())
                    result += ' ';
                result += value;
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string> SplitClasses(const std::string_view text)
        {
            std::istringstream stream{std::string(text)};
            std::vector<std::string> result;
            std::string value;
            while (stream >> value)
            {
                if (std::ranges::find(result, value) == result.end())
                    result.push_back(std::move(value));
            }
            return result;
        }

        [[nodiscard]] std::string NamedValue(const std::span<const Keire::UiNamedValue> values,
                                             const std::string_view name)
        {
            const auto found = std::ranges::find(values, name, &Keire::UiNamedValue::Name);
            return found == values.end() ? std::string{} : found->Value;
        }

        void SetNamedValue(std::vector<Keire::UiNamedValue>& values, const std::string_view name, std::string value)
        {
            const auto found = std::ranges::find(values, name, &Keire::UiNamedValue::Name);
            if (value.empty())
            {
                if (found != values.end())
                    values.erase(found);
                return;
            }
            if (found != values.end())
                found->Value = std::move(value);
            else
                values.push_back({std::string(name), std::move(value)});
        }

        [[nodiscard]] std::string InlineStyles(const std::span<const Keire::UiNamedValue> values)
        {
            std::string result;
            for (const auto& value : values)
            {
                if (!result.empty())
                    result += ' ';
                result += value.Name + ": " + value.Value + ';';
            }
            return result;
        }

        [[nodiscard]] std::vector<Keire::UiNamedValue> ParseInlineStyles(const std::string_view text)
        {
            std::vector<Keire::UiNamedValue> result;
            std::size_t begin = 0;
            while (begin < text.size())
            {
                const auto end = text.find(';', begin);
                auto rule = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
                const auto colon = rule.find(':');
                if (colon != std::string_view::npos)
                {
                    auto trim = [](std::string_view value)
                    {
                        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                            value.remove_prefix(1);
                        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                            value.remove_suffix(1);
                        return value;
                    };
                    const auto name = trim(rule.substr(0, colon));
                    const auto value = trim(rule.substr(colon + 1));
                    if (!name.empty() && !value.empty())
                        result.push_back({std::string(name), std::string(value)});
                }
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return result;
        }

        constexpr std::array LibraryTypes{
            Keire::UiVisualElementType::VisualElement, Keire::UiVisualElementType::Label,
            Keire::UiVisualElementType::Image,         Keire::UiVisualElementType::Button,
            Keire::UiVisualElementType::TextField,     Keire::UiVisualElementType::Toggle,
            Keire::UiVisualElementType::Slider,        Keire::UiVisualElementType::ProgressBar,
            Keire::UiVisualElementType::ScrollView,    Keire::UiVisualElementType::ListView,
            Keire::UiVisualElementType::TreeView,      Keire::UiVisualElementType::DropdownField,
            Keire::UiVisualElementType::Foldout,       Keire::UiVisualElementType::TabView,
            Keire::UiVisualElementType::Toolbar,       Keire::UiVisualElementType::Spacer};

        [[nodiscard]] bool CanContainUiChildren(const Keire::UiVisualElementType type) noexcept
        {
            using Type = Keire::UiVisualElementType;
            return type == Type::VisualElement || type == Type::TemplateContainer || type == Type::ScrollView ||
                   type == Type::ListView || type == Type::TreeView || type == Type::Foldout || type == Type::TabView ||
                   type == Type::Toolbar || type == Type::Custom || type == Type::Slot;
        }

        [[nodiscard]] Keire::AssetId PreferredInsertionParent(const UiBuilderDocument& document) noexcept
        {
            auto candidate = document.Selection();
            while (candidate)
            {
                const auto* element = document.Find(candidate);
                if (element && CanContainUiChildren(element->Type))
                    return candidate;
                candidate = document.ParentOf(candidate);
            }
            return document.Definition().Root.StableId;
        }

        [[nodiscard]] std::string_view RuntimeUiEventName(const Keire::RuntimeUiEventType type) noexcept
        {
            switch (type)
            {
            case Keire::RuntimeUiEventType::PointerEnter:
                return "PointerEnter";
            case Keire::RuntimeUiEventType::PointerExit:
                return "PointerExit";
            case Keire::RuntimeUiEventType::PointerDown:
                return "PointerDown";
            case Keire::RuntimeUiEventType::PointerUp:
                return "PointerUp";
            case Keire::RuntimeUiEventType::Click:
                return "Click";
            case Keire::RuntimeUiEventType::Focus:
                return "Focus";
            case Keire::RuntimeUiEventType::Blur:
                return "Blur";
            case Keire::RuntimeUiEventType::Submit:
                return "Submit";
            case Keire::RuntimeUiEventType::Cancel:
                return "Cancel";
            case Keire::RuntimeUiEventType::ValueChanged:
                return "ValueChanged";
            case Keire::RuntimeUiEventType::TextChanged:
                return "TextChanged";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string OptionalCount(const std::optional<std::size_t> value)
        {
            return value ? std::to_string(*value) : "unavailable";
        }

        [[nodiscard]] std::string OptionalMilliseconds(const std::optional<float> value)
        {
            return value ? std::to_string(*value) + " ms" : "unavailable";
        }
    } // namespace

    std::string_view UiBuilderElementTypeName(const Keire::UiVisualElementType type) noexcept
    {
        switch (type)
        {
        case Keire::UiVisualElementType::VisualElement:
            return "Visual Element";
        case Keire::UiVisualElementType::TemplateContainer:
            return "Template Container";
        case Keire::UiVisualElementType::Label:
            return "Label";
        case Keire::UiVisualElementType::Image:
            return "Image";
        case Keire::UiVisualElementType::Button:
            return "Button";
        case Keire::UiVisualElementType::TextField:
            return "Text Field";
        case Keire::UiVisualElementType::Toggle:
            return "Toggle";
        case Keire::UiVisualElementType::Slider:
            return "Slider";
        case Keire::UiVisualElementType::ProgressBar:
            return "Progress Bar";
        case Keire::UiVisualElementType::ScrollView:
            return "Scroll View";
        case Keire::UiVisualElementType::ListView:
            return "List View";
        case Keire::UiVisualElementType::TreeView:
            return "Tree View";
        case Keire::UiVisualElementType::DropdownField:
            return "Dropdown";
        case Keire::UiVisualElementType::Foldout:
            return "Foldout";
        case Keire::UiVisualElementType::TabView:
            return "Tab View";
        case Keire::UiVisualElementType::Toolbar:
            return "Toolbar";
        case Keire::UiVisualElementType::Spacer:
            return "Spacer";
        case Keire::UiVisualElementType::Custom:
            return "Custom Element";
        case Keire::UiVisualElementType::Slot:
            return "Slot";
        }
        return "Unknown";
    }

    void UiBuilderPanel::ResetTransientState() noexcept
    {
        if (m_LivePicking && m_LiveDebugAsset)
            m_Controller.SetUiBuilderLivePicking(m_LiveDebugAsset, false);
        m_DraftElement = {};
        m_SourceAsset = {};
        m_DebugAsset = {};
        m_DebugSelection = {};
        m_DebugGeneration = 0;
        m_DebugSnapshot.reset();
        m_DebugStyleSheets.clear();
        m_DebugTemplates.clear();
        m_LiveDebugStore.Close();
        m_LiveDebugAsset = {};
        m_PreviewAsset = {};
        m_PreviewSelection = {};
        m_PreviewGeneration = 0;
        m_PreviewSnapshot.reset();
        m_PreviewStyleSheets.clear();
        m_PreviewTemplates.clear();
        m_BuiltPreviewSettings.reset();
        m_PreviewSettings = {};
        m_PreviewDiagnostic.clear();
        m_NameDraft.clear();
        m_ClassesDraft.clear();
        m_TextDraft.clear();
        m_CustomTypeDraft.clear();
        m_InlineStyleDraft.clear();
        m_StyleSheetPicker.Clear();
        m_StyleSheetDraft = {};
        m_StyleRuleAsset = {};
        m_StyleRuleGeneration = 0;
        m_StyleRuleSelection.reset();
        m_StyleSelectorDraft.clear();
        m_StyleDeclarationsDraft.clear();
        m_TemplateDraft.clear();
        m_SlotDraft.clear();
        m_NewTemplateDraft.clear();
        m_NewSlotDraft = "content";
        m_BindingPropertyDraft.clear();
        m_BindingPathDraft.clear();
        m_BindingModeDraft = "OneWay";
        m_SourceDraft.clear();
        m_CanvasGesture = {};
        m_SourceEditing = false;
        m_LivePicking = false;
    }

    void UiBuilderPanel::SynchronizeDraft()
    {
        const auto& document = m_Controller.UiBuilderState();
        const auto* element = document.Find(document.Selection());
        if (!element || m_DraftElement == element->StableId)
            return;
        m_DraftElement = element->StableId;
        m_NameDraft = element->Name;
        m_ClassesDraft = JoinClasses(element->Classes);
        m_TextDraft = NamedValue(element->Attributes, "text");
        m_CustomTypeDraft = element->CustomType;
        m_InlineStyleDraft = InlineStyles(element->InlineStyles);
        m_TemplateDraft = element->Template ? element->Template.ToString() : std::string{};
        m_SlotDraft = element->Slot;
    }

    void UiBuilderPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;
        auto& document = m_Controller.UiBuilderState();
        const auto& theme = m_Controller.UiBuilderTheme();
        if (ui.WindowFocused())
            m_Controller.ActivateUiBuilderHistory();
        if (!document.Asset())
        {
            ui.TextColored(theme.Accent, "UI BUILDER");
            ui.Separator();
            ui.Text("No UI document is open.");
            ui.TextColored(theme.MutedText, "Create or double-click a .keireui asset in the Project panel.");
            return;
        }

        SynchronizeDraft();
        if (ui.WindowFocused() && ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
        {
            m_Clipboard = document.CopySelection();
            m_Message = m_Clipboard.Empty()
                            ? "Select a non-root UI element to copy."
                            : std::to_string(m_Clipboard.Elements.size()) + " UI element root(s) copied.";
        }
        if (ui.WindowFocused() && ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}) && !m_Clipboard.Empty())
        {
            try
            {
                (void)document.PasteElements(document.Selection(), m_Clipboard);
                m_DraftElement = {};
                m_Message = "Pasted UI elements with regenerated IDs and names.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        if (ui.WindowFocused() && ui.Shortcut({.Key = Keire::UiKey::Delete}))
        {
            if (document.RemoveSelection())
                m_DraftElement = {};
        }
        {
            [[maybe_unused]] auto heading = ui.PushFont(Keire::UiFontRole::Heading);
            ui.Text(document.Definition().Name.empty() ? "UI Document" : document.Definition().Name);
        }
        ui.SameLine();
        ui.TextColored(document.Dirty() ? theme.Warning : theme.MutedText,
                       document.Dirty() ? "Unsaved changes" : document.SourcePath().filename().string());
        ui.Separator();
        if (ui.IconButton("UiBuilderSave", Keire::UiIcon::Save, false, {30.0F, 28.0F}))
        {
            try
            {
                m_Controller.SaveUiBuilderDocument();
                m_SourceEditing = false;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        if (ui.LastItemState().Hovered)
            ui.SetTooltip("Save UI document", {.Delayed = true});
        ui.SameLine();
        if (ui.IconButton("UiBuilderRevert", Keire::UiIcon::Refresh, false, {30.0F, 28.0F}))
        {
            try
            {
                m_Controller.ReloadUiBuilderDocument();
                ResetTransientState();
                m_Message = "Reverted to the imported source.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanUndo()); disabled)
        {
            if (ui.IconButton("UiBuilderUndo", Keire::UiIcon::Undo, false, {30.0F, 28.0F}))
            {
                (void)document.Undo();
                m_DraftElement = {};
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanRedo()); disabled)
        {
            if (ui.IconButton("UiBuilderRedo", Keire::UiIcon::Redo, false, {30.0F, 28.0F}))
            {
                (void)document.Redo();
                m_DraftElement = {};
            }
        }
        ui.SameLine();
        ui.TextColored(theme.MutedText,
                       std::to_string(m_PreviewSettings.Width) + " x " + std::to_string(m_PreviewSettings.Height) +
                           "  |  " + std::to_string(static_cast<int>(std::lround(m_PreviewSettings.Zoom * 100.0F))) +
                           "% fit zoom  |  Retained preview");
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        ui.Separator();

        const auto available = ui.ContentAvailable();
        const float leftWidth = std::clamp(available.Width * 0.20F, 210.0F, 300.0F);
        const float rightWidth = std::clamp(available.Width * 0.25F, 260.0F, 380.0F);
        const float centerWidth = std::max(260.0F, available.Width - leftWidth - rightWidth - 16.0F);
        if (auto left = ui.BeginChild("UiBuilderLeft", {leftWidth, 0.0F}, true); left)
        {
            if (auto tabs = ui.BeginTabBar("UiBuilderLeftTabs"); tabs)
            {
                if (auto hierarchy = ui.BeginTabItem("Hierarchy"); hierarchy)
                    DrawHierarchy(ui);
                if (auto library = ui.BeginTabItem("Library"); library)
                    DrawLibrary(ui);
            }
        }
        ui.SameLine();
        if (auto center = ui.BeginChild("UiBuilderCenter", {centerWidth, 0.0F}, true); center)
            DrawViewport(ui);
        ui.SameLine();
        if (auto right = ui.BeginChild("UiBuilderRight", {rightWidth, 0.0F}, true); right)
        {
            if (auto tabs = ui.BeginTabBar("UiBuilderRightTabs"); tabs)
            {
                if (auto inspector = ui.BeginTabItem("Inspector"); inspector)
                    DrawInspector(ui);
                if (auto styles = ui.BeginTabItem("Style Sheets"); styles)
                    DrawStyleSheets(ui);
                if (auto debugger = ui.BeginTabItem("Debugger"); debugger)
                    DrawDebugger(ui);
                if (auto source = ui.BeginTabItem("Source"); source)
                    DrawSource(ui);
            }
        }
    }

    void UiBuilderPanel::DrawHierarchy(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        std::vector<ElementRow> rows;
        Flatten(document.Definition().Root, 0, rows);
        for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
        {
            const auto& row = rows[rowIndex];
            std::string label(row.Depth * 2, ' ');
            label += std::string(UiBuilderElementTypeName(row.Element->Type));
            if (!row.Element->Name.empty())
                label += "  #" + row.Element->Name;
            label += "##" + row.Element->StableId.ToString();
            if (ui.Selectable(label, document.IsSelected(row.Element->StableId)))
            {
                if (ui.ControlDown())
                {
                    document.ToggleSelection(row.Element->StableId);
                }
                else if (ui.ShiftDown())
                {
                    const auto anchor = std::ranges::find(rows, document.Selection(), [](const ElementRow& value)
                                                          { return value.Element->StableId; });
                    const auto anchorIndex =
                        anchor == rows.end() ? rowIndex : static_cast<std::size_t>(std::distance(rows.begin(), anchor));
                    const auto first = std::min(anchorIndex, rowIndex);
                    const auto last = std::max(anchorIndex, rowIndex);
                    std::vector<Keire::AssetId> selection;
                    selection.reserve(last - first + 1);
                    for (std::size_t index = first; index <= last; ++index)
                        selection.push_back(rows[index].Element->StableId);
                    document.SetSelection(selection, row.Element->StableId);
                }
                else
                {
                    document.Select(row.Element->StableId);
                }
                m_DraftElement = {};
            }
            if (auto source = ui.BeginDragSource(); source)
            {
                std::vector<Keire::AssetId> dragged;
                if (!document.IsSelected(row.Element->StableId))
                {
                    dragged.push_back(row.Element->StableId);
                }
                else
                {
                    for (const auto& ordered : rows)
                        if (document.IsSelected(ordered.Element->StableId))
                            dragged.push_back(ordered.Element->StableId);
                }
                const auto payload = EncodeAssetPayload(dragged);
                ui.SetDragPayload("KEIRE_UI_ELEMENTS", std::as_bytes(std::span(payload)));
                ui.Text(dragged.size() == 1 ? row.Element->Name : std::to_string(dragged.size()) + " UI elements");
            }
            if (auto target = ui.BeginDragTarget(); target)
            {
                const auto rectangle = ui.LastItemRect();
                const auto pointer = ui.PointerState().Position;
                const float rowHeight = std::max(1.0F, rectangle.Size().Height);
                const float relative = (pointer.Y - rectangle.Minimum.Y) / rowHeight;
                const bool insertBefore = row.Parent && relative < 0.25F;
                const bool insertAfter = row.Parent && relative > 0.75F;
                if (insertBefore || insertAfter)
                {
                    const float y = insertBefore ? rectangle.Minimum.Y : rectangle.Maximum.Y;
                    ui.DrawLine({rectangle.Minimum.X, y}, {rectangle.Maximum.X, y},
                                m_Controller.UiBuilderTheme().Accent, 2.0F);
                }
                else
                {
                    ui.DrawRectangle(rectangle, m_Controller.UiBuilderTheme().Accent, 2.0F, 3.0F);
                }
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_UI_ELEMENTS", payload))
                {
                    try
                    {
                        const auto dragged = DecodeAssetPayload(payload);
                        const auto parent = insertBefore || insertAfter ? row.Parent : row.Element->StableId;
                        const auto index = insertBefore  ? row.ChildIndex
                                           : insertAfter ? row.ChildIndex + 1
                                                         : row.Element->Children.size();
                        if (document.ReparentElements(dragged, parent, index))
                            m_DraftElement = {};
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_Controller.ReportUiBuilderError(m_Message);
                    }
                }
            }
        }
        ui.Separator();
        const bool canCopy = std::ranges::any_of(document.Selections(), [&](const Keire::AssetId element)
                                                 { return element != document.Definition().Root.StableId; });
        if (auto disabled = ui.BeginDisabled(!canCopy); disabled)
        {
            if (ui.Button("Copy Selected"))
                m_Clipboard = document.CopySelection();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_Clipboard.Empty()); disabled)
        {
            if (ui.Button("Paste as Child"))
            {
                try
                {
                    (void)document.PasteElements(document.Selection(), m_Clipboard);
                    m_DraftElement = {};
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
        }
        if (auto disabled = ui.BeginDisabled(!canCopy); disabled)
        {
            if (ui.Button("Remove Selected"))
            {
                (void)document.RemoveSelection();
                m_DraftElement = {};
            }
        }
    }

    void UiBuilderPanel::DrawLibrary(Keire::UiFrame& ui)
    {
        ui.Text("Click to add, or drag onto the canvas");
        ui.TextColored(m_Controller.UiBuilderTheme().MutedText,
                       "Non-container selections add beside the selection. Every insertion supports undo/redo.");
        ui.Separator();
        auto& document = m_Controller.UiBuilderState();
        for (const auto type : LibraryTypes)
        {
            if (ui.Button(std::string("+ ") + std::string(UiBuilderElementTypeName(type))))
            {
                try
                {
                    const auto parent = PreferredInsertionParent(document);
                    const auto bounds = CanvasParentBounds(parent);
                    const auto created = document.AddCanvasElement(
                        parent, type, bounds, {bounds.X + bounds.Width * 0.5F, bounds.Y + bounds.Height * 0.5F});
                    document.Select(created);
                    m_DraftElement = {};
                    m_Message = "Added " + std::string(UiBuilderElementTypeName(type)) + ".";
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
            if (auto source = ui.BeginDragSource(); source)
            {
                const std::array payload{static_cast<std::byte>(type)};
                ui.SetDragPayload("KEIRE_UI_CONTROL", payload);
                ui.Text("Add " + std::string(UiBuilderElementTypeName(type)));
            }
        }
        auto customControls = Keire::Ui::UxmlElementRegistry::Snapshot();
        std::ranges::sort(customControls, {}, &Keire::Ui::UxmlElementDescriptor::Name);
        if (!customControls.empty())
        {
            ui.Separator();
            ui.Text("Registered custom controls");
            for (const auto& descriptor : customControls)
            {
                if (ui.Button("+ " + descriptor.Name + "##UiBuilderCustom"))
                {
                    try
                    {
                        const auto parent = PreferredInsertionParent(document);
                        const auto bounds = CanvasParentBounds(parent);
                        (void)document.AddCanvasCustomElement(
                            parent, descriptor.Name, bounds,
                            {bounds.X + bounds.Width * 0.5F, bounds.Y + bounds.Height * 0.5F});
                        m_DraftElement = {};
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_Controller.ReportUiBuilderError(m_Message);
                    }
                }
                if (auto source = ui.BeginDragSource(); source)
                {
                    const auto payload = std::as_bytes(std::span(descriptor.Name));
                    ui.SetDragPayload("KEIRE_UI_CUSTOM_CONTROL", payload);
                    ui.Text("Add " + descriptor.Name);
                }
                if (ui.LastItemState().Hovered)
                {
                    std::string tooltip = descriptor.Name;
                    for (const auto& attribute : descriptor.Attributes)
                        tooltip += "\n" + attribute.Name + ": " + attribute.Type;
                    ui.SetTooltip(tooltip, {.Delayed = true});
                }
            }
        }
        ui.Separator();
        ui.Text("Templates and slots");
        (void)ui.InputText("Visual Tree Asset ID", m_NewTemplateDraft);
        if (ui.Button("+ Template Instance"))
        {
            try
            {
                const auto asset = Keire::AssetId::Parse(m_NewTemplateDraft);
                if (!asset)
                    throw std::invalid_argument("Enter a non-zero .keireui asset ID for the template.");
                (void)document.AddTemplate(PreferredInsertionParent(document), asset);
                m_DraftElement = {};
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        (void)ui.InputText("Slot Name", m_NewSlotDraft);
        if (ui.Button("+ Slot"))
        {
            try
            {
                (void)document.AddSlot(PreferredInsertionParent(document), m_NewSlotDraft);
                m_DraftElement = {};
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
    }

    void UiBuilderPanel::DrawInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        const auto* element = document.Find(document.Selection());
        if (!element)
        {
            ui.Text("Select an element to edit it.");
            return;
        }
        ui.Text(std::string(UiBuilderElementTypeName(element->Type)) +
                (document.Selections().size() > 1 ? "  (" + std::to_string(document.Selections().size()) + " selected)"
                                                  : std::string{}));
        ui.TextColored(m_Controller.UiBuilderTheme().MutedText, element->StableId.ToString());
        ui.Separator();
        (void)ui.InputText("Name", m_NameDraft);
        (void)ui.InputText(document.Selections().size() > 1 ? "Classes (all selected)" : "Classes", m_ClassesDraft);
        (void)ui.InputText("Text", m_TextDraft);
        if (element->Type == Keire::UiVisualElementType::Custom)
            (void)ui.InputText("Custom Type", m_CustomTypeDraft);
        if (element->Type == Keire::UiVisualElementType::TemplateContainer)
            (void)ui.InputText("Template Asset ID", m_TemplateDraft);
        if (element->Type != Keire::UiVisualElementType::Slot)
            (void)ui.InputText("Slot Assignment", m_SlotDraft);
        (void)ui.InputTextMultiline("Inline Style", m_InlineStyleDraft, 5);

        ui.Separator();
        ui.Text("Bindings");
        ui.TextColored(m_Controller.UiBuilderTheme().MutedText,
                       "Target property <- source path. Target properties are unique per element.");
        for (std::size_t index = 0; index < element->Bindings.size(); ++index)
        {
            const auto& binding = element->Bindings[index];
            ui.Text(binding.Property + " <- " + binding.Path + "  [" + binding.Mode + "]");
            ui.SameLine();
            if (ui.Button("Remove##UiBinding" + std::to_string(index)))
            {
                auto bindings = element->Bindings;
                bindings.erase(bindings.begin() + static_cast<std::ptrdiff_t>(index));
                (void)document.SetBindings(element->StableId, std::move(bindings));
                m_DraftElement = {};
                return;
            }
        }
        (void)ui.InputText("Target Property", m_BindingPropertyDraft);
        (void)ui.InputText("Source Path", m_BindingPathDraft);
        if (auto combo = ui.BeginCombo("Binding Mode", m_BindingModeDraft); combo)
        {
            constexpr std::array modes{"OneWay", "TwoWay", "OneTime"};
            for (const auto mode : modes)
                if (ui.Selectable(mode, m_BindingModeDraft == mode))
                    m_BindingModeDraft = mode;
        }
        if (ui.Button("Add or Replace Binding"))
        {
            try
            {
                if (m_BindingPropertyDraft.empty() || m_BindingPathDraft.empty())
                    throw std::invalid_argument("A binding requires both a target property and source path.");
                auto bindings = element->Bindings;
                const auto found =
                    std::ranges::find(bindings, m_BindingPropertyDraft, &Keire::UiBindingDefinition::Property);
                const Keire::UiBindingDefinition binding{m_BindingPropertyDraft, m_BindingPathDraft,
                                                         m_BindingModeDraft};
                if (found == bindings.end())
                    bindings.push_back(binding);
                else
                    *found = binding;
                (void)document.SetBindings(element->StableId, std::move(bindings));
                m_BindingPropertyDraft.clear();
                m_BindingPathDraft.clear();
                m_DraftElement = {};
                return;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }

        ui.Separator();
        if (ui.Button("Apply Properties"))
        {
            try
            {
                auto candidate = document.Definition();
                auto find = [&](auto&& self,
                                Keire::UiVisualElementDefinition& current) -> Keire::UiVisualElementDefinition*
                {
                    if (current.StableId == element->StableId)
                        return &current;
                    for (auto& child : current.Children)
                        if (auto* result = self(self, child))
                            return result;
                    return nullptr;
                };
                auto* edited = find(find, candidate.Root);
                if (!edited)
                    throw std::runtime_error("The selected element no longer exists.");
                edited->Name = m_NameDraft;
                edited->CustomType = m_CustomTypeDraft;
                SetNamedValue(edited->Attributes, "text", m_TextDraft);
                edited->InlineStyles = ParseInlineStyles(m_InlineStyleDraft);
                edited->Slot = edited->Type == Keire::UiVisualElementType::Slot ? std::string{} : m_SlotDraft;
                if (edited->Type == Keire::UiVisualElementType::TemplateContainer)
                {
                    edited->Template = Keire::AssetId::Parse(m_TemplateDraft);
                    if (!edited->Template)
                        throw std::invalid_argument("A template container requires a non-zero visual-tree asset ID.");
                }
                const auto classes = SplitClasses(m_ClassesDraft);
                for (const auto selection : document.Selections())
                {
                    auto findSelection =
                        [&](auto&& self, Keire::UiVisualElementDefinition& current) -> Keire::UiVisualElementDefinition*
                    {
                        if (current.StableId == selection)
                            return &current;
                        for (auto& child : current.Children)
                            if (auto* result = self(self, child))
                                return result;
                        return nullptr;
                    };
                    if (auto* selected = findSelection(findSelection, candidate.Root))
                        selected->Classes = classes;
                }
                (void)document.Edit(document.Selections().size() > 1 ? "Edit UI elements" : "Edit UI element",
                                    std::move(candidate));
                m_Message = "Applied element properties.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
    }

    void UiBuilderPanel::RefreshDebuggerSnapshot()
    {
        auto& document = m_Controller.UiBuilderState();
        std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets;
        std::vector<const Keire::UiStyleSheetAsset*> styleSheetIdentities;
        const auto assets = m_Controller.UiBuilderAssets();
        if (assets)
        {
            for (const auto asset : document.Definition().StyleSheets)
            {
                const auto styleSheet =
                    assets->Load<Keire::UiStyleSheetAsset>(asset, Keire::AssetPriority::High).TryGetLoaded();
                if (!styleSheet)
                    continue;
                styleSheetIdentities.push_back(styleSheet.Get());
                styleSheets.push_back(styleSheet);
            }
        }
        std::vector<const Keire::UiVisualTreeAsset*> templateIdentities;
        auto templateResolver = CreateTemplateResolver(document.Definition(), templateIdentities);

        if (m_DebugSnapshot && m_DebugAsset == document.Asset() && m_DebugSelection == document.Selection() &&
            m_DebugGeneration == document.Generation() && m_DebugStyleSheets == styleSheetIdentities &&
            m_DebugTemplates == templateIdentities)
        {
            return;
        }

        m_DebugAsset = document.Asset();
        m_DebugSelection = document.Selection();
        m_DebugGeneration = document.Generation();
        m_DebugStyleSheets = std::move(styleSheetIdentities);
        m_DebugTemplates = std::move(templateIdentities);
        m_DebugSnapshot = BuildUiBuilderRuntimeDebugSnapshot(document.Definition(), document.Selection(), styleSheets,
                                                             1920.0F, 1080.0F, std::move(templateResolver));
    }

    void UiBuilderPanel::DrawDebugger(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        const auto asset = document.Asset();
        if (m_LiveDebugAsset != asset)
        {
            if (m_LivePicking && m_LiveDebugAsset)
                m_Controller.SetUiBuilderLivePicking(m_LiveDebugAsset, false);
            m_LiveDebugStore.Close();
            m_LiveDebugAsset = asset;
            m_LivePicking = false;
        }
        if (const auto picked = m_Controller.ConsumeUiBuilderLivePick(asset); picked)
        {
            m_LivePicking = false;
            if (document.Find(*picked))
            {
                document.Select(*picked);
                m_DraftElement = {};
                m_Message = "Selected the live UI element in the authored hierarchy.";
            }
            else
            {
                m_Message = "The picked element is instantiated by a template and is not authored in this document.";
            }
        }

        try
        {
            m_LiveDebugStore.Refresh(asset, m_Controller.CaptureUiBuilderLiveDebug(asset));
        }
        catch (const std::exception& error)
        {
            m_LiveDebugStore.Refresh(asset, {{}, error.what()});
        }

        ui.Text("Live Play source");
        const auto liveStatus = m_LiveDebugStore.Status();
        const auto& theme = m_Controller.UiBuilderTheme();
        if (liveStatus == UiBuilderLiveDebugStatus::Live)
            ui.TextColored(theme.Success, "LIVE");
        else if (liveStatus == UiBuilderLiveDebugStatus::Stale)
            ui.TextColored(theme.Warning, "STALE - preserving the last committed Play snapshot");
        else
            ui.TextColored(theme.MutedText, "UNAVAILABLE");
        if (!m_LiveDebugStore.Diagnostic().empty())
            ui.TextColored(liveStatus == UiBuilderLiveDebugStatus::Unavailable ? theme.MutedText : theme.Warning,
                           m_LiveDebugStore.Diagnostic());

        if (ui.Button(m_LivePicking ? "Stop Picking" : "Pick in Game View"))
        {
            m_LivePicking = !m_LivePicking;
            m_Controller.SetUiBuilderLivePicking(asset, m_LivePicking);
        }
        if (m_LivePicking)
            ui.TextColored(theme.Accent, "Click a UI Document element in the Game view.");

        if (const auto& live = m_LiveDebugStore.Current(); live)
        {
            ui.Text("Capture sequence: " + std::to_string(live->Sequence) +
                    "  | documents: " + std::to_string(live->Documents.size()));
            for (const auto& runtimeDocument : live->Documents)
            {
                ui.Separator();
                ui.Text("Runtime document " + runtimeDocument.Entity.ToString());
                ui.Text("Document generation: " + std::to_string(runtimeDocument.DocumentGeneration));
                const auto& statistics = runtimeDocument.PresentationStatistics;
                ui.TextColored(theme.MutedText, "Shared presentation-tree statistics");
                ui.Text("Elements: " + std::to_string(statistics.Elements) + "  | visible " +
                        std::to_string(statistics.VisibleElements) + "  | interactive " +
                        std::to_string(statistics.InteractableElements));
                ui.Text("Commands: " + std::to_string(statistics.DrawCommands) + "  | batches " +
                        std::to_string(statistics.DrawBatches) + "  | clipped " +
                        std::to_string(statistics.ClippedElements));
                ui.Text("Dirty: " + std::to_string(statistics.DirtyElements) + "  | layout passes " +
                        std::to_string(statistics.LayoutPasses) + "  | reused " +
                        std::to_string(statistics.ReusedLayoutPasses));
                ui.Text("Layout CPU: " + std::to_string(statistics.LayoutMilliseconds) + " ms");
                ui.Text("Focus: " + (runtimeDocument.FocusedElement ? runtimeDocument.FocusedElement->ToString()
                                                                    : std::string("none")));
                const auto captureLabel = [&](const std::size_t index)
                {
                    if (runtimeDocument.CapturedElements[index])
                        return runtimeDocument.CapturedElements[index]->ToString();
                    return runtimeDocument.PresentationPointerCaptures[index] ? std::string("presentation")
                                                                              : std::string("none");
                };
                ui.Text("Pointer capture: primary " + captureLabel(0) + "  | secondary " + captureLabel(1) +
                        "  | middle " + captureLabel(2));
                if (!live->ElementPointerCaptureAvailable)
                    ui.TextColored(theme.MutedText,
                                   "Element-level pointer capture is unavailable; presentation capture is exact.");

                ui.Text("Event propagation trace");
                if (runtimeDocument.EventTrace.empty())
                    ui.TextColored(theme.MutedText, "No dispatched event routes in the bounded history.");
                for (const auto& event : runtimeDocument.EventTrace)
                {
                    const auto phase = event.PropagationPhase == UiBuilderLiveDebugEvent::Phase::Capture  ? "capture"
                                       : event.PropagationPhase == UiBuilderLiveDebugEvent::Phase::Bubble ? "bubble"
                                                                                                          : "target";
                    ui.Text("#" + std::to_string(event.Sequence) + "  " + phase + "  " +
                            std::string(RuntimeUiEventName(event.Type)) + "  target " +
                            (event.Target ? event.Target->ToString() : std::string("unmapped")) + "  current " +
                            (event.CurrentTarget ? event.CurrentTarget->ToString() : std::string("unmapped")));
                }
                ui.Text("Pending target queue");
                if (runtimeDocument.PendingTargetEvents.empty())
                    ui.TextColored(theme.MutedText, "No pending target events.");
                for (const auto& event : runtimeDocument.PendingTargetEvents)
                {
                    const auto phase = event.PropagationPhase == UiBuilderLiveDebugEvent::Phase::Capture  ? "capture"
                                       : event.PropagationPhase == UiBuilderLiveDebugEvent::Phase::Bubble ? "bubble"
                                                                                                          : "target";
                    ui.Text(std::string(phase) + "  " + std::string(RuntimeUiEventName(event.Type)) + " -> " +
                            (event.Target ? event.Target->ToString() : std::string("unmapped target")));
                }
                if (!live->EventPropagationTraceAvailable)
                    ui.TextColored(theme.MutedText, "Capture/target/bubble dispatch history is unavailable.");
            }

            const UiBuilderLiveDebugElement* liveSelection = nullptr;
            for (const auto& runtimeDocument : live->Documents)
            {
                const auto found = std::ranges::find_if(runtimeDocument.Elements, [&](const auto& element)
                                                        { return element.StableId == document.Selection(); });
                if (found != runtimeDocument.Elements.end())
                {
                    liveSelection = &*found;
                    break;
                }
            }
            if (liveSelection)
            {
                const auto& state = liveSelection->State;
                ui.Text("Selected live layout: x " + std::to_string(state.Rect.X) + "  y " +
                        std::to_string(state.Rect.Y) + "  w " + std::to_string(state.Rect.Width) + "  h " +
                        std::to_string(state.Rect.Height));
                ui.Text("Selected live style: " + std::to_string(state.Style.Width) + " x " +
                        std::to_string(state.Style.Height) + "  | opacity " + std::to_string(state.Style.Opacity));
                ui.Text(std::string("Selected live state: ") + (state.Focused ? "focused" : "unfocused") +
                        (state.Hovered ? ", hovered" : ", not hovered") +
                        (state.Pressed ? ", pressed" : ", not pressed"));
            }

            ui.Separator();
            ui.Text("GPU/UI resources");
            ui.Text("Vertices: " + OptionalCount(live->VertexCount) + "  | atlas textures: " +
                    OptionalCount(live->AtlasTextureCount) + "  | atlas bytes: " + OptionalCount(live->AtlasBytes));
            ui.Text("Style CPU: " + OptionalMilliseconds(live->StyleMilliseconds) +
                    "  | layout CPU: " + OptionalMilliseconds(live->LayoutMilliseconds) +
                    "  | repaint CPU: " + OptionalMilliseconds(live->RepaintMilliseconds));
            if (live->DirtyReasonsAvailable)
                for (const auto& reason : live->DirtyReasons)
                    ui.Text("Dirty: " + reason);
            else
                ui.TextColored(theme.MutedText, "Per-element dirty reasons are unavailable; dirty counters are exact.");
            if (live->SelectorPrecedenceAvailable)
                for (const auto& selector : live->SelectorPrecedence)
                    ui.Text(selector);
            else
                ui.TextColored(theme.MutedText,
                               "Runtime selector-precedence chains are unavailable; matched live resolved style "
                               "values remain available above.");
        }
        ui.Separator();

        const auto* selected = document.Find(document.Selection());
        if (!selected)
        {
            ui.Text("Select an element to inspect its runtime state.");
            return;
        }

        try
        {
            RefreshDebuggerSnapshot();
        }
        catch (const std::exception& error)
        {
            m_DebugSnapshot.reset();
            ui.TextColored(m_Controller.UiBuilderTheme().Error, std::string("Runtime preview failed: ") + error.what());
            return;
        }
        if (!m_DebugSnapshot)
            return;

        const auto& snapshot = *m_DebugSnapshot;
        ui.Text("Selected authored source");
        ui.TextColored(m_Controller.UiBuilderTheme().Accent,
                       std::string(UiBuilderElementTypeName(selected->Type)) + "  #" + selected->Name);
        ui.Text("Stable ID: " + selected->StableId.ToString());
        ui.Text("Classes: " + (selected->Classes.empty() ? std::string("(none)") : JoinClasses(selected->Classes)));
        ui.Text("Inline declarations: " + std::to_string(selected->InlineStyles.size()));
        ui.Separator();

        ui.Text("Authoring preview style and layout");
        if (snapshot.SelectedState)
        {
            const auto& state = *snapshot.SelectedState;
            ui.Text("Layout box: x " + std::to_string(state.Rect.X) + "  y " + std::to_string(state.Rect.Y) + "  w " +
                    std::to_string(state.Rect.Width) + "  h " + std::to_string(state.Rect.Height));
            ui.Text("Clip box: x " + std::to_string(state.ClipRect.X) + "  y " + std::to_string(state.ClipRect.Y) +
                    "  w " + std::to_string(state.ClipRect.Width) + "  h " + std::to_string(state.ClipRect.Height));
            ui.Text("Size: " + std::to_string(state.Style.Width) + " x " + std::to_string(state.Style.Height) +
                    "  | opacity " + std::to_string(state.Style.Opacity));
            ui.Text("Flex grow: " + std::to_string(state.Style.FlexGrow) + "  | gap " +
                    std::to_string(state.Style.Gap) + "  | font " + std::to_string(state.Style.FontSize));
            ui.Text(std::string("State: ") + (state.Visible ? "visible" : "hidden") +
                    (state.Enabled ? ", enabled" : ", disabled") +
                    (state.Interactable ? ", interactable" : ", non-interactable"));
        }
        else
        {
            ui.TextColored(m_Controller.UiBuilderTheme().Warning,
                           "The selected stable ID did not map into the runtime visual tree.");
        }
        ui.Separator();

        const auto& statistics = snapshot.Statistics;
        ui.Text("Authoring preview profiler snapshot");
        ui.Text("Elements: " + std::to_string(statistics.Elements) + "  | visible " +
                std::to_string(statistics.VisibleElements) + "  | interactive " +
                std::to_string(statistics.InteractableElements));
        ui.Text("Draw commands: " + std::to_string(statistics.DrawCommands) + "  | batches " +
                std::to_string(statistics.DrawBatches) + "  | clipped " + std::to_string(statistics.ClippedElements));
        ui.Text("Dirty elements: " + std::to_string(statistics.DirtyElements) + "  | layout passes " +
                std::to_string(statistics.LayoutPasses) + "  | reused " +
                std::to_string(statistics.ReusedLayoutPasses));
        ui.Text("Layout CPU: " + std::to_string(statistics.LayoutMilliseconds) + " ms  | generation " +
                std::to_string(statistics.Generation));
        ui.Text(std::string("Authoring source: ") + (document.Dirty() ? "modified" : "saved"));
        ui.Text("Style sources: " + std::to_string(snapshot.ResolvedStyleSheets) + "/" +
                std::to_string(snapshot.LinkedStyleSheets) + " linked sheets resolved, " +
                std::to_string(snapshot.InlineStyleProperties) + " inline declarations");
        if (snapshot.ResolvedStyleSheets != snapshot.LinkedStyleSheets)
            ui.TextColored(m_Controller.UiBuilderTheme().Warning,
                           "Some linked style sheets are not loaded; this snapshot preserves their asset links.");
    }

    void UiBuilderPanel::DrawSource(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        if (m_SourceAsset != document.Asset() || !m_SourceEditing)
        {
            m_SourceAsset = document.Asset();
            m_SourceDraft = document.SourcePreview();
        }
        (void)ui.InputTextMultiline("##UiBuilderSource", m_SourceDraft, 28);
        m_SourceEditing = ui.LastItemState().Active || ui.LastItemState().Edited;
        if (ui.Button("Apply Source"))
        {
            std::string diagnostic;
            if (document.ApplySource(std::as_bytes(std::span(m_SourceDraft)), diagnostic))
            {
                m_SourceEditing = false;
                m_DraftElement = {};
                m_Message = "Applied and validated UI source.";
            }
            else if (!diagnostic.empty())
            {
                m_Message = std::move(diagnostic);
                m_Controller.ReportUiBuilderError(m_Message);
            }
        }
        ui.SameLine();
        if (ui.Button("Reset Source"))
        {
            m_SourceDraft = document.SourcePreview();
            m_SourceEditing = false;
        }
    }
} // namespace KeireEditor
