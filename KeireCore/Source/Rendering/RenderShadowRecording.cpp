#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    using GeometryDetail::Add;
    using GeometryDetail::BuildFrustumPlanes;
    using GeometryDetail::Cross;
    using GeometryDetail::IntersectsFrustum;
    using GeometryDetail::Length;
    using GeometryDetail::NormalizeOr;
    using GeometryDetail::Scale;
    using GeometryDetail::Subtract;
    using GeometryDetail::TransformClip;

    void RenderSharedState::RecordSampledDepth(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                               const SceneRenderPacket& packet,
                                               const PreparedSceneDrawList& opaqueDraws)
    {
        surface.SampledDepthValid = false;
        if (!surface.ActiveWorkset().SampledDepth || !SceneDepthPipeline)
            return;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = surface.ActiveWorkset().SampledDepth;
        depth.clear_depth = 1.0F;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(sampled depth) failed: " + LastSdlError());
        SDL_BindGPUGraphicsPipeline(pass, SceneDepthPipeline);
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        const SceneDrawItem* boundItem = nullptr;
        SDL_GPUBuffer* boundVertices = nullptr;
        SDL_GPUBuffer* boundIndices = nullptr;
        for (const auto& draw : opaqueDraws.Draws)
        {
            const auto& item = *draw.Item;
            const auto& mesh = ResolveMesh(item.Mesh);
            if (boundItem != draw.Item)
            {
                const auto object = Math::Multiply(viewProjection, item.World);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                boundItem = draw.Item;
            }
            auto* vertices = item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices;
            if (vertices != boundVertices)
            {
                const SDL_GPUBufferBinding vertexBinding{vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                boundVertices = vertices;
            }
            if (mesh.Indices != boundIndices)
            {
                const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                boundIndices = mesh.Indices;
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, 1, draw.Submesh.FirstIndex, 0, 0);
            ++Statistics.DepthDrawCalls;
            Statistics.DepthTriangles += draw.Submesh.IndexCount / 3U;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.SampledResolvedDepthAvailable = true;
        surface.SampledDepthViewProjection = viewProjection;
        surface.SampledDepthInverseViewProjection = Math::Inverse(viewProjection);
        surface.SampledDepthValid = true;
    }

    ShadowFrameData RenderSharedState::RecordShadows(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                     const SceneRenderPacket& packet)
    {
        ShadowFrameData result;
        result.LocalLayers.fill(-1.0F);
        if (!ShadowPipeline || !ShadowSampler)
            return result;

        struct PreparedShadowCaster final
        {
            const SceneDrawItem* Item = nullptr;
            const GpuMeshResources* Mesh = nullptr;
            std::uint32_t FirstSubmesh = 0;
            std::uint32_t SubmeshCount = 0;
            MeshBounds Bounds;
            bool Cullable = true;
            bool CoarseCullable = false;
            bool UsesCurrentPoseBounds = false;
        };
        std::vector<PreparedShadowCaster> shadowCasters;
        shadowCasters.reserve(packet.DrawItems.size());
        for (const auto& item : packet.DrawItems)
        {
            if (!item.CastShadows)
                continue;
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty() || mesh.Submeshes.empty())
                continue;
            std::uint32_t firstSubmesh = 0;
            std::uint32_t submeshCount = static_cast<std::uint32_t>(mesh.Submeshes.size());
            auto bounds = mesh.Bounds;
            bool coarseCullable = mesh.BoundsEncloseSubmeshes;
            if (!mesh.Lods.empty())
            {
                const auto& lod = mesh.Lods.front();
                firstSubmesh = lod.FirstSubmesh;
                submeshCount = lod.SubmeshCount;
                bounds = lod.Bounds;
                coarseCullable = mesh.LodBoundsEncloseSubmeshes.front();
            }
            const bool freshPoseBounds = item.HasFreshCurrentPoseBounds(packet.FrameIndex, mesh.Submeshes.size());
            if (freshPoseBounds && submeshCount > 0)
            {
                bounds = item.CurrentPoseSubmeshBounds[firstSubmesh];
                for (std::uint32_t offset = 1; offset < submeshCount; ++offset)
                {
                    const auto& submeshBounds = item.CurrentPoseSubmeshBounds[firstSubmesh + offset];
                    bounds.Minimum.X = std::min(bounds.Minimum.X, submeshBounds.Minimum.X);
                    bounds.Minimum.Y = std::min(bounds.Minimum.Y, submeshBounds.Minimum.Y);
                    bounds.Minimum.Z = std::min(bounds.Minimum.Z, submeshBounds.Minimum.Z);
                    bounds.Maximum.X = std::max(bounds.Maximum.X, submeshBounds.Maximum.X);
                    bounds.Maximum.Y = std::max(bounds.Maximum.Y, submeshBounds.Maximum.Y);
                    bounds.Maximum.Z = std::max(bounds.Maximum.Z, submeshBounds.Maximum.Z);
                }
                coarseCullable = true;
            }
            shadowCasters.push_back({std::addressof(item), std::addressof(mesh), firstSubmesh, submeshCount, bounds,
                                     !item.AlwaysVisible && (!item.SkinnedAssetVertices || freshPoseBounds),
                                     coarseCullable, freshPoseBounds});
        }

        const auto ensureTexture = [&](SDL_GPUTexture*& texture, std::uint32_t& currentResolution,
                                       std::uint32_t& currentLayers, const std::uint32_t resolution,
                                       const std::uint32_t layers)
        {
            if (texture && currentResolution == resolution && currentLayers == layers)
                return;
            if (texture)
            {
                GpuTextureResources retired;
                retired.Texture = texture;
                Retire(retired);
                texture = nullptr;
            }
            SDL_GPUTextureCreateInfo information{};
            information.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            information.format = ShadowDepthFormat;
            information.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            information.width = resolution;
            information.height = resolution;
            information.layer_count_or_depth = layers;
            information.num_levels = 1;
            information.sample_count = SDL_GPU_SAMPLECOUNT_1;
            texture = SDL_CreateGPUTexture(Device, &information);
            if (!texture)
                throw std::runtime_error("SDL_CreateGPUTexture(shadow array) failed: " + LastSdlError());
            currentResolution = resolution;
            currentLayers = layers;
        };

        const auto drawScene = [&](SDL_GPURenderPass* pass, const Matrix4& lightMatrix)
        {
            SDL_GPUBuffer* boundVertices = nullptr;
            SDL_GPUBuffer* boundIndices = nullptr;
            for (const auto& caster : shadowCasters)
            {
                const auto& item = *caster.Item;
                const auto& mesh = *caster.Mesh;
                const auto object = Math::Multiply(lightMatrix, item.World);
                const auto frustum = BuildFrustumPlanes(object);
                if (caster.Cullable && caster.CoarseCullable && !IntersectsFrustum(frustum, caster.Bounds))
                {
                    Statistics.CulledShadowSubmeshes += caster.SubmeshCount;
                    continue;
                }
                bool objectBound = false;
                for (std::uint32_t offset = 0; offset < caster.SubmeshCount; ++offset)
                {
                    const auto submeshIndex = caster.FirstSubmesh + offset;
                    const auto& submesh = mesh.Submeshes[submeshIndex];
                    const auto& bounds =
                        caster.UsesCurrentPoseBounds ? item.CurrentPoseSubmeshBounds[submeshIndex] : submesh.Bounds;
                    if (caster.Cullable && !IntersectsFrustum(frustum, bounds))
                    {
                        ++Statistics.CulledShadowSubmeshes;
                        continue;
                    }
                    if (!objectBound)
                    {
                        SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                        auto* vertices = item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices;
                        if (vertices != boundVertices)
                        {
                            const SDL_GPUBufferBinding vertexBinding{vertices, 0};
                            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                            boundVertices = vertices;
                        }
                        if (mesh.Indices != boundIndices)
                        {
                            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                            boundIndices = mesh.Indices;
                        }
                        objectBound = true;
                    }
                    SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
                    ++Statistics.ShadowDrawCalls;
                    Statistics.ShadowTriangles += submesh.IndexCount / 3U;
                }
            }
        };
        const auto drawLayer = [&](SDL_GPUTexture* texture, const std::uint32_t layer, const Matrix4& lightMatrix)
        {
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = texture;
            depth.clear_depth = 1.0F;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.layer = static_cast<std::uint8_t>(layer);
            auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPURenderPass(shadow) failed: " + LastSdlError());
            SDL_BindGPUGraphicsPipeline(pass, ShadowPipeline);
            drawScene(pass, lightMatrix);
            SDL_EndGPURenderPass(pass);
            ++Statistics.Passes;
        };

        if (packet.Lighting.Enabled && packet.Lighting.Shadows != ShadowQuality::Disabled)
        {
            const auto cascadeCount = std::clamp(packet.Environment.DirectionalShadowCascadeCount, 1U, 4U);
            const auto resolution = DirectionalShadowResolutionForHint(packet.Environment.DirectionalShadowResolution,
                                                                       packet.Lighting.ShadowResolution);
            ensureTexture(surface.ActiveWorkset().DirectionalShadow,
                          surface.ActiveWorkset().DirectionalShadowResolution,
                          surface.ActiveWorkset().DirectionalShadowLayers, resolution, cascadeCount);
            const float nearPlane = std::max(packet.Camera.NearPlane, 0.0001F);
            const float shadowDistance = std::min(
                std::max(packet.Environment.DirectionalShadowDistance, nearPlane + 0.0001F), packet.Camera.FarPlane);
            const auto splits = BuildPracticalCascadeSplits(nearPlane, shadowDistance, cascadeCount,
                                                            packet.Environment.DirectionalShadowSplitLambda);
            const auto inverseViewProjection =
                Math::Inverse(Math::Multiply(packet.UnjitteredProjection, packet.Camera.View));
            std::array<Vector3, 4> nearCorners{};
            std::array<Vector3, 4> farCorners{};
            constexpr std::array<Vector2, 4> coordinates{Vector2{-1.0F, -1.0F}, Vector2{1.0F, -1.0F},
                                                         Vector2{1.0F, 1.0F}, Vector2{-1.0F, 1.0F}};
            for (std::size_t index = 0; index < coordinates.size(); ++index)
            {
                const auto nearClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 0.0F});
                const auto farClip =
                    TransformClip(inverseViewProjection, {coordinates[index].X, coordinates[index].Y, 1.0F});
                nearCorners[index] = {nearClip.X / nearClip.W, nearClip.Y / nearClip.W, nearClip.Z / nearClip.W};
                farCorners[index] = {farClip.X / farClip.W, farClip.Y / farClip.W, farClip.Z / farClip.W};
            }
            const auto direction = Normalize(
                Vector3{packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z});
            const auto up = std::abs(direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
            const auto lightRight = NormalizeOr(Cross(up, direction), {1.0F, 0.0F, 0.0F});
            const auto lightUp = NormalizeOr(Cross(direction, lightRight), up);
            float previousSplit = nearPlane;
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            {
                const float nearRatio = (previousSplit - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                const float farRatio = (splits[cascade] - nearPlane) / (packet.Camera.FarPlane - nearPlane);
                std::array<Vector3, 8> corners{};
                for (std::size_t corner = 0; corner < 4; ++corner)
                {
                    const auto ray = Subtract(farCorners[corner], nearCorners[corner]);
                    corners[corner] = Add(nearCorners[corner], Scale(ray, nearRatio));
                    corners[corner + 4] = Add(nearCorners[corner], Scale(ray, farRatio));
                }
                Vector3 center{};
                for (const auto corner : corners)
                    center = Add(center, corner);
                center = Scale(center, 1.0F / static_cast<float>(corners.size()));
                float radius = 0.0F;
                for (const auto corner : corners)
                    radius = std::max(radius, Length(Subtract(corner, center)));
                radius = std::max(std::ceil(radius * 16.0F) / 16.0F, 0.25F);
                if (resolution > 4U)
                    radius *= static_cast<float>(resolution) / static_cast<float>(resolution - 4U);
                center = StabilizeShadowCenter(center, lightRight, lightUp, radius * 2.0F, resolution);
                const auto eye = Subtract(center, Scale(direction, radius * 2.0F));
                const auto view = Math::LookAt(eye, center, up);
                const auto projection = Math::Orthographic(radius * 2.0F, 1.0F, 0.01F, radius * 4.0F);
                result.Directional.DirectionalMatrices[cascade] = Math::Multiply(projection, view);
                drawLayer(surface.ActiveWorkset().DirectionalShadow, cascade,
                          result.Directional.DirectionalMatrices[cascade]);
                switch (cascade)
                {
                case 0:
                    result.Directional.DirectionalCascadeSplits.X = splits[cascade];
                    break;
                case 1:
                    result.Directional.DirectionalCascadeSplits.Y = splits[cascade];
                    break;
                case 2:
                    result.Directional.DirectionalCascadeSplits.Z = splits[cascade];
                    break;
                default:
                    result.Directional.DirectionalCascadeSplits.W = splits[cascade];
                    break;
                }
                previousSplit = splits[cascade];
            }
            const float encodedCascadeCount = packet.Lighting.Shadows == ShadowQuality::Hard
                                                  ? -static_cast<float>(cascadeCount)
                                                  : static_cast<float>(cascadeCount);
            result.Directional.DirectionalParameters = {encodedCascadeCount, packet.Lighting.ShadowStrength,
                                                        std::max(packet.Lighting.ShadowBias * 0.01F, 0.0001F),
                                                        1.0F / static_cast<float>(resolution)};
            Statistics.DirectionalShadowCascades += cascadeCount;
        }

        const bool hasLocalShadows = std::ranges::any_of(packet.LocalLights, [](const SceneLocalLight& light)
                                                         { return light.Shadows != ShadowQuality::Disabled; });
        if (hasLocalShadows)
        {
            ensureTexture(surface.ActiveWorkset().LocalShadow, surface.ActiveWorkset().LocalShadowResolution,
                          surface.ActiveWorkset().LocalShadowLayers, LocalShadowResolution, 1U);
            const auto resolution = [](const ShadowResolutionHint hint) -> std::uint16_t
            {
                switch (hint)
                {
                case ShadowResolutionHint::Low:
                    return 256;
                case ShadowResolutionHint::High:
                    return 1024;
                case ShadowResolutionHint::VeryHigh:
                    return 2048;
                case ShadowResolutionHint::Medium:
                default:
                    return 512;
                }
            };
            const auto lightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
            std::vector<Detail::LocalShadowCandidate> candidates;
            candidates.reserve(lightCount);
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                constexpr auto maximumImportance = std::numeric_limits<std::int32_t>::max();
                const auto scaledImportance = std::max(0.0F, light.ColorAndIntensity.Alpha * 100.0F);
                const auto importance = scaledImportance >= static_cast<float>(maximumImportance)
                                            ? maximumImportance
                                            : static_cast<std::int32_t>(scaledImportance);
                const auto type = light.Type == SceneLocalLightType::Spot ? Detail::LocalShadowCandidateType::Spot
                                                                          : Detail::LocalShadowCandidateType::Point;
                candidates.push_back({light.Entity.Value(), lightIndex, type, importance});
            }
            const auto selectedLights =
                Detail::SelectLocalShadowCandidates(candidates, MaximumShadowedSpotLights, MaximumShadowedPointLights);
            std::vector<Detail::ShadowAtlasRequest> requests;
            requests.reserve(MaximumShadowedSpotLights + MaximumShadowedPointLights * 6U);
            for (const auto& selected : selectedLights)
            {
                const auto& light = packet.LocalLights[selected.LightIndex];
                if (selected.Type == Detail::LocalShadowCandidateType::Spot)
                {
                    requests.push_back({{selected.Light, 0}, resolution(light.ShadowResolution), selected.Importance});
                }
                else
                {
                    for (std::uint8_t face = 0; face < 6U; ++face)
                    {
                        requests.push_back(
                            {{selected.Light, face}, resolution(light.ShadowResolution), selected.Importance, true});
                    }
                }
            }
            const auto allocations = surface.ShadowAtlas.Allocate(requests);
            const auto findAllocation = [&](const EntityId light,
                                            const std::uint8_t face) -> const Detail::ShadowAtlasAllocation*
            {
                const auto found = std::ranges::find_if(
                    allocations, [&](const auto& allocation)
                    { return allocation.Key.Light == light.Value() && allocation.Key.Subresource == face; });
                return found == allocations.end() ? nullptr : &*found;
            };
            const auto atlasMatrix = [](const Matrix4& lightMatrix, const Detail::ShadowAtlasAllocation& allocation)
            {
                const auto scale = allocation.ScaleOffset.X;
                const auto offsetX = allocation.ScaleOffset.Z;
                const auto offsetY = allocation.ScaleOffset.W;
                Matrix4 atlasTransform{{scale, 0.0F, 0.0F, 0.0F, 0.0F, scale, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                                        2.0F * offsetX + scale - 1.0F, 1.0F - 2.0F * offsetY - scale, 0.0F, 1.0F}};
                return Math::Multiply(atlasTransform, lightMatrix);
            };
            SDL_GPUDepthStencilTargetInfo localDepth{};
            localDepth.texture = surface.ActiveWorkset().LocalShadow;
            localDepth.clear_depth = 1.0F;
            localDepth.load_op = SDL_GPU_LOADOP_CLEAR;
            localDepth.store_op = SDL_GPU_STOREOP_STORE;
            localDepth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            localDepth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto* localPass = SDL_BeginGPURenderPass(commands, nullptr, 0, &localDepth);
            if (!localPass)
                throw std::runtime_error("SDL_BeginGPURenderPass(shadow atlas) failed: " + LastSdlError());
            SDL_BindGPUGraphicsPipeline(localPass, ShadowPipeline);
            const auto drawAtlasTile = [&](const Matrix4& matrix, const Detail::ShadowAtlasAllocation& allocation)
            {
                const auto innerX = static_cast<std::uint16_t>(allocation.X + Detail::ShadowAtlasGuardTexels);
                const auto innerY = static_cast<std::uint16_t>(allocation.Y + Detail::ShadowAtlasGuardTexels);
                const auto innerSize =
                    static_cast<std::uint16_t>(allocation.Size - Detail::ShadowAtlasGuardTexels * 2U);
                const SDL_GPUViewport viewport{static_cast<float>(innerX),
                                               static_cast<float>(innerY),
                                               static_cast<float>(innerSize),
                                               static_cast<float>(innerSize),
                                               0.0F,
                                               1.0F};
                const SDL_Rect scissor{innerX, innerY, innerSize, innerSize};
                SDL_SetGPUViewport(localPass, &viewport);
                SDL_SetGPUScissor(localPass, &scissor);
                drawScene(localPass, matrix);
            };
            std::size_t spotCount = 0;
            std::size_t pointCount = 0;
            constexpr float radiansToDegrees = 57.295779513082320876F;
            constexpr std::array<Vector3, 6> pointDirections{Vector3{1.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F},
                                                             Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, -1.0F, 0.0F},
                                                             Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 0.0F, -1.0F}};
            constexpr std::array<Vector3, 6> pointUps{Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F},
                                                      Vector3{0.0F, 0.0F, -1.0F}, Vector3{0.0F, 0.0F, 1.0F},
                                                      Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F}};
            for (const auto& selected : selectedLights)
            {
                const auto& light = packet.LocalLights[selected.LightIndex];
                if (selected.Type == Detail::LocalShadowCandidateType::Spot)
                {
                    const auto* allocation = findAllocation(light.Entity, 0);
                    if (!allocation)
                    {
                        ++spotCount;
                        continue;
                    }
                    const float outerAngle = std::acos(std::clamp(light.OuterConeCosine, -1.0F, 1.0F));
                    const auto up =
                        std::abs(light.Direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                    const auto view = Math::LookAt(light.Position, Add(light.Position, light.Direction), up);
                    const auto projection = Math::Perspective(
                        std::clamp(outerAngle * 2.0F * radiansToDegrees, 1.01F, 178.0F), 1.0F, 0.05F, light.Range);
                    const auto lightMatrix = Math::Multiply(projection, view);
                    result.Local.Matrices[spotCount] = atlasMatrix(lightMatrix, *allocation);
                    result.Local.SampleBounds[spotCount] = allocation->SampleBounds;
                    result.LocalLayers[selected.LightIndex] = static_cast<float>(spotCount);
                    drawAtlasTile(lightMatrix, *allocation);
                    ++spotCount;
                }
                else
                {
                    const auto baseLayer = static_cast<std::uint32_t>(MaximumShadowedSpotLights + pointCount * 6U);
                    std::array<const Detail::ShadowAtlasAllocation*, 6> faceAllocations{};
                    bool complete = true;
                    for (std::uint8_t face = 0; face < 6U; ++face)
                    {
                        faceAllocations[face] = findAllocation(light.Entity, face);
                        complete &= faceAllocations[face] != nullptr;
                    }
                    if (!complete)
                    {
                        ++pointCount;
                        continue;
                    }
                    result.LocalLayers[selected.LightIndex] = static_cast<float>(baseLayer);
                    const auto projection = Math::Perspective(90.0F, 1.0F, 0.05F, light.Range);
                    for (std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto view =
                            Math::LookAt(light.Position, Add(light.Position, pointDirections[face]), pointUps[face]);
                        const auto lightMatrix = Math::Multiply(projection, view);
                        result.Local.Matrices[baseLayer + face] = atlasMatrix(lightMatrix, *faceAllocations[face]);
                        result.Local.SampleBounds[baseLayer + face] = faceAllocations[face]->SampleBounds;
                        drawAtlasTile(lightMatrix, *faceAllocations[face]);
                    }
                    ++pointCount;
                }
            }
            SDL_EndGPURenderPass(localPass);
            ++Statistics.Passes;
        }
        return result;
    }

} // namespace Keire::RenderBackend
