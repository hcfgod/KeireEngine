#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Keire::RenderBackend
{
    void RenderSharedState::PublishGpuOcclusionReadbackStatistics()
    {
        Statistics.GpuOcclusionCandidates = 0;
        Statistics.GpuOcclusionVisible = 0;
        Statistics.GpuOcclusionCulled = 0;
        Statistics.GpuOcclusionCandidateTriangles = 0;
        Statistics.GpuOcclusionCulledTriangles = 0;
        Statistics.GpuOcclusionReadbackAge = 0;
        Statistics.GpuOcclusionReadbackValid = false;
        for (const auto& surface : LiveSurfaces())
        {
            auto& diagnostics = surface->GpuOcclusionDiagnostics;
            if (diagnostics.State == GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid)
            {
                const auto age =
                    Statistics.Frame >= diagnostics.SourceFrame ? Statistics.Frame - diagnostics.SourceFrame : 0U;
                diagnostics.ReadbackAge = age > std::numeric_limits<std::uint32_t>::max()
                                              ? std::numeric_limits<std::uint32_t>::max()
                                              : static_cast<std::uint32_t>(age);
                Statistics.GpuOcclusionCandidates += diagnostics.Candidates;
                Statistics.GpuOcclusionVisible += diagnostics.Visible;
                Statistics.GpuOcclusionCulled += diagnostics.Culled;
                Statistics.GpuOcclusionCandidateTriangles += surface->GpuOcclusionLatestCandidateTriangles;
                Statistics.GpuOcclusionCulledTriangles += surface->GpuOcclusionLatestCandidateTriangles -
                                                          std::min(surface->GpuOcclusionLatestCandidateTriangles,
                                                                   surface->GpuOcclusionLatestVisibleTriangles);
                Statistics.GpuOcclusionReadbackAge =
                    std::max(Statistics.GpuOcclusionReadbackAge, diagnostics.ReadbackAge);
                Statistics.GpuOcclusionReadbackValid = true;
            }
            surface->PublishGpuOcclusionDiagnosticsSnapshot();
        }
        if (!Statistics.GpuOcclusionReadbackValid)
            Statistics.GpuOcclusionReadbackAge = std::numeric_limits<std::uint32_t>::max();
    }
} // namespace Keire::RenderBackend
