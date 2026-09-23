#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] SDL_Rect RuntimeUiSurfaceScissor(const RuntimeUiRect clip, const std::uint32_t width,
                                                       const std::uint32_t height) noexcept
        {
            if (clip.Empty() || width == 0U || height == 0U)
                return {0, 0, static_cast<int>(width), static_cast<int>(height)};
            const float minimumX = std::clamp(clip.X, 0.0F, static_cast<float>(width));
            const float minimumY = std::clamp(clip.Y, 0.0F, static_cast<float>(height));
            const float maximumX = std::clamp(clip.X + clip.Width, minimumX, static_cast<float>(width));
            const float maximumY = std::clamp(clip.Y + clip.Height, minimumY, static_cast<float>(height));
            const int x = static_cast<int>(std::floor(minimumX));
            const int y = static_cast<int>(std::floor(minimumY));
            const int maximumPixelX = static_cast<int>(std::ceil(maximumX));
            const int maximumPixelY = static_cast<int>(std::ceil(maximumY));
            return {x, y, std::max(0, maximumPixelX - x), std::max(0, maximumPixelY - y)};
        }
    } // namespace

    void RenderSharedState::RecordRuntimeUiCameraPanels(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
    {
        if (!ActiveFrame || !commands || !surface.Resources.WriterColor(surface.ActiveWorksetSlot))
            return;

        std::vector<const CapturedRuntimeUiCameraPanel*> panels;
        for (const auto& panel : ActiveFrame->RuntimeUiCameraPanels)
        {
            if (panel.Surface.Id != surface.Id || panel.Surface.Epoch != surface.Epoch)
                continue;
            if (!RuntimeUiCameraPanelOwnershipValid(panel, *ActiveFrame))
            {
                throw std::logic_error(
                    "Camera-overlay runtime UI packet does not belong to the active frame slot and device generation.");
            }
            panels.push_back(&panel);
        }
        if (panels.empty())
            return;
        std::ranges::sort(panels,
                          [](const auto* first, const auto* second)
                          {
                              return std::tie(first->SortingOrder, first->Sequence) <
                                     std::tie(second->SortingOrder, second->Sequence);
                          });
        const auto started = std::chrono::steady_clock::now();
        RuntimeUiGeometry geometry;
        for (const auto* panel : panels)
        {
            auto part = BuildRuntimeUiCameraGeometry(*panel, surface.Width, surface.Height);
            if (part.Vertices.size() > std::numeric_limits<std::uint32_t>::max() - geometry.Vertices.size())
                throw std::length_error("Camera UI geometry exceeds the 32-bit vertex range.");
            const auto offset = static_cast<std::uint32_t>(geometry.Vertices.size());
            geometry.Vertices.insert(geometry.Vertices.end(), part.Vertices.begin(), part.Vertices.end());
            for (auto batch : part.Batches)
            {
                batch.FirstVertex += offset;
                geometry.Batches.push_back(batch);
            }
        }
        AccumulateRuntimeUiGeometryStatistics(Statistics.RuntimeUiRenderer, geometry);
        if (geometry.Vertices.empty())
            return;
        if (!RuntimeUiCameraOverlayPipeline)
        {
            RuntimeUiCameraOverlayPipeline = CreateRuntimeUiPipeline(false, false, SDL_GPU_SAMPLECOUNT_1, ColorFormat);
        }
        auto* buffer = UploadRuntimeUiBuffer(commands, std::as_bytes(std::span(geometry.Vertices)), "camera overlay");

        SDL_GPUColorTargetInfo color{};
        color.texture = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(camera runtime UI) failed: " + LastSdlError());
        const SDL_GPUViewport viewport{
            0.0F, 0.0F, static_cast<float>(surface.Width), static_cast<float>(surface.Height), 0.0F, 1.0F};
        const Vector4 viewportUniform{static_cast<float>(surface.Width), static_cast<float>(surface.Height), 0.0F,
                                      0.0F};
        const SDL_GPUBufferBinding binding{buffer, 0};
        SDL_BindGPUGraphicsPipeline(pass, RuntimeUiCameraOverlayPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_SetGPUViewport(pass, &viewport);
        SDL_PushGPUVertexUniformData(commands, 0, &viewportUniform, sizeof(viewportUniform));
        for (const auto& batch : geometry.Batches)
        {
            const auto scissor = RuntimeUiSurfaceScissor(batch.ClipRect, surface.Width, surface.Height);
            if (scissor.w == 0 || scissor.h == 0)
                continue;
            SDL_SetGPUScissor(pass, &scissor);
            const auto texture = RuntimeUiTextureBinding(batch.Asset);
            if (!BindRuntimeUiMaterial(commands, pass, batch.Material, RuntimeUiCameraOverlayPipeline, texture, false,
                                       false, SDL_GPU_SAMPLECOUNT_1, ColorFormat))
                continue;
            SDL_DrawGPUPrimitives(pass, batch.VertexCount, 1, batch.FirstVertex, 0);
            ++Statistics.DrawCalls;
            Statistics.Triangles += batch.VertexCount / 3U;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.RuntimeUiRenderer.RepaintCpuMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
    void RenderSharedState::RecordFullscreenEffect(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                   const AssetId material, const bool hdr, const Vector2 time)
    {
        if (!material)
            return;
        auto& workset = surface.ActiveWorkset();
        auto* target = hdr ? workset.HdrColor : surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        auto*& input = workset.FullscreenInputs[hdr ? 0 : 1];
        const auto format = hdr ? SceneColorFormat : ColorFormat;
        if (!input)
        {
            SDL_GPUTextureCreateInfo description{};
            description.type = SDL_GPU_TEXTURETYPE_2D;
            description.format = format;
            description.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            description.width = surface.Width;
            description.height = surface.Height;
            description.layer_count_or_depth = 1;
            description.num_levels = 1;
            description.sample_count = SDL_GPU_SAMPLECOUNT_1;
            // Reserve before acquiring the native resource so allocation failure cannot leak it.
            workset.TransientTextures.reserve(workset.TransientTextures.size() + 1);
            input = SDL_CreateGPUTexture(Device, &description);
            if (!input)
                throw std::runtime_error("Fullscreen scene input allocation failed: " + LastSdlError());
            workset.TransientTextures.push_back(input);
        }
        const float width = static_cast<float>(surface.Width);
        const float height = static_cast<float>(surface.Height);
        const Color white{1, 1, 1, 1};
        const std::array vertices{
            RuntimeUiVertex{{0, 0, 0}, white, {0, 0}},          RuntimeUiVertex{{width, 0, 0}, white, {1, 0}},
            RuntimeUiVertex{{width, height, 0}, white, {1, 1}}, RuntimeUiVertex{{0, 0, 0}, white, {0, 0}},
            RuntimeUiVertex{{width, height, 0}, white, {1, 1}}, RuntimeUiVertex{{0, height, 0}, white, {0, 1}}};
        auto* buffer = UploadRuntimeUiBuffer(commands, std::as_bytes(std::span(vertices)), "fullscreen effect");
        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            throw std::runtime_error("Fullscreen scene copy failed: " + LastSdlError());
        const SDL_GPUTextureLocation source{target, 0, 0, 0, 0, 0};
        const SDL_GPUTextureLocation destination{input, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
            throw std::runtime_error("Fullscreen effect pass failed: " + LastSdlError());
        try
        {
            if (BindRuntimeUiMaterial(commands, pass, material, nullptr, {input, ToneMapSampler}, false, false,
                                      SDL_GPU_SAMPLECOUNT_1, format, true, time))
            {
                const SDL_GPUBufferBinding binding{buffer, 0};
                const SDL_GPUViewport viewport{0, 0, width, height, 0, 1};
                const Vector4 dimensions{width, height, 0, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                SDL_SetGPUViewport(pass, &viewport);
                SDL_PushGPUVertexUniformData(commands, 0, &dimensions, sizeof(dimensions));
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
                ++Statistics.DrawCalls;
                Statistics.Triangles += 2;
            }
        }
        catch (...)
        {
            SDL_EndGPURenderPass(pass);
            throw;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }
} // namespace Keire::RenderBackend
