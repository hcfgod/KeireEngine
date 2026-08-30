#pragma once

#include "Keire/ECS/Component.h"

namespace Keire
{
    class UiFrame;
    struct UiThemeDefinition;
} // namespace Keire

namespace KeireEditor
{
    class IInspectorController;

    [[nodiscard]] float UiDocumentInspectorActionsHeight(const Keire::Ref<Keire::Component>& component) noexcept;
    void DrawUiDocumentInspectorActions(Keire::UiFrame& ui, const Keire::Ref<Keire::Component>& component,
                                        IInspectorController& controller, const Keire::UiThemeDefinition& theme);
} // namespace KeireEditor
