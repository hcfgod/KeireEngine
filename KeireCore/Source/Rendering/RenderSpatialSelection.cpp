#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinSpatialSelectionShaders.h"
#include "Keire/Log.h"
#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"
#include "KeireInternal/Rendering/SpatialVisibilitySelectionInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct EmbeddedShader final
    {
        const unsigned char* Code = nullptr;
        std::size_t Size = 0;
    };

    [[nodiscard]] SDL_GPUShaderFormat SelectShaderFormat(SDL_GPUDevice* device) noexcept
    {
        const auto supported = SDL_GetGPUShaderFormats(device);
        return (supported & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
               : (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
               : (supported & SDL_GPU_SHADERFORMAT_MSL)   ? SDL_GPU_SHADERFORMAT_MSL
                                                          : SDL_GPU_SHADERFORMAT_INVALID;
    }

    [[nodiscard]] EmbeddedShader SelectShader(const SDL_GPUShaderFormat format) noexcept
    {
        if (format == SDL_GPU_SHADERFORMAT_DXIL)
        {
            return {Keire::Detail::BuiltinSpatialSelectionComputeDxil,
                    sizeof(Keire::Detail::BuiltinSpatialSelectionComputeDxil)};
        }
        if (format == SDL_GPU_SHADERFORMAT_SPIRV)
        {
            return {Keire::Detail::BuiltinSpatialSelectionComputeSpirv,
                    sizeof(Keire::Detail::BuiltinSpatialSelectionComputeSpirv)};
        }
        if (format == SDL_GPU_SHADERFORMAT_MSL)
        {
            return {Keire::Detail::BuiltinSpatialSelectionComputeMsl,
                    sizeof(Keire::Detail::BuiltinSpatialSelectionComputeMsl)};
        }
        return {};
    }

    [[nodiscard]] std::uint32_t GrowCapacity(const std::size_t required)
    {
        if (required == 0U || required > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Spatial-selection resource exceeds SDL's 32-bit size limit.");
        const auto minimum = std::max<std::size_t>(required, 256U);
        const auto capacity = std::bit_ceil(minimum);
        return capacity > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max()
                                                                    : static_cast<std::uint32_t>(capacity);
    }

    [[nodiscard]] std::size_t AlignUpload(const std::size_t value) noexcept
    {
        return (value + 15U) & ~std::size_t{15U};
    }

    [[nodiscard]] float ReflectionWeight(const Keire::Vector3 worldPosition,
                                         const Keire::Detail::SpatialReflectionProbe& probe) noexcept
    {
        const auto local = Keire::Math::TransformPoint(probe.WorldToLocal, worldPosition);
        const auto nearest = std::min({probe.BoxExtents.X - std::abs(local.X), probe.BoxExtents.Y - std::abs(local.Y),
                                       probe.BoxExtents.Z - std::abs(local.Z)});
        if (nearest < 0.0F)
            return 0.0F;
        return probe.BlendDistance <= 1.0e-6F ? 1.0F : std::clamp(nearest / probe.BlendDistance, 0.0F, 1.0F);
    }

    [[nodiscard]] float Distance(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        const auto x = left.X - right.X;
        const auto y = left.Y - right.Y;
        const auto z = left.Z - right.Z;
        return std::sqrt(x * x + y * y + z * z);
    }

} // namespace

namespace Keire::RenderBackend
{
    bool RenderSharedState::EnsureSpatialSelectionPipeline()
    {
        if (SpatialSelectionPipelineAttempted)
            return SpatialSelectionPipeline != nullptr;
        SpatialSelectionPipelineAttempted = true;
        if (!Device)
            return false;

        try
        {
            const auto format = SelectShaderFormat(Device);
            const auto shader = SelectShader(format);
            if (format == SDL_GPU_SHADERFORMAT_INVALID || !shader.Code || shader.Size == 0U)
                throw std::runtime_error("GPU spatial selection requires a DXIL, SPIR-V, or MSL shader backend.");

            SDL_GPUComputePipelineCreateInfo information{};
            information.code = shader.Code;
            information.code_size = shader.Size;
            information.entrypoint = "CSSelectSpatialLighting";
            information.format = format;
            information.num_readonly_storage_buffers = 4U;
            information.num_readwrite_storage_buffers = 1U;
            information.num_uniform_buffers = 1U;
            information.threadcount_x = 64U;
            information.threadcount_y = 1U;
            information.threadcount_z = 1U;
            SpatialSelectionPipeline = SDL_CreateGPUComputePipeline(Device, &information);
            if (!SpatialSelectionPipeline)
            {
                throw std::runtime_error("SDL_CreateGPUComputePipeline(spatial selection) failed: " + LastSdlError());
            }
            return true;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("spatial-selection pipeline creation", error.what());
            KEIRE_CORE_WARN("GPU spatial selection is unavailable; ABI-v3 draws remain fail-visible: {}", error.what());
            return false;
        }
    }

    void RenderSharedState::ReleaseSpatialSelectionPipeline() noexcept
    {
        if (Device && SpatialSelectionPipeline)
            SDL_ReleaseGPUComputePipeline(Device, SpatialSelectionPipeline);
        SpatialSelectionPipeline = nullptr;
        SpatialSelectionPipelineAttempted = false;
    }

    bool RenderSharedState::PrepareSpatialSelection(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                    const SceneRenderPacket& packet, PreparedSceneDrawLists& draws,
                                                    const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU spatial selection prepare");
        const auto invalidateDraws = [&]
        {
            for (auto& draw : draws.Opaque.Draws)
                draw.SpatialSelectionRecordIndex = InvalidAssetSpatialSelectionIndex;
            for (auto& draw : draws.Transparent.Draws)
                draw.SpatialSelectionRecordIndex = InvalidAssetSpatialSelectionIndex;
        };
        invalidateDraws();

        auto& resources = surface.ActiveWorkset().SpatialSelection;
        resources.OwnershipValid = false;
        resources.DispatchSucceeded = false;
        resources.RecordCount = 0;
        resources.SpatialMaskCount = 0;
        if (!commands)
            return false;

        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        const bool useGpuSelection = occlusion.Enabled && occlusion.Resources && EnsureSpatialSelectionPipeline();
        const auto* visibility = useGpuSelection ? occlusion.Resources : nullptr;
        const auto expectedMaskCount =
            static_cast<std::uint64_t>(packet.ReflectionProbes.size()) + packet.LightProbeVolumes.size();
        if (expectedMaskCount > std::numeric_limits<std::uint32_t>::max() ||
            (useGpuSelection && (!visibility->OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                                      deviceGeneration) ||
                                 !visibility->SpatialVolumeVisibilityMask.Buffer ||
                                 visibility->SpatialVolumeVisibilityCount != expectedMaskCount)))
        {
            return false;
        }

        try
        {
            struct Context final
            {
                Ref<const LightingSetAsset> Asset;
                const LightingSetDefinition* LightingSet = nullptr;
                std::span<const Detail::SpatialReflectionProbe> ReflectionProbes;
                std::span<const SceneLightProbeVolume> LightProbeVolumes;
                SpatialVisibilityContributionRange VisibilityRange;
                std::uint32_t ReflectionMipLevels = 1U;
            };
            std::vector<SpatialVisibilityContributionCounts> counts;
            if (packet.SpatialContributions.empty())
            {
                counts.push_back({static_cast<std::uint32_t>(packet.ReflectionProbes.size()),
                                  static_cast<std::uint32_t>(packet.LightProbeVolumes.size())});
            }
            else
            {
                counts.reserve(packet.SpatialContributions.size());
                for (const auto& contribution : packet.SpatialContributions)
                {
                    if (contribution.ReflectionProbes.size() > std::numeric_limits<std::uint32_t>::max() ||
                        contribution.LightProbeVolumes.size() > std::numeric_limits<std::uint32_t>::max())
                    {
                        throw std::length_error("Spatial contribution exceeds the 32-bit GPU index range.");
                    }
                    counts.push_back({static_cast<std::uint32_t>(contribution.ReflectionProbes.size()),
                                      static_cast<std::uint32_t>(contribution.LightProbeVolumes.size())});
                }
            }
            const auto layout = BuildSpatialVisibilityLayout(counts);
            if (layout.TotalCount() != expectedMaskCount ||
                layout.ReflectionProbeCount != packet.ReflectionProbes.size() ||
                layout.LightProbeVolumeCount != packet.LightProbeVolumes.size())
            {
                return false;
            }

            std::vector<Context> contexts;
            contexts.reserve(counts.size());
            const auto appendContext = [&](const AssetId bakedLighting,
                                           const std::span<const Detail::SpatialReflectionProbe> reflections,
                                           const std::span<const SceneLightProbeVolume> volumes,
                                           const SpatialVisibilityContributionRange range)
            {
                Context context;
                context.Asset = ResolveLightingSet(bakedLighting);
                context.LightingSet = context.Asset ? &context.Asset->Definition() : nullptr;
                context.ReflectionProbes = reflections;
                context.LightProbeVolumes = volumes;
                context.VisibilityRange = range;
                context.ReflectionMipLevels =
                    context.LightingSet
                        ? ResolveLightingTexture(context.LightingSet->ReflectionCubemaps, true).MipLevels
                        : DefaultReflectionCubeArray.MipLevels;
                contexts.push_back(std::move(context));
            };
            if (packet.SpatialContributions.empty())
            {
                appendContext(packet.BakedLighting, packet.ReflectionProbes, packet.LightProbeVolumes,
                              layout.Contributions.front());
            }
            else
            {
                for (std::size_t index = 0; index < packet.SpatialContributions.size(); ++index)
                {
                    const auto& contribution = packet.SpatialContributions[index];
                    appendContext(contribution.BakedLighting, contribution.ReflectionProbes,
                                  contribution.LightProbeVolumes, layout.Contributions[index]);
                }
            }

            struct ReflectionCandidate final
            {
                GpuSpatialReflectionCandidate Gpu;
                AssetId StableId;
                std::int32_t Importance = 0;
                float Weight = 0.0F;
                float Distance = 0.0F;
            };
            struct LightProbeCandidate final
            {
                GpuSpatialLightProbeCandidate Gpu;
                AssetId StableId;
                std::int32_t Priority = 0;
            };
            std::vector<GpuSpatialSelectionDraw> gpuDraws;
            std::vector<GpuSpatialReflectionCandidate> gpuReflections;
            std::vector<GpuSpatialLightProbeCandidate> gpuLightProbes;
            std::vector<AssetSpatialSelectionRecord> cpuRecords;
            gpuDraws.reserve(draws.Opaque.Draws.size() + draws.Transparent.Draws.size());
            cpuRecords.reserve(draws.Opaque.Draws.size() + draws.Transparent.Draws.size());

            const auto prepareDraw = [&](PreparedSceneDraw& draw)
            {
                const auto* material =
                    draw.Material ? ResolveAssetMaterial(draw.Material, ToSdlSampleCount(surface.ActualSamples))
                                  : nullptr;
                if (!material || material->SpatialLightingAbiVersion != 3U || !draw.Item ||
                    (useGpuSelection && (draw.Item->AlwaysVisible ||
                                         !HasShaderOcclusionSupport(material->OcclusionSupport,
                                                                    ShaderOcclusionSupport::ConservativeBounds))))
                {
                    return;
                }
                const auto& item = *draw.Item;
                const auto submeshCount = ResolveMesh(item.Mesh).Submeshes.size();
                const bool freshPoseBounds = item.HasFreshCurrentPoseBounds(packet.FrameIndex, submeshCount);
                if ((useGpuSelection && RequiresConservativeCpuVisibility(item.VisibilityClass, freshPoseBounds)) ||
                    !Math::IsFinite(item.World))
                {
                    return;
                }

                const auto contribution = std::min<std::size_t>(item.ContributionOrder, contexts.size() - 1U);
                const auto& context = contexts[contribution];
                if (!context.LightingSet)
                    return;
                const auto worldPosition = Math::TransformPoint(item.World, {});
                if (!Math::IsFinite(worldPosition))
                    return;

                std::vector<ReflectionCandidate> reflections;
                reflections.reserve(context.ReflectionProbes.size());
                for (std::size_t index = 0; index < context.ReflectionProbes.size(); ++index)
                {
                    const auto& probe = context.ReflectionProbes[index];
                    if (!probe.Entity || !Math::IsFinite(probe.LocalToWorld) || !Math::IsFinite(probe.WorldToLocal) ||
                        !Math::IsFinite(probe.BoxExtents) || probe.BoxExtents.X <= 0.0F || probe.BoxExtents.Y <= 0.0F ||
                        probe.BoxExtents.Z <= 0.0F || !std::isfinite(probe.BlendDistance) ||
                        probe.BlendDistance < 0.0F || !std::isfinite(probe.Intensity))
                    {
                        continue;
                    }
                    const auto weight = ReflectionWeight(worldPosition, probe);
                    if (weight <= 0.0F)
                        continue;
                    if (useGpuSelection && !DisplacementBounds::WhollyContained(
                                               draw.Submesh.Bounds, item.World, probe.WorldToLocal, probe.BoxExtents,
                                               material->MaximumWorldPositionDisplacementRadius))
                        return;

                    auto cubeIndex = probe.CubeIndex;
                    const auto binding = std::ranges::find(context.LightingSet->ReflectionProbes, probe.Entity,
                                                           &ReflectionProbeBinding::Probe);
                    if (binding != context.LightingSet->ReflectionProbes.end())
                        cubeIndex = binding->CubeIndex;
                    GpuSpatialReflectionCandidate gpu;
                    gpu.Descriptor.WorldToLocal = probe.WorldToLocal;
                    gpu.Descriptor.LocalToWorld = probe.LocalToWorld;
                    gpu.Descriptor.ExtentsWeight = {probe.BoxExtents.X, probe.BoxExtents.Y, probe.BoxExtents.Z, weight};
                    gpu.Descriptor.Parameters = {static_cast<float>(cubeIndex), probe.Intensity,
                                                 probe.BoxProjection ? 1.0F : 0.0F,
                                                 static_cast<float>(std::max(context.ReflectionMipLevels, 1U) - 1U)};
                    gpu.Metadata = {FlatReflectionProbeIndex(layout, static_cast<std::uint32_t>(contribution),
                                                             static_cast<std::uint32_t>(index)),
                                    std::bit_cast<std::uint32_t>(probe.Importance), 0U, 0U};
                    reflections.push_back({gpu, probe.Entity, probe.Importance, weight,
                                           Distance(worldPosition, Math::TransformPoint(probe.LocalToWorld, {}))});
                }
                std::ranges::sort(reflections,
                                  [](const auto& left, const auto& right)
                                  {
                                      if (left.Importance != right.Importance)
                                          return left.Importance > right.Importance;
                                      if (left.Weight != right.Weight)
                                          return left.Weight > right.Weight;
                                      if (left.Distance != right.Distance)
                                          return left.Distance < right.Distance;
                                      if (left.StableId != right.StableId)
                                          return left.StableId < right.StableId;
                                      return left.Gpu.Metadata[0] < right.Gpu.Metadata[0];
                                  });

                std::vector<LightProbeCandidate> lightProbes;
                lightProbes.reserve(context.LightProbeVolumes.size());
                for (std::size_t index = 0; index < context.LightProbeVolumes.size(); ++index)
                {
                    const auto& volume = context.LightProbeVolumes[index];
                    if (!Math::IsFinite(volume.WorldToLocal) || !Math::IsFinite(volume.BoxExtents) ||
                        volume.BoxExtents.X <= 0.0F || volume.BoxExtents.Y <= 0.0F || volume.BoxExtents.Z <= 0.0F)
                    {
                        continue;
                    }
                    const auto binding = std::ranges::find(context.LightingSet->LightProbeVolumes,
                                                           volume.Entity.Value(), &LightProbeVolumeBinding::Volume);
                    if (binding == context.LightingSet->LightProbeVolumes.end())
                        continue;
                    const auto data = ResolveLightProbeVolume(binding->Data);
                    if (!data)
                        continue;
                    const auto coefficients = Detail::SampleLightProbeCoefficients(
                        data->Definition(), Math::TransformPoint(volume.WorldToLocal, worldPosition));
                    if (!coefficients)
                        continue;
                    if (useGpuSelection && !DisplacementBounds::WhollyContained(
                                               draw.Submesh.Bounds, item.World, volume.WorldToLocal, volume.BoxExtents,
                                               material->MaximumWorldPositionDisplacementRadius))
                        return;

                    GpuSpatialLightProbeCandidate gpu;
                    for (std::size_t coefficient = 0; coefficient < coefficients->size(); ++coefficient)
                    {
                        const auto value = (*coefficients)[coefficient];
                        gpu.ProbeIrradiance[coefficient] = {value.X, value.Y, value.Z, 0.0F};
                    }
                    gpu.Metadata[0] = FlatLightProbeVolumeIndex(layout, static_cast<std::uint32_t>(contribution),
                                                                static_cast<std::uint32_t>(index));
                    lightProbes.push_back({gpu, volume.Entity.Value(), volume.Priority});
                }
                std::ranges::sort(lightProbes,
                                  [](const auto& left, const auto& right)
                                  {
                                      if (left.Priority != right.Priority)
                                          return left.Priority > right.Priority;
                                      if (left.StableId != right.StableId)
                                          return left.StableId < right.StableId;
                                      return left.Gpu.Metadata[0] < right.Gpu.Metadata[0];
                                  });

                if (!useGpuSelection)
                {
                    if (cpuRecords.size() >= 65535U)
                        return;
                    AssetSpatialSelectionRecord record;
                    if (!reflections.empty())
                    {
                        const auto selectedImportance = reflections.front().Importance;
                        const auto selectedCount =
                            reflections.size() > 1U && reflections[1].Importance == selectedImportance ? 2U : 1U;
                        float totalWeight = 0.0F;
                        for (std::size_t index = 0; index < selectedCount; ++index)
                            totalWeight += reflections[index].Weight;
                        for (std::size_t index = 0; index < selectedCount; ++index)
                        {
                            record.ReflectionProbes[index] = reflections[index].Gpu.Descriptor;
                            record.ReflectionProbes[index].ExtentsWeight.W =
                                totalWeight > 1.0e-6F ? reflections[index].Weight / totalWeight : 0.0F;
                            record.Metadata[0] |= index == 0U ? AssetSpatialSelectionHasReflectionProbe0
                                                              : AssetSpatialSelectionHasReflectionProbe1;
                        }
                    }
                    if (!lightProbes.empty())
                    {
                        record.ProbeIrradiance = lightProbes.front().Gpu.ProbeIrradiance;
                        record.Metadata[0] |= AssetSpatialSelectionHasLightProbe;
                    }
                    draw.SpatialSelectionRecordIndex = static_cast<std::uint32_t>(cpuRecords.size());
                    cpuRecords.push_back(record);
                    return;
                }

                if (reflections.size() > MaximumSpatialSelectionCandidatesPerDraw ||
                    lightProbes.size() > MaximumSpatialSelectionCandidatesPerDraw ||
                    gpuDraws.size() >= static_cast<std::uint64_t>(MaximumGpuDispatchGroupsPerDimension) * 64U ||
                    gpuReflections.size() > std::numeric_limits<std::uint32_t>::max() - reflections.size() ||
                    gpuLightProbes.size() > std::numeric_limits<std::uint32_t>::max() - lightProbes.size())
                {
                    return;
                }

                GpuSpatialSelectionDraw gpuDraw;
                gpuDraw.Ranges = {
                    static_cast<std::uint32_t>(gpuReflections.size()), static_cast<std::uint32_t>(reflections.size()),
                    static_cast<std::uint32_t>(gpuLightProbes.size()), static_cast<std::uint32_t>(lightProbes.size())};
                std::ranges::transform(reflections, std::back_inserter(gpuReflections), &ReflectionCandidate::Gpu);
                std::ranges::transform(lightProbes, std::back_inserter(gpuLightProbes), &LightProbeCandidate::Gpu);
                draw.SpatialSelectionRecordIndex = static_cast<std::uint32_t>(gpuDraws.size());
                gpuDraws.push_back(gpuDraw);
            };
            for (auto& draw : draws.Opaque.Draws)
                prepareDraw(draw);
            for (auto& draw : draws.Transparent.Draws)
                prepareDraw(draw);
            if (useGpuSelection ? gpuDraws.empty() : cpuRecords.empty())
                return false;

            if (!useGpuSelection)
            {
                const auto bytes = std::as_bytes(std::span(cpuRecords));
                const auto required = GrowCapacity(bytes.size());
                if (!resources.OutputRecords.Buffer || resources.OutputRecords.CapacityBytes < required)
                {
                    SDL_GPUBufferCreateInfo information{};
                    information.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                    information.size = required;
                    auto* replacement = SDL_CreateGPUBuffer(Device, &information);
                    if (!replacement)
                        throw std::runtime_error("SDL_CreateGPUBuffer(CPU spatial selection) failed: " +
                                                 LastSdlError());
                    Retire(std::exchange(resources.OutputRecords.Buffer, replacement));
                    resources.OutputRecords.CapacityBytes = required;
                }
                const auto uploadCapacity = GrowCapacity(bytes.size());
                if (!resources.Upload || resources.UploadCapacityBytes < uploadCapacity)
                {
                    SDL_GPUTransferBufferCreateInfo information{};
                    information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    information.size = uploadCapacity;
                    auto* replacement = SDL_CreateGPUTransferBuffer(Device, &information);
                    if (!replacement)
                    {
                        throw std::runtime_error("SDL_CreateGPUTransferBuffer(CPU spatial selection) failed: " +
                                                 LastSdlError());
                    }
                    Retire(std::exchange(resources.Upload, replacement));
                    resources.UploadCapacityBytes = uploadCapacity;
                }
                auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, resources.Upload, true));
                if (!mapped)
                    throw std::runtime_error("SDL_MapGPUTransferBuffer(CPU spatial selection) failed: " +
                                             LastSdlError());
                std::memcpy(mapped, bytes.data(), bytes.size());
                SDL_UnmapGPUTransferBuffer(Device, resources.Upload);
                auto* copy = SDL_BeginGPUCopyPass(commands);
                if (!copy)
                    throw std::runtime_error("SDL_BeginGPUCopyPass(CPU spatial selection) failed: " + LastSdlError());
                const SDL_GPUTransferBufferLocation source{resources.Upload, 0U};
                const SDL_GPUBufferRegion destination{resources.OutputRecords.Buffer, 0U,
                                                      static_cast<std::uint32_t>(bytes.size())};
                SDL_UploadToGPUBuffer(copy, &source, &destination, true);
                SDL_EndGPUCopyPass(copy);
                resources.RecordCount = static_cast<std::uint32_t>(cpuRecords.size());
                resources.SpatialMaskCount = 0U;
                resources.DispatchSucceeded = true;
                resources.TakeOwnership(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                        deviceGeneration);
                return true;
            }

            const std::array<GpuSpatialReflectionCandidate, 1> emptyReflection{};
            const std::array<GpuSpatialLightProbeCandidate, 1> emptyLightProbe{};
            const auto reflectionBytes = gpuReflections.empty() ? std::as_bytes(std::span(emptyReflection))
                                                                : std::as_bytes(std::span(gpuReflections));
            const auto lightProbeBytes = gpuLightProbes.empty() ? std::as_bytes(std::span(emptyLightProbe))
                                                                : std::as_bytes(std::span(gpuLightProbes));
            const std::array payloads{std::as_bytes(std::span(gpuDraws)), reflectionBytes, lightProbeBytes};
            const std::array usage{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                                   SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                                   SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                       SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ};
            const std::array required{GrowCapacity(payloads[0].size()), GrowCapacity(payloads[1].size()),
                                      GrowCapacity(payloads[2].size()),
                                      GrowCapacity(gpuDraws.size() * sizeof(AssetSpatialSelectionRecord))};
            const auto createOrGrow =
                [&](DynamicGpuBuffer& buffer, const std::uint32_t capacity, const SDL_GPUBufferUsageFlags bufferUsage)
            {
                if (buffer.Buffer && buffer.CapacityBytes >= capacity)
                    return;
                SDL_GPUBufferCreateInfo information{};
                information.usage = bufferUsage;
                information.size = capacity;
                auto* replacement = SDL_CreateGPUBuffer(Device, &information);
                if (!replacement)
                    throw std::runtime_error("SDL_CreateGPUBuffer(spatial selection) failed: " + LastSdlError());
                Retire(std::exchange(buffer.Buffer, replacement));
                buffer.CapacityBytes = capacity;
            };
            createOrGrow(resources.Draws, required[0], usage[0]);
            createOrGrow(resources.ReflectionCandidates, required[1], usage[1]);
            createOrGrow(resources.LightProbeCandidates, required[2], usage[2]);
            createOrGrow(resources.OutputRecords, required[3], usage[3]);

            std::array<std::size_t, 3> offsets{};
            std::size_t uploadBytes = 0;
            for (std::size_t index = 0; index < payloads.size(); ++index)
            {
                offsets[index] = uploadBytes;
                uploadBytes = AlignUpload(uploadBytes + payloads[index].size());
            }
            const auto uploadCapacity = GrowCapacity(uploadBytes);
            if (!resources.Upload || resources.UploadCapacityBytes < uploadCapacity)
            {
                SDL_GPUTransferBufferCreateInfo information{};
                information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                information.size = uploadCapacity;
                auto* replacement = SDL_CreateGPUTransferBuffer(Device, &information);
                if (!replacement)
                {
                    throw std::runtime_error("SDL_CreateGPUTransferBuffer(spatial selection) failed: " +
                                             LastSdlError());
                }
                Retire(std::exchange(resources.Upload, replacement));
                resources.UploadCapacityBytes = uploadCapacity;
            }

            auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, resources.Upload, true));
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(spatial selection) failed: " + LastSdlError());
            for (std::size_t index = 0; index < payloads.size(); ++index)
                std::memcpy(mapped + offsets[index], payloads[index].data(), payloads[index].size());
            SDL_UnmapGPUTransferBuffer(Device, resources.Upload);

            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
                throw std::runtime_error("SDL_BeginGPUCopyPass(spatial selection) failed: " + LastSdlError());
            const std::array destinations{resources.Draws.Buffer, resources.ReflectionCandidates.Buffer,
                                          resources.LightProbeCandidates.Buffer};
            for (std::size_t index = 0; index < payloads.size(); ++index)
            {
                const SDL_GPUTransferBufferLocation source{resources.Upload,
                                                           static_cast<std::uint32_t>(offsets[index])};
                const SDL_GPUBufferRegion destination{destinations[index], 0,
                                                      static_cast<std::uint32_t>(payloads[index].size())};
                SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            }
            SDL_EndGPUCopyPass(copy);

            resources.RecordCount = static_cast<std::uint32_t>(gpuDraws.size());
            resources.SpatialMaskCount = static_cast<std::uint32_t>(expectedMaskCount);
            resources.TakeOwnership(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch, deviceGeneration);
            return true;
        }
        catch (const GpuDeviceLostError&)
        {
            invalidateDraws();
            throw;
        }
        catch (const std::exception& error)
        {
            RethrowIfDeviceLost("spatial-selection preparation");
            invalidateDraws();
            resources.OwnershipValid = false;
            resources.DispatchSucceeded = false;
            resources.RecordCount = 0;
            KEIRE_CORE_WARN("GPU spatial selection preparation failed; ABI-v3 draws remain fail-visible: {}",
                            error.what());
            return false;
        }
    }

    void RenderSharedState::RecordSpatialSelection(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                   const SceneRenderPacket& packet,
                                                   const PreparedGpuOcclusion& occlusion)
    {
        auto& resources = surface.ActiveWorkset().SpatialSelection;
        if (resources.DispatchSucceeded && resources.SpatialMaskCount == 0U)
            return;
        resources.DispatchSucceeded = false;
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        if (!commands || !SpatialSelectionPipeline || !occlusion.Enabled || !occlusion.Resources ||
            !resources.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch, deviceGeneration) ||
            !occlusion.Resources->OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch,
                                          deviceGeneration) ||
            !resources.Draws.Buffer || !resources.ReflectionCandidates.Buffer ||
            !resources.LightProbeCandidates.Buffer || !resources.OutputRecords.Buffer ||
            !occlusion.Resources->SpatialVolumeVisibilityMask.Buffer || resources.RecordCount == 0U ||
            resources.SpatialMaskCount != occlusion.Resources->SpatialVolumeVisibilityCount)
        {
            return;
        }

        try
        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU spatial lighting selection");
            const SDL_GPUStorageBufferReadWriteBinding write{resources.OutputRecords.Buffer, false};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &write, 1U);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(spatial selection) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, SpatialSelectionPipeline);
            const std::array read{resources.Draws.Buffer, resources.ReflectionCandidates.Buffer,
                                  resources.LightProbeCandidates.Buffer,
                                  occlusion.Resources->SpatialVolumeVisibilityMask.Buffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0U, read.data(), static_cast<std::uint32_t>(read.size()));
            const std::array<std::uint32_t, 4> dispatch{resources.RecordCount, resources.SpatialMaskCount, 0U, 0U};
            SDL_PushGPUComputeUniformData(commands, 0U, dispatch.data(), sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (resources.RecordCount + 63U) / 64U, 1U, 1U);
            SDL_EndGPUComputePass(pass);
            resources.DispatchSucceeded = true;
            ++Statistics.GpuOcclusionDispatches;
            ++Statistics.Passes;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            RethrowIfDeviceLost("spatial-selection dispatch");
            resources.DispatchSucceeded = false;
            KEIRE_CORE_WARN("GPU spatial selection dispatch failed; ABI-v3 draws remain fail-visible: {}",
                            error.what());
        }
    }
} // namespace Keire::RenderBackend
