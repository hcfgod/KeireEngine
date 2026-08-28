#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/GpuOcclusionPolicyInternal.h"
#include "KeireInternal/Rendering/RenderFramePacketInternal.h"
#include "KeireInternal/Rendering/SpatialLightingInternal.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Keire::RenderBackend
{
    struct GpuOcclusionBuffer final
    {
        SDL_GPUBuffer* Buffer = nullptr;
        std::uint32_t CapacityBytes = 0;
    };

    struct GpuOcclusionFrameResources final
    {
        SDL_GPUTexture* Depth = nullptr;
        std::vector<SDL_GPUTexture*> Pyramid;
        GpuOcclusionBuffer Candidates;
        GpuOcclusionBuffer InputInstances;
        GpuOcclusionBuffer GeometryVisibility;
        GpuOcclusionBuffer VfxVisibilityMask;
        GpuOcclusionBuffer LocalLightVisibilityMask;
        GpuOcclusionBuffer SpatialVolumeVisibilityMask;
        GpuOcclusionBuffer LocalOffsets;
        GpuOcclusionBuffer Chunks;
        GpuOcclusionBuffer ChunkCounts;
        GpuOcclusionBuffer Batches;
        GpuOcclusionBuffer ChunkOffsets;
        GpuOcclusionBuffer VisibleInstances;
        GpuOcclusionBuffer IndirectArguments;
        GpuOcclusionBuffer Status;
        SDL_GPUTransferBuffer* Upload = nullptr;
        SDL_GPUTransferBuffer* Readback = nullptr;
        std::uint32_t UploadCapacityBytes = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint64_t FrameId = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = 0;
        std::uint32_t GeometryVisibilityCount = 0;
        std::uint32_t VfxVisibilityCount = 0;
        std::uint32_t LocalLightVisibilityCount = 0;
        std::uint32_t SpatialVolumeVisibilityCount = 0;
        bool OwnershipValid = false;

        [[nodiscard]] bool OwnedBy(const std::uint64_t frameId, const std::uint32_t frameSlot,
                                   const std::uint64_t surfaceEpoch,
                                   const std::uint32_t deviceGeneration) const noexcept
        {
            return OwnershipValid && FrameId == frameId && FrameSlot == frameSlot && SurfaceEpoch == surfaceEpoch &&
                   DeviceGeneration == deviceGeneration;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return !Depth && Pyramid.empty() && !Candidates.Buffer && !InputInstances.Buffer &&
                   !GeometryVisibility.Buffer && !VfxVisibilityMask.Buffer && !LocalLightVisibilityMask.Buffer &&
                   !SpatialVolumeVisibilityMask.Buffer && !LocalOffsets.Buffer && !Chunks.Buffer &&
                   !ChunkCounts.Buffer && !Batches.Buffer && !ChunkOffsets.Buffer && !VisibleInstances.Buffer &&
                   !IndirectArguments.Buffer && !Status.Buffer && !Upload && !Readback;
        }
    };

    struct SurfaceResources final
    {
        // Index zero is the published output. Indices 1..N are writer outputs owned by accepted frame slots 0..N-1.
        std::vector<SDL_GPUTexture*> FinalOutputs;
        std::vector<struct SurfaceFrameWorkset> Worksets;

        [[nodiscard]] bool Empty() const noexcept;

        [[nodiscard]] SDL_GPUTexture* PublishedColor() const noexcept
        {
            return FinalOutputs.empty() ? nullptr : FinalOutputs.front();
        }

        [[nodiscard]] SDL_GPUTexture* WriterColor(const std::uint32_t frameSlot) const noexcept
        {
            const auto index = static_cast<std::size_t>(frameSlot) + 1U;
            return index < FinalOutputs.size() ? FinalOutputs[index] : nullptr;
        }
    };

    struct ForwardPlusGpuResources final
    {
        SDL_GPUBuffer* Lights = nullptr;
        SDL_GPUBuffer* Tiles = nullptr;
        SDL_GPUBuffer* LightIndices = nullptr;
        std::uint32_t Columns = 0;
        std::uint32_t Rows = 0;
        std::uint32_t LightCapacityBytes = 0;
        std::uint32_t TileCapacityBytes = 0;
        std::uint32_t LightIndexCapacityBytes = 0;
        std::uint64_t FrameId = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = 0;
        bool VisibilityCompacted = false;
        bool OwnershipValid = false;

        void TakeOwnership(const std::uint64_t frameId, const std::uint32_t frameSlot, const std::uint64_t surfaceEpoch,
                           const std::uint32_t deviceGeneration) noexcept
        {
            FrameId = frameId;
            FrameSlot = frameSlot;
            SurfaceEpoch = surfaceEpoch;
            DeviceGeneration = deviceGeneration;
            OwnershipValid = true;
        }

        [[nodiscard]] bool OwnedBy(const std::uint64_t frameId, const std::uint32_t frameSlot,
                                   const std::uint64_t surfaceEpoch,
                                   const std::uint32_t deviceGeneration) const noexcept
        {
            return OwnershipValid && FrameId == frameId && FrameSlot == frameSlot && SurfaceEpoch == surfaceEpoch &&
                   DeviceGeneration == deviceGeneration;
        }

        [[nodiscard]] bool Empty() const noexcept { return !Lights && !Tiles && !LightIndices; }
    };

    struct DynamicGpuBuffer final
    {
        SDL_GPUBuffer* Buffer = nullptr;
        std::uint32_t CapacityBytes = 0;
    };

    struct SurfaceDynamicUploadResources final
    {
        std::vector<DynamicGpuBuffer> InstanceBatches;
        SDL_GPUTransferBuffer* InstanceTransfer = nullptr;
        std::uint32_t InstanceTransferCapacityBytes = 0;
        DynamicGpuBuffer CpuVfxVertices;
        SDL_GPUTransferBuffer* CpuVfxTransfer = nullptr;

        [[nodiscard]] bool Empty() const noexcept
        {
            return InstanceBatches.empty() && !InstanceTransfer && !CpuVfxVertices.Buffer && !CpuVfxTransfer;
        }
    };

    struct GpuSpatialSelectionFrameResources final
    {
        DynamicGpuBuffer Draws;
        DynamicGpuBuffer ReflectionCandidates;
        DynamicGpuBuffer LightProbeCandidates;
        DynamicGpuBuffer OutputRecords;
        SDL_GPUTransferBuffer* Upload = nullptr;
        std::uint32_t UploadCapacityBytes = 0;
        std::uint32_t RecordCount = 0;
        std::uint32_t SpatialMaskCount = 0;
        std::uint32_t ConsumedDraws = 0;
        std::uint64_t FrameId = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = 0;
        bool DispatchSucceeded = false;
        bool OwnershipValid = false;

        void TakeOwnership(const std::uint64_t frameId, const std::uint32_t frameSlot, const std::uint64_t surfaceEpoch,
                           const std::uint32_t deviceGeneration) noexcept
        {
            FrameId = frameId;
            FrameSlot = frameSlot;
            SurfaceEpoch = surfaceEpoch;
            DeviceGeneration = deviceGeneration;
            OwnershipValid = true;
            DispatchSucceeded = false;
            ConsumedDraws = 0;
        }

        [[nodiscard]] bool OwnedBy(const std::uint64_t frameId, const std::uint32_t frameSlot,
                                   const std::uint64_t surfaceEpoch,
                                   const std::uint32_t deviceGeneration) const noexcept
        {
            return OwnershipValid && FrameId == frameId && FrameSlot == frameSlot && SurfaceEpoch == surfaceEpoch &&
                   DeviceGeneration == deviceGeneration;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return !Draws.Buffer && !ReflectionCandidates.Buffer && !LightProbeCandidates.Buffer &&
                   !OutputRecords.Buffer && !Upload;
        }
    };

    struct GpuVfxFrameOutput final
    {
        std::uint64_t WorldId = 0;
        std::uint32_t HandleIndex = 0;
        std::uint32_t HandleGeneration = 0;
        std::uint32_t Renderer = 0;
        SDL_GPUBuffer* Indices = nullptr;
        SDL_GPUBuffer* IndirectArguments = nullptr;
        SDL_GPUBuffer* Instances = nullptr;
        std::uint32_t Capacity = 0;

        [[nodiscard]] bool Empty() const noexcept { return !Indices && !IndirectArguments && !Instances; }
    };

    struct GpuVfxFrameResources final
    {
        std::vector<GpuVfxFrameOutput> Outputs;
        std::uint64_t FrameId = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = 0;
        std::uint32_t ConsumedDraws = 0;
        bool OwnershipValid = false;

        void TakeOwnership(const std::uint64_t frameId, const std::uint32_t frameSlot, const std::uint64_t surfaceEpoch,
                           const std::uint32_t deviceGeneration) noexcept
        {
            FrameId = frameId;
            FrameSlot = frameSlot;
            SurfaceEpoch = surfaceEpoch;
            DeviceGeneration = deviceGeneration;
            OwnershipValid = true;
            ConsumedDraws = 0;
        }

        [[nodiscard]] bool OwnedBy(const std::uint64_t frameId, const std::uint32_t frameSlot,
                                   const std::uint64_t surfaceEpoch,
                                   const std::uint32_t deviceGeneration) const noexcept
        {
            return OwnershipValid && FrameId == frameId && FrameSlot == frameSlot && SurfaceEpoch == surfaceEpoch &&
                   DeviceGeneration == deviceGeneration;
        }

        [[nodiscard]] bool Empty() const noexcept { return Outputs.empty(); }
    };

    [[nodiscard]] inline const GpuVfxFrameOutput*
    ResolveGpuVfxFrameOutput(const GpuVfxFrameResources& resources, const std::uint64_t frameId,
                             const std::uint32_t frameSlot, const std::uint64_t surfaceEpoch,
                             const std::uint32_t deviceGeneration, const std::uint64_t worldId,
                             const std::uint32_t handleIndex, const std::uint32_t handleGeneration) noexcept
    {
        if (!resources.OwnedBy(frameId, frameSlot, surfaceEpoch, deviceGeneration))
            return nullptr;
        for (const auto& output : resources.Outputs)
        {
            if (output.WorldId == worldId && output.HandleIndex == handleIndex &&
                output.HandleGeneration == handleGeneration)
            {
                return std::addressof(output);
            }
        }
        return nullptr;
    }

    struct SurfaceFrameWorkset final
    {
        SDL_GPUTexture* HdrColor = nullptr;
        SDL_GPUTexture* MultisampleHdrColor = nullptr;
        SDL_GPUTexture* Depth = nullptr;
        SDL_GPUTexture* SampledDepth = nullptr;
        SDL_GPUTexture* DirectionalShadow = nullptr;
        SDL_GPUTexture* LocalShadow = nullptr;
        GpuOcclusionFrameResources GpuOcclusion;
        std::vector<SDL_GPUTexture*> TransientTextures;
        std::uint32_t DirectionalShadowResolution = 0;
        std::uint32_t DirectionalShadowLayers = 0;
        std::uint32_t LocalShadowResolution = 0;
        std::uint32_t LocalShadowLayers = 0;
        ForwardPlusGpuResources ForwardPlus;
        GpuSpatialSelectionFrameResources SpatialSelection;
        GpuVfxFrameResources GpuVfx;
        SurfaceDynamicUploadResources DynamicUploads;
        std::uint64_t ForwardPlusContentHash = 0;
        bool ForwardPlusContentValid = false;

        [[nodiscard]] bool Empty() const noexcept
        {
            return !HdrColor && !MultisampleHdrColor && !Depth && !SampledDepth && !DirectionalShadow && !LocalShadow &&
                   GpuOcclusion.Empty() && TransientTextures.empty() && ForwardPlus.Empty() &&
                   SpatialSelection.Empty() && GpuVfx.Empty() && DynamicUploads.Empty();
        }
    };

    inline bool SurfaceResources::Empty() const noexcept
    {
        return FinalOutputs.empty() && std::ranges::all_of(Worksets, &SurfaceFrameWorkset::Empty);
    }

    struct RenderSharedState;

    struct RenderSurfacePropertySnapshot final
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        RenderSampleCount SampleCount = RenderSampleCount::One;

        bool operator==(const RenderSurfacePropertySnapshot&) const noexcept = default;
    };

    struct RenderSurfaceState final
    {
        std::weak_ptr<RenderSharedState> Owner;
        std::shared_ptr<const RenderSurfaceEpochLease> Lifetime;
        RenderSurfaceSpecification Specification;
        std::uint64_t Id = 0;
        std::uint64_t Epoch = 1;
        std::uint32_t RequestedWidth = 1;
        std::uint32_t RequestedHeight = 1;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t FailedWidth = 0;
        std::uint32_t FailedHeight = 0;
        RenderSampleCount ActualSamples = RenderSampleCount::One;
        RenderSurfacePropertySnapshot PublishedProperties;
        mutable std::mutex SurfacePropertyPublicationMutex;
        Color FrameClearColor;
        SurfaceResources Resources;
        std::atomic<bool> ResourcesAvailable{false};
        std::atomic<SDL_GPUTexture*> PublishedTexture{nullptr};
        std::atomic<std::uint32_t> PublishedWorksetSlot{0};
        std::atomic<bool> PublishedDepthAvailable{false};
        std::uint32_t ActiveWorksetSlot = 0;
        Detail::ShadowAtlasAllocator ShadowAtlas{4096, 256};
        Matrix4 SampledDepthViewProjection;
        Matrix4 SampledDepthInverseViewProjection;
        bool SampledDepthValid = false;
        GpuOcclusionSurfaceDiagnostics GpuOcclusionDiagnostics;
        GpuOcclusionSurfaceDiagnostics PublishedGpuOcclusionDiagnostics;
        mutable std::mutex GpuOcclusionPublicationMutex;
        GpuOcclusionPolicy::AllocationRetryState GpuOcclusionAllocationRetry;
        GpuOcclusionMode GpuOcclusionSubmittedMode = GpuOcclusionMode::Automatic;
        std::uint64_t GpuOcclusionSubmissionEpoch = 1;
        std::atomic<GpuOcclusionDebugView> GpuOcclusionDebugMode{GpuOcclusionDebugView::None};
        std::atomic<std::uint32_t> GpuOcclusionDebugMipLevel{0};
        std::uint32_t GpuOcclusionAutomaticQualifyingFrames = 0;
        std::uint32_t GpuOcclusionAutomaticMinimumFrames = 0;
        std::uint32_t GpuOcclusionAutomaticCooldownFrames = 0;
        std::uint64_t GpuOcclusionLatestCandidateTriangles = 0;
        std::uint64_t GpuOcclusionLatestVisibleTriangles = 0;
        bool GpuOcclusionAutomaticActive = false;
        bool GpuOcclusionValidationCooldown = false;
        bool GpuOcclusionValidationFallbackEventPending = false;
        std::uint64_t Generation = 0;
        bool Submitted = false;
        bool HasOutput = false;

        [[nodiscard]] SurfaceFrameWorkset& Workset(const std::uint32_t frameSlot)
        {
            return Resources.Worksets.at(frameSlot);
        }

        [[nodiscard]] const SurfaceFrameWorkset& Workset(const std::uint32_t frameSlot) const
        {
            return Resources.Worksets.at(frameSlot);
        }

        [[nodiscard]] SurfaceFrameWorkset& ActiveWorkset() { return Workset(ActiveWorksetSlot); }
        [[nodiscard]] const SurfaceFrameWorkset& ActiveWorkset() const { return Workset(ActiveWorksetSlot); }

        [[nodiscard]] const SurfaceFrameWorkset& PublishedWorkset() const
        {
            return Workset(PublishedWorksetSlot.load(std::memory_order_acquire));
        }

        void PublishSurfacePropertiesSnapshot() noexcept
        {
            std::scoped_lock lock(SurfacePropertyPublicationMutex);
            PublishedProperties = {Width, Height, ActualSamples};
        }

        [[nodiscard]] RenderSurfacePropertySnapshot SurfacePropertiesSnapshot() const noexcept
        {
            std::scoped_lock lock(SurfacePropertyPublicationMutex);
            return PublishedProperties;
        }

        void PublishGpuOcclusionDiagnosticsSnapshot() noexcept
        {
            std::scoped_lock lock(GpuOcclusionPublicationMutex);
            PublishedGpuOcclusionDiagnostics = GpuOcclusionDiagnostics;
        }

        [[nodiscard]] GpuOcclusionSurfaceDiagnostics GpuOcclusionDiagnosticsSnapshot() const noexcept
        {
            std::scoped_lock lock(GpuOcclusionPublicationMutex);
            return PublishedGpuOcclusionDiagnostics;
        }
    };
} // namespace Keire::RenderBackend
