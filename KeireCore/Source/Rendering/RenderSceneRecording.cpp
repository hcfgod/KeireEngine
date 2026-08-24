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
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace
{
    // Dense project shaders can bind sixteen or more samplers per draw. Keep the command-buffer-local D3D12
    // descriptor working set below SDL's rollover threshold instead of waiting for a failed binding or corrupted
    // texture table before continuing the surface.
    constexpr std::size_t MaximumSceneBatchesPerCommandBuffer = 12U;

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
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Submeshes.empty())
                continue;
            const auto viewFromLocal = Math::Multiply(camera.View, item.World);
            const auto clipFromLocal = Math::Multiply(camera.Projection, viewFromLocal);
            std::uint32_t firstSubmesh = 0;
            std::uint32_t submeshCount = static_cast<std::uint32_t>(mesh.Submeshes.size());
            if (!mesh.Lods.empty())
            {
                const auto height = ProjectedHeight(viewFromLocal, camera.Projection, mesh.Lods.front().Bounds);
                const auto selected =
                    std::ranges::find_if(mesh.Lods, [&](const auto& lod) { return height >= lod.MinimumScreenHeight; });
                const auto& lod = selected != mesh.Lods.end() ? *selected : mesh.Lods.back();
                firstSubmesh = lod.FirstSubmesh;
                submeshCount = lod.SubmeshCount;
            }
            for (std::uint32_t offset = 0; offset < submeshCount; ++offset)
            {
                const auto submeshIndex = firstSubmesh + offset;
                const auto& submesh = mesh.Submeshes[submeshIndex];
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                MaterialSurfaceState surfaceState;
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr)
                    surfaceState = material->Surface;
                if (!IsFrustumVisible(clipFromLocal, submesh.Bounds, item.AlwaysVisible))
                {
                    ++Statistics.CulledSubmeshes;
                    continue;
                }
                const Vector3 center{(submesh.Bounds.Minimum.X + submesh.Bounds.Maximum.X) * 0.5F,
                                     (submesh.Bounds.Minimum.Y + submesh.Bounds.Maximum.Y) * 0.5F,
                                     (submesh.Bounds.Minimum.Z + submesh.Bounds.Maximum.Z) * 0.5F};
                auto& destination = IsTransparentMaterial(surfaceState.AlphaMode) ? result.Transparent : result.Opaque;
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
                    if (left.Item->Entity != right.Item->Entity)
                        return left.Item->Entity < right.Item->Entity;
                    return left.SubmeshIndex < right.SubmeshIndex;
                });
        };
        sortDraws(result.Opaque.Draws);
        sortDraws(result.Transparent.Draws);

        const auto prepareBatches = [&](PreparedSceneDrawList& list)
        {
            std::vector<InstanceBatchKey> instanceKeys;
            instanceKeys.reserve(list.Draws.size());
            for (const auto& draw : list.Draws)
            {
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                instanceKeys.push_back({draw.Item->Mesh, draw.Material, draw.SubmeshIndex, draw.Surface.AlphaMode,
                                        draw.Item->ReceiveShadows, draw.Item->CastShadows,
                                        material && material->UsesInstancing && !draw.Item->SkinnedAssetVertices &&
                                            draw.Item->MaterialProperties.empty() &&
                                            draw.Item->MaterialInstanceProperties.empty()});
            }
            const auto batches = BuildInstanceBatches(instanceKeys);
            list.Batches.reserve(batches.size());
            for (const auto batch : batches)
            {
                const auto drawIndex = static_cast<std::size_t>(batch.First);
                const auto& draw = list.Draws[drawIndex];
                const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
                SDL_GPUBuffer* instanceBuffer = nullptr;
                if (material && material->UsesInstancing)
                {
                    std::vector<GpuInstanceUniform> instances;
                    instances.reserve(batch.Count);
                    for (std::uint32_t instance = 0; instance < batch.Count; ++instance)
                    {
                        const auto& instanceDraw = list.Draws[drawIndex + instance];
                        instances.push_back({instanceDraw.Item->World,
                                             Transpose(Math::Inverse(instanceDraw.Item->World)),
                                             instanceDraw.Item->Tint});
                    }
                    instanceBuffer = UploadBuffer(commands, std::as_bytes(std::span(instances)),
                                                  SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
                    FrameTransientBuffers.push_back(instanceBuffer);
                }
                list.Batches.push_back({batch.First, batch.Count, batch.GpuFirstInstance(), instanceBuffer});
            }
        };
        prepareBatches(result.Opaque);
        prepareBatches(result.Transparent);

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
        for (const auto& particle : packet.Vfx.Particles())
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
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;
        if (surface.ForwardPlus.Empty())
            throw std::logic_error("Forward+ GPU resources were not prepared before scene recording.");
        std::array<SDL_GPUBuffer*, 3> forwardPlusBuffers{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                         surface.ForwardPlus.LightIndices};
        const auto& requestedEnvironment =
            packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
        const auto& environment = requestedEnvironment.HasDiffuseIrradiance ? requestedEnvironment : DefaultSkyTexture;
        AssetEnvironmentUniforms environmentUniforms{};
        environmentUniforms.DiffuseIrradiance = environment.DiffuseIrradiance;
        environmentUniforms.Parameters = {
            packet.Environment.EnvironmentRotationDegrees, packet.Environment.EnvironmentDiffuseIntensity,
            packet.Environment.EnvironmentSpecularIntensity, static_cast<float>(environment.MipLevels - 1U)};
        environmentUniforms.Encoding = {static_cast<float>(environment.EnvironmentLayout) +
                                            (environment.HdrEncoded ? 16.0F : 0.0F),
                                        0.0F, 0.0F, 0.0F};
        const std::array environmentBindings{
            SDL_GPUTextureSamplerBinding{environment.Texture, environment.Sampler},
            SDL_GPUTextureSamplerBinding{BrdfIntegrationLut.Texture, BrdfIntegrationLut.Sampler}};
        const auto bakedLighting = ResolveLightingSet(packet.BakedLighting);
        const auto* lightingSet = bakedLighting ? &bakedLighting->Definition() : nullptr;
        const auto& bakedLightmaps =
            lightingSet ? ResolveLightingTexture(lightingSet->Lightmaps) : DefaultLightingArray;
        const auto& bakedDirectionality =
            lightingSet ? ResolveLightingTexture(lightingSet->Directionality) : DefaultLightingArray;
        const auto& bakedShadowMasks =
            lightingSet ? ResolveLightingTexture(lightingSet->ShadowMasks, false, true) : DefaultLightingMaskArray;
        const auto& bakedReflections =
            lightingSet ? ResolveLightingTexture(lightingSet->ReflectionCubemaps, true) : DefaultReflectionCubeArray;
        std::array<SDL_GPUTextureSamplerBinding, 5> spatialBindings{};
        spatialBindings[0] = {bakedLightmaps.Texture, bakedLightmaps.Sampler};
        spatialBindings[1] = {bakedDirectionality.Texture, bakedDirectionality.Sampler};
        spatialBindings[2] = {bakedShadowMasks.Texture, bakedShadowMasks.Sampler};
        spatialBindings[3] = {bakedReflections.Texture, bakedReflections.Sampler};
        spatialBindings[4] = {WhiteTexture.Texture, WhiteTexture.Sampler};
        AssetSpatialLightingUniforms spatialBase{};
        spatialBase.LightmapScaleOffset = {1.0F, 1.0F, 0.0F, 0.0F};
        spatialBase.ShadowMaskParameters.X = lightingSet ? static_cast<float>(lightingSet->Renderers.size()) : 0.0F;
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
        spatialBindings[4] = {cookieAtlas.Texture, cookieAtlas.Sampler};
        const auto mixedLightChannel = [&](const AssetId light) -> float
        {
            if (!lightingSet || !light)
                return 0.0F;
            const auto found = std::ranges::find(lightingSet->MixedLights, light, &MixedLightBinding::Light);
            return found == lightingSet->MixedLights.end() ? 0.0F : static_cast<float>(found->ShadowMaskChannel + 1U);
        };
        auto spatialProbes = packet.ReflectionProbes;
        if (lightingSet)
        {
            for (auto& probe : spatialProbes)
            {
                const auto binding =
                    std::ranges::find(lightingSet->ReflectionProbes, probe.Entity, &ReflectionProbeBinding::Probe);
                if (binding != lightingSet->ReflectionProbes.end())
                    probe.CubeIndex = binding->CubeIndex;
            }
        }
        const auto spatialUniforms = [&](const SceneDrawItem& item)
        {
            auto result = spatialBase;
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
                result.LightmapParameters.W = mixedLightChannel(lighting.Entity.Value());
            }
            const auto worldPosition = Math::TransformPoint(item.World, {});
            for (const auto& volume : packet.LightProbeVolumes)
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
            const auto selected = Detail::SelectReflectionProbes(worldPosition, spatialProbes, 2U);
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
                                     static_cast<float>(bakedReflections.MipLevels - 1U)};
            }
            return result;
        };

        if (firstBatch == 0U && phase == SceneDrawPhase::Opaque && packet.Environment.SkyVisible && pipelines.Sky)
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

        if (firstBatch == 0U && phase == SceneDrawPhase::Opaque && packet.DrawGrid)
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
        localLights.Counts.Y = static_cast<float>(surface.ForwardPlus.Columns);
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
            uniform.Parameters.Z = mixedLightChannel(light.Entity.Value());
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
            const auto instanceCount = batch.Count;
            if (material)
            {
                SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
                if (material->UsesForwardPlus)
                    SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                                      static_cast<std::uint32_t>(forwardPlusBuffers.size()));
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
                                         static_cast<float>(packet.FrameIndex & 0x00ffffffULL), 0.0F};
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
                    const AssetEnvironmentSpatialUniforms combined{environmentUniforms, spatialUniforms(item)};
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
                        bindings[bindingCount++] = {surface.Resources.DirectionalShadow
                                                        ? surface.Resources.DirectionalShadow
                                                        : EmptyShadowTexture,
                                                    ShadowSampler};
                        bindings[bindingCount++] = {surface.Resources.LocalShadow ? surface.Resources.LocalShadow
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
                        std::ranges::copy(spatialBindings,
                                          bindings.begin() + static_cast<std::ptrdiff_t>(bindingCount));
                        bindingCount += spatialBindings.size();
                    }
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
                }
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                if (material->UsesInstancing)
                    SDL_BindGPUVertexStorageBuffers(pass, 0, &batch.InstanceBuffer, 1);
            }
            else
            {
                const Color tint = draw.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                const ObjectUniforms object =
                    MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), item.World, camera.View, tint,
                                       lighting, packet.Environment, item.ReceiveShadows);
                const auto& builtInShadows = item.ReceiveShadows ? shadowUniforms : disabledShadowUniforms;
                const std::array shadowBindings{
                    SDL_GPUTextureSamplerBinding{
                        surface.Resources.DirectionalShadow ? surface.Resources.DirectionalShadow : EmptyShadowTexture,
                        ShadowSampler},
                    SDL_GPUTextureSamplerBinding{surface.Resources.LocalShadow ? surface.Resources.LocalShadow
                                                                               : EmptyShadowTexture,
                                                 ShadowSampler}};
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &builtInShadows, sizeof(builtInShadows));
                SDL_PushGPUFragmentUniformData(commands, 1, &localLights, sizeof(localLights));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                            static_cast<std::uint32_t>(shadowBindings.size()));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedBuiltinVertices ? item.SkinnedBuiltinVertices : mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, instanceCount, draw.Submesh.FirstIndex, 0,
                                         batch.GpuFirstInstance);
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3 * instanceCount;
            Statistics.InstanceBatches += instanceCount > 1 ? 1U : 0U;
        }
    }

    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer*& commands, RenderSurfaceState& surface,
                                          std::vector<SDL_GPUCommandBuffer*>& frameCommands)
    {
        if (!surface.Resources.SampledColor || !surface.Resources.HdrColor)
            return;

        const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
        PreparedSceneDrawLists preparedDraws;
        PreparedCpuVfx preparedCpuVfx;
        if (request != Requests.end())
        {
            auto started = std::chrono::steady_clock::now();
            PrepareSkinning(commands, request->Packet, surface.Id);
            Statistics.SkinningPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            PrepareGpuVfx(commands, request->Packet.Vfx, surface);
            preparedCpuVfx = PrepareCpuVfxDraws(commands, request->Packet);
            Statistics.VfxPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            started = std::chrono::steady_clock::now();
            preparedDraws = PrepareSceneDrawLists(commands, surface, request->Packet);
            Statistics.DrawPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
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
                if (frameGraphPass == SceneFrameGraph.DirectionalShadows)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != Requests.end())
                        shadows = RecordShadows(commands, surface, request->Packet);
                    Statistics.ShadowRecordingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.ForwardPlusCulling)
                {
                    if (request == Requests.end())
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
                    hashValue(surface.Width);
                    hashValue(surface.Height);
                    hashValue(request->Packet.BakedLighting.High());
                    hashValue(request->Packet.BakedLighting.Low());
                    hashValue(request->Packet.Lighting.Cookie.High());
                    hashValue(request->Packet.Lighting.Cookie.Low());
                    for (const auto value : request->Packet.Camera.View.Elements)
                        hashValue(value);
                    for (const auto value : request->Packet.Camera.Projection.Elements)
                        hashValue(value);
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
                    }
                    Statistics.VisibleLocalLights += static_cast<std::uint32_t>(request->Packet.LocalLights.size());
                    if (surface.ForwardPlusContentValid && surface.ForwardPlusContentHash == contentHash &&
                        !surface.ForwardPlus.Empty())
                    {
                        ++Statistics.ForwardPlusCacheHits;
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
                    const auto tiles = BuildForwardPlusCpuTiles(surface.Width, surface.Height,
                                                                request->Packet.Camera.Projection, localLightBounds);
                    Statistics.OverflowedLightTiles += tiles.OverflowedTiles;
                    std::vector<AssetLocalLightUniform> gpuLights(
                        std::max<std::size_t>(1, request->Packet.LocalLights.size()));
                    const auto forwardLightingSet = ResolveLightingSet(request->Packet.BakedLighting);
                    const auto forwardMixedChannel = [&](const AssetId light) -> float
                    {
                        if (!forwardLightingSet)
                            return 0.0F;
                        const auto found = std::ranges::find(forwardLightingSet->Definition().MixedLights, light,
                                                             &MixedLightBinding::Light);
                        return found == forwardLightingSet->Definition().MixedLights.end()
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
                             forwardMixedChannel(light.Entity.Value()),
                             cookie + (light.ContactShadows ? 16.0F : 0.0F)}};
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
                        surface.ForwardPlus.Empty() || surface.ForwardPlus.LightCapacityBytes < requiredCapacities[0] ||
                        surface.ForwardPlus.TileCapacityBytes < requiredCapacities[1] ||
                        surface.ForwardPlus.LightIndexCapacityBytes < requiredCapacities[2];
                    if (requiresReplacement)
                    {
                        ForwardPlusGpuResources replacement;
                        const auto createBuffer = [&](const std::uint32_t byteSize)
                        {
                            SDL_GPUBufferCreateInfo information{};
                            information.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                            information.size = byteSize;
                            auto* buffer = SDL_CreateGPUBuffer(Device, &information);
                            if (!buffer)
                                throw std::runtime_error("SDL_CreateGPUBuffer(Forward+) failed: " + LastSdlError());
                            return buffer;
                        };
                        try
                        {
                            replacement.Lights = createBuffer(requiredCapacities[0]);
                            replacement.Tiles = createBuffer(requiredCapacities[1]);
                            replacement.LightIndices = createBuffer(requiredCapacities[2]);
                            replacement.LightCapacityBytes = requiredCapacities[0];
                            replacement.TileCapacityBytes = requiredCapacities[1];
                            replacement.LightIndexCapacityBytes = requiredCapacities[2];
                        }
                        catch (...)
                        {
                            ReleaseForwardPlusResources(replacement);
                            throw;
                        }
                        Retire(std::exchange(surface.ForwardPlus, replacement));
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
                        const std::array destinations{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                      surface.ForwardPlus.LightIndices};
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
                        if (transfer)
                            SDL_ReleaseGPUTransferBuffer(Device, transfer);
                        throw;
                    }
                    surface.ForwardPlus.Columns = tiles.Columns;
                    surface.ForwardPlus.Rows = tiles.Rows;
                    surface.ForwardPlusContentHash = contentHash;
                    surface.ForwardPlusContentValid = true;
                    Statistics.ForwardPlusUploadBytes += totalBytes;
                    Statistics.ForwardPlusCullingMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Opaque)
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal =
                        request != Requests.end() ? preparedDraws.Opaque.Batches.size() : std::size_t{};
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        if (chunk != 0U)
                            acquireContinuation();
                        const auto finalChunk = chunk + 1U == chunkTotal;
                        SDL_GPUColorTargetInfo color{};
                        color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                              : surface.Resources.HdrColor;
                        color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                             surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                        color.load_op = chunk == 0U ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                        color.store_op = surface.Resources.MultisampleHdrColor && finalChunk
                                             ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                             : SDL_GPU_STOREOP_STORE;
                        color.resolve_texture =
                            surface.Resources.MultisampleHdrColor && finalChunk ? surface.Resources.HdrColor : nullptr;
                        SDL_GPUDepthStencilTargetInfo depth{};
                        SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                        if (surface.Resources.Depth)
                        {
                            depth.texture = surface.Resources.Depth;
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
                        if (request != Requests.end())
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
                if (frameGraphPass == SceneFrameGraph.ResolveDepth)
                {
                    const auto started = std::chrono::steady_clock::now();
                    if (request != Requests.end())
                        RecordSampledDepth(commands, surface, request->Packet);
                    Statistics.DepthPassMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return;
                }
                if (frameGraphPass == SceneFrameGraph.Transparency)
                {
                    const auto started = std::chrono::steady_clock::now();
                    const auto batchTotal =
                        request != Requests.end() ? preparedDraws.Transparent.Batches.size() : std::size_t{};
                    const auto chunkTotal =
                        std::max<std::size_t>(1U, (batchTotal + MaximumSceneBatchesPerCommandBuffer - 1U) /
                                                      MaximumSceneBatchesPerCommandBuffer);
                    for (std::size_t chunk = 0; chunk < chunkTotal; ++chunk)
                    {
                        acquireContinuation();
                        const auto finalChunk = chunk + 1U == chunkTotal;
                        SDL_GPUColorTargetInfo color{};
                        color.texture = surface.Resources.MultisampleHdrColor ? surface.Resources.MultisampleHdrColor
                                                                              : surface.Resources.HdrColor;
                        color.load_op = SDL_GPU_LOADOP_LOAD;
                        color.store_op = surface.Resources.MultisampleHdrColor && finalChunk ? SDL_GPU_STOREOP_RESOLVE
                                                                                             : SDL_GPU_STOREOP_STORE;
                        color.resolve_texture =
                            surface.Resources.MultisampleHdrColor && finalChunk ? surface.Resources.HdrColor : nullptr;
                        SDL_GPUDepthStencilTargetInfo depth{};
                        SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                        if (surface.Resources.Depth)
                        {
                            depth.texture = surface.Resources.Depth;
                            depth.load_op = SDL_GPU_LOADOP_LOAD;
                            depth.store_op = finalChunk ? SDL_GPU_STOREOP_DONT_CARE : SDL_GPU_STOREOP_STORE;
                            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                            depthPointer = &depth;
                        }
                        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                        if (!pass)
                            throw std::runtime_error("SDL_BeginGPURenderPass(transparency) failed: " + LastSdlError());
                        if (request != Requests.end())
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
                if (frameGraphPass == SceneFrameGraph.ToneMap)
                {
                    const auto started = std::chrono::steady_clock::now();
                    RecordToneMap(commands, surface);
                    Statistics.ToneMapMilliseconds +=
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                }
            });
        SceneFrameGraph.Graph.Execute(SceneFrameGraph.Compiled, execution);
        ++Statistics.Surfaces;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface)
    {
        if (!ToneMapPipeline || !ToneMapSampler || !surface.Resources.HdrColor || !surface.Resources.SampledColor)
            throw std::logic_error("Tone-map resources are unavailable for an active render surface.");
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.HasOutput ? surface.Resources.ExchangeColor : surface.Resources.SampledColor;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
        const SDL_GPUTextureSamplerBinding binding{surface.Resources.HdrColor, ToneMapSampler};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

} // namespace Keire::RenderBackend
