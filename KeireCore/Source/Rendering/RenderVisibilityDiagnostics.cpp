#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>

namespace Keire::RenderBackend
{
    void RenderSharedState::FinalizeGpuOcclusionConsumerEvidence(const RenderSurfaceState& surface,
                                                                 const SceneRenderPacket& packet,
                                                                 const PreparedGpuOcclusion& occlusion)
    {
        if (!occlusion.Enabled || !occlusion.Resources)
            return;
        const auto pending = std::ranges::find_if(
            FrameGpuOcclusionReadbacks.rbegin(), FrameGpuOcclusionReadbacks.rend(),
            [&](const GpuOcclusionPendingReadback& candidate)
            { return candidate.SurfaceId == surface.Id && candidate.SourceFrame == packet.AcceptedFrameId; });
        if (pending == FrameGpuOcclusionReadbacks.rend())
            return;

        const auto& visibility = surface.ActiveWorkset().GpuOcclusion;
        const auto& forwardPlus = surface.ActiveWorkset().ForwardPlus;
        const auto& gpuVfx = surface.ActiveWorkset().GpuVfx;
        const auto& spatial = surface.ActiveWorkset().SpatialSelection;
        const auto owned = [&](const auto& resources)
        {
            return resources.OwnedBy(pending->SourceFrame, pending->SourceFrameSlot, pending->SourceSurfaceEpoch,
                                     pending->SourceDeviceGeneration);
        };
        if (!owned(visibility))
            return;

        pending->LocalLightMaskConsumed = owned(forwardPlus) && forwardPlus.VisibilityCompacted &&
                                          visibility.LocalLightVisibilityCount == pending->LocalLightCandidates;
        pending->VfxMaskedDraws = owned(gpuVfx) ? gpuVfx.ConsumedDraws : 0U;
        pending->VfxMaskConsumed = pending->VfxMaskEntries != 0U && owned(gpuVfx) && !gpuVfx.Outputs.empty() &&
                                   gpuVfx.ConsumedDraws == gpuVfx.Outputs.size() &&
                                   visibility.VfxVisibilityCount == pending->VfxMaskEntries;
        pending->SpatialSelectionRecords = owned(spatial) ? spatial.RecordCount : 0U;
        pending->SpatialSelectionDraws = owned(spatial) ? spatial.ConsumedDraws : 0U;
        pending->SpatialMaskConsumed =
            pending->SpatialMaskEntries != 0U && owned(spatial) && spatial.DispatchSucceeded &&
            spatial.SpatialMaskCount == pending->SpatialMaskEntries && spatial.ConsumedDraws != 0U &&
            visibility.SpatialVolumeVisibilityCount == pending->SpatialMaskEntries;
    }
} // namespace Keire::RenderBackend
