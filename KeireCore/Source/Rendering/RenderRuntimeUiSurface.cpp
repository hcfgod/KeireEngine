#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
        std::vector<RuntimeUiDrawCommand> drawCommands;
        for (const auto* panel : panels)
            drawCommands.insert(drawCommands.end(), panel->Commands.begin(), panel->Commands.end());

        const auto started = std::chrono::steady_clock::now();
        const auto geometry = BuildRuntimeUiGeometry(drawCommands);
        AccumulateRuntimeUiGeometryStatistics(Statistics.RuntimeUiRenderer, geometry);
        if (geometry.Vertices.empty())
            return;
        if (!RuntimeUiCameraOverlayPipeline)
        {
            RuntimeUiCameraOverlayPipeline = CreateRuntimeUiPipeline(false, false, SDL_GPU_SAMPLECOUNT_1, ColorFormat);
        }
        auto* buffer = UploadBuffer(commands, std::as_bytes(std::span(geometry.Vertices)), SDL_GPU_BUFFERUSAGE_VERTEX);
        FrameTransientBuffers.push_back(buffer);

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
            SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
            SDL_DrawGPUPrimitives(pass, batch.VertexCount, 1, batch.FirstVertex, 0);
            ++Statistics.DrawCalls;
            Statistics.Triangles += batch.VertexCount / 3U;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.RuntimeUiRenderer.RepaintCpuMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
} // namespace Keire::RenderBackend
