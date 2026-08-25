#pragma once

#include <compare>
#include <cstdint>

namespace Keire::Internal
{
    enum class GraphicsCaptureProvider : std::uint8_t
    {
        None,
        RenderDoc
    };

    enum class GraphicsCaptureState : std::uint8_t
    {
        Unavailable,
        Ready,
        Capturing
    };

    enum class GraphicsCaptureRequestResult : std::uint8_t
    {
        Unavailable,
        CaptureAlreadyActive,
        Queued
    };

    struct GraphicsCaptureStatus
    {
        GraphicsCaptureProvider Provider = GraphicsCaptureProvider::None;
        GraphicsCaptureState State = GraphicsCaptureState::Unavailable;

        [[nodiscard]] bool Available() const noexcept { return State != GraphicsCaptureState::Unavailable; }

        auto operator<=>(const GraphicsCaptureStatus&) const noexcept = default;
    };

    // These calls only inspect an already-injected capture module. They never load a library or launch a tool.
    [[nodiscard]] GraphicsCaptureStatus QueryGraphicsCaptureStatus() noexcept;
    [[nodiscard]] GraphicsCaptureRequestResult QueueGraphicsCaptureNextFrame() noexcept;
} // namespace Keire::Internal
