#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"
#include "KeireInternal/Rendering/ForwardPlusInternal.h"
#include "KeireInternal/Rendering/InstanceBatchInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"
#include "KeireInternal/Vfx/VfxGpuValidationInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    // Dense project shaders can bind sixteen or more samplers per draw. CPU VFX now coalesces compatible particles,
    // leaving enough command-buffer-local D3D12 descriptor capacity for this scene budget without the submission
    // overhead of the former twelve-batch workaround.
    constexpr std::size_t MaximumSceneBatchesPerCommandBuffer = 32U;

    class CallbackFrameGraphExecutionContext final : public Keire::RenderBackend::FrameGraphExecutionContext
    {
      public:
        using TransitionCallback = std::function<void(const Keire::RenderBackend::CompiledFrameGraph::Transition&)>;
        using PassCallback = std::function<void(Keire::RenderBackend::FrameGraphPass)>;

        CallbackFrameGraphExecutionContext(TransitionCallback transition, PassCallback pass)
            : m_Transition(std::move(transition)), m_Pass(std::move(pass))
        {
        }

        void Transition(const Keire::RenderBackend::CompiledFrameGraph::Transition& transition) override
        {
            m_Transition(transition);
        }

        void Execute(const Keire::RenderBackend::FrameGraphPass pass,
                     const Keire::RenderBackend::FrameGraphPassDescription&) override
        {
            m_Pass(pass);
        }

      private:
        TransitionCallback m_Transition;
        PassCallback m_Pass;
    };
} // namespace

namespace Keire::RenderBackend
{
    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer*& commands, RenderSurfaceState& surface,
                                          std::vector<SDL_GPUCommandBuffer*>& frameCommands)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("Record render surface");
        surface.IrradynRecordedThisFrame = false;
        if (!surface.Resources.PublishedColor() || !surface.ActiveWorkset().HdrColor)
            return;

        if (!ActiveFrame)
            throw std::logic_error("Render surface recording requires an active immutable frame packet.");
        auto& requests = ActiveFrame->Requests;
        const auto request = std::ranges::find_if(
            requests, [&surface](const auto& candidate)
            { return candidate.Surface.Id == surface.Id && candidate.Surface.Epoch == surface.Epoch; });
        const auto& workset = surface.ActiveWorkset();
        const bool deferredResourcesAvailable =
            DeferredCapability.load(std::memory_order_acquire) && workset.Depth && workset.GBufferBaseColorMetallic &&
            workset.GBufferNormalRoughness && workset.GBufferMaterial && workset.GBufferLighting &&
            workset.GBufferVelocity && workset.DBufferBaseColor && workset.DBufferNormal && workset.DBufferMaterial &&
            workset.IrradynRadiance && surface.Resources.PublishedIrradynHistory() &&
            (surface.ActualSamples == RenderSampleCount::One ||
             (workset.MultisampleHdrColor && workset.MultisampleDepth));
        RenderFeatureCapabilities featureCapabilities{};
        featureCapabilities.DeferredHybrid = deferredResourcesAvailable;
        featureCapabilities.BakedGlobalIllumination = true;
        featureCapabilities.RealtimeGlobalIllumination = true;
        featureCapabilities.IrradynGlobalIllumination = deferredResourcesAvailable;
        featureCapabilities.Fxaa = true;
        featureCapabilities.TemporalAntiAliasing = true;
        featureCapabilities.Msaa2 = Msaa2Capability.load(std::memory_order_acquire);
        featureCapabilities.Msaa4 = Msaa4Capability.load(std::memory_order_acquire);
        featureCapabilities.DeferredMultisample = deferredResourcesAvailable;
        featureCapabilities.DynamicResolution = true;
        const auto featureSelection =
            request != requests.end() ? ResolveRenderFeatureSelection(request->Packet.Environment, featureCapabilities)
                                      : RenderFeatureSelection{};
        surface.ActiveFeatureSelection = featureSelection;
        const bool deferred = request != requests.end() && featureSelection.EffectivePath == RenderPath::DeferredHybrid;
        const bool deferredMultisample = deferred && workset.MultisampleHdrColor;
        const bool preserveMultisampleForWorldUi =
            !deferredMultisample && workset.MultisampleHdrColor &&
            std::ranges::any_of(ActiveFrame->RuntimeUiWorldPanels, [&surface](const CapturedRuntimeUiWorldPanel& panel)
                                { return panel.Surface.Id == surface.Id && panel.Surface.Epoch == surface.Epoch; });
        if (deferred && std::max<std::size_t>(request->Packet.SpatialContributions.size(), 1U) > 255U)
            throw std::length_error("Deferred lighting exceeds the 8-bit additive-scene contribution limit.");
        const auto& frameGraph = deferred ? DeferredSceneFrameGraph : SceneFrameGraph;
        PreparedSceneDrawLists preparedDraws;
        PreparedGpuOcclusion preparedOcclusion;
        PreparedCpuVfx preparedCpuVfx;
        bool sampledDepthRecorded = false;
        bool gpuDepthCollisionRequired = false;
        if (request != requests.end())
        {
            auto started = std::chrono::steady_clock::now();
            PrepareSkinning(commands, request->Packet, surface.Id);
            Statistics.SkinningPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            preparedDraws = PrepareSceneDrawLists(commands, surface, request->Packet);
            preparedOcclusion = PrepareGpuOcclusion(commands, surface, request->Packet, preparedDraws);
            (void)PrepareSpatialSelection(commands, surface, request->Packet, preparedDraws, preparedOcclusion);
            Statistics.DrawPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            gpuDepthCollisionRequired =
                std::ranges::any_of(request->Packet.VfxSnapshots, [](const VfxRenderSnapshot& snapshot)
                                    { return RequiresGpuDepthCollision(snapshot.GpuEmitters()); });
            if (gpuDepthCollisionRequired && !surface.SampledDepthValid)
            {
                started = std::chrono::steady_clock::now();
                RecordSampledDepth(commands, surface, request->Packet, preparedDraws.Opaque);
                Statistics.DepthPassMilliseconds +=
                    std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                sampledDepthRecorded = surface.SampledDepthValid;
            }
        }
        ShadowFrameData shadows;
        shadows.LocalLayers.fill(-1.0F);
        const auto acquireContinuation = [&]
        {
            commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(surface continuation) failed: " + LastSdlError());
            frameCommands.push_back(commands);
        };
        CallbackFrameGraphExecutionContext execution(
            [&](const CompiledFrameGraph::Transition&) { ++Statistics.FrameGraphTransitions; },
            [&](const FrameGraphPass frameGraphPass)
            {
                ++Statistics.ExecutedFrameGraphPasses;
                if (frameGraphPass == frameGraph.DirectionalShadows)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != requests.end())
                        shadows = RecordShadows(commands, surface, request->Packet);
                    Statistics.ShadowRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.ForwardPlusCulling)
                {
                    if (request == requests.end())
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    auto contentHash = std::uint64_t{1469598103934665603ULL};
                    const auto hashValue = [&](const auto value)
                    {
                        const auto bytes = std::as_bytes(std::span(std::addressof(value), 1));
                        for (const auto byte : bytes)
                        {
                            contentHash ^= std::to_integer<std::uint8_t>(byte);
                            contentHash *= 1099511628211ULL;
                        }
                    };
                    const bool hasLocalLights = !request->Packet.LocalLights.empty();
                    hashValue(hasLocalLights);
                    if (hasLocalLights)
                    {
                        hashValue(surface.Width);
                        hashValue(surface.Height);
                        for (const auto& contribution : request->Packet.SpatialContributions)
                        {
                            hashValue(contribution.BakedLighting.High());
                            hashValue(contribution.BakedLighting.Low());
                        }
                        hashValue(request->Packet.Lighting.Cookie.High());
                        hashValue(request->Packet.Lighting.Cookie.Low());
                        for (const auto value : request->Packet.Camera.View.Elements)
                            hashValue(value);
                        for (const auto value : request->Packet.Camera.Projection.Elements)
                            hashValue(value);
                        hashValue(request->Packet.Camera.NearPlane);
                        for (const auto& light : request->Packet.LocalLights)
                        {
                            hashValue(light.Position.X);
                            hashValue(light.Position.Y);
                            hashValue(light.Position.Z);
                            hashValue(light.Range);
                            hashValue(light.Direction.X);
                            hashValue(light.Direction.Y);
                            hashValue(light.Direction.Z);
                            hashValue(light.OuterConeCosine);
                            hashValue(light.ColorAndIntensity.Red);
                            hashValue(light.ColorAndIntensity.Green);
                            hashValue(light.ColorAndIntensity.Blue);
                            hashValue(light.ColorAndIntensity.Alpha);
                            hashValue(light.InnerConeCosine);
                            hashValue(light.Type);
                            hashValue(light.Cookie.High());
                            hashValue(light.Cookie.Low());
                            hashValue(light.ContactShadows);
                            hashValue(light.ContributionOrder);
                        }
                    }
                    Statistics.VisibleLocalLights += static_cast<std::uint32_t>(request->Packet.LocalLights.size());
                    if (surface.ActiveWorkset().ForwardPlusContentValid &&
                        surface.ActiveWorkset().ForwardPlusContentHash == contentHash &&
                        !surface.ActiveWorkset().ForwardPlus.Empty() &&
                        !surface.ActiveWorkset().ForwardPlus.VisibilityCompacted)
                    {
                        ++Statistics.ForwardPlusCacheHits;
                        auto& forwardPlus = surface.ActiveWorkset().ForwardPlus;
                        forwardPlus.TakeOwnership(request->Packet.AcceptedFrameId, surface.ActiveWorksetSlot,
                                                  surface.Epoch, DeviceGeneration.load(std::memory_order_acquire));
                        forwardPlus.VisibilityCompacted =
                            RecordForwardPlusVisibilityMask(commands, surface, request->Packet, preparedOcclusion);
                        Statistics.ForwardPlusCullingMilliseconds +=
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
                                .count();
                        return;
                    }
                    std::vector<ForwardPlusLightBounds> localLightBounds;
                    localLightBounds.reserve(request->Packet.LocalLights.size());
                    for (const auto& light : request->Packet.LocalLights)
                        localLightBounds.push_back(
                            {Math::TransformPoint(request->Packet.Camera.View, light.Position), light.Range});
                    const auto tiles = [&]
                    {
                        if (hasLocalLights)
                            return BuildForwardPlusCpuTiles(surface.Width, surface.Height,
                                                            request->Packet.Camera.Projection,
                                                            request->Packet.Camera.NearPlane, localLightBounds);
                        ForwardPlusTileGrid empty;
                        empty.Columns = 1;
                        empty.Rows = 1;
                        empty.Offsets.push_back(0);
                        empty.Counts.push_back(0);
                        return empty;
                    }();
                    Statistics.OverflowedLightTiles += tiles.OverflowedTiles;
                    std::vector<AssetLocalLightUniform> gpuLights(
                        std::max<std::size_t>(1, request->Packet.LocalLights.size()));
                    const auto forwardMixedChannel = [&](const SceneLocalLight& light) -> float
                    {
                        const auto contribution = std::min<std::size_t>(
                            light.ContributionOrder, request->Packet.SpatialContributions.empty()
                                                         ? 0U
                                                         : request->Packet.SpatialContributions.size() - 1U);
                        const auto bakedLighting =
                            request->Packet.SpatialContributions.empty()
                                ? request->Packet.BakedLighting
                                : request->Packet.SpatialContributions[contribution].BakedLighting;
                        const auto lightingSet = ResolveLightingSet(bakedLighting);
                        if (!lightingSet)
                            return 0.0F;
                        const auto found = std::ranges::find(lightingSet->Definition().MixedLights,
                                                             light.Entity.Value(), &MixedLightBinding::Light);
                        return found == lightingSet->Definition().MixedLights.end()
                                   ? 0.0F
                                   : static_cast<float>(found->ShadowMaskChannel + 1U);
                    };
                    std::uint32_t forwardCookieSlot = request->Packet.Lighting.Cookie ? 1U : 0U;
                    for (std::size_t lightIndex = 0; lightIndex < request->Packet.LocalLights.size(); ++lightIndex)
                    {
                        const auto& light = request->Packet.LocalLights[lightIndex];
                        float cookie = 0.0F;
                        if (light.Cookie && forwardCookieSlot < 8U)
                            cookie = static_cast<float>(++forwardCookieSlot);
                        gpuLights[lightIndex] = {
                            {light.Position.X, light.Position.Y, light.Position.Z, light.Range},
                            {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine},
                            {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green, light.ColorAndIntensity.Blue,
                             light.ColorAndIntensity.Alpha},
                            {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F,
                             forwardMixedChannel(light),
                             cookie + (light.ContactShadows ? 16.0F : 0.0F) +
                                 static_cast<float>((std::min(light.ContributionOrder, 65534U) + 1U) * 32U)}};
                    }
                    std::vector<ForwardPlusTileUniform> gpuTiles(tiles.Offsets.size());
                    for (std::size_t tileIndex = 0; tileIndex < gpuTiles.size(); ++tileIndex)
                        gpuTiles[tileIndex] = {tiles.Offsets[tileIndex], tiles.Counts[tileIndex]};
                    std::vector<ForwardPlusIndexGroup> gpuIndices(
                        std::max<std::size_t>(1, (tiles.LightIndices.size() + 3U) / 4U));
                    for (std::size_t index = 0; index < tiles.LightIndices.size(); ++index)
                        gpuIndices[index / 4U].Indices[index % 4U] = tiles.LightIndices[index];

                    const std::array payloads{std::as_bytes(std::span(gpuLights)), std::as_bytes(std::span(gpuTiles)),
                                              std::as_bytes(std::span(gpuIndices))};
                    const auto capacityFor = [](const std::size_t required)
                    {
                        if (required == 0 || required > std::numeric_limits<std::uint32_t>::max())
                            throw std::invalid_argument("Forward+ buffer payload exceeds SDL's 32-bit limit.");
                        auto capacity = std::uint32_t{256};
                        while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U)
                            capacity *= 2U;
                        if (capacity < required)
                            capacity = static_cast<std::uint32_t>(required);
                        return capacity;
                    };
                    const std::array requiredCapacities{capacityFor(payloads[0].size()),
                                                        capacityFor(payloads[1].size()),
                                                        capacityFor(payloads[2].size())};
                    const bool requiresReplacement =
                        surface.ActiveWorkset().ForwardPlus.Empty() ||
                        surface.ActiveWorkset().ForwardPlus.LightCapacityBytes < requiredCapacities[0] ||
                        surface.ActiveWorkset().ForwardPlus.TileCapacityBytes < requiredCapacities[1] ||
                        surface.ActiveWorkset().ForwardPlus.LightIndexCapacityBytes < requiredCapacities[2];
                    if (requiresReplacement)
                    {
                        ForwardPlusGpuResources replacement;
                        const auto createBuffer = [&](const std::uint32_t byteSize, const SDL_GPUBufferUsageFlags usage)
                        {
                            SDL_GPUBufferCreateInfo information{};
                            information.usage = usage;
                            information.size = byteSize;
                            auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                            if (!buffer)
                                throw std::runtime_error("SDL_CreateGPUBuffer(Forward+) failed: " + LastSdlError());
                            return buffer;
                        };
                        try
                        {
                            replacement.Lights =
                                createBuffer(requiredCapacities[0], SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                            constexpr auto compactedUsage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                                                            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                                            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
                            replacement.Tiles = createBuffer(requiredCapacities[1], compactedUsage);
                            replacement.LightIndices = createBuffer(requiredCapacities[2], compactedUsage);
                            replacement.LightCapacityBytes = requiredCapacities[0];
                            replacement.TileCapacityBytes = requiredCapacities[1];
                            replacement.LightIndexCapacityBytes = requiredCapacities[2];
                        }
                        catch (...)
                        {
                            RethrowIfDeviceLost("Forward+ buffer allocation");
                            ReleaseForwardPlusResources(replacement);
                            throw;
                        }
                        Retire(std::exchange(surface.ActiveWorkset().ForwardPlus, replacement));
                        ++Statistics.ForwardPlusBufferReallocations;
                    }

                    std::size_t totalBytes = 0;
                    for (const auto payload : payloads)
                    {
                        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                            throw std::invalid_argument("Combined Forward+ upload exceeds SDL's 32-bit limit.");
                        totalBytes += payload.size();
                    }
                    SDL_GPUTransferBuffer* transfer = nullptr;
                    try
                    {
                        SDL_GPUTransferBufferCreateInfo information{};
                        information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                        information.size = static_cast<std::uint32_t>(totalBytes);
                        transfer = SDL_CreateGPUTransferBuffer(Device, &information);
                        if (!transfer)
                            throw std::runtime_error("SDL_CreateGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
                        if (!mapped)
                            throw std::runtime_error("SDL_MapGPUTransferBuffer(Forward+) failed: " + LastSdlError());
                        std::size_t offset = 0;
                        for (const auto payload : payloads)
                        {
                            std::memcpy(mapped + offset, payload.data(), payload.size());
                            offset += payload.size();
                        }
                        SDL_UnmapGPUTransferBuffer(Device, transfer);

                        auto* copy = SDL_BeginGPUCopyPass(commands);
                        if (!copy)
                            throw std::runtime_error("SDL_BeginGPUCopyPass(Forward+) failed: " + LastSdlError());
                        const std::array destinations{surface.ActiveWorkset().ForwardPlus.Lights,
                                                      surface.ActiveWorkset().ForwardPlus.Tiles,
                                                      surface.ActiveWorkset().ForwardPlus.LightIndices};
                        offset = 0;
                        for (std::size_t index = 0; index < payloads.size(); ++index)
                        {
                            SDL_GPUTransferBufferLocation source{transfer, static_cast<std::uint32_t>(offset)};
                            SDL_GPUBufferRegion destination{destinations[index], 0,
                                                            static_cast<std::uint32_t>(payloads[index].size())};
                            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
                            offset += payloads[index].size();
                        }
                        SDL_EndGPUCopyPass(copy);
                        FrameUploadTransfers.push_back(transfer);
                        transfer = nullptr;
                    }
                    catch (...)
                    {
                        RethrowIfDeviceLost("Forward+ buffer upload");
                        if (transfer)
                            SDL_ReleaseGPUTransferBuffer(Device, transfer);
                        throw;
                    }
                    surface.ActiveWorkset().ForwardPlus.Columns = tiles.Columns;
                    surface.ActiveWorkset().ForwardPlus.Rows = tiles.Rows;
                    surface.ActiveWorkset().ForwardPlusContentHash = contentHash;
                    surface.ActiveWorkset().ForwardPlusContentValid = true;
                    auto& forwardPlus = surface.ActiveWorkset().ForwardPlus;
                    forwardPlus.TakeOwnership(request->Packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                              DeviceGeneration.load(std::memory_order_acquire));
                    forwardPlus.VisibilityCompacted = false;
                    forwardPlus.VisibilityCompacted =
                        RecordForwardPlusVisibilityMask(commands, surface, request->Packet, preparedOcclusion);
                    Statistics.ForwardPlusUploadBytes += totalBytes;
                    Statistics.ForwardPlusCullingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.GpuOcclusionDepthPass)
                {
                    if (request != requests.end())
                    {
                        RecordGpuOcclusionDepth(commands, surface, request->Packet, preparedDraws, preparedOcclusion);
                    }
                    return;
                }
                if (frameGraphPass == frameGraph.VfxSimulation)
                {
                    if (request == requests.end())
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    for (const auto& snapshot : request->Packet.VfxSnapshots)
                        PrepareGpuVfx(commands, snapshot, surface);
                    RecordGpuVfxVisibilityCandidates(commands, surface, request->Packet, preparedOcclusion);
                    Statistics.VfxPreparationMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.GpuOcclusionPyramidPass)
                {
                    if (request != requests.end())
                        RecordGpuOcclusionPyramid(commands, surface, request->Packet, preparedOcclusion);
                    return;
                }
                if (frameGraphPass == frameGraph.GpuOcclusionCullingPass)
                {
                    if (request != requests.end())
                    {
                        RecordGpuOcclusionCulling(commands, surface, request->Packet, preparedDraws, preparedOcclusion);
                    }
                    return;
                }
                if (frameGraphPass == frameGraph.SpatialSelection)
                {
                    if (request != requests.end())
                        RecordSpatialSelection(commands, surface, request->Packet, preparedOcclusion);
                    return;
                }
                if (frameGraphPass == frameGraph.VfxPreparation)
                {
                    if (request == requests.end())
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    RecordGpuVfxVisibilityExpansion(commands, surface, request->Packet, preparedOcclusion);
                    preparedCpuVfx = PrepareCpuVfxDraws(commands, surface, request->Packet);
                    Statistics.VfxPreparationMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.DepthVelocity)
                {
                    if (request == requests.end() ||
                        (!deferred && featureSelection.EffectiveAntiAliasing != RenderAntiAliasingMode::Taa))
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal = preparedDraws.Opaque.Batches.size();
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        if (chunk != 0U)
                            acquireContinuation();
                        const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                        const auto count = std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                        RecordDeferredDepthVelocity(commands, surface, request->Packet, shadows, preparedDraws.Opaque,
                                                    firstBatch, count, chunk == 0U);
                    }
                    Statistics.DepthPassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (deferred && (frameGraphPass == frameGraph.DeferredGBufferStandard ||
                                 frameGraphPass == frameGraph.DeferredGBufferExtended))
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto phase = frameGraphPass == frameGraph.DeferredGBufferStandard
                                           ? SceneDrawPhase::DeferredGBufferStandard
                                           : SceneDrawPhase::DeferredGBufferExtended;
                    const auto batchTotal = preparedDraws.Opaque.Batches.size();
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        acquireContinuation();
                        const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                        const auto count = std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                        const bool clearTargets = phase == SceneDrawPhase::DeferredGBufferStandard && chunk == 0U;
                        RecordDeferredGBuffer(commands, surface, request->Packet, shadows, preparedDraws.Opaque, phase,
                                              firstBatch, count, clearTargets);
                    }
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (deferred && frameGraphPass == frameGraph.DeferredDecals)
                {
                    const auto batchTotal = preparedDraws.Decals.Batches.size();
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        if (chunk != 0U)
                            acquireContinuation();
                        const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                        const auto count = std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                        RecordDeferredDBuffer(commands, surface, request->Packet, shadows, preparedDraws.Decals,
                                              firstBatch, count, chunk == 0U);
                    }
                    return;
                }
                if (deferred && frameGraphPass == frameGraph.DeferredLighting)
                {
                    if (deferredMultisample)
                        return;
                    const auto started = std::chrono::steady_clock::now();
                    RecordDeferredLighting(commands, surface, request->Packet, shadows);
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (deferred && frameGraphPass == frameGraph.ForwardOpaqueTail)
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal = preparedDraws.Opaque.Batches.size();
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        acquireContinuation();
                        SDL_GPUColorTargetInfo color{};
                        color.texture = deferredMultisample ? surface.ActiveWorkset().MultisampleHdrColor
                                                            : surface.ActiveWorkset().HdrColor;
                        color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                             surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                        color.load_op = deferredMultisample && chunk == 0U ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                        color.store_op = SDL_GPU_STOREOP_STORE;
                        SDL_GPUDepthStencilTargetInfo depth{};
                        depth.texture = deferredMultisample ? surface.ActiveWorkset().MultisampleDepth
                                                            : surface.ActiveWorkset().Depth;
                        depth.clear_depth = 1.0F;
                        depth.load_op = deferredMultisample && chunk == 0U ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                        depth.store_op = SDL_GPU_STOREOP_STORE;
                        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depth);
                        if (!pass)
                        {
                            throw std::runtime_error("SDL_BeginGPURenderPass(deferred forward tail) failed: " +
                                                     LastSdlError());
                        }
                        const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                        const auto count = std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                        if (deferredMultisample && chunk == 0U && !preparedDraws.Decals.Batches.empty())
                        {
                            DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Transparent,
                                      preparedDraws.Decals, 0U, preparedDraws.Decals.Batches.size());
                        }
                        DrawScene(commands, pass, surface, request->Packet, shadows,
                                  deferredMultisample ? SceneDrawPhase::Opaque
                                                      : SceneDrawPhase::DeferredForwardOpaqueTail,
                                  preparedDraws.Opaque, firstBatch, count);
                        SDL_EndGPURenderPass(pass);
                        ++Statistics.Passes;
                    }
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.Opaque)
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal =
                        request != requests.end() ? preparedDraws.Opaque.Batches.size() : std::size_t{};
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        if (chunk != 0U)
                            acquireContinuation();
                        SDL_GPUColorTargetInfo color{};
                        color.texture = surface.ActiveWorkset().MultisampleHdrColor
                                            ? surface.ActiveWorkset().MultisampleHdrColor
                                            : surface.ActiveWorkset().HdrColor;
                        color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                             surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                        color.load_op = chunk == 0U ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                        color.store_op = SDL_GPU_STOREOP_STORE;
                        SDL_GPUDepthStencilTargetInfo depth{};
                        SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                        auto* const sceneDepth = surface.ActiveWorkset().MultisampleDepth
                                                     ? surface.ActiveWorkset().MultisampleDepth
                                                     : surface.ActiveWorkset().Depth;
                        if (sceneDepth)
                        {
                            depth.texture = sceneDepth;
                            depth.clear_depth = 1.0F;
                            depth.load_op = chunk == 0U ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                            depth.store_op = SDL_GPU_STOREOP_STORE;
                            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                            depthPointer = &depth;
                        }
                        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                        if (!pass)
                            throw std::runtime_error("SDL_BeginGPURenderPass(HDR scene) failed: " + LastSdlError());
                        if (request != requests.end())
                        {
                            const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                            const auto batchCount =
                                std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                            DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Opaque,
                                      preparedDraws.Opaque, firstBatch, batchCount);
                        }
                        SDL_EndGPURenderPass(pass);
                        ++Statistics.Passes;
                    }
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.ResolveDepth)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != requests.end() && gpuDepthCollisionRequired && !sampledDepthRecorded)
                        RecordSampledDepth(commands, surface, request->Packet, preparedDraws.Opaque);
                    else if (!gpuDepthCollisionRequired)
                        surface.SampledDepthValid = false;
                    Statistics.DepthPassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.Transparency)
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal =
                        request != requests.end() ? preparedDraws.Transparent.Batches.size() : std::size_t{};
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        acquireContinuation();
                        const auto finalChunk = chunk + 1U == chunkTotal;
                        SDL_GPUColorTargetInfo color{};
                        color.texture = surface.ActiveWorkset().MultisampleHdrColor
                                            ? surface.ActiveWorkset().MultisampleHdrColor
                                            : surface.ActiveWorkset().HdrColor;
                        color.load_op = SDL_GPU_LOADOP_LOAD;
                        color.store_op = surface.ActiveWorkset().MultisampleHdrColor && finalChunk
                                             ? (preserveMultisampleForWorldUi ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                                                              : SDL_GPU_STOREOP_RESOLVE)
                                             : SDL_GPU_STOREOP_STORE;
                        color.resolve_texture = surface.ActiveWorkset().MultisampleHdrColor && finalChunk
                                                    ? surface.ActiveWorkset().HdrColor
                                                    : nullptr;
                        SDL_GPUDepthStencilTargetInfo depth{};
                        SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                        auto* const sceneDepth = surface.ActiveWorkset().MultisampleDepth
                                                     ? surface.ActiveWorkset().MultisampleDepth
                                                     : surface.ActiveWorkset().Depth;
                        if (sceneDepth)
                        {
                            depth.texture = sceneDepth;
                            depth.load_op = SDL_GPU_LOADOP_LOAD;
                            depth.store_op = SDL_GPU_STOREOP_STORE;
                            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                            depthPointer = &depth;
                        }
                        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                        if (!pass)
                            throw std::runtime_error("SDL_BeginGPURenderPass(transparency) failed: " + LastSdlError());
                        if (request != requests.end())
                        {
                            const auto firstBatch = chunk * MaximumSceneBatchesPerCommandBuffer;
                            const auto batchCount =
                                std::min(MaximumSceneBatchesPerCommandBuffer, batchTotal - firstBatch);
                            DrawScene(commands, pass, surface, request->Packet, shadows, SceneDrawPhase::Transparent,
                                      preparedDraws.Transparent, firstBatch, batchCount);
                            if (finalChunk)
                                DrawVfx(commands, pass, surface, request->Packet, shadows, preparedCpuVfx);
                        }
                        SDL_EndGPURenderPass(pass);
                        ++Statistics.Passes;
                    }
                    Statistics.ScenePassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (deferred && frameGraphPass == frameGraph.IrradynTrace)
                {
                    if (request != requests.end())
                    {
                        const auto started = std::chrono::steady_clock::now();
                        RecordIrradynTrace(commands, surface, request->Packet,
                                           request->Packet.TemporalHistoryContinuous);
                        Statistics.ScenePassMilliseconds +=
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
                                .count();
                    }
                    return;
                }
                if (deferred && frameGraphPass == frameGraph.IrradynComposite)
                {
                    if (request != requests.end())
                    {
                        const auto started = std::chrono::steady_clock::now();
                        RecordIrradynComposite(commands, surface, request->Packet);
                        Statistics.ScenePassMilliseconds +=
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
                                .count();
                    }
                    return;
                }
                if (frameGraphPass == frameGraph.ToneMap)
                {
                    auto started = std::chrono::steady_clock::now();
                    RecordRuntimeUiWorldPanels(commands, surface);
                    Statistics.UiRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    started = std::chrono::steady_clock::now();
                    const bool temporalHistoryContinuous =
                        request != requests.end() && request->Packet.TemporalHistoryContinuous;
                    RecordToneMap(commands, surface, featureSelection.EffectiveAntiAliasing, temporalHistoryContinuous);
                    Statistics.ToneMapMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == frameGraph.Overlays)
                {
                    const auto started = std::chrono::steady_clock::now();
                    RecordRuntimeUiCameraPanels(commands, surface);
                    Statistics.UiRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    if (request != requests.end())
                        RecordGpuOcclusionDebug(commands, surface, request->Packet, preparedOcclusion);
                }
            });
        frameGraph.Graph.Execute(frameGraph.Compiled, execution);
        if (request != requests.end())
            FinalizeGpuOcclusionConsumerEvidence(surface, request->Packet, preparedOcclusion);
        ++Statistics.Surfaces;
    }

} // namespace Keire::RenderBackend
