#pragma once

#include "Keire/Ui/RuntimeUi.h"
#include "KeireInternal/Rendering/RenderFramePacketInternal.h"
#include "KeireInternal/Rendering/RenderShaderDataInternal.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    struct RuntimeUiGeometryBatch final
    {
        AssetId Asset;
        RuntimeUiRect ClipRect;
        std::uint32_t FirstVertex = 0;
        std::uint32_t VertexCount = 0;
    };

    struct RuntimeUiGeometry final
    {
        std::vector<RuntimeUiVertex> Vertices;
        std::vector<RuntimeUiGeometryBatch> Batches;
    };

    [[nodiscard]] Color EvaluateRuntimeUiGradient(const RuntimeUiGradient& gradient, Vector2 normalizedPosition);
    [[nodiscard]] float RuntimeUiRoundedCoverage(RuntimeUiRect rectangle, float radius, Vector2 position) noexcept;
    void AccumulateRuntimeUiGeometryStatistics(RuntimeUiRendererStatistics& statistics,
                                               const RuntimeUiGeometry& geometry) noexcept;
    [[nodiscard]] RuntimeUiGeometry BuildRuntimeUiGeometry(std::span<const RuntimeUiDrawCommand> commands);
    [[nodiscard]] RuntimeUiGeometry BuildRuntimeUiWorldGeometry(const CapturedRuntimeUiWorldPanel& panel,
                                                                std::uint32_t width, std::uint32_t height);
} // namespace Keire::RenderBackend
