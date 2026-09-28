#include "KeireClient/Editor/UiBuilderInspector.h"

#include "KeireClient/Editor/EditorPanels.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Ui.h"

#include <algorithm>
#include <stdexcept>

namespace KeireEditor
{
    Keire::UiVisualTreeDefinition ApplyUiBuilderInspectorEdit(Keire::UiVisualTreeDefinition definition,
                                                              const Keire::AssetId primary,
                                                              const std::span<const Keire::AssetId> selection,
                                                              const UiBuilderInspectorEdit& edit)
    {
        auto find = [](auto&& self, Keire::UiVisualElementDefinition& current,
                       const Keire::AssetId id) -> Keire::UiVisualElementDefinition*
        {
            if (current.StableId == id)
                return &current;
            for (auto& child : current.Children)
                if (auto* result = self(self, child, id))
                    return result;
            return nullptr;
        };
        auto* element = find(find, definition.Root, primary);
        if (!element)
            throw std::invalid_argument("The selected element no longer exists.");
        if (edit.Name)
            element->Name = *edit.Name;
        if (edit.Text)
        {
            const auto found = std::ranges::find(element->Attributes, "text", &Keire::UiNamedValue::Name);
            if (edit.Text->empty())
            {
                if (found != element->Attributes.end())
                    element->Attributes.erase(found);
            }
            else if (found != element->Attributes.end())
                found->Value = *edit.Text;
            else
                element->Attributes.push_back({"text", *edit.Text});
        }
        if (edit.CustomType)
            element->CustomType = *edit.CustomType;
        if (edit.Slot)
            element->Slot = *edit.Slot;
        if (edit.Template)
        {
            if (!*edit.Template)
                throw std::invalid_argument("A template container requires a non-zero visual-tree asset ID.");
            element->Template = *edit.Template;
        }
        if (edit.InlineStyles)
            element->InlineStyles = *edit.InlineStyles;
        if (edit.Classes)
            for (const auto id : selection)
                if (auto* selected = find(find, definition.Root, id))
                    selected->Classes = *edit.Classes;
        return definition;
    }

    float UiDocumentInspectorActionsHeight(const Keire::Ref<Keire::Component>& component) noexcept
    {
        return Keire::DynamicRefCast<Keire::UiDocumentComponent>(component) ? 38.0F : 0.0F;
    }

    void DrawUiDocumentInspectorActions(Keire::UiFrame& ui, const Keire::Ref<Keire::Component>& component,
                                        IInspectorController& controller, const Keire::UiThemeDefinition& theme)
    {
        const auto document = Keire::DynamicRefCast<Keire::UiDocumentComponent>(component);
        if (!document)
            return;
        if (auto disabled = ui.BeginDisabled(!document->VisualTree()); disabled)
            if (ui.Button("Open Visual Tree in UI Builder"))
                controller.OpenInspectorUiDocument(document->VisualTree());
        if (!document->VisualTree())
        {
            ui.SameLine();
            ui.TextColored(theme.MutedText, "Assign a .keireui asset first.");
        }
    }
} // namespace KeireEditor
