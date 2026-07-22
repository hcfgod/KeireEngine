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
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet)
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
                MakeObjectUniforms(Math::Multiply(camera.Projection, camera.View), {}, {1.0F, 1.0F, 1.0F, 1.0F},
                                   lighting, packet.Environment, false);
            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
            const SDL_GPUBufferBinding binding{GridBuffer, 0};
            SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
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
        bool localLightsPushed = false;

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
                if (!localLightsPushed)
                {
                    SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
                    localLightsPushed = true;
                }
                SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
                if (!material->Textures.empty())
                {
                    SDL_BindGPUFragmentSamplers(pass, 0, material->Textures.data(),
                                                static_cast<std::uint32_t>(material->Textures.size()));
                }
                const SDL_GPUBufferBinding vertexBinding{mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            else
            {
                const Color tint = draw.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                const ObjectUniforms object =
                    MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), item.World, tint, lighting,
                                       packet.Environment, item.ReceiveShadows);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                const SDL_GPUBufferBinding vertexBinding{mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, 1, draw.Submesh.FirstIndex, 0, 0);
            ++Statistics.DrawCalls;
            Statistics.Triangles += draw.Submesh.IndexCount / 3;
        }
    }

    void RenderSharedState::RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
    {
        if (!surface.Resources.SampledColor)
            return;

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
        const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
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
            if (request->Packet.Lighting.Shadows != ShadowQuality::Disabled)
            {
                const auto splits =
                    BuildPracticalCascadeSplits(std::max(request->Packet.Camera.NearPlane, 0.0001F),
                                                std::max(request->Packet.Environment.DirectionalShadowDistance,
                                                         request->Packet.Camera.NearPlane + 0.0001F),
                                                request->Packet.Environment.DirectionalShadowCascadeCount,
                                                request->Packet.Environment.DirectionalShadowSplitLambda);
                Statistics.DirectionalShadowCascades += static_cast<std::uint32_t>(splits.size());
            }
            DrawScene(commands, pass, surface, request->Packet);
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
