#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    struct alignas(16) VfxVisibilityDispatch final
    {
        std::array<std::uint32_t, 4> Metadata{};
        std::array<std::uint32_t, 4> Execution{};
        Keire::Vector4 BoundsMinimum;
        Keire::Vector4 BoundsMaximum;
    };

    static_assert(sizeof(VfxVisibilityDispatch) == 64);

    [[nodiscard]] std::uint64_t EmitterKey(const Keire::VfxGpuEmitter& emitter) noexcept
    {
        return (static_cast<std::uint64_t>(emitter.Handle.Index()) << 32U) | emitter.Handle.Generation();
    }
} // namespace

namespace Keire::RenderBackend
{
    void RenderSharedState::RecordGpuVfxVisibilityCandidates(SDL_GPUCommandBuffer* commands,
                                                             RenderSurfaceState& surface,
                                                             const SceneRenderPacket& packet,
                                                             const PreparedGpuOcclusion& occlusion)
    {
        if (!commands || !occlusion.Enabled || !occlusion.Resources || occlusion.VfxVisibility.CandidateCount == 0U ||
            !VfxBuildVisibilityPipeline)
        {
            return;
        }
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        if (!occlusion.Resources->OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                          deviceGeneration) ||
            occlusion.Resources->VfxVisibilityCount < occlusion.VfxVisibility.CandidateCount)
        {
            return;
        }

        auto& frameResources = surface.ActiveWorkset().GpuVfx;
        frameResources.OwnershipValid = false;
        const auto createOutput =
            [this](const std::uint64_t worldId, const VfxGpuEmitter& emitter, const std::uint32_t capacity)
        {
            const auto createBuffer = [this](const std::uint64_t size, const SDL_GPUBufferUsageFlags usage)
            {
                if (size == 0U || size > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Frame-owned GPU VFX output exceeds SDL's 32-bit buffer limit.");
                SDL_GPUBufferCreateInfo information{};
                information.usage = usage;
                information.size = static_cast<std::uint32_t>(size);
                auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer(frame-owned GPU VFX) failed: " + LastSdlError());
                return buffer;
            };

            GpuVfxFrameOutput output;
            output.WorldId = worldId;
            output.HandleIndex = emitter.Handle.Index();
            output.HandleGeneration = emitter.Handle.Generation();
            output.Renderer = static_cast<std::uint32_t>(emitter.Renderer);
            output.Capacity = capacity;
            try
            {
                output.Indices =
                    createBuffer(static_cast<std::uint64_t>(sizeof(std::uint32_t)) * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                output.IndirectArguments =
                    createBuffer(sizeof(SDL_GPUIndexedIndirectDrawCommand),
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT);
                output.Instances =
                    createBuffer(static_cast<std::uint64_t>(sizeof(GpuInstanceUniform)) * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
            }
            catch (...)
            {
                GpuVfxFrameResources failed;
                failed.Outputs.push_back(output);
                ReleaseGpuVfxFrameResources(failed);
                throw;
            }
            return output;
        };

        auto previous = std::exchange(frameResources.Outputs, {});
        const auto takeReusable =
            [&](const std::uint64_t worldId, const VfxGpuEmitter& emitter, const std::uint32_t capacity)
        {
            for (auto& output : previous)
            {
                if (output.WorldId == worldId && output.HandleIndex == emitter.Handle.Index() &&
                    output.HandleGeneration == emitter.Handle.Generation() &&
                    output.Renderer == static_cast<std::uint32_t>(emitter.Renderer) && output.Capacity == capacity)
                {
                    return std::exchange(output, {});
                }
            }
            return createOutput(worldId, emitter, capacity);
        };

        try
        {
            frameResources.Outputs.reserve(occlusion.VfxVisibility.Entries.size());
            for (const auto& planEntry : occlusion.VfxVisibility.Entries)
            {
                if (planEntry.Source != VfxVisibilityPlanSource::GpuEmitter ||
                    planEntry.Disposition != VfxVisibilityPlanDisposition::CandidateRange ||
                    planEntry.SnapshotIndex >= packet.VfxSnapshots.size())
                {
                    continue;
                }
                const auto& snapshot = packet.VfxSnapshots[planEntry.SnapshotIndex];
                if (planEntry.SourceIndex >= snapshot.GpuEmitters().size())
                    continue;
                const auto& emitter = snapshot.GpuEmitters()[planEntry.SourceIndex];
                const auto world = GpuVfxWorlds.find(snapshot.WorldId());
                if (world == GpuVfxWorlds.end())
                    continue;
                const auto state = world->second.Emitters.find(EmitterKey(emitter));
                if (state == world->second.Emitters.end() || !state->second.RenderBuffers)
                    continue;
                const auto& source = *state->second.RenderBuffers;
                if (!world->second.Particles || !source.Indices || !source.IndirectArguments || !source.Instances)
                    continue;

                frameResources.Outputs.push_back(takeReusable(snapshot.WorldId(), emitter, source.Capacity));
                const auto& output = frameResources.Outputs.back();
                const auto primitiveCount =
                    emitter.Renderer == VfxRendererType::Mesh ? ResolveMesh(emitter.Mesh).IndexCount : 6U;
                VfxVisibilityDispatch dispatch;
                dispatch.Metadata = {occlusion.VfxCandidateFirst + planEntry.First, planEntry.First, planEntry.Count,
                                     primitiveCount};
                dispatch.Execution = {world->second.Capacity, source.Capacity,
                                      static_cast<std::uint32_t>(emitter.Renderer), 0U};
                if (emitter.Renderer == VfxRendererType::Mesh)
                {
                    const auto& bounds = ResolveMesh(emitter.Mesh).Bounds;
                    const auto* material =
                        emitter.Material
                            ? ResolveAssetMaterial(emitter.Material, ToSdlSampleCount(surface.ActualSamples))
                            : nullptr;
                    const auto radius = material && material->MaximumWorldPositionDisplacementRadius
                                            ? *material->MaximumWorldPositionDisplacementRadius
                                            : -1.0F;
                    dispatch.BoundsMinimum = {bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z, radius};
                    dispatch.BoundsMaximum = {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z, radius};
                }

                const std::array readBindings{world->second.Particles, source.Indices, source.IndirectArguments,
                                              source.Instances, occlusion.Resources->VfxVisibilityMask.Buffer};
                const std::array writeBindings{
                    SDL_GPUStorageBufferReadWriteBinding{occlusion.Resources->Candidates.Buffer, false},
                    SDL_GPUStorageBufferReadWriteBinding{occlusion.Resources->InputInstances.Buffer, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.Indices, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.IndirectArguments, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.Instances, false},
                };
                auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                     static_cast<std::uint32_t>(writeBindings.size()));
                if (!pass)
                {
                    throw std::runtime_error("SDL_BeginGPUComputePass(VFX visibility bounds) failed: " +
                                             LastSdlError());
                }
                SDL_BindGPUComputePipeline(pass, VfxBuildVisibilityPipeline);
                SDL_BindGPUComputeStorageBuffers(pass, 0U, readBindings.data(),
                                                 static_cast<std::uint32_t>(readBindings.size()));
                SDL_PushGPUComputeUniformData(commands, 0U, &dispatch, sizeof(dispatch));
                const auto groups = emitter.Renderer == VfxRendererType::Ribbon ? 1U : (source.Capacity + 255U) / 256U;
                SDL_DispatchGPUCompute(pass, groups, 1, 1);
                SDL_EndGPUComputePass(pass);
                ++Statistics.VfxComputeDispatches;
                Statistics.VfxComputeThreadGroups += groups;
            }
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU VFX visibility bounds");
            ReleaseGpuVfxFrameResources(frameResources);
            GpuVfxFrameResources leftovers;
            leftovers.Outputs = std::move(previous);
            ReleaseGpuVfxFrameResources(leftovers);
            throw;
        }
        GpuVfxFrameResources leftovers;
        leftovers.Outputs = std::move(previous);
        ReleaseGpuVfxFrameResources(leftovers);
        frameResources.TakeOwnership(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                     deviceGeneration);
    }

    void RenderSharedState::RecordGpuVfxVisibilityExpansion(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                            const SceneRenderPacket& packet,
                                                            const PreparedGpuOcclusion& occlusion)
    {
        auto& frameResources = surface.ActiveWorkset().GpuVfx;
        frameResources.OwnershipValid = false;
        if (!commands || !occlusion.Enabled || !occlusion.Resources || occlusion.VfxVisibility.CandidateCount == 0U ||
            !VfxCompactVisibilityPipeline)
        {
            return;
        }
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        if (!occlusion.Resources->OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                          deviceGeneration) ||
            occlusion.Resources->VfxVisibilityCount < occlusion.VfxVisibility.CandidateCount)
        {
            return;
        }

        const auto createOutput =
            [this](const std::uint64_t worldId, const VfxGpuEmitter& emitter, const std::uint32_t capacity)
        {
            const auto createBuffer = [this](const std::uint64_t size, const SDL_GPUBufferUsageFlags usage)
            {
                if (size == 0U || size > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Frame-owned GPU VFX output exceeds SDL's 32-bit buffer limit.");
                SDL_GPUBufferCreateInfo information{};
                information.usage = usage;
                information.size = static_cast<std::uint32_t>(size);
                auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer(frame-owned GPU VFX) failed: " + LastSdlError());
                return buffer;
            };

            GpuVfxFrameOutput output;
            output.WorldId = worldId;
            output.HandleIndex = emitter.Handle.Index();
            output.HandleGeneration = emitter.Handle.Generation();
            output.Renderer = static_cast<std::uint32_t>(emitter.Renderer);
            output.Capacity = capacity;
            try
            {
                output.Indices =
                    createBuffer(static_cast<std::uint64_t>(sizeof(std::uint32_t)) * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                output.IndirectArguments =
                    createBuffer(sizeof(SDL_GPUIndexedIndirectDrawCommand),
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT);
                output.Instances =
                    createBuffer(static_cast<std::uint64_t>(sizeof(GpuInstanceUniform)) * capacity,
                                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
            }
            catch (...)
            {
                GpuVfxFrameResources failed;
                failed.Outputs.push_back(output);
                ReleaseGpuVfxFrameResources(failed);
                throw;
            }
            return output;
        };

        auto previous = std::exchange(frameResources.Outputs, {});
        const auto takeReusable =
            [&](const std::uint64_t worldId, const VfxGpuEmitter& emitter, const std::uint32_t capacity)
        {
            for (auto& output : previous)
            {
                if (output.WorldId == worldId && output.HandleIndex == emitter.Handle.Index() &&
                    output.HandleGeneration == emitter.Handle.Generation() &&
                    output.Renderer == static_cast<std::uint32_t>(emitter.Renderer) && output.Capacity == capacity)
                {
                    return std::exchange(output, {});
                }
            }
            return createOutput(worldId, emitter, capacity);
        };

        try
        {
            frameResources.Outputs.reserve(occlusion.VfxVisibility.Entries.size());
            for (const auto& planEntry : occlusion.VfxVisibility.Entries)
            {
                if (planEntry.Source != VfxVisibilityPlanSource::GpuEmitter ||
                    planEntry.Disposition != VfxVisibilityPlanDisposition::CandidateRange ||
                    planEntry.SnapshotIndex >= packet.VfxSnapshots.size())
                {
                    continue;
                }
                const auto& snapshot = packet.VfxSnapshots[planEntry.SnapshotIndex];
                if (planEntry.SourceIndex >= snapshot.GpuEmitters().size())
                    continue;
                const auto& emitter = snapshot.GpuEmitters()[planEntry.SourceIndex];
                const auto world = GpuVfxWorlds.find(snapshot.WorldId());
                if (world == GpuVfxWorlds.end())
                    continue;
                const auto state = world->second.Emitters.find(EmitterKey(emitter));
                if (state == world->second.Emitters.end() || !state->second.RenderBuffers)
                    continue;
                const auto& source = *state->second.RenderBuffers;
                if (!world->second.Particles || !source.Indices || !source.IndirectArguments || !source.Instances)
                    continue;

                frameResources.Outputs.push_back(takeReusable(snapshot.WorldId(), emitter, source.Capacity));
                const auto& output = frameResources.Outputs.back();
                const auto primitiveCount =
                    emitter.Renderer == VfxRendererType::Mesh ? ResolveMesh(emitter.Mesh).IndexCount : 6U;
                VfxVisibilityDispatch dispatch;
                dispatch.Metadata = {occlusion.VfxCandidateFirst + planEntry.First, planEntry.First, planEntry.Count,
                                     primitiveCount};
                dispatch.Execution = {world->second.Capacity, source.Capacity,
                                      static_cast<std::uint32_t>(emitter.Renderer), 0U};
                const std::array readBindings{world->second.Particles, source.Indices, source.IndirectArguments,
                                              source.Instances, occlusion.Resources->VfxVisibilityMask.Buffer};
                const std::array writeBindings{
                    SDL_GPUStorageBufferReadWriteBinding{occlusion.Resources->Candidates.Buffer, false},
                    SDL_GPUStorageBufferReadWriteBinding{occlusion.Resources->InputInstances.Buffer, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.Indices, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.IndirectArguments, false},
                    SDL_GPUStorageBufferReadWriteBinding{output.Instances, false},
                };
                auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                     static_cast<std::uint32_t>(writeBindings.size()));
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPUComputePass(VFX visibility expansion) failed: " +
                                             LastSdlError());
                SDL_BindGPUComputePipeline(pass, VfxCompactVisibilityPipeline);
                SDL_BindGPUComputeStorageBuffers(pass, 0U, readBindings.data(),
                                                 static_cast<std::uint32_t>(readBindings.size()));
                SDL_PushGPUComputeUniformData(commands, 0U, &dispatch, sizeof(dispatch));
                SDL_DispatchGPUCompute(pass, 1, 1, 1);
                SDL_EndGPUComputePass(pass);
                ++Statistics.VfxComputeDispatches;
                ++Statistics.VfxComputeThreadGroups;
            }
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU VFX visibility expansion");
            ReleaseGpuVfxFrameResources(frameResources);
            GpuVfxFrameResources leftovers;
            leftovers.Outputs = std::move(previous);
            ReleaseGpuVfxFrameResources(leftovers);
            throw;
        }
        GpuVfxFrameResources leftovers;
        leftovers.Outputs = std::move(previous);
        ReleaseGpuVfxFrameResources(leftovers);
        frameResources.TakeOwnership(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                     deviceGeneration);
    }
} // namespace Keire::RenderBackend
