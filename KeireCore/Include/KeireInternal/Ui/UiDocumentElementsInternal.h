#pragma once

#include "Keire/Ui/UiElements.h"
#include "Keire/Ui/UiToolkit.h"

namespace Keire::Detail
{
    [[nodiscard]] Ref<Ui::VisualElement> CreateUiDocumentVisualElement(const UiVisualElementDefinition& definition);
    [[nodiscard]] RuntimeUiElementType UiDocumentRuntimeType(UiVisualElementType type) noexcept;
    [[nodiscard]] RuntimeUiElementType UiDocumentRuntimeType(const Ui::VisualElement& element,
                                                             UiVisualElementType fallback) noexcept;
} // namespace Keire::Detail
