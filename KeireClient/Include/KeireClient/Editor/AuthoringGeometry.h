#pragma once

#include "Keire/Core.h"

namespace KeireEditor::Detail
{
    [[nodiscard]] Keire::UiPosition Add(Keire::UiPosition left, Keire::UiPosition right) noexcept;
    [[nodiscard]] Keire::UiPosition Subtract(Keire::UiPosition left, Keire::UiPosition right) noexcept;
    [[nodiscard]] Keire::UiPosition Scale(Keire::UiPosition value, float scale) noexcept;
    [[nodiscard]] Keire::UiColor ScaleColor(Keire::UiColor color, float scale, float alpha) noexcept;
} // namespace KeireEditor::Detail
