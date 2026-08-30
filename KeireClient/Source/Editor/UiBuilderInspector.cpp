#include "KeireClient/Editor/UiBuilderInspector.h"

#include "KeireClient/Editor/EditorPanels.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Ui.h"

namespace KeireEditor
{
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
