#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <cmath>

namespace KeireEditor
{
    NodeGraphCanvasDetail StableNodeGraphCanvas::DetailForZoom(const float zoom) noexcept
    {
        const float boundedZoom = std::isfinite(zoom) ? std::clamp(zoom, 0.35F, 2.5F) : 1.0F;
        return {.NodeSubtitle = boundedZoom >= 0.65F,
                .BlockLabels = boundedZoom >= 0.5F,
                .PinLabels = boundedZoom >= 0.5F,
                .ConnectionLabels = boundedZoom >= 0.55F};
    }

    NodeGraphCanvasResult StableNodeGraphCanvas::Draw(Keire::UiFrame& ui, const std::string_view id,
                                                      const std::span<NodeGraphNode> nodes,
                                                      const std::span<const NodeGraphConnection> connections,
                                                      const bool editable)
    {
        return Draw(ui, id, nodes, connections,
                    NodeGraphCanvasOptions{.Editable = editable, .InteractiveConnections = editable});
    }
} // namespace KeireEditor
