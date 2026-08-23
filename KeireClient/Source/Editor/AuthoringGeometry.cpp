#include "KeireClient/Editor/AuthoringGeometry.h"

#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>

namespace KeireEditor::Detail
{
    Keire::UiPosition Add(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y};
    }

    Keire::UiPosition Subtract(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y};
    }

    Keire::UiPosition Scale(const Keire::UiPosition value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale};
    }

    Keire::UiColor ScaleColor(const Keire::UiColor color, const float scale, const float alpha) noexcept
    {
        return {std::clamp(color.Red * scale, 0.0F, 1.0F), std::clamp(color.Green * scale, 0.0F, 1.0F),
                std::clamp(color.Blue * scale, 0.0F, 1.0F), alpha};
    }
} // namespace KeireEditor::Detail

namespace KeireEditor
{
    Keire::UiPosition StableNodeGraphCanvas::ToScreen(const Keire::Vector2 position,
                                                      const Keire::UiItemRect canvas) const noexcept
    {
        return {canvas.Minimum.X + (position.X + m_Pan.X) * m_Zoom, canvas.Minimum.Y + (position.Y + m_Pan.Y) * m_Zoom};
    }

    Keire::Vector2 StableNodeGraphCanvas::ToGraph(const Keire::UiPosition position,
                                                  const Keire::UiItemRect canvas) const noexcept
    {
        return {(position.X - canvas.Minimum.X) / m_Zoom - m_Pan.X, (position.Y - canvas.Minimum.Y) / m_Zoom - m_Pan.Y};
    }
} // namespace KeireEditor
