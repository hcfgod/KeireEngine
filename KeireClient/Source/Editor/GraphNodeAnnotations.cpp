#include "KeireClient/Editor/GraphComments.h"

#include <algorithm>

namespace KeireEditor
{
    void DrawNodeGraphAnnotation(Keire::UiFrame& ui, const NodeGraphNode& node, const Keire::UiItemRect rectangle,
                                 const float zoom)
    {
        if (node.Comment.empty())
            return;
        const float font = node.CommentPinned ? 11.0F : std::clamp(11.0F * zoom, 8.0F, 13.0F);
        const auto measured = ui.MeasureText(node.Comment, font);
        const float padding = node.CommentPinned ? 7.0F : std::clamp(7.0F * zoom, 4.0F, 9.0F);
        const float width = std::min(std::max(measured.Width + padding * 2.0F, 64.0F), 280.0F);
        const float height = measured.Height + padding * 2.0F;
        const Keire::UiItemRect bubble{{rectangle.Minimum.X, rectangle.Minimum.Y - height - 7.0F},
                                       {rectangle.Minimum.X + width, rectangle.Minimum.Y - 7.0F}};
        ui.DrawFilledRectangle(bubble, {0.075F, 0.085F, 0.105F, 0.96F}, 5.0F);
        ui.DrawRectangle(bubble, {0.42F, 0.5F, 0.65F, 0.92F}, 1.0F, 5.0F);
        ui.DrawOverlayText({bubble.Minimum.X + padding, bubble.Minimum.Y + padding}, {0.9F, 0.93F, 0.98F, 1.0F},
                           node.Comment, font, bubble);
    }
} // namespace KeireEditor
