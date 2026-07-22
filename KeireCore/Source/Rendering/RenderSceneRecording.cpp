#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"

#include <imgui_impl_sdlgpu3.h>

#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                      RenderSurfaceState& surface, const SceneRenderPacket& packet)
    {
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        auto& pipelines = PipelinesFor(samples);
        const auto& camera = packet.Camera;
        const auto& lighting = packet.Lighting;

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

        for (const auto& item : packet.DrawItems)
        {
            const Matrix4 viewModel = Math::Multiply(camera.View, item.World);
            const auto& mesh = ResolveMesh(item.Mesh);
            const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
            const auto* material = item.Material ? ResolveAssetMaterial(item.Material, samples) : nullptr;
            if (material)
            {
                const AssetObjectUniforms object{item.World, camera.View, camera.Projection,
                                                 Transpose(Math::Inverse(item.World))};
                const AssetSceneUniforms scene{
                    {packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                     packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity},
                    {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green, lighting.ColorAndIntensity.Blue,
                     lighting.ColorAndIntensity.Alpha},
                    {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z, packet.Environment.Exposure}};
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
                const Color tint = item.Material ? Color{1.0F, 0.0F, 1.0F, 1.0F} : item.Tint;
                const ObjectUniforms object = MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel),
                                                                 item.World, tint, lighting, packet.Environment, true);
                SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                const SDL_GPUBufferBinding vertexBinding{mesh.Vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            }
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh.IndexCount, 1, 0, 0, 0);
            ++Statistics.DrawCalls;
            Statistics.Triangles += mesh.IndexCount / 3;
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
            DrawScene(commands, pass, surface, request->Packet);
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
            for (const auto& [samples, pipeline] : entry.Pipelines)
            {
                (void)samples;
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            }
        }
        ShaderCache.clear();
        ReleaseTextureResources(CheckerboardTexture);
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
