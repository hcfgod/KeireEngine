#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/ForwardPlusInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"

#include <imgui_impl_sdlgpu3.h>

#include <stdexcept>
#include <tuple>

namespace
{
    struct ClipPoint final
    {
        float X;
        float Y;
        float Z;
        float W;
    };

    [[nodiscard]] ClipPoint TransformClip(const Keire::Matrix4& matrix, const Keire::Vector3 point) noexcept
    {
        const auto& value = matrix.Elements;
        return {value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12],
                value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13],
                value[2] * point.X + value[6] * point.Y + value[10] * point.Z + value[14],
                value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15]};
    }

    [[nodiscard]] Keire::Vector3 Add(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    [[nodiscard]] Keire::Vector3 Subtract(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] Keire::Vector3 Scale(const Keire::Vector3 value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale, value.Z * scale};
    }

    [[nodiscard]] float Length(const Keire::Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] bool IntersectsFrustum(const Keire::Matrix4& clipFromLocal, const Keire::MeshBounds bounds) noexcept
    {
        const std::array corners{Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
        std::array<ClipPoint, corners.size()> clip{};
        std::ranges::transform(corners, clip.begin(),
                               [&](const auto corner) { return TransformClip(clipFromLocal, corner); });
        const auto all = [&](const auto predicate) { return std::ranges::all_of(clip, predicate); };
        return !all([](const auto point) { return point.X < -point.W; }) &&
               !all([](const auto point) { return point.X > point.W; }) &&
               !all([](const auto point) { return point.Y < -point.W; }) &&
               !all([](const auto point) { return point.Y > point.W; }) &&
               !all([](const auto point) { return point.Z < 0.0F; }) &&
               !all([](const auto point) { return point.Z > point.W; });
    }

    [[nodiscard]] float ProjectedHeight(const Keire::Matrix4& viewFromLocal, const Keire::Matrix4& projection,
                                        const Keire::MeshBounds bounds) noexcept
    {
        const Keire::Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                    (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                                    (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
        const Keire::Vector3 extent{(bounds.Maximum.X - bounds.Minimum.X) * 0.5F,
                                    (bounds.Maximum.Y - bounds.Minimum.Y) * 0.5F,
                                    (bounds.Maximum.Z - bounds.Minimum.Z) * 0.5F};
        const auto viewCenter = Keire::Math::TransformPoint(viewFromLocal, center);
        const float localRadius = std::sqrt(extent.X * extent.X + extent.Y * extent.Y + extent.Z * extent.Z);
        const auto& matrix = viewFromLocal.Elements;
        const float scaleX = std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
        const float scaleY = std::sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
        const float scaleZ = std::sqrt(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10]);
        const float radius = localRadius * std::max({scaleX, scaleY, scaleZ});
        return viewCenter.Z > 0.0001F ? 2.0F * radius * std::abs(projection.Elements[5]) / viewCenter.Z : 1.0F;
    }
} // namespace

namespace Keire::RenderBackend
{
    void RenderSharedState::DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                      const ShadowFrameData& shadows)
    {
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;

        if (packet.Environment.SkyVisible && pipelines.Sky)
        {
            const auto& environment =
                packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
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

        if (packet.DrawGrid && GridBuffer && GridVertexCount > 0)
        {
            const ObjectUniforms object =
                MakeObjectUniforms(Math::Multiply(camera.Projection, camera.View), {}, {}, {1.0F, 1.0F, 1.0F, 1.0F},
                                   lighting, packet.Environment, false);
            const AssetShadowUniforms noShadows{};
            const AssetLocalLightUniforms noLocalLights{};
            const std::array shadowBindings{SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler},
                                            SDL_GPUTextureSamplerBinding{EmptyShadowTexture, ShadowSampler}};
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            SDL_PushGPUFragmentUniformData(commands, 0, &noShadows, sizeof(noShadows));
            SDL_PushGPUFragmentUniformData(commands, 1, &noLocalLights, sizeof(noLocalLights));
            const SDL_GPUBufferBinding binding{GridBuffer, 0};
            SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
            SDL_BindGPUFragmentSamplers(pass, 0, shadowBindings.data(),
                                        static_cast<std::uint32_t>(shadowBindings.size()));
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, GridVertexCount, 1, 0, 0);
            ++Statistics.DrawCalls;
        }

        struct PreparedDraw final
        {
            const SceneDrawItem* Item = nullptr;
            MeshSubmesh Submesh;
            AssetId Material;
            MaterialSurfaceState Surface;
            float Depth = 0.0F;
            std::uint32_t SubmeshIndex = 0;
        };
        std::vector<PreparedDraw> prepared;
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
                if (!IntersectsFrustum(clipFromLocal, submesh.Bounds))
                {
                    ++Statistics.CulledSubmeshes;
                    continue;
                }
                AssetId materialId;
                if (submesh.MaterialSlot < item.Materials.size() && item.Materials[submesh.MaterialSlot])
                    materialId = item.Materials[submesh.MaterialSlot];
                else if (submesh.MaterialSlot < mesh.DefaultMaterials.size())
                    materialId = mesh.DefaultMaterials[submesh.MaterialSlot];
                MaterialSurfaceState surfaceState;
                if (const auto* material = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr)
                    surfaceState = material->Surface;
                const Vector3 center{(submesh.Bounds.Minimum.X + submesh.Bounds.Maximum.X) * 0.5F,
                                     (submesh.Bounds.Minimum.Y + submesh.Bounds.Maximum.Y) * 0.5F,
                                     (submesh.Bounds.Minimum.Z + submesh.Bounds.Maximum.Z) * 0.5F};
                prepared.push_back({&item, submesh, materialId, surfaceState,
                                    Math::TransformPoint(viewFromLocal, center).Z, submeshIndex});
                ++Statistics.VisibleSubmeshes;
            }
        }
        std::ranges::stable_sort(
            prepared,
            [](const PreparedDraw& left, const PreparedDraw& right)
            {
                const bool leftBlended = left.Surface.AlphaMode == MaterialAlphaMode::Blend;
                const bool rightBlended = right.Surface.AlphaMode == MaterialAlphaMode::Blend;
                if (leftBlended != rightBlended)
                    return !leftBlended;
                if (leftBlended && left.Depth != right.Depth)
                    return left.Depth > right.Depth;
                if (!leftBlended)
                {
                    const auto leftKey = std::tie(left.Surface.AlphaMode, left.Material, left.Item->Mesh, left.Depth);
                    const auto rightKey =
                        std::tie(right.Surface.AlphaMode, right.Material, right.Item->Mesh, right.Depth);
                    if (leftKey != rightKey)
                        return leftKey < rightKey;
                }
                if (left.Item->Entity != right.Item->Entity)
                    return left.Item->Entity < right.Item->Entity;
                return left.SubmeshIndex < right.SubmeshIndex;
            });

        AssetLocalLightUniforms localLights{};
        const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
        localLights.Counts.X = static_cast<float>(localLightCount);
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
                                                           std::max(light.ShadowBias * 0.01F, 0.002F)};
        }

        for (const auto& draw : prepared)
        {
            const auto& item = *draw.Item;
            const auto& mesh = ResolveMesh(item.Mesh);
            const auto viewModel = Math::Multiply(camera.View, item.World);
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            if (material)
            {
                SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
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
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                std::array<Vector4, 64> numericProperties;
                std::ranges::copy(material->NumericProperties, numericProperties.begin());
                if (material->TintSlot)
                {
                    auto& tint = numericProperties[*material->TintSlot];
                    tint.X *= item.Tint.Red;
                    tint.Y *= item.Tint.Green;
                    tint.Z *= item.Tint.Blue;
                    tint.W *= item.Tint.Alpha;
                }
                SDL_PushGPUFragmentUniformData(
                    commands, 1, numericProperties.data(),
                    static_cast<std::uint32_t>(material->NumericProperties.size() * sizeof(Vector4)));
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
                if (!material->Textures.empty() || material->ReceivesShadows)
                {
                    std::array<SDL_GPUTextureSamplerBinding, 18> bindings{};
                    std::ranges::copy(material->Textures, bindings.begin());
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
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
                }
                const SDL_GPUBufferBinding vertexBinding{mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
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
                const SDL_GPUBufferBinding vertexBinding{mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, 1, draw.Submesh.FirstIndex, 0, 0);
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3;
        }
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
            for (const auto& item : packet.DrawItems)
            {
                if (!item.CastShadows)
                    continue;
                const auto& mesh = ResolveMesh(item.Mesh);
                if (mesh.Empty())
                    continue;
                const auto object = Math::Multiply(lightMatrix, item.World);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                const SDL_GPUBufferBinding vertexBinding{mesh.AssetVertices, 0};
                const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                for (const auto& submesh : mesh.Submeshes)
                    SDL_DrawGPUIndexedPrimitives(pass, submesh.IndexCount, 1, submesh.FirstIndex, 0, 0);
            }
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
                                                        std::max(packet.Lighting.ShadowBias * 0.01F, 0.002F),
                                                        1.0F / static_cast<float>(resolution)};
            Statistics.DirectionalShadowCascades += cascadeCount;
        }

        const bool hasLocalShadows = std::ranges::any_of(packet.LocalLights, [](const SceneLocalLight& light)
                                                         { return light.Shadows != ShadowQuality::Disabled; });
        if (hasLocalShadows)
        {
            ensureTexture(surface.Resources.LocalShadow, surface.Resources.LocalShadowResolution,
                          surface.Resources.LocalShadowLayers, LocalShadowResolution, LocalShadowLayerCount);
            std::size_t spotCount = 0;
            std::size_t pointCount = 0;
            constexpr float radiansToDegrees = 57.295779513082320876F;
            constexpr std::array<Vector3, 6> pointDirections{Vector3{1.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F},
                                                             Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, -1.0F, 0.0F},
                                                             Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 0.0F, -1.0F}};
            constexpr std::array<Vector3, 6> pointUps{Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F},
                                                      Vector3{0.0F, 0.0F, -1.0F}, Vector3{0.0F, 0.0F, 1.0F},
                                                      Vector3{0.0F, 1.0F, 0.0F},  Vector3{0.0F, 1.0F, 0.0F}};
            const auto lightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
            for (std::size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto& light = packet.LocalLights[lightIndex];
                if (light.Shadows == ShadowQuality::Disabled)
                    continue;
                if (light.Type == SceneLocalLightType::Spot && spotCount < MaximumShadowedSpotLights)
                {
                    const float outerAngle = std::acos(std::clamp(light.OuterConeCosine, -1.0F, 1.0F));
                    const auto up =
                        std::abs(light.Direction.Y) > 0.95F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
                    const auto view = Math::LookAt(light.Position, Add(light.Position, light.Direction), up);
                    const auto projection = Math::Perspective(
                        std::clamp(outerAngle * 2.0F * radiansToDegrees, 1.01F, 178.0F), 1.0F, 0.05F, light.Range);
                    result.Local.Matrices[spotCount] = Math::Multiply(projection, view);
                    result.LocalLayers[lightIndex] = static_cast<float>(spotCount);
                    drawLayer(surface.Resources.LocalShadow, static_cast<std::uint32_t>(spotCount),
                              result.Local.Matrices[spotCount]);
                    ++spotCount;
                }
                else if (light.Type == SceneLocalLightType::Point && pointCount < MaximumShadowedPointLights)
                {
                    const auto baseLayer = static_cast<std::uint32_t>(MaximumShadowedSpotLights + pointCount * 6U);
                    result.LocalLayers[lightIndex] = static_cast<float>(baseLayer);
                    const auto projection = Math::Perspective(90.0F, 1.0F, 0.05F, light.Range);
                    for (std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto view =
                            Math::LookAt(light.Position, Add(light.Position, pointDirections[face]), pointUps[face]);
                        result.Local.Matrices[baseLayer + face] = Math::Multiply(projection, view);
                        drawLayer(surface.Resources.LocalShadow, baseLayer + face,
                                  result.Local.Matrices[baseLayer + face]);
                    }
                    ++pointCount;
                }
            }
        }
        return result;
    }

    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
    {
        if (!surface.Resources.SampledColor)
            return;

        const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
        ShadowFrameData shadows;
        shadows.LocalLayers.fill(-1.0F);
        if (request != Requests.end())
            shadows = RecordShadows(commands, surface, request->Packet);

        SDL_GPUColorTargetInfo color{};
        color.texture =
            surface.Resources.MultisampleColor ? surface.Resources.MultisampleColor : surface.Resources.SampledColor;
        color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green, surface.FrameClearColor.Blue,
                             surface.FrameClearColor.Alpha};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = surface.Resources.MultisampleColor ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
        color.resolve_texture = surface.Resources.MultisampleColor ? surface.Resources.SampledColor : nullptr;

        SDL_GPUDepthStencilTargetInfo depth{};
        SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
        if (surface.Resources.Depth)
        {
            depth.texture = surface.Resources.Depth;
            depth.clear_depth = 1.0F;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depthPointer = &depth;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(surface) failed: " + LastSdlError());
        if (request != Requests.end())
        {
            std::vector<ForwardPlusLightBounds> localLightBounds;
            localLightBounds.reserve(request->Packet.LocalLights.size());
            for (const auto& light : request->Packet.LocalLights)
                localLightBounds.push_back(
                    {Math::TransformPoint(request->Packet.Camera.View, light.Position), light.Range});
            const auto tiles = BuildForwardPlusCpuTiles(surface.Width, surface.Height,
                                                        request->Packet.Camera.Projection, localLightBounds);
            Statistics.VisibleLocalLights += static_cast<std::uint32_t>(localLightBounds.size());
            Statistics.OverflowedLightTiles += tiles.OverflowedTiles;
            DrawScene(commands, pass, surface, request->Packet, shadows);
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        ++Statistics.Surfaces;
    }

    void RenderSharedState::EndFrame(ImDrawData* drawData)
    {
        RequireOwner("EndFrame");
        if (!FrameActive)
            throw std::logic_error("No render frame is active.");
        FrameActive = false;

        if (Specification.Mode == RenderMode::Headless)
        {
            Statistics.Surfaces = static_cast<std::uint32_t>(LiveSurfaces().size());
            Statistics.Passes = Statistics.Surfaces;
            return;
        }

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
        if (!commands)
            throw std::runtime_error("SDL_AcquireGPUCommandBuffer failed: " + LastSdlError());

        try
        {
            for (const auto& surface : LiveSurfaces())
                RecordSurface(commands, *surface);

            SDL_GPUTexture* swapchain = nullptr;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, nullptr, nullptr))
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_WaitAndAcquireGPUSwapchainTexture failed: " + LastSdlError());
            }

            if (swapchain)
            {
                const bool renderUi = drawData && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F;
                if (renderUi)
                    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

                SDL_GPUColorTargetInfo target{};
                target.texture = swapchain;
                target.clear_color = {Specification.SwapchainClearColor.Red, Specification.SwapchainClearColor.Green,
                                      Specification.SwapchainClearColor.Blue, Specification.SwapchainClearColor.Alpha};
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPURenderPass(swapchain) failed: " + LastSdlError());
                if (renderUi)
                    ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
            }

            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
            InFlight.push_back({fence, std::move(PendingRetired), std::move(PendingRetiredMeshes),
                                std::move(PendingRetiredTextures), std::move(PendingRetiredPipelines)});
            PendingRetired.clear();
            PendingRetiredMeshes.clear();
            PendingRetiredTextures.clear();
            PendingRetiredPipelines.clear();
        }
        catch (...)
        {
            FrameActive = false;
            throw;
        }
    }

    void RenderSharedState::Close() noexcept
    {
        if (!Open)
            return;
        Open = false;
        FrameActive = false;

        if (Device)
            (void)SDL_WaitForGPUIdle(Device);
        for (const auto& surface : LiveSurfaces())
        {
            ReleaseResources(surface->Resources);
            surface->Owner.reset();
            surface->Width = 0;
            surface->Height = 0;
        }
        for (auto& resources : PendingRetired)
            ReleaseResources(resources);
        PendingRetired.clear();
        for (auto& resources : PendingRetiredMeshes)
            ReleaseMeshResources(resources);
        PendingRetiredMeshes.clear();
        for (auto& resources : PendingRetiredTextures)
            ReleaseTextureResources(resources);
        PendingRetiredTextures.clear();
        for (auto* pipeline : PendingRetiredPipelines)
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        PendingRetiredPipelines.clear();
        for (auto& frame : InFlight)
        {
            for (auto& resources : frame.Retired)
                ReleaseResources(resources);
            for (auto& resources : frame.RetiredMeshes)
                ReleaseMeshResources(resources);
            for (auto& resources : frame.RetiredTextures)
                ReleaseTextureResources(resources);
            for (auto* pipeline : frame.RetiredPipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            if (Device && frame.Fence)
                SDL_ReleaseGPUFence(Device, frame.Fence);
        }
        InFlight.clear();
        Requests.clear();

        for (auto& pipelines : Pipelines)
        {
            if (pipelines.Sky)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Sky);
            if (pipelines.Grid)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Grid);
            if (pipelines.Cube)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Cube);
        }
        Pipelines.clear();
        for (auto& [id, entry] : MeshCache)
        {
            (void)id;
            ReleaseMeshResources(entry.Resources);
        }
        MeshCache.clear();
        for (auto& [id, entry] : TextureCache)
        {
            (void)id;
            ReleaseTextureResources(entry.Resources);
        }
        TextureCache.clear();
        MaterialCache.clear();
        for (auto& [id, entry] : ShaderCache)
        {
            (void)id;
            for (const auto& pipeline : entry.Pipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline.Handle);
        }
        ShaderCache.clear();
        ReleaseTextureResources(CheckerboardTexture);
        ReleaseTextureResources(DefaultSkyTexture);
        ReleaseTextureResources(WhiteTexture);
        ReleaseTextureResources(FlatNormalTexture);
        ReleaseTextureResources(NeutralOrmTexture);
        ReleaseTextureResources(BlackTexture);
        ReleaseTextureResources(BlackDataTexture);
        ReleaseTextureResources(WhiteDataTexture);
        for (const auto& [description, sampler] : SamplerCache)
        {
            (void)description;
            SDL_ReleaseGPUSampler(Device, sampler);
        }
        SamplerCache.clear();
        if (ShadowSampler)
            SDL_ReleaseGPUSampler(Device, ShadowSampler);
        ShadowSampler = nullptr;
        if (EmptyShadowTexture)
            SDL_ReleaseGPUTexture(Device, EmptyShadowTexture);
        EmptyShadowTexture = nullptr;
        if (ShadowPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ShadowPipeline);
        ShadowPipeline = nullptr;
        ReleaseMeshResources(ErrorMesh);
        ReleaseMeshResources(DefaultMesh);
        if (GridBuffer)
            SDL_ReleaseGPUBuffer(Device, GridBuffer);
        GridBuffer = nullptr;
        GridVertexCount = 0;

        if (WindowClaimed && Device && NativeWindow)
            SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
        WindowClaimed = false;
        if (Device)
            SDL_DestroyGPUDevice(Device);
        Device = nullptr;
        NativeWindow = nullptr;
        Window.Reset();
        Windows.Reset();
        Assets.Reset();
    }
} // namespace Keire::RenderBackend
