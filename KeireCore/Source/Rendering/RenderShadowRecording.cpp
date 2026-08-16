#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    using GeometryDetail::Add;
    using GeometryDetail::Length;
    using GeometryDetail::Scale;
    using GeometryDetail::Subtract;
    using GeometryDetail::TransformClip;

    void RenderSharedState::RecordSampledDepth(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                               const SceneRenderPacket& packet)
    {
        if (!surface.Resources.SampledDepth || !SceneDepthPipeline)
            return;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = surface.Resources.SampledDepth;
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
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        for (const auto& item : packet.DrawItems)
        {
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty())
                continue;
            const auto object = Math::Multiply(viewProjection, item.World);
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            const SDL_GPUBufferBinding vertexBinding{
                item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            for (const auto& submesh : mesh.Submeshes)
            {
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                    material && IsTransparentMaterial(material->Surface.AlphaMode))
                {
                    continue;
                }
                SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
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
                Retire(std::move(retired));
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
            for (const auto& item : packet.DrawItems)
            {
                if (!item.CastShadows)
                    continue;
                const auto& mesh = ResolveMesh(item.Mesh);
                if (mesh.Empty())
                    continue;
                const auto object = Math::Multiply(lightMatrix, item.World);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                const SDL_GPUBufferBinding vertexBinding{
                    item.SkinnedAssetVertices ? item.SkinnedAssetVertices : mesh.AssetVertices, 0};
                const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                for (const auto& submesh : mesh.Submeshes)
                    SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
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
            const auto resolution = packet.Environment.DirectionalShadowResolution;
            ensureTexture(surface.Resources.DirectionalShadow, surface.Resources.DirectionalShadowResolution,
                          surface.Resources.DirectionalShadowLayers, resolution, cascadeCount);
            const float nearPlane = std::max(packet.Camera.NearPlane, 0.0001F);
            const float shadowDistance = std::min(
                std::max(packet.Environment.DirectionalShadowDistance, nearPlane + 0.0001F), packet.Camera.FarPlane);
            const auto splits = BuildPracticalCascadeSplits(nearPlane, shadowDistance, cascadeCount,
                                                            packet.Environment.DirectionalShadowSplitLambda);
            const auto inverseViewProjection =
                Math::Inverse(Math::Multiply(packet.Camera.Projection, packet.Camera.View));
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
                const auto up = std::abs(direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                const auto eye = Subtract(center, Scale(direction, radius * 2.0F));
                const auto view = Math::LookAt(eye, center, up);
                const auto projection = Math::Orthographic(radius * 2.0F, 1.0F, 0.01F, radius * 4.0F);
                result.Directional.DirectionalMatrices[cascade] = Math::Multiply(projection, view);
                drawLayer(surface.Resources.DirectionalShadow, cascade,
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
            ensureTexture(surface.Resources.LocalShadow, surface.Resources.LocalShadowResolution,
                          surface.Resources.LocalShadowLayers, LocalShadowResolution, 1U);
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
            std::vector<Detail::ShadowAtlasRequest> requests;
            std::size_t requestedSpots = 0;
            std::size_t requestedPoints = 0;
            const auto lightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                const auto importance = static_cast<std::int32_t>(std::clamp(
                    light.ColorAndIntensity.Alpha * 100.0F, 0.0F, static_cast<float>(std::numeric_limits<int>::max())));
                if (light.Type == SceneLocalLightType::Spot && requestedSpots < MaximumShadowedSpotLights)
                {
                    requests.push_back({{light.Entity.Value(), 0}, resolution(light.ShadowResolution), importance});
                    ++requestedSpots;
                }
                else if (light.Type == SceneLocalLightType::Point && requestedPoints < MaximumShadowedPointLights)
                {
                    for (std::uint8_t face = 0; face < 6U; ++face)
                        requests.push_back(
                            {{light.Entity.Value(), face}, resolution(light.ShadowResolution), importance});
                    ++requestedPoints;
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
            localDepth.texture = surface.Resources.LocalShadow;
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
                const SDL_GPUViewport viewport{static_cast<float>(allocation.X),
                                               static_cast<float>(allocation.Y),
                                               static_cast<float>(allocation.Size),
                                               static_cast<float>(allocation.Size),
                                               0.0F,
                                               1.0F};
                const SDL_Rect scissor{allocation.X, allocation.Y, allocation.Size, allocation.Size};
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
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                if (light.Type == SceneLocalLightType::Spot && spotCount < MaximumShadowedSpotLights)
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
                    result.LocalLayers[lightIndex] = static_cast<float>(spotCount);
                    drawAtlasTile(lightMatrix, *allocation);
                    ++spotCount;
                }
                else if (light.Type == SceneLocalLightType::Point && pointCount < MaximumShadowedPointLights)
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
                    result.LocalLayers[lightIndex] = static_cast<float>(baseLayer);
                    const auto projection = Math::Perspective(90.0F, 1.0F, 0.05F, light.Range);
                    for (std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto view =
                            Math::LookAt(light.Position, Add(light.Position, pointDirections[face]), pointUps[face]);
                        const auto lightMatrix = Math::Multiply(projection, view);
                        result.Local.Matrices[baseLayer + face] = atlasMatrix(lightMatrix, *faceAllocations[face]);
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
