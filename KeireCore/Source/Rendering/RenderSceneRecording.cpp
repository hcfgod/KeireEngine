#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"
#include "KeireInternal/Rendering/ForwardPlusInternal.h"
#include "KeireInternal/Rendering/GlobalIlluminationPolicyInternal.h"
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
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace
{
    // Dense project shaders can bind sixteen or more samplers per draw. CPU VFX now coalesces compatible particles,
    // leaving enough command-buffer-local D3D12 descriptor capacity for this scene budget without the submission
    // overhead of the former twelve-batch workaround.
    constexpr std::size_t MaximumSceneBatchesPerCommandBuffer = 32U;

    using Keire::RenderBackend::GeometryDetail::IsFrustumVisible;
    using Keire::RenderBackend::GeometryDetail::ProjectedHeight;

    [[nodiscard]] bool PackMaterialProperty(const Keire::MaterialPropertyValue& value,
                                            const Keire::ShaderPropertyType type, Keire::Vector4& packed) noexcept
    {
        switch (type)
        {
        case Keire::ShaderPropertyType::Scalar:
            if (const auto* selected = std::get_if<float>(&value))
                packed = {*selected, 0.0F, 0.0F, 0.0F};
            else
                return false;
            break;
        case Keire::ShaderPropertyType::Vector2:
            if (const auto* selected = std::get_if<Keire::Vector2>(&value))
                packed = {selected->X, selected->Y, 0.0F, 0.0F};
            else
                return false;
            break;
        case Keire::ShaderPropertyType::Vector3:
            if (const auto* selected = std::get_if<Keire::Vector3>(&value))
                packed = {selected->X, selected->Y, selected->Z, 0.0F};
            else
                return false;
            break;
        case Keire::ShaderPropertyType::Vector4:
            if (const auto* selected = std::get_if<Keire::Vector4>(&value))
                packed = *selected;
            else
                return false;
            break;
        case Keire::ShaderPropertyType::Color:
            if (const auto* selected = std::get_if<Keire::Color>(&value))
                packed = {selected->Red, selected->Green, selected->Blue, selected->Alpha};
            else
                return false;
            break;
        case Keire::ShaderPropertyType::Texture2D:
            return false;
        }
        return true;
    }

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
    PreparedSceneDrawLists RenderSharedState::PrepareSceneDrawLists(SDL_GPUCommandBuffer* commands,
                                                                    RenderSurfaceState& surface,
                                                                    const SceneRenderPacket& packet)
    {
        PreparedSceneDrawLists result;
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        const auto& camera = packet.Camera;
        const auto& workset = surface.ActiveWorkset();
        const bool deferredActive =
            surface.ActiveFeatureSelection.EffectivePath == RenderPath::DeferredHybrid && workset.Depth &&
            workset.GBufferBaseColorMetallic && workset.GBufferNormalRoughness && workset.GBufferMaterial &&
            workset.GBufferLighting && workset.DBufferBaseColor && workset.DBufferNormal && workset.DBufferMaterial;
        const auto illumination = ResolveGlobalIlluminationPolicy(
            surface.ActiveFeatureSelection.EffectiveGlobalIllumination, packet.Environment.RequestedIrradynQuality);
        struct BuiltInSpatialBatchContext final
        {
            std::unordered_set<AssetId> LightmappedRenderers;
            bool HasSpatialVolumes = false;
        };
        std::vector<BuiltInSpatialBatchContext> builtInSpatialBatchContexts;
        builtInSpatialBatchContexts.reserve(std::max<std::size_t>(packet.SpatialContributions.size(), 1U));
        const auto appendBuiltInSpatialBatchContext = [&](const AssetId bakedLighting,
                                                          const std::size_t reflectionProbeCount,
                                                          const std::size_t lightProbeVolumeCount)
        {
            BuiltInSpatialBatchContext context;
            if (illumination.BakedLighting)
            {
                const auto lightingSet = ResolveLightingSet(bakedLighting);
                if (lightingSet)
                {
                    context.LightmappedRenderers.reserve(lightingSet->Definition().Renderers.size());
                    for (const auto& renderer : lightingSet->Definition().Renderers)
                        context.LightmappedRenderers.insert(renderer.Renderer);
                }
                context.HasSpatialVolumes = reflectionProbeCount != 0U || lightProbeVolumeCount != 0U;
            }
            builtInSpatialBatchContexts.push_back(std::move(context));
        };
        for (const auto& contribution : packet.SpatialContributions)
        {
            appendBuiltInSpatialBatchContext(contribution.BakedLighting, contribution.ReflectionProbes.size(),
                                             contribution.LightProbeVolumes.size());
        }
        if (builtInSpatialBatchContexts.empty())
        {
            appendBuiltInSpatialBatchContext(packet.BakedLighting, packet.ReflectionProbes.size(),
                                             packet.LightProbeVolumes.size());
        }
        const auto builtInSpatialBatchContextFor =
            [&](const std::uint32_t contributionOrder) -> const BuiltInSpatialBatchContext&
        {
            return builtInSpatialBatchContexts[std::min<std::size_t>(contributionOrder,
                                                                     builtInSpatialBatchContexts.size() - 1U)];
        };
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Submeshes.empty())
                continue;
            const bool freshPoseBounds = item.HasFreshCurrentPoseBounds(packet.FrameIndex, mesh.Submeshes.size());
            const bool forceConservativeVisibility =
                item.AlwaysVisible || RequiresConservativeCpuVisibility(item.VisibilityClass, freshPoseBounds);
            const auto viewFromLocal = Math::Multiply(camera.View, item.World);
            const auto clipFromWorld = Math::Multiply(camera.Projection, camera.View);
            std::uint32_t firstSubmesh = 0;
            std::uint32_t submeshCount = static_cast<std::uint32_t>(mesh.Submeshes.size());
            const MeshBounds* selectedBounds =
                !freshPoseBounds && mesh.BoundsEncloseSubmeshes ? std::addressof(mesh.Bounds) : nullptr;
            if (!mesh.Lods.empty())
            {
                const auto height = ProjectedHeight(viewFromLocal, camera.Projection, mesh.Lods.front().Bounds);
                const auto selected =
                    std::ranges::find_if(mesh.Lods, [&](const auto& lod) { return height >= lod.MinimumScreenHeight; });
                const auto lodIndex = selected != mesh.Lods.end()
                                          ? static_cast<std::size_t>(selected - mesh.Lods.begin())
                                          : mesh.Lods.size() - 1U;
                const auto& lod = mesh.Lods[lodIndex];
                firstSubmesh = lod.FirstSubmesh;
                submeshCount = lod.SubmeshCount;
                selectedBounds =
                    !freshPoseBounds && mesh.LodBoundsEncloseSubmeshes[lodIndex] ? std::addressof(lod.Bounds) : nullptr;
            }
            const auto materialIdForSubmesh = [&](const MeshSubmesh& submesh)
            {
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    return item.Materials[submesh.MaterialSlot];
                if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    return mesh.DefaultMaterials[submesh.MaterialSlot];
                return AssetId{};
            };
            std::optional<float> aggregateDisplacementRadius = 0.0F;
            for (std::uint32_t offset = 0; offset < submeshCount; ++offset)
            {
                const auto materialId = materialIdForSubmesh(mesh.Submeshes[firstSubmesh + offset]);
                const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                if (materialId &&
                    (!material || !DisplacementBounds::IsKnown(material->MaximumWorldPositionDisplacementRadius)))
                {
                    aggregateDisplacementRadius.reset();
                    break;
                }
                if (material)
                {
                    aggregateDisplacementRadius =
                        std::max(*aggregateDisplacementRadius, *material->MaximumWorldPositionDisplacementRadius);
                }
            }
            const auto selectedWorldBounds =
                selectedBounds
                    ? DisplacementBounds::WorldBounds(*selectedBounds, item.World, aggregateDisplacementRadius)
                    : std::nullopt;
            if (selectedWorldBounds &&
                !IsFrustumVisible(clipFromWorld, *selectedWorldBounds, forceConservativeVisibility))
            {
                Statistics.CulledSubmeshes += submeshCount;
                continue;
            }
            for (std::uint32_t offset = 0; offset < submeshCount; ++offset)
            {
                const auto submeshIndex = firstSubmesh + offset;
                auto submesh = mesh.Submeshes[submeshIndex];
                if (freshPoseBounds)
                    submesh.Bounds = item.CurrentPoseSubmeshBounds[submeshIndex];
                const auto materialId = materialIdForSubmesh(submesh);
                const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                const auto worldBounds =
                    DisplacementBounds::WorldBounds(submesh.Bounds, item.World,
                                                    material     ? material->MaximumWorldPositionDisplacementRadius
                                                    : materialId ? std::nullopt
                                                                 : std::optional<float>{0.0F});
                if (worldBounds && !IsFrustumVisible(clipFromWorld, *worldBounds, forceConservativeVisibility))
                {
                    ++Statistics.CulledSubmeshes;
                    continue;
                }
                MaterialSurfaceState surfaceState;
                if (material)
                    surfaceState = material->Surface;
                const Vector3 center{(submesh.Bounds.Minimum.X + submesh.Bounds.Maximum.X) * 0.5F,
                                     (submesh.Bounds.Minimum.Y + submesh.Bounds.Maximum.Y) * 0.5F,
                                     (submesh.Bounds.Minimum.Z + submesh.Bounds.Maximum.Z) * 0.5F};
                const bool decal = material && material->PassPipeline(RuntimeMaterialPassRole::DecalDBuffer);
                auto& destination = decal && deferredActive                                  ? result.Decals
                                    : IsTransparentMaterial(surfaceState.AlphaMode) || decal ? result.Transparent
                                                                                             : result.Opaque;
                destination.Draws.push_back({&item, submesh, materialId, surfaceState,
                                             Math::TransformPoint(viewFromLocal, center).Z, submeshIndex});
                ++Statistics.VisibleSubmeshes;
            }
        }

        const auto sortDraws = [](std::vector<PreparedSceneDraw>& draws)
        {
            std::ranges::stable_sort(
                draws,
                [](const PreparedSceneDraw& left, const PreparedSceneDraw& right)
                {
                    const bool blended = IsTransparentMaterial(left.Surface.AlphaMode);
                    if (blended && left.Depth != right.Depth)
                        return Detail::TransparentBackToFront(left.Depth, right.Depth);
                    if (!blended)
                    {
                        const auto leftKey =
                            std::tie(left.Surface.AlphaMode, left.Material, left.Item->Mesh, left.SubmeshIndex,
                                     left.Item->ReceiveShadows, left.Item->CastShadows, left.Depth);
                        const auto rightKey =
                            std::tie(right.Surface.AlphaMode, right.Material, right.Item->Mesh, right.SubmeshIndex,
                                     right.Item->ReceiveShadows, right.Item->CastShadows, right.Depth);
                        if (leftKey != rightKey)
                            return leftKey < rightKey;
                    }
                    if (left.Item->ContributionOrder != right.Item->ContributionOrder)
                        return left.Item->ContributionOrder < right.Item->ContributionOrder;
                    if (left.Item->Entity != right.Item->Entity)
                        return left.Item->Entity < right.Item->Entity;
                    return left.SubmeshIndex < right.SubmeshIndex;
                });
        };
        sortDraws(result.Opaque.Draws);
        sortDraws(result.Transparent.Draws);
        sortDraws(result.Decals.Draws);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        {
            std::scoped_lock lock(PublicationMutex);
            LastPreparedOpaqueContributionOrder.clear();
            LastPreparedOpaqueEntities.clear();
            LastPreparedOpaqueContributionOrder.reserve(result.Opaque.Draws.size());
            LastPreparedOpaqueEntities.reserve(result.Opaque.Draws.size());
            for (const auto& draw : result.Opaque.Draws)
            {
                LastPreparedOpaqueContributionOrder.push_back(draw.Item->ContributionOrder);
                LastPreparedOpaqueEntities.push_back(draw.Item->Entity);
            }
            LastPreparedTransparentContributionOrder.clear();
            LastPreparedTransparentEntities.clear();
            LastPreparedTransparentContributionOrder.reserve(result.Transparent.Draws.size());
            LastPreparedTransparentEntities.reserve(result.Transparent.Draws.size());
            for (const auto& draw : result.Transparent.Draws)
            {
                LastPreparedTransparentContributionOrder.push_back(draw.Item->ContributionOrder);
                LastPreparedTransparentEntities.push_back(draw.Item->Entity);
            }
        }
#endif

        std::vector<GpuInstanceUniform> instanceData;
        instanceData.reserve(result.Opaque.Draws.size() + result.Transparent.Draws.size() + result.Decals.Draws.size());
        const auto prepareBatches = [&](PreparedSceneDrawList& list)
        {
            std::vector<InstanceBatchKey> instanceKeys;
            instanceKeys.reserve(list.Draws.size());
            for (const auto& draw : list.Draws)
            {
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                const auto& spatialBatchContext = builtInSpatialBatchContextFor(draw.Item->ContributionOrder);
                const bool requiresUniqueBuiltInSpatialData =
                    spatialBatchContext.HasSpatialVolumes ||
                    spatialBatchContext.LightmappedRenderers.contains(draw.Item->Entity.Value());
                const bool usesBuiltInInstancing = !material && !requiresUniqueBuiltInSpatialData &&
                                                   !draw.Item->SkinnedAssetVertices &&
                                                   !draw.Item->SkinnedBuiltinVertices;
                instanceKeys.push_back(
                    {draw.Item->Mesh, draw.Material, draw.SubmeshIndex, draw.Surface.AlphaMode,
                     draw.Item->ReceiveShadows, draw.Item->CastShadows,
                     usesBuiltInInstancing ||
                         (material && material->UsesInstancing && !draw.Item->SkinnedAssetVertices &&
                          !draw.Item->SkinnedBuiltinVertices && material->SpatialLightingAbiVersion != 3U &&
                          draw.Item->MaterialProperties.empty() && draw.Item->MaterialInstanceProperties.empty())});
            }
            const auto batches = BuildInstanceBatches(instanceKeys);
            list.Batches.reserve(batches.size());
            for (const auto batch : batches)
            {
                const auto drawIndex = static_cast<std::size_t>(batch.First);
                const auto& draw = list.Draws[drawIndex];
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                std::uint32_t instanceDataFirst = 0;
                std::uint32_t instanceDataCount = 0;
                if ((!material && !draw.Item->SkinnedAssetVertices && !draw.Item->SkinnedBuiltinVertices) ||
                    (material && material->UsesInstancing))
                {
                    if (instanceData.size() > std::numeric_limits<std::uint32_t>::max() - batch.Count)
                        throw std::length_error("Scene instance data exceeds the renderer's 32-bit draw limit.");
                    instanceDataFirst = static_cast<std::uint32_t>(instanceData.size());
                    instanceDataCount = batch.Count;
                    for (std::uint32_t instance = 0; instance < batch.Count; ++instance)
                    {
                        const auto& instanceDraw = list.Draws[drawIndex + instance];
                        instanceData.push_back({instanceDraw.Item->World,
                                                Transpose(Math::Inverse(instanceDraw.Item->World)),
                                                instanceDraw.Item->Tint});
                    }
                }
                list.Batches.push_back({batch.First, batch.Count, batch.GpuFirstInstance(), instanceDataFirst,
                                        instanceDataCount, nullptr});
            }
        };
        prepareBatches(result.Opaque);
        prepareBatches(result.Transparent);
        prepareBatches(result.Decals);
        UploadSceneInstances(commands, surface, result, instanceData);

        if (packet.Environment.Environment)
            (void)ResolveTexture(packet.Environment.Environment);
        const auto resolvePropertyTextures = [&](const auto& properties)
        {
            for (const auto& [name, value] : properties)
            {
                (void)name;
                if (const auto* texture = std::get_if<AssetId>(&value); texture && *texture)
                    (void)ResolveTexture(*texture);
            }
        };
        resolvePropertyTextures(packet.GlobalMaterialProperties);
        for (const auto& item : packet.DrawItems)
        {
            resolvePropertyTextures(item.MaterialProperties);
            for (const auto& [slot, properties] : item.MaterialInstanceProperties)
            {
                (void)slot;
                resolvePropertyTextures(properties);
            }
        }
        for (const auto& snapshot : packet.VfxSnapshots)
            for (const auto& particle : snapshot.Particles())
                if (particle.Renderer != VfxRendererType::Sprite && particle.Mesh)
                    (void)ResolveMesh(particle.Mesh);
        return result;
    }

    void RenderSharedState::DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                      const ShadowFrameData& shadows, const SceneDrawPhase phase,
                                      const PreparedSceneDrawList& prepared, const std::size_t firstBatch,
                                      const std::size_t batchCount)
    {
        const bool deferredDataPass =
            phase == SceneDrawPhase::DeferredDepthVelocity || phase == SceneDrawPhase::DeferredGBufferStandard ||
            phase == SceneDrawPhase::DeferredGBufferExtended || phase == SceneDrawPhase::DeferredDecal;
        const auto samples = deferredDataPass ? SDL_GPU_SAMPLECOUNT_1 : ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;
        if (surface.ActiveWorkset().ForwardPlus.Empty())
            throw std::logic_error("Forward+ GPU resources were not prepared before scene recording.");
        std::array<SDL_GPUBuffer*, 3> forwardPlusBuffers{surface.ActiveWorkset().ForwardPlus.Lights,
                                                         surface.ActiveWorkset().ForwardPlus.Tiles,
                                                         surface.ActiveWorkset().ForwardPlus.LightIndices};
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        const bool fallbackSpatialSelectionValid =
            SpatialSelectionFallbackBuffer && SpatialSelectionFallbackDeviceGeneration == deviceGeneration;
        const auto& requestedEnvironment =
            packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
        const auto& environment = requestedEnvironment.HasDiffuseIrradiance ? requestedEnvironment : DefaultSkyTexture;
        const auto illumination = ResolveGlobalIlluminationPolicy(
            surface.ActiveFeatureSelection.EffectiveGlobalIllumination, packet.Environment.RequestedIrradynQuality);
        AssetEnvironmentUniforms environmentUniforms{};
        environmentUniforms.DiffuseIrradiance = environment.DiffuseIrradiance;
        environmentUniforms.Parameters = {
            packet.Environment.EnvironmentRotationDegrees,
            illumination.EnvironmentDiffuse ? packet.Environment.EnvironmentDiffuseIntensity : 0.0F,
            illumination.EnvironmentSpecular ? packet.Environment.EnvironmentSpecularIntensity : 0.0F,
            static_cast<float>(environment.MipLevels - 1U)};
        environmentUniforms.Encoding = {static_cast<float>(environment.EnvironmentLayout) +
                                            (environment.HdrEncoded ? 16.0F : 0.0F),
                                        0.0F, 0.0F, 0.0F};
        const auto cameraPosition = Math::TransformPoint(Math::Inverse(camera.View), {});
        const std::array environmentBindings{
            SDL_GPUTextureSamplerBinding{environment.Texture, environment.Sampler},
            SDL_GPUTextureSamplerBinding{BrdfIntegrationLut.Texture, BrdfIntegrationLut.Sampler}};
        struct SpatialContext final
        {
            Ref<const LightingSetAsset> Asset;
            const LightingSetDefinition* LightingSet = nullptr;
            std::array<SDL_GPUTextureSamplerBinding, 5> Bindings{};
            std::vector<Detail::SpatialReflectionProbe> ReflectionProbes;
            std::vector<SceneLightProbeVolume> LightProbeVolumes;
            std::uint32_t ReflectionMipLevels = 1;
            bool LightmapsRgbe = false;
            bool ReflectionsRgbe = false;
        };
        std::vector<SpatialContext> spatialContexts;
        spatialContexts.reserve(std::max<std::size_t>(packet.SpatialContributions.size(), 1U));
        const auto appendSpatialContext = [&](const AssetId bakedLighting,
                                              std::vector<Detail::SpatialReflectionProbe> reflectionProbes,
                                              std::vector<SceneLightProbeVolume> lightProbeVolumes)
        {
            SpatialContext context;
            context.Asset =
                illumination.BakedLighting ? ResolveLightingSet(bakedLighting) : Ref<const LightingSetAsset>{};
            context.LightingSet = context.Asset ? &context.Asset->Definition() : nullptr;
            const auto& lightmaps =
                context.LightingSet ? ResolveLightingTexture(context.LightingSet->Lightmaps) : DefaultLightingArray;
            const auto& directionality = context.LightingSet
                                             ? ResolveLightingTexture(context.LightingSet->Directionality)
                                             : DefaultLightingArray;
            const auto& shadowMasks = context.LightingSet
                                          ? ResolveLightingTexture(context.LightingSet->ShadowMasks, false, true)
                                          : DefaultLightingMaskArray;
            const auto& reflections = context.LightingSet
                                          ? ResolveLightingTexture(context.LightingSet->ReflectionCubemaps, true)
                                          : DefaultReflectionCubeArray;
            context.Bindings[0] = {lightmaps.Texture, lightmaps.Sampler};
            context.Bindings[1] = {directionality.Texture, directionality.Sampler};
            context.Bindings[2] = {shadowMasks.Texture, shadowMasks.Sampler};
            context.Bindings[3] = {reflections.Texture, reflections.Sampler};
            context.Bindings[4] = {WhiteTexture.Texture, WhiteTexture.Sampler};
            context.ReflectionMipLevels = reflections.MipLevels;
            context.LightmapsRgbe = lightmaps.HdrEncoded;
            context.ReflectionsRgbe = reflections.HdrEncoded;
            if (illumination.BakedLighting)
            {
                context.ReflectionProbes = std::move(reflectionProbes);
                context.LightProbeVolumes = std::move(lightProbeVolumes);
            }
            if (context.LightingSet)
            {
                for (auto& probe : context.ReflectionProbes)
                {
                    const auto binding = std::ranges::find(context.LightingSet->ReflectionProbes, probe.Entity,
                                                           &ReflectionProbeBinding::Probe);
                    if (binding != context.LightingSet->ReflectionProbes.end())
                        probe.CubeIndex = binding->CubeIndex;
                }
            }
            spatialContexts.push_back(std::move(context));
        };
        for (const auto& contribution : packet.SpatialContributions)
            appendSpatialContext(contribution.BakedLighting, contribution.ReflectionProbes,
                                 contribution.LightProbeVolumes);
        if (spatialContexts.empty())
            appendSpatialContext(packet.BakedLighting, packet.ReflectionProbes, packet.LightProbeVolumes);
        const auto spatialContextFor = [&](const std::uint32_t contributionOrder) -> const SpatialContext&
        { return spatialContexts[std::min<std::size_t>(contributionOrder, spatialContexts.size() - 1U)]; };
        AssetSpatialLightingUniforms spatialBase{};
        spatialBase.LightmapScaleOffset = {1.0F, 1.0F, 0.0F, 0.0F};
        spatialBase.ViewProjection = Math::Multiply(camera.Projection, camera.View);
        std::uint32_t cookieCount = 0;
        std::array<AssetId, 8> cookieAssets{};
        const auto addCookie = [&](const AssetId cookie, const Vector2 scale, const Vector2 offset,
                                   const float rotationDegrees) -> float
        {
            if (!cookie || cookieCount >= 8U)
                return 0.0F;
            const auto slot = cookieCount++;
            cookieAssets[slot] = cookie;
            spatialBase.CookieTransforms[slot] = {scale.X, scale.Y, offset.X, offset.Y};
            auto& rotations = spatialBase.CookieRotations[slot / 4U];
            switch (slot % 4U)
            {
            case 0:
                rotations.X = rotationDegrees;
                break;
            case 1:
                rotations.Y = rotationDegrees;
                break;
            case 2:
                rotations.Z = rotationDegrees;
                break;
            default:
                rotations.W = rotationDegrees;
                break;
            }
            return static_cast<float>(slot + 1U);
        };
        const auto directionalCookie =
            addCookie(lighting.Cookie, lighting.CookieScale, lighting.CookieOffset, lighting.CookieRotationDegrees);
        spatialBase.DirectionalCookieAndContact = {directionalCookie, lighting.ContactShadows ? 1.0F : 0.0F, 0.35F,
                                                   0.0025F};
        std::array<float, MaximumShaderLocalLights> localCookieBindings{};
        for (std::size_t lightIndex = 0; lightIndex < std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
             ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            localCookieBindings[lightIndex] =
                addCookie(light.Cookie, light.CookieScale, light.CookieOffset, light.CookieRotationDegrees);
        }
        const auto& cookieAtlas = ResolveCookieAtlas(cookieAssets);
        for (auto& context : spatialContexts)
            context.Bindings[4] = {cookieAtlas.Texture, cookieAtlas.Sampler};
        const auto mixedLightChannel = [](const AssetId light, const LightingSetDefinition* lightingSet) -> float
        {
            if (!lightingSet || !light)
                return 0.0F;
            const auto found = std::ranges::find(lightingSet->MixedLights, light, &MixedLightBinding::Light);
            return found == lightingSet->MixedLights.end() ? 0.0F : static_cast<float>(found->ShadowMaskChannel + 1U);
        };
        const auto spatialUniforms = [&](const SceneDrawItem& item)
        {
            auto result = spatialBase;
            const auto& context = spatialContextFor(item.ContributionOrder);
            const auto* lightingSet = context.LightingSet;
            result.ShadowMaskParameters.X = lightingSet ? static_cast<float>(lightingSet->Renderers.size()) : 0.0F;
            result.ShadowMaskParameters.Y = context.LightmapsRgbe ? 1.0F : 0.0F;
            result.ShadowMaskParameters.Z = context.ReflectionsRgbe ? 1.0F : 0.0F;
            if (!lightingSet)
                return result;
            const auto renderer =
                std::ranges::find(lightingSet->Renderers, item.Entity.Value(), &LightmapRendererBinding::Renderer);
            if (renderer != lightingSet->Renderers.end())
            {
                result.LightmapScaleOffset = renderer->ScaleOffset;
                result.LightmapParameters.X = static_cast<float>(renderer->LightmapLayer);
                result.LightmapParameters.Y = static_cast<float>(renderer->ShadowMaskLayer);
                result.LightmapParameters.Z = 1.0F;
                result.LightmapParameters.W = mixedLightChannel(lighting.Entity.Value(), lightingSet);
            }
            const auto worldPosition = Math::TransformPoint(item.World, {});
            for (const auto& volume : context.LightProbeVolumes)
            {
                const auto binding = std::ranges::find(lightingSet->LightProbeVolumes, volume.Entity.Value(),
                                                       &LightProbeVolumeBinding::Volume);
                if (binding == lightingSet->LightProbeVolumes.end())
                    continue;
                const auto data = ResolveLightProbeVolume(binding->Data);
                if (!data)
                    continue;
                const auto coefficients = Detail::SampleLightProbeCoefficients(
                    data->Definition(), Math::TransformPoint(volume.WorldToLocal, worldPosition));
                if (!coefficients)
                    continue;
                for (std::size_t coefficient = 0; coefficient < coefficients->size(); ++coefficient)
                {
                    const auto& value = (*coefficients)[coefficient];
                    result.ProbeIrradiance[coefficient] = {value.X, value.Y, value.Z, 0.0F};
                }
                result.LightmapParameters.Z += 2.0F;
                break;
            }
            const auto selected = Detail::SelectReflectionProbes(worldPosition, context.ReflectionProbes, 2U);
            for (std::size_t index = 0; index < selected.size(); ++index)
            {
                const auto& selectedProbe = selected[index];
                auto& output = result.ReflectionProbes[index];
                output.WorldToLocal = selectedProbe.Probe->WorldToLocal;
                output.LocalToWorld = selectedProbe.Probe->LocalToWorld;
                output.ExtentsWeight = {selectedProbe.Probe->BoxExtents.X, selectedProbe.Probe->BoxExtents.Y,
                                        selectedProbe.Probe->BoxExtents.Z, selectedProbe.Weight};
                output.Parameters = {static_cast<float>(selectedProbe.Probe->CubeIndex), selectedProbe.Probe->Intensity,
                                     selectedProbe.Probe->BoxProjection ? 1.0F : 0.0F,
                                     static_cast<float>(std::max(context.ReflectionMipLevels, 1U) - 1U)};
            }
            return result;
        };

        if (firstBatch == 0U && (phase == SceneDrawPhase::Opaque || phase == SceneDrawPhase::DeferredSky) &&
            packet.Environment.SkyVisible && pipelines.Sky)
        {
            if (!environment.Empty())
            {
                const SkyUniforms sky{
                    Math::Inverse(camera.Projection),
                    Math::Inverse(camera.View),
                    {packet.Environment.EnvironmentRotationDegrees, packet.Environment.EnvironmentSpecularIntensity,
                     packet.Environment.Exposure,
                     static_cast<float>(environment.EnvironmentLayout) + (environment.HdrEncoded ? 16.0F : 0.0F)}};
                const SDL_GPUTextureSamplerBinding binding{environment.Texture, environment.Sampler};
                SDL_PushGPUFragmentUniformData(commands, 0, &sky, sizeof(sky));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Sky);
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                ++Statistics.DrawCalls;
            }
        }

        if (firstBatch == 0U &&
            (phase == SceneDrawPhase::Opaque || phase == SceneDrawPhase::DeferredForwardOpaqueTail) && packet.DrawGrid)
        {
            const GridUniforms grid{Math::Inverse(camera.Projection),
                                    Math::Inverse(camera.View),
                                    Math::Multiply(camera.Projection, camera.View),
                                    {1.0F, 10.0F, 0.45F, 0.7F}};
            SDL_PushGPUFragmentUniformData(commands, 0, &grid, sizeof(grid));
            SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            ++Statistics.DrawCalls;
        }

        AssetLocalLightUniforms localLights{};
        const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
        localLights.Counts.X = static_cast<float>(packet.LocalLights.size());
        localLights.Counts.Y = static_cast<float>(surface.ActiveWorkset().ForwardPlus.Columns);
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            auto& uniform = localLights.Lights[lightIndex];
            uniform.PositionRange = {light.Position.X, light.Position.Y, light.Position.Z, light.Range};
            uniform.DirectionOuter = {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine};
            uniform.ColorIntensity = {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green,
                                      light.ColorAndIntensity.Blue, light.ColorAndIntensity.Alpha};
            uniform.Parameters = {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F,
                                  0.0F};
            uniform.Parameters.Z =
                mixedLightChannel(light.Entity.Value(), spatialContextFor(light.ContributionOrder).LightingSet);
            uniform.Parameters.W = localCookieBindings[lightIndex] + (light.ContactShadows ? 16.0F : 0.0F);
        }
        enum class FragmentSlot2Binding : std::uint8_t
        {
            None,
            LocalLights,
            Shadows
        };
        auto fragmentSlot2Binding = FragmentSlot2Binding::None;
        AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
        AssetShadowUniforms disabledShadowUniforms{};
        for (auto& parameters : disabledShadowUniforms.Local.Parameters)
            parameters.X = -1.0F;
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex], light.ShadowStrength,
                                                           light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                           std::max(light.ShadowBias * 0.01F, 0.0001F)};
        }

        const auto batches = std::span(prepared.Batches).subspan(firstBatch, batchCount);
        for (const auto& batch : batches)
        {
            const auto drawIndex = static_cast<std::size_t>(batch.First);
            const auto& draw = prepared.Draws[drawIndex];
            const auto& item = *draw.Item;
            const auto& mesh = ResolveMesh(item.Mesh);
            const auto viewModel = Math::Multiply(camera.View, item.World);
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            SDL_GPUGraphicsPipeline* selectedPipeline = nullptr;
            bool builtInDeferredGBuffer = false;
            if (material)
            {
                switch (phase)
                {
                case SceneDrawPhase::Opaque:
                    selectedPipeline = material->Pipeline;
                    break;
                case SceneDrawPhase::Transparent:
                    selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::ForwardTransparent);
                    if (!selectedPipeline)
                        selectedPipeline = material->Pipeline;
                    break;
                case SceneDrawPhase::DeferredDepthVelocity:
                    selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::DepthVelocity);
                    break;
                case SceneDrawPhase::DeferredGBufferStandard:
                    selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::DeferredGBufferStandard);
                    break;
                case SceneDrawPhase::DeferredGBufferExtended:
                    selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::DeferredGBufferExtended);
                    break;
                case SceneDrawPhase::DeferredDecal:
                    selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::DecalDBuffer);
                    break;
                case SceneDrawPhase::DeferredForwardOpaqueTail:
                    if (!material->HasDeferredGBufferPipeline())
                    {
                        selectedPipeline = material->PassPipeline(RuntimeMaterialPassRole::ForwardOpaque);
                        if (!selectedPipeline)
                            selectedPipeline = material->Pipeline;
                    }
                    break;
                case SceneDrawPhase::DeferredSky:
                    break;
                }
            }
            else
            {
                switch (phase)
                {
                case SceneDrawPhase::Opaque:
                case SceneDrawPhase::Transparent:
                    selectedPipeline = pipelines.Cube;
                    break;
                case SceneDrawPhase::DeferredGBufferStandard:
                    if (!draw.Material)
                    {
                        selectedPipeline = DeferredGBufferPipeline;
                        builtInDeferredGBuffer = true;
                    }
                    break;
                case SceneDrawPhase::DeferredForwardOpaqueTail:
                    if (draw.Material)
                        selectedPipeline = pipelines.Cube;
                    break;
                case SceneDrawPhase::DeferredDepthVelocity:
                case SceneDrawPhase::DeferredGBufferExtended:
                case SceneDrawPhase::DeferredDecal:
                case SceneDrawPhase::DeferredSky:
                    break;
                }
            }
            if (!selectedPipeline)
                continue;
            const auto instanceCount = batch.Count;
            const auto& spatial = surface.ActiveWorkset().SpatialSelection;
            const auto& visibility = surface.ActiveWorkset().GpuOcclusion;
            const bool selectionOwned =
                spatial.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch, deviceGeneration) &&
                spatial.DispatchSucceeded && spatial.OutputRecords.Buffer;
            const bool cpuSelection = selectionOwned && spatial.SpatialMaskCount == 0U;
            const bool gpuSelection = selectionOwned && spatial.SpatialMaskCount != 0U &&
                                      visibility.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot,
                                                         surface.Epoch, deviceGeneration) &&
                                      visibility.SpatialVolumeVisibilityMask.Buffer &&
                                      visibility.SpatialVolumeVisibilityCount == spatial.SpatialMaskCount;
            const bool consumesSpatialSelection =
                ((material && material->SpatialLightingAbiVersion == 3U) || builtInDeferredGBuffer) &&
                (phase == SceneDrawPhase::Opaque || phase == SceneDrawPhase::Transparent ||
                 phase == SceneDrawPhase::DeferredGBufferStandard || phase == SceneDrawPhase::DeferredGBufferExtended ||
                 phase == SceneDrawPhase::DeferredForwardOpaqueTail);
            const bool gpuSpatialSelectionValid =
                consumesSpatialSelection && batch.Count == 1U &&
                draw.SpatialSelectionRecordIndex != InvalidAssetSpatialSelectionIndex &&
                draw.SpatialSelectionRecordIndex < spatial.RecordCount && (cpuSelection || gpuSelection);
            if (material)
            {
                SDL_BindGPUGraphicsPipeline(pass, selectedPipeline);
                if (material->SpatialLightingAbiVersion == 3U)
                {
                    if (!fallbackSpatialSelectionValid)
                    {
                        throw std::logic_error(
                            "The mandatory device-generation spatial-selection fallback buffer is unavailable.");
                    }
                    const std::array storageBuffers{forwardPlusBuffers[0], forwardPlusBuffers[1], forwardPlusBuffers[2],
                                                    gpuSpatialSelectionValid ? spatial.OutputRecords.Buffer
                                                                             : SpatialSelectionFallbackBuffer};
                    SDL_BindGPUFragmentStorageBuffers(pass, 0, storageBuffers.data(),
                                                      static_cast<std::uint32_t>(storageBuffers.size()));
                }
                else if (material->UsesForwardPlus)
                {
                    SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                                      static_cast<std::uint32_t>(forwardPlusBuffers.size()));
                }
                const AssetObjectUniforms object{item.World, camera.View, camera.Projection,
                                                 Transpose(Math::Inverse(item.World))};
                AssetSceneUniforms scene{};
                scene.AmbientColorIntensity = {
                    packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                    packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity};
                scene.DirectionalColorIntensity = {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green,
                                                   lighting.ColorAndIntensity.Blue, lighting.ColorAndIntensity.Alpha};
                scene.DirectionalDirectionExposure = {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z,
                                                      packet.Environment.Exposure};
                scene.SurfaceParameters = {material->Surface.AlphaCutoff,
                                           static_cast<float>(material->Surface.AlphaMode),
                                           item.ReceiveShadows ? 1.0F : 0.0F, item.CastShadows ? 1.0F : 0.0F};
                scene.LocalLightCounts = localLights.Counts;
                scene.LocalLights = localLights.Lights;
                scene.FrameParameters = {packet.MaterialTimeSeconds, packet.MaterialDeltaSeconds,
                                         static_cast<float>(packet.FrameIndex & 0x00ffffffULL),
                                         static_cast<float>(std::min(item.ContributionOrder, 65534U) + 1U)};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                std::array<Vector4, 64> numericProperties{};
                std::ranges::copy(material->NumericProperties, numericProperties.begin());
                const auto applyNumericProperties = [&](const auto& properties)
                {
                    for (const auto& [name, value] : properties)
                    {
                        const auto binding = material->Properties.find(name);
                        if (binding != material->Properties.end() &&
                            binding->second.Type != ShaderPropertyType::Texture2D)
                        {
                            (void)PackMaterialProperty(value, binding->second.Type,
                                                       numericProperties[binding->second.Slot]);
                        }
                    }
                };
                applyNumericProperties(packet.GlobalMaterialProperties);
                applyNumericProperties(item.MaterialProperties);
                const auto materialInstance = item.MaterialInstanceProperties.find(draw.Submesh.MaterialSlot);
                if (materialInstance != item.MaterialInstanceProperties.end())
                    applyNumericProperties(materialInstance->second);
                if (material->TintSlot && !material->UsesInstancing)
                {
                    auto& tint = numericProperties[*material->TintSlot];
                    tint.X *= item.Tint.Red;
                    tint.Y *= item.Tint.Green;
                    tint.Z *= item.Tint.Blue;
                    tint.W *= item.Tint.Alpha;
                }
                const auto numericPropertyBytes = static_cast<std::uint32_t>(
                    std::max<std::size_t>(material->NumericProperties.size(), 1U) * sizeof(Vector4));
                if (material->UsesVertexMaterialParameters)
                    SDL_PushGPUVertexUniformData(commands, 1, numericProperties.data(), numericPropertyBytes);
                SDL_PushGPUFragmentUniformData(commands, 1, numericProperties.data(), numericPropertyBytes);
                if (material->ReceivesShadows)
                {
                    if (fragmentSlot2Binding != FragmentSlot2Binding::Shadows)
                    {
                        SDL_PushGPUFragmentUniformData(commands, 2, &shadowUniforms, sizeof(shadowUniforms));
                        fragmentSlot2Binding = FragmentSlot2Binding::Shadows;
                    }
                }
                else if (fragmentSlot2Binding != FragmentSlot2Binding::LocalLights)
                {
                    SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
                    fragmentSlot2Binding = FragmentSlot2Binding::LocalLights;
                }
                if (material->UsesSpatialLighting)
                {
                    auto selectedSpatialUniforms = spatialUniforms(item);
                    selectedSpatialUniforms.SpatialSelection[0] =
                        gpuSpatialSelectionValid ? draw.SpatialSelectionRecordIndex : InvalidAssetSpatialSelectionIndex;
                    selectedSpatialUniforms.SpatialSelection[1] = item.ContributionOrder;
                    const AssetEnvironmentSpatialUniforms combined{environmentUniforms, selectedSpatialUniforms};
                    SDL_PushGPUFragmentUniformData(commands, 3, &combined, sizeof(combined));
                }
                else if (material->UsesImageBasedLighting)
                    SDL_PushGPUFragmentUniformData(commands, 3, &environmentUniforms, sizeof(environmentUniforms));
                if (!material->Textures.empty() || material->ReceivesShadows || material->UsesImageBasedLighting ||
                    material->UsesSpatialLighting)
                {
                    std::array<SDL_GPUTextureSamplerBinding, 40> bindings{};
                    std::ranges::copy(material->Textures, bindings.begin());
                    const auto applyTextureProperties = [&](const auto& properties)
                    {
                        for (const auto& [name, value] : properties)
                        {
                            const auto binding = material->Properties.find(name);
                            const auto* texture = std::get_if<AssetId>(&value);
                            if (binding == material->Properties.end() ||
                                binding->second.Type != ShaderPropertyType::Texture2D || !texture)
                            {
                                continue;
                            }
                            const auto& resolved =
                                *texture ? ResolveTexture(*texture) : DefaultTexture(binding->second.TextureSemantic);
                            bindings[binding->second.Slot] = {resolved.Texture, resolved.Sampler};
                        }
                    };
                    applyTextureProperties(packet.GlobalMaterialProperties);
                    applyTextureProperties(item.MaterialProperties);
                    if (materialInstance != item.MaterialInstanceProperties.end())
                        applyTextureProperties(materialInstance->second);
                    auto bindingCount = material->Textures.size();
                    if (material->ReceivesShadows)
                    {
                        bindings[bindingCount++] = {surface.ActiveWorkset().DirectionalShadow
                                                        ? surface.ActiveWorkset().DirectionalShadow
                                                        : EmptyShadowTexture,
                                                    ShadowSampler};
                        bindings[bindingCount++] = {surface.ActiveWorkset().LocalShadow
                                                        ? surface.ActiveWorkset().LocalShadow
                                                        : EmptyShadowTexture,
                                                    ShadowSampler};
                    }
                    if (material->UsesImageBasedLighting)
                    {
                        bindings[bindingCount++] = environmentBindings[0];
                        bindings[bindingCount++] = environmentBindings[1];
                    }
                    if (material->UsesSpatialLighting)
                    {
                        const auto& spatialBindings = spatialContextFor(item.ContributionOrder).Bindings;
                        std::ranges::copy(spatialBindings,
                                          bindings.begin() + static_cast<std::ptrdiff_t>(bindingCount));
                        bindingCount += spatialBindings.size();
                    }
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
                }
                auto* currentVertices = item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices;
                if (phase == SceneDrawPhase::DeferredDepthVelocity)
                {
                    auto* previousVertices =
                        item.PreviousSkinnedAssetVertices ? item.PreviousSkinnedAssetVertices : currentVertices;
                    const std::array vertexBindings{SDL_GPUBufferBinding{currentVertices, 0},
                                                    SDL_GPUBufferBinding{previousVertices, 0}};
                    SDL_BindGPUVertexBuffers(pass, 0, vertexBindings.data(),
                                             static_cast<std::uint32_t>(vertexBindings.size()));
                }
                else
                {
                    const SDL_GPUBufferBinding vertexBinding{currentVertices, 0};
                    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                }
                if (material->UsesInstancing)
                {
                    const std::array<std::uint32_t, 4> instanceParameters{
                        batch.GpuOcclusion ? batch.GpuOcclusionInstanceBase : 0U, 0U, 0U, 0U};
                    if (material->InstanceAddressingAbiVersion == 2U)
                        SDL_PushGPUVertexUniformData(commands, 2, instanceParameters.data(),
                                                     sizeof(instanceParameters));
                    auto* instances = batch.GpuOcclusion ? prepared.GpuOcclusionVisibleInstances : batch.InstanceBuffer;
                    SDL_BindGPUVertexStorageBuffers(pass, 0, &instances, 1);
                }
            }
            else
            {
                const bool usesInstancing = batch.InstanceDataCount != 0U;
                const Color tint = draw.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                auto object = MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), item.World, camera.View,
                                                 camera.Projection, tint, lighting, packet.Environment,
                                                 item.ReceiveShadows, usesInstancing);
                object.Parameters.Z = static_cast<float>(std::min(item.ContributionOrder, 65534U) + 1U);
                const auto selectedSpatialUniforms = spatialUniforms(item);
                const BuiltInSpatialLightingUniforms builtInSpatialUniforms{
                    selectedSpatialUniforms.LightmapScaleOffset,
                    selectedSpatialUniforms.LightmapParameters,
                    selectedSpatialUniforms.ShadowMaskParameters,
                    {cameraPosition.X, cameraPosition.Y, cameraPosition.Z, packet.Environment.Exposure},
                    {packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                     packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity},
                    {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z, lighting.Enabled ? 1.0F : 0.0F},
                    {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green, lighting.ColorAndIntensity.Blue,
                     lighting.ColorAndIntensity.Alpha}};
                if (builtInDeferredGBuffer)
                {
                    object.LightDirection = selectedSpatialUniforms.LightmapScaleOffset;
                    object.LightColor = {
                        selectedSpatialUniforms.LightmapParameters.X, selectedSpatialUniforms.LightmapParameters.Y,
                        selectedSpatialUniforms.LightmapParameters.Z, selectedSpatialUniforms.LightmapParameters.W};
                    object.Parameters.W =
                        gpuSpatialSelectionValid ? static_cast<float>(draw.SpatialSelectionRecordIndex + 1U) : 0.0F;
                }
                const auto& builtInShadows = item.ReceiveShadows ? shadowUniforms : disabledShadowUniforms;
                const std::array shadowBindings{
                    SDL_GPUTextureSamplerBinding{surface.ActiveWorkset().DirectionalShadow
                                                     ? surface.ActiveWorkset().DirectionalShadow
                                                     : EmptyShadowTexture,
                                                 ShadowSampler},
                    SDL_GPUTextureSamplerBinding{
                        surface.ActiveWorkset().LocalShadow ? surface.ActiveWorkset().LocalShadow : EmptyShadowTexture,
                        ShadowSampler}};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_BindGPUGraphicsPipeline(pass, selectedPipeline);
                if (!builtInDeferredGBuffer)
                {
                    SDL_PushGPUVertexUniformData(commands, 3, &builtInSpatialUniforms, sizeof(builtInSpatialUniforms));
                    SDL_PushGPUFragmentUniformData(commands, 0, &builtInShadows, sizeof(builtInShadows));
                    SDL_PushGPUFragmentUniformData(commands, 1, &localLights, sizeof(localLights));
                    SDL_PushGPUFragmentUniformData(commands, 2, &environmentUniforms, sizeof(environmentUniforms));
                    SDL_PushGPUFragmentUniformData(commands, 3, &builtInSpatialUniforms,
                                                   sizeof(builtInSpatialUniforms));
                    const auto& spatialBindings = spatialContextFor(item.ContributionOrder).Bindings;
                    const std::array builtInBindings{shadowBindings[0], shadowBindings[1], spatialBindings[0],
                                                     spatialBindings[1], environmentBindings[0]};
                    SDL_BindGPUFragmentSamplers(pass, 0, builtInBindings.data(),
                                                static_cast<std::uint32_t>(builtInBindings.size()));
                }
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                if (usesInstancing)
                {
                    const std::array<std::uint32_t, 4> instanceParameters{
                        batch.GpuOcclusion ? batch.GpuOcclusionInstanceBase : 0U, 0U, 0U, 0U};
                    SDL_PushGPUVertexUniformData(commands, 2, instanceParameters.data(), sizeof(instanceParameters));
                    auto* instances = batch.GpuOcclusion ? prepared.GpuOcclusionVisibleInstances : batch.InstanceBuffer;
                    SDL_BindGPUVertexStorageBuffers(pass, 0, &instances, 1);
                }
            }
            if (phase == SceneDrawPhase::DeferredDepthVelocity && material &&
                material->InstanceAddressingAbiVersion == 2U && batch.InstanceBuffer)
            {
                auto* instances = batch.InstanceBuffer;
                SDL_BindGPUVertexStorageBuffers(pass, 0, &instances, 1);
                for (std::uint32_t instance = 0; instance < instanceCount; ++instance)
                {
                    const std::array<std::uint32_t, 4> instanceParameters{instance, 0U, 0U, 0U};
                    SDL_PushGPUVertexUniformData(commands, 2, instanceParameters.data(), sizeof(instanceParameters));
                    const auto& instanceDraw = prepared.Draws[drawIndex + instance];
                    const AssetObjectUniforms motionObject{instanceDraw.Item->PreviousWorld, camera.View,
                                                           camera.Projection, packet.PreviousViewProjection};
                    SDL_PushGPUVertexUniformData(commands, 0, &motionObject, sizeof(motionObject));
                    SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, 1U, draw.Submesh.FirstIndex, 0, 0U);
                    ++Statistics.DrawCalls;
                    Statistics.Triangles += draw.Submesh.IndexCount / 3U;
                }
                continue;
            }
            if (batch.GpuOcclusion)
            {
                SDL_DrawGPUIndexedPrimitivesIndirect(pass, prepared.GpuOcclusionIndirectArguments,
                                                     batch.GpuOcclusionIndirectOffset, 1);
                ++Statistics.GpuOcclusionIndirectDraws;
            }
            else
            {
                SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, instanceCount, draw.Submesh.FirstIndex, 0,
                                             batch.GpuFirstInstance);
            }
            if (gpuSpatialSelectionValid)
                ++surface.ActiveWorkset().SpatialSelection.ConsumedDraws;
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3 * instanceCount;
            Statistics.InstanceBatches += instanceCount > 1 ? 1U : 0U;
        }
    }

    void RenderSharedState::RecordDeferredDepthVelocity(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                        const SceneRenderPacket& packet, const ShadowFrameData& shadows,
                                                        const PreparedSceneDrawList& opaqueDraws,
                                                        const std::size_t firstBatch, const std::size_t batchCount,
                                                        const bool clearTargets)
    {
        auto& workset = surface.ActiveWorkset();
        if (!workset.GBufferVelocity || !workset.Depth)
            throw std::logic_error("Deferred depth/velocity resources are unavailable for an active render surface.");

        SDL_GPUColorTargetInfo velocity{};
        velocity.texture = workset.GBufferVelocity;
        // Motion vectors intentionally exclude projection jitter. Reprojecting the jitter itself aligns every
        // history sample to the moving sample pattern and makes the resolved image visibly shake.
        velocity.clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
        velocity.load_op = clearTargets ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        velocity.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = workset.Depth;
        depth.clear_depth = 1.0F;
        depth.load_op = clearTargets ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, &velocity, 1, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(deferred depth/velocity) failed: " + LastSdlError());
        DrawScene(commands, pass, surface, packet, shadows, SceneDrawPhase::DeferredDepthVelocity, opaqueDraws,
                  firstBatch, batchCount);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordDeferredGBuffer(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                  const SceneRenderPacket& packet, const ShadowFrameData& shadows,
                                                  const PreparedSceneDrawList& opaqueDraws, const SceneDrawPhase phase,
                                                  const std::size_t firstBatch, const std::size_t batchCount,
                                                  const bool clearTargets)
    {
        auto& workset = surface.ActiveWorkset();
        if (!workset.GBufferBaseColorMetallic || !workset.GBufferNormalRoughness || !workset.GBufferMaterial ||
            !workset.GBufferLighting || !workset.Depth)
        {
            throw std::logic_error("Deferred GBuffer resources are unavailable for an active render surface.");
        }

        std::array<SDL_GPUColorTargetInfo, 4> colors{};
        colors[0].texture = workset.GBufferBaseColorMetallic;
        colors[0].clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
        colors[1].texture = workset.GBufferNormalRoughness;
        colors[1].clear_color = {0.5F, 0.5F, 1.0F, 1.0F};
        colors[2].texture = workset.GBufferMaterial;
        colors[2].clear_color = {1.0F, 0.5F, 0.0F, 0.0F};
        colors[3].texture = workset.GBufferLighting;
        colors[3].clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
        for (auto& color : colors)
        {
            color.load_op = clearTargets ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            color.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = workset.Depth;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, colors.data(), static_cast<std::uint32_t>(colors.size()), &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(deferred GBuffer) failed: " + LastSdlError());
        DrawScene(commands, pass, surface, packet, shadows, phase, opaqueDraws, firstBatch, batchCount);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordDeferredDBuffer(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                  const SceneRenderPacket& packet, const ShadowFrameData& shadows,
                                                  const PreparedSceneDrawList& decalDraws, const std::size_t firstBatch,
                                                  const std::size_t batchCount, const bool clearTargets)
    {
        const auto& workset = surface.ActiveWorkset();
        if (!workset.DBufferBaseColor || !workset.DBufferNormal || !workset.DBufferMaterial || !workset.Depth)
            throw std::logic_error("Deferred DBuffer resources are unavailable for an active render surface.");

        std::array<SDL_GPUColorTargetInfo, 3> colors{};
        colors[0].texture = workset.DBufferBaseColor;
        colors[0].clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
        colors[1].texture = workset.DBufferNormal;
        colors[1].clear_color = {0.5F, 0.5F, 1.0F, 0.0F};
        colors[2].texture = workset.DBufferMaterial;
        colors[2].clear_color = {0.0F, 1.0F, 0.5F, 0.0F};
        for (auto& color : colors)
        {
            color.load_op = clearTargets ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            color.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = workset.Depth;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, colors.data(), static_cast<std::uint32_t>(colors.size()), &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(deferred DBuffer) failed: " + LastSdlError());
        DrawScene(commands, pass, surface, packet, shadows, SceneDrawPhase::DeferredDecal, decalDraws, firstBatch,
                  batchCount);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordDeferredLighting(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                   const SceneRenderPacket& packet, const ShadowFrameData& shadows)
    {
        auto& workset = surface.ActiveWorkset();
        if (!DeferredLightingPipeline || !DeferredSampler || !workset.HdrColor || !workset.GBufferBaseColorMetallic ||
            !workset.GBufferNormalRoughness || !workset.GBufferMaterial || !workset.GBufferLighting || !workset.Depth ||
            !workset.DBufferBaseColor || !workset.DBufferNormal || !workset.DBufferMaterial ||
            workset.ForwardPlus.Empty())
        {
            throw std::logic_error("Deferred lighting resources are unavailable for an active render surface.");
        }

        SDL_GPUColorTargetInfo color{};
        color.texture = workset.HdrColor;
        color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green, surface.FrameClearColor.Blue,
                             surface.FrameClearColor.Alpha};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = workset.Depth;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(deferred sky) failed: " + LastSdlError());
        const PreparedSceneDrawList noDraws;
        DrawScene(commands, pass, surface, packet, shadows, SceneDrawPhase::DeferredSky, noDraws, 0U, 0U);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;

        color.load_op = SDL_GPU_LOADOP_LOAD;
        pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(deferred lighting) failed: " + LastSdlError());
        const std::array commonBindings{
            SDL_GPUTextureSamplerBinding{workset.GBufferBaseColorMetallic, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.GBufferNormalRoughness, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.GBufferMaterial, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.Depth, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.DBufferBaseColor, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.DBufferNormal, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.DBufferMaterial, DeferredSampler},
            SDL_GPUTextureSamplerBinding{workset.DirectionalShadow ? workset.DirectionalShadow : EmptyShadowTexture,
                                         ShadowSampler},
            SDL_GPUTextureSamplerBinding{workset.LocalShadow ? workset.LocalShadow : EmptyShadowTexture,
                                         ShadowSampler}};
        const auto& lighting = packet.Lighting;
        const auto cameraPosition = Math::TransformPoint(Math::Inverse(packet.Camera.View), {});
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        const auto illumination = ResolveGlobalIlluminationPolicy(
            surface.ActiveFeatureSelection.EffectiveGlobalIllumination, packet.Environment.RequestedIrradynQuality);
        const DeferredLightingUniforms uniforms{
            {surface.FrameClearColor.Red, surface.FrameClearColor.Green, surface.FrameClearColor.Blue,
             surface.FrameClearColor.Alpha},
            {packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
             packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity},
            {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green, lighting.ColorAndIntensity.Blue,
             lighting.ColorAndIntensity.Alpha},
            {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z, packet.Environment.Exposure},
            Math::Inverse(viewProjection),
            packet.Camera.View,
            viewProjection,
            {cameraPosition.X, cameraPosition.Y, cameraPosition.Z, static_cast<float>(packet.LocalLights.size())},
            {static_cast<float>(workset.ForwardPlus.Columns), static_cast<float>(workset.ForwardPlus.Rows),
             static_cast<float>(ForwardPlusTileGrid::TileSize), 0.0F},
            {0.35F, 0.0025F, 8.0F, 0.0F},
            {illumination.EnvironmentDiffuse ? 1.0F : 0.0F, illumination.EnvironmentSpecular ? 1.0F : 0.0F,
             illumination.BakedLighting ? 1.0F : 0.0F, 0.0F}};
        AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
        for (auto& parameters : shadowUniforms.Local.Parameters)
            parameters.X = -1.0F;
        for (std::size_t lightIndex = 0;
             lightIndex < std::min(packet.LocalLights.size(), shadowUniforms.Local.Parameters.size()); ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex], light.ShadowStrength,
                                                           light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                           std::max(light.ShadowBias * 0.01F, 0.0001F)};
        }
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        const auto& spatialSelection = workset.SpatialSelection;
        const bool spatialSelectionValid = spatialSelection.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot,
                                                                    surface.Epoch, deviceGeneration) &&
                                           spatialSelection.DispatchSucceeded && spatialSelection.OutputRecords.Buffer;
        if (!SpatialSelectionFallbackBuffer || SpatialSelectionFallbackDeviceGeneration != deviceGeneration)
            throw std::logic_error("Deferred lighting requires a device-generation spatial-selection fallback.");
        const std::array forwardPlusBuffers{
            workset.ForwardPlus.Lights, workset.ForwardPlus.Tiles, workset.ForwardPlus.LightIndices,
            spatialSelectionValid ? spatialSelection.OutputRecords.Buffer : SpatialSelectionFallbackBuffer};

        const auto& requestedEnvironment =
            packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
        const auto& environment = requestedEnvironment.HasDiffuseIrradiance ? requestedEnvironment : DefaultSkyTexture;
        AssetEnvironmentUniforms environmentUniforms{};
        environmentUniforms.DiffuseIrradiance = environment.DiffuseIrradiance;
        environmentUniforms.Parameters = {
            packet.Environment.EnvironmentRotationDegrees,
            illumination.EnvironmentDiffuse ? packet.Environment.EnvironmentDiffuseIntensity : 0.0F,
            illumination.EnvironmentSpecular ? packet.Environment.EnvironmentSpecularIntensity : 0.0F,
            static_cast<float>(environment.MipLevels - 1U)};
        environmentUniforms.Encoding = {static_cast<float>(environment.EnvironmentLayout) +
                                            (environment.HdrEncoded ? 16.0F : 0.0F),
                                        0.0F, 0.0F, 0.0F};

        std::uint32_t cookieCount = 0U;
        std::array<AssetId, 8> cookieAssets{};
        std::array<Vector4, 8> cookieTransforms{};
        std::array<Vector4, 2> cookieRotations{};
        const auto addCookie = [&](const AssetId cookie, const Vector2 scale, const Vector2 offset,
                                   const float rotationDegrees) -> float
        {
            if (!cookie || cookieCount >= cookieAssets.size())
                return 0.0F;
            const auto slot = cookieCount++;
            cookieAssets[slot] = cookie;
            cookieTransforms[slot] = {scale.X, scale.Y, offset.X, offset.Y};
            auto& rotations = cookieRotations[slot / 4U];
            switch (slot % 4U)
            {
            case 0:
                rotations.X = rotationDegrees;
                break;
            case 1:
                rotations.Y = rotationDegrees;
                break;
            case 2:
                rotations.Z = rotationDegrees;
                break;
            default:
                rotations.W = rotationDegrees;
                break;
            }
            return static_cast<float>(slot + 1U);
        };
        const auto directionalCookie =
            addCookie(lighting.Cookie, lighting.CookieScale, lighting.CookieOffset, lighting.CookieRotationDegrees);
        for (std::size_t lightIndex = 0; lightIndex < std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
             ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            (void)addCookie(light.Cookie, light.CookieScale, light.CookieOffset, light.CookieRotationDegrees);
        }
        const auto& cookieAtlas = ResolveCookieAtlas(cookieAssets);

        struct DeferredSpatialContext final
        {
            Ref<const LightingSetAsset> Asset;
            const LightingSetDefinition* LightingSet = nullptr;
            std::array<SDL_GPUTextureSamplerBinding, 5> Bindings{};
            bool LightmapsRgbe = false;
            bool ReflectionsRgbe = false;
        };
        std::vector<DeferredSpatialContext> spatialContexts;
        spatialContexts.reserve(std::max<std::size_t>(packet.SpatialContributions.size(), 1U));
        const auto appendSpatialContext = [&](const AssetId bakedLighting)
        {
            DeferredSpatialContext context;
            context.Asset =
                illumination.BakedLighting ? ResolveLightingSet(bakedLighting) : Ref<const LightingSetAsset>{};
            context.LightingSet = context.Asset ? &context.Asset->Definition() : nullptr;
            const auto& lightmaps =
                context.LightingSet ? ResolveLightingTexture(context.LightingSet->Lightmaps) : DefaultLightingArray;
            const auto& directionality = context.LightingSet
                                             ? ResolveLightingTexture(context.LightingSet->Directionality)
                                             : DefaultLightingArray;
            const auto& shadowMasks = context.LightingSet
                                          ? ResolveLightingTexture(context.LightingSet->ShadowMasks, false, true)
                                          : DefaultLightingMaskArray;
            const auto& reflections = context.LightingSet
                                          ? ResolveLightingTexture(context.LightingSet->ReflectionCubemaps, true)
                                          : DefaultReflectionCubeArray;
            context.Bindings = {{{lightmaps.Texture, lightmaps.Sampler},
                                 {directionality.Texture, directionality.Sampler},
                                 {shadowMasks.Texture, shadowMasks.Sampler},
                                 {reflections.Texture, reflections.Sampler},
                                 {cookieAtlas.Texture, cookieAtlas.Sampler}}};
            context.LightmapsRgbe = lightmaps.HdrEncoded;
            context.ReflectionsRgbe = reflections.HdrEncoded;
            spatialContexts.push_back(std::move(context));
        };
        for (const auto& contribution : packet.SpatialContributions)
            appendSpatialContext(contribution.BakedLighting);
        if (spatialContexts.empty())
            appendSpatialContext(packet.BakedLighting);
        if (spatialContexts.size() > 255U)
            throw std::length_error("Deferred lighting exceeds the 8-bit additive-scene contribution limit.");

        const auto mixedLightChannel = [](const AssetId light, const LightingSetDefinition* lightingSet) -> float
        {
            if (!lightingSet || !light)
                return 0.0F;
            const auto found = std::ranges::find(lightingSet->MixedLights, light, &MixedLightBinding::Light);
            return found == lightingSet->MixedLights.end() ? 0.0F : static_cast<float>(found->ShadowMaskChannel + 1U);
        };
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 1, &shadowUniforms, sizeof(shadowUniforms));
        SDL_BindGPUGraphicsPipeline(pass, DeferredLightingPipeline);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                          static_cast<std::uint32_t>(forwardPlusBuffers.size()));
        for (std::size_t contextIndex = 0; contextIndex < spatialContexts.size(); ++contextIndex)
        {
            const auto& context = spatialContexts[contextIndex];
            DeferredSpatialLightingUniforms spatialUniforms{};
            spatialUniforms.Environment = environmentUniforms;
            spatialUniforms.CookieTransforms = cookieTransforms;
            spatialUniforms.CookieRotations = cookieRotations;
            spatialUniforms.DirectionalCookieAndContact = {directionalCookie, lighting.ContactShadows ? 1.0F : 0.0F,
                                                           0.35F, 0.0025F};
            spatialUniforms.Context = {static_cast<float>(contextIndex + 1U),
                                       mixedLightChannel(lighting.Entity.Value(), context.LightingSet),
                                       context.LightmapsRgbe ? 1.0F : 0.0F, context.ReflectionsRgbe ? 1.0F : 0.0F};
            SDL_PushGPUFragmentUniformData(commands, 2, &spatialUniforms, sizeof(spatialUniforms));

            std::array<SDL_GPUTextureSamplerBinding, 16> bindings{};
            std::ranges::copy_n(commonBindings.begin(), 3, bindings.begin());
            auto bindingIndex = std::size_t{3};
            bindings[bindingIndex++] = {workset.GBufferLighting, DeferredSampler};
            std::ranges::copy(commonBindings.begin() + 3, commonBindings.end(), bindings.begin() + bindingIndex);
            bindingIndex += commonBindings.size() - 3U;
            bindings[bindingIndex++] = {environment.Texture, environment.Sampler};
            std::ranges::copy(context.Bindings, bindings.begin() + static_cast<std::ptrdiff_t>(bindingIndex));
            bindingIndex += context.Bindings.size();
            SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingIndex));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            ++Statistics.DrawCalls;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                          const RenderAntiAliasingMode antiAliasing,
                                          const bool temporalHistoryContinuous)
    {
        if (!ToneMapPipeline || !ToneMapSampler || !surface.ActiveWorkset().HdrColor ||
            !surface.Resources.WriterColor(surface.ActiveWorksetSlot) ||
            !surface.Resources.PublishedTemporalHistory() ||
            !surface.Resources.WriterTemporalHistory(surface.ActiveWorksetSlot) || !BlackDataTexture.Texture)
            throw std::logic_error("Tone-map resources are unavailable for an active render surface.");
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
        auto* const velocity = surface.ActiveWorkset().GBufferVelocity ? surface.ActiveWorkset().GBufferVelocity
                                                                       : BlackDataTexture.Texture;
        const std::array bindings{
            SDL_GPUTextureSamplerBinding{surface.ActiveWorkset().HdrColor, ToneMapSampler},
            SDL_GPUTextureSamplerBinding{surface.Resources.PublishedTemporalHistory(), ToneMapSampler},
            SDL_GPUTextureSamplerBinding{velocity, ToneMapSampler}};
        const bool taa = antiAliasing == RenderAntiAliasingMode::Taa;
        const bool historyValid = taa && temporalHistoryContinuous && surface.TemporalHistoryValid &&
                                  surface.TemporalHistoryPath == surface.ActiveFeatureSelection.EffectivePath;
        const Vector4 parameters{1.0F / static_cast<float>(std::max(surface.Width, 1U)),
                                 1.0F / static_cast<float>(std::max(surface.Height, 1U)),
                                 taa                                            ? 2.0F
                                 : antiAliasing == RenderAntiAliasingMode::Fxaa ? 1.0F
                                                                                : 0.0F,
                                 historyValid ? 1.0F : 0.0F};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, &parameters, sizeof(parameters));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;

        if (taa)
        {
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
                throw std::runtime_error("SDL_BeginGPUCopyPass(temporal history) failed: " + LastSdlError());
            const SDL_GPUTextureLocation source{
                surface.Resources.WriterColor(surface.ActiveWorksetSlot), 0, 0, 0, 0, 0};
            const SDL_GPUTextureLocation destination{
                surface.Resources.WriterTemporalHistory(surface.ActiveWorksetSlot), 0, 0, 0, 0, 0};
            SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
            SDL_EndGPUCopyPass(copy);
            ++Statistics.Passes;
        }
    }

} // namespace Keire::RenderBackend
