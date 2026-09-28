#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Ui/UiToolkit.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class UiFrame;
    struct UiThemeDefinition;
} // namespace Keire

namespace KeireEditor
{
    class IInspectorController;

    struct UiBuilderInspectorEdit final
    {
        std::optional<std::string> Name;
        std::optional<std::string> Text;
        std::optional<std::string> CustomType;
        std::optional<std::string> Slot;
        std::optional<Keire::AssetId> Template;
        std::optional<std::vector<std::string>> Classes;
        std::optional<std::vector<Keire::UiNamedValue>> InlineStyles;
    };

    [[nodiscard]] Keire::UiVisualTreeDefinition ApplyUiBuilderInspectorEdit(Keire::UiVisualTreeDefinition definition,
                                                                            Keire::AssetId primary,
                                                                            std::span<const Keire::AssetId> selection,
                                                                            const UiBuilderInspectorEdit& edit);

    [[nodiscard]] float UiDocumentInspectorActionsHeight(const Keire::Ref<Keire::Component>& component) noexcept;
    void DrawUiDocumentInspectorActions(Keire::UiFrame& ui, const Keire::Ref<Keire::Component>& component,
                                        IInspectorController& controller, const Keire::UiThemeDefinition& theme);
} // namespace KeireEditor
