#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"
#include "KeireInternal/UiContextAccessInternal.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <imgui_impl_sdlgpu3.h>
#include <stb_easy_font.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <stdexcept>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::size_t MaximumRuntimeUiVertices = 1'000'000;
        constexpr std::size_t MaximumRuntimeUiTextBytes = 4096;

        struct EasyFontVertex final
        {
            float X = 0.0F;
            float Y = 0.0F;
            float Z = 0.0F;
            std::array<std::uint8_t, 4> Color{};
        };

        static_assert(sizeof(EasyFontVertex) == 16);

        void AppendRuntimeUiRectangle(std::vector<RuntimeUiVertex>& output, const RuntimeUiRect rectangle,
                                      const Color color)
        {
            if (rectangle.Empty() || color.Alpha <= 0.0F || output.size() > MaximumRuntimeUiVertices - 6U)
                return;
            const RuntimeUiVertex topLeft{{rectangle.X, rectangle.Y}, color};
            const RuntimeUiVertex topRight{{rectangle.X + rectangle.Width, rectangle.Y}, color};
            const RuntimeUiVertex bottomLeft{{rectangle.X, rectangle.Y + rectangle.Height}, color};
            const RuntimeUiVertex bottomRight{{rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height}, color};
            output.insert(output.end(), {topLeft, topRight, bottomRight, topLeft, bottomRight, bottomLeft});
        }

        void AppendRuntimeUiBorder(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            const float thickness = std::min(command.BorderWidth, std::min(command.Rect.Width, command.Rect.Height));
            if (thickness <= 0.0F || command.BorderColor.Alpha <= 0.0F)
                return;
            const auto append = [&](const RuntimeUiRect rectangle)
            { AppendRuntimeUiRectangle(output, rectangle.Intersect(command.ClipRect), command.BorderColor); };
            append({command.Rect.X, command.Rect.Y, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + command.Rect.Height - thickness, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
            append({command.Rect.X + command.Rect.Width - thickness, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
        }

        void AppendRuntimeUiText(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            if (command.Text.empty() || command.FontSize <= 0.0F || command.ColorValue.Alpha <= 0.0F ||
                output.size() >= MaximumRuntimeUiVertices)
                return;
            std::string text = command.Text.substr(0, MaximumRuntimeUiTextBytes);
            const float scale = command.FontSize / 12.0F;
            const float width = static_cast<float>(stb_easy_font_width(text.data())) * scale;
            const float height = static_cast<float>(stb_easy_font_height(text.data())) * scale;
            float originX = command.Rect.X;
            float originY = command.Rect.Y;
            if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                originX += (command.Rect.Width - width) * 0.5F;
            else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                originX += command.Rect.Width - width;
            if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                originY += (command.Rect.Height - height) * 0.5F;
            else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                originY += command.Rect.Height - height;

            const auto bufferBytes = std::min<std::size_t>(text.size() * 300U, std::numeric_limits<int>::max());
            std::vector<std::byte> buffer(bufferBytes);
            const int quads =
                stb_easy_font_print(0.0F, 0.0F, text.data(), nullptr, buffer.data(), static_cast<int>(buffer.size()));
            const auto* vertices = reinterpret_cast<const EasyFontVertex*>(buffer.data());
            for (int quad = 0; quad < quads && output.size() <= MaximumRuntimeUiVertices - 6U; ++quad)
            {
                const auto glyph = std::span(vertices + quad * 4, 4);
                float minimumX = glyph.front().X;
                float minimumY = glyph.front().Y;
                float maximumX = glyph.front().X;
                float maximumY = glyph.front().Y;
                for (const auto& vertex : glyph)
                {
                    minimumX = std::min(minimumX, vertex.X);
                    minimumY = std::min(minimumY, vertex.Y);
                    maximumX = std::max(maximumX, vertex.X);
                    maximumY = std::max(maximumY, vertex.Y);
                }
                const RuntimeUiRect rectangle{originX + minimumX * scale, originY + minimumY * scale,
                                              (maximumX - minimumX) * scale, (maximumY - minimumY) * scale};
                AppendRuntimeUiRectangle(output, rectangle.Intersect(command.ClipRect), command.ColorValue);
            }
        }

        [[nodiscard]] std::vector<RuntimeUiVertex>
        BuildRuntimeUiVertices(const std::span<const RuntimeUiDrawCommand> commands)
        {
            std::vector<RuntimeUiVertex> result;
            result.reserve(std::min<std::size_t>(commands.size() * 12U, MaximumRuntimeUiVertices));
            for (const auto& command : commands)
            {
                if (result.size() >= MaximumRuntimeUiVertices)
                    break;
                switch (command.Type)
                {
                case RuntimeUiDrawType::Quad:
                    AppendRuntimeUiRectangle(result, command.Rect.Intersect(command.ClipRect), command.ColorValue);
                    AppendRuntimeUiBorder(result, command);
                    break;
                case RuntimeUiDrawType::Image:
                    AppendRuntimeUiRectangle(result, command.Rect.Intersect(command.ClipRect), command.ColorValue);
                    break;
                case RuntimeUiDrawType::Text:
                    AppendRuntimeUiText(result, command);
                    break;
                case RuntimeUiDrawType::PushClip:
                case RuntimeUiDrawType::PopClip:
                    break;
                }
            }
            return result;
        }
    } // namespace

    void RenderSharedState::RecordSwapchain(SDL_GPUCommandBuffer*& commands, ImDrawData* drawData)
    {
        SDL_GPUTexture* swapchain = nullptr;
        std::uint32_t swapchainWidth = 0;
        std::uint32_t swapchainHeight = 0;
        const auto swapchainStarted = std::chrono::steady_clock::now();
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, &swapchainWidth,
                                                   &swapchainHeight))
        {
            (void)SDL_CancelGPUCommandBuffer(commands);
            commands = nullptr;
            throw std::runtime_error("SDL_WaitAndAcquireGPUSwapchainTexture failed: " + LastSdlError());
        }
        Statistics.SwapchainWaitMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - swapchainStarted).count();

        const auto uiRecordingStarted = std::chrono::steady_clock::now();
        if (swapchain)
        {
            std::shared_ptr<Keire::Detail::UiContextAccess> editorUiContextAccess;
            std::unique_lock<std::recursive_mutex> editorUiContextLock;
            if (drawData)
            {
                editorUiContextAccess = EditorUiContextAccess.load(std::memory_order_acquire);
                editorUiContextLock = Keire::Detail::AcquireRequiredUiContext(
                    editorUiContextAccess, "Dear ImGui GPU recording requires the renderer's live UI context binding.");
            }
            const bool renderEditorUi = drawData && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F;
            bool presentedSurface = false;
            SDL_GPUBuffer* runtimeUiBuffer = nullptr;
            std::uint32_t runtimeUiVertexCount = 0;
            const auto runtimeUiCommands = ActiveFrame
                                               ? std::span<const RuntimeUiDrawCommand>(ActiveFrame->RuntimeUiCommands)
                                               : std::span<const RuntimeUiDrawCommand>{};
            if (!runtimeUiCommands.empty())
            {
                const auto vertices = BuildRuntimeUiVertices(runtimeUiCommands);
                if (!vertices.empty())
                {
                    if (!RuntimeUiPipeline)
                        RuntimeUiPipeline = CreateRuntimeUiPipeline();
                    runtimeUiBuffer =
                        UploadBuffer(commands, std::as_bytes(std::span(vertices)), SDL_GPU_BUFFERUSAGE_VERTEX);
                    FrameTransientBuffers.push_back(runtimeUiBuffer);
                    runtimeUiVertexCount = static_cast<std::uint32_t>(vertices.size());
                }
            }
            const auto presentation =
                ActiveFrame ? ResolveSurface(ActiveFrame->PresentationSurface) : std::shared_ptr<RenderSurfaceState>{};
            if (presentation && presentation->Resources.PublishedColor() && presentation->Width != 0 &&
                presentation->Height != 0 && swapchainWidth != 0 && swapchainHeight != 0)
            {
                const bool submitted = std::ranges::any_of(
                    ActiveFrame->Requests, [&presentation](const auto& request)
                    { return request.Surface.Id == presentation->Id && request.Surface.Epoch == presentation->Epoch; });
                SDL_GPUBlitInfo blit{};
                blit.source.texture = submitted ? presentation->Resources.WriterColor(ActiveFrame->FrameSlot)
                                                : presentation->Resources.PublishedColor();
                blit.source.w = presentation->Width;
                blit.source.h = presentation->Height;
                blit.destination.texture = swapchain;
                blit.destination.w = swapchainWidth;
                blit.destination.h = swapchainHeight;
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;
                SDL_BlitGPUTexture(commands, &blit);
                presentedSurface = true;
            }
            if (renderEditorUi)
                ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

            if (renderEditorUi || runtimeUiBuffer || !presentedSurface)
            {
                SDL_GPUColorTargetInfo target{};
                target.texture = swapchain;
                target.clear_color = {Specification.SwapchainClearColor.Red, Specification.SwapchainClearColor.Green,
                                      Specification.SwapchainClearColor.Blue, Specification.SwapchainClearColor.Alpha};
                target.load_op = presentedSurface ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPURenderPass(swapchain) failed: " + LastSdlError());
                if (runtimeUiBuffer)
                {
                    const SDL_GPUViewport viewport{
                        0.0F, 0.0F, static_cast<float>(swapchainWidth), static_cast<float>(swapchainHeight),
                        0.0F, 1.0F};
                    const Vector4 viewportUniform{static_cast<float>(swapchainWidth),
                                                  static_cast<float>(swapchainHeight), 0.0F, 0.0F};
                    const SDL_GPUBufferBinding binding{runtimeUiBuffer, 0};
                    SDL_BindGPUGraphicsPipeline(pass, RuntimeUiPipeline);
                    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                    SDL_SetGPUViewport(pass, &viewport);
                    SDL_PushGPUVertexUniformData(commands, 0, &viewportUniform, sizeof(viewportUniform));
                    SDL_DrawGPUPrimitives(pass, runtimeUiVertexCount, 1, 0, 0);
                    ++Statistics.DrawCalls;
                    Statistics.Triangles += runtimeUiVertexCount / 3U;
                }
                if (renderEditorUi)
                {
                    ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    RenderedEditorUiFrameCount.fetch_add(1U, std::memory_order_relaxed);
#endif
                }
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
            }
        }
        Statistics.UiRecordingMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uiRecordingStarted).count();
    }

    RenderSharedState::RenderSharedState(RenderSpecification specification, Ref<WindowSystem> windows,
                                         Ref<Keire::Window> window, Ref<AssetSystem> assets, Ref<JobSystem> jobs,
                                         Ref<StreamingSystem> streaming)
        : Specification(std::move(specification)), Windows(std::move(windows)), Window(std::move(window)),
          Assets(std::move(assets)), Streaming(std::move(streaming)), OwnerThread(std::this_thread::get_id()),
          Jobs(std::move(jobs))
    {
        if (!ValidColor(Specification.SwapchainClearColor))
            throw std::invalid_argument("Render swapchain clear color must contain finite values in 0..1.");
        SceneFrameGraph = BuildStaticSceneFrameGraph();
        if (Specification.MaximumFramesInFlight < 1 || Specification.MaximumFramesInFlight > 3)
            throw std::invalid_argument("MaximumFramesInFlight must be in the range 1..3.");
        if (Specification.DeviceLossRecoveryAttempts > 3)
            throw std::invalid_argument("DeviceLossRecoveryAttempts must be in the range 0..3.");
        if (Specification.Mode == RenderMode::Automatic)
            throw std::invalid_argument("RenderSystem requires a resolved render mode.");
        Statistics.AllowedFramesInFlight = Specification.MaximumFramesInFlight;
        PublishedStatistics = Statistics;
        InFlight.reserve(Specification.MaximumFramesInFlight);
        for (std::uint32_t slot = 0; slot < Specification.MaximumFramesInFlight; ++slot)
            AvailableFrameSlots.push_back(slot);
        if (Specification.Mode != RenderMode::Rendered)
            return;
        if (!Jobs)
            throw std::invalid_argument("Rendered mode requires an application job system.");
        RenderJobs = Jobs->CreateScope("Renderer background work");
        if (!Windows || !Window)
            throw std::invalid_argument("Rendered mode requires an open window system and primary window.");

        NativeWindow = WindowSystemInternalAccess::NativeWindow(*Windows, Window->Id());
        if (!NativeWindow)
            throw std::runtime_error("The renderer could not resolve the primary native window.");

        PresentMode = ToSdlPresentMode(Specification.PresentMode);
        try
        {
            StartRenderThread();
            DispatchRender(
                [this]
                {
                    CreateDeviceAndMandatoryResources(false);
                    PublishedStatistics = Statistics;
                });
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    RenderSharedState::~RenderSharedState() { Close(); }

    void RenderSharedState::StartRenderThread()
    {
        bool shouldStart = Specification.Mode == RenderMode::Rendered;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        shouldStart = shouldStart || (Specification.Mode == RenderMode::Headless && ThreadedHeadlessForTest);
#endif
        if (!shouldStart || RenderThread.joinable())
            return;
        RenderThread = std::jthread(
            [this]
            {
                RenderThreadId = std::this_thread::get_id();
                for (;;)
                {
                    RenderQueueItem item;
                    {
                        std::unique_lock lock(RenderQueueMutex);
                        RenderQueueReady.wait_for(lock, std::chrono::milliseconds(1),
                                                  [&] { return StopRenderQueue || !RenderQueue.empty(); });
                        if (StopRenderQueue && RenderQueue.empty())
                        {
                            lock.unlock();
                            if (!DeviceLost)
                            {
                                try
                                {
                                    while (!InFlight.empty())
                                        CollectCompletedFrames(true);
                                }
                                catch (...)
                                {
                                    HandleRenderThreadFailure(std::current_exception());
                                }
                            }
                            break;
                        }
                        if (!RenderQueue.empty())
                        {
                            item = std::move(RenderQueue.front());
                            RenderQueue.pop_front();
                        }
                    }
                    RenderQueueSpace.notify_one();
                    if (item.Work)
                        item.Work();
                    else if (!DeviceLost)
                    {
                        try
                        {
                            CollectCompletedFrames(false);
                        }
                        catch (...)
                        {
                            HandleRenderThreadFailure(std::current_exception());
                        }
                    }
                }
            });
    }

    void RenderSharedState::StopRenderThread() noexcept
    {
        if (!RenderThread.joinable())
            return;
        {
            std::scoped_lock lock(RenderQueueMutex);
            StopRenderQueue = true;
        }
        RenderQueueReady.notify_all();
        RenderThread.join();
        RenderThreadId = {};
    }

    void RenderSharedState::DispatchRender(const std::function<void()>& work)
    {
        if (!RenderThread.joinable())
        {
            work();
            return;
        }
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(work));
        auto completion = task->get_future();
        {
            std::unique_lock lock(RenderQueueMutex);
            const auto capacity = static_cast<std::size_t>(Specification.MaximumFramesInFlight);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            const bool admissionBlocked = !StopRenderQueue && RenderQueue.size() >= capacity;
            if (admissionBlocked)
            {
                ++RenderDispatchAdmissionWaiters;
                RenderQueueReady.notify_all();
            }
            try
            {
#endif
                RenderQueueSpace.wait(lock, [&] { return StopRenderQueue || RenderQueue.size() < capacity; });
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            }
            catch (...)
            {
                if (admissionBlocked)
                {
                    --RenderDispatchAdmissionWaiters;
                    RenderQueueReady.notify_all();
                }
                throw;
            }
            if (admissionBlocked)
            {
                --RenderDispatchAdmissionWaiters;
                RenderQueueReady.notify_all();
            }
#endif
            if (StopRenderQueue)
                throw std::logic_error("Renderer submission queue is closed.");
            RenderQueue.push_back({[task] { (*task)(); }, 0});
            const auto queueDepth = static_cast<std::uint32_t>(RenderQueue.size());
            auto queueHighWater = RenderQueueHighWaterMark.load(std::memory_order_relaxed);
            while (queueHighWater < queueDepth && !RenderQueueHighWaterMark.compare_exchange_weak(
                                                      queueHighWater, queueDepth, std::memory_order_relaxed))
            {
            }
        }
        RenderQueueReady.notify_one();
        completion.get();
    }

    void RenderSharedState::RequireOwner(const char* operation) const
    {
        if (std::this_thread::get_id() != OwnerThread)
            throw std::logic_error(std::string("RenderSystem::") + operation +
                                   " must be called on the application owner thread.");
        if (!Open)
            throw std::logic_error(std::string("RenderSystem::") + operation + " called after shutdown.");
    }

    void RenderSharedState::RequireRenderThread(const char* operation) const
    {
        if (Specification.Mode == RenderMode::Rendered && std::this_thread::get_id() != RenderThreadId)
            throw std::logic_error(std::string("RenderSystem::") + operation + " must run on the render thread.");
    }

    std::vector<std::shared_ptr<RenderSurfaceState>> RenderSharedState::LiveSurfaces()
    {
        std::scoped_lock lock(SurfaceMutex);
        std::vector<std::shared_ptr<RenderSurfaceState>> result;
        result.reserve(Surfaces.size());
        for (const auto& entry : Surfaces)
            if (entry.Current && entry.State)
                result.push_back(entry.State);
        std::ranges::sort(result, {}, &RenderSurfaceState::Id);
        return result;
    }

    std::vector<std::shared_ptr<RenderSurfaceState>> RenderSharedState::AllSurfaceEpochs()
    {
        std::scoped_lock lock(SurfaceMutex);
        std::vector<std::shared_ptr<RenderSurfaceState>> result;
        result.reserve(Surfaces.size());
        for (const auto& entry : Surfaces)
            if (entry.State)
                result.push_back(entry.State);
        std::ranges::sort(result, [](const auto& first, const auto& second)
                          { return std::tie(first->Id, first->Epoch) < std::tie(second->Id, second->Epoch); });
        return result;
    }

    std::vector<RenderSurfaceToken> RenderSharedState::CaptureLiveSurfaceTokens()
    {
        std::scoped_lock lock(SurfaceMutex);
        std::vector<RenderSurfaceToken> result;
        result.reserve(Surfaces.size());
        for (const auto& entry : Surfaces)
        {
            if (entry.Current && entry.State && entry.State->Lifetime)
                result.push_back({entry.State->Id, entry.State->Epoch, entry.State->Lifetime});
        }
        std::ranges::sort(result, [](const auto& first, const auto& second)
                          { return std::tie(first.Id, first.Epoch) < std::tie(second.Id, second.Epoch); });
        return result;
    }

    RenderSurfaceToken RenderSharedState::CaptureSurfaceToken(const std::shared_ptr<RenderSurfaceState>& surface)
    {
        if (!surface)
            return {};
        std::scoped_lock lock(SurfaceMutex);
        const auto found = std::ranges::find_if(Surfaces, [&surface](const RenderSurfaceRegistryEntry& entry)
                                                { return entry.Current && entry.State == surface; });
        if (found == Surfaces.end() || !surface->Lifetime)
            throw std::invalid_argument("Render surface epoch is no longer current for frame capture.");
        return {surface->Id, surface->Epoch, surface->Lifetime};
    }

    SDL_GPUTexture* RenderSharedState::CaptureUiSurfaceTexture(const std::shared_ptr<RenderSurfaceState>& surface)
    {
        RequireOwner("CaptureUiSurfaceTexture");
        if (!FrameActive)
            throw std::logic_error("Render-surface UI images may only be captured during an active frame.");

        const auto token = CaptureSurfaceToken(surface);
        auto* texture = surface->PublishedTexture.load(std::memory_order_acquire);
        if (!texture)
            return nullptr;

        const auto textureIdentity = reinterpret_cast<std::uintptr_t>(texture);
        const auto existing =
            std::ranges::find_if(PendingUiSurfaceTextureBindings,
                                 [&token, textureIdentity](const CapturedSurfaceTextureBinding& binding)
                                 {
                                     return binding.Surface.Id == token.Id && binding.Surface.Epoch == token.Epoch &&
                                            binding.TextureIdentity == textureIdentity;
                                 });
        if (existing == PendingUiSurfaceTextureBindings.end())
            PendingUiSurfaceTextureBindings.push_back({token, textureIdentity});
        return texture;
    }

    std::shared_ptr<RenderSurfaceState> RenderSharedState::ResolveSurface(const RenderSurfaceToken& token)
    {
        if (!token)
            return {};
        std::scoped_lock lock(SurfaceMutex);
        const auto found = std::ranges::find_if(Surfaces,
                                                [&token](const RenderSurfaceRegistryEntry& entry)
                                                {
                                                    return entry.State && entry.State->Id == token.Id &&
                                                           entry.State->Epoch == token.Epoch &&
                                                           entry.State->Lifetime == token.Lifetime;
                                                });
        return found == Surfaces.end() ? std::shared_ptr<RenderSurfaceState>{} : found->State;
    }

    std::shared_ptr<RenderSurfaceState>
    RenderSharedState::CreateSurfaceEpoch(const std::shared_ptr<RenderSurfaceState>& previous,
                                          const std::uint32_t width, const std::uint32_t height)
    {
        RequireOwner("CreateSurfaceEpoch");
        if (!previous)
            throw std::invalid_argument("Render surface resizing requires a current epoch.");
        auto replacement = std::make_shared<RenderSurfaceState>();
        replacement->Owner = shared_from_this();
        replacement->Specification = previous->Specification;
        replacement->Specification.Width = width;
        replacement->Specification.Height = height;
        replacement->Id = previous->Id;
        replacement->Epoch = previous->Epoch + 1U;
        replacement->RequestedWidth = width;
        replacement->RequestedHeight = height;
        const auto previousProperties = previous->SurfacePropertiesSnapshot();
        replacement->Width = previousProperties.Width;
        replacement->Height = previousProperties.Height;
        replacement->ActualSamples = previousProperties.SampleCount;
        replacement->PublishedProperties = previousProperties;
        replacement->FrameClearColor = previous->Specification.ClearColor;
        replacement->GpuOcclusionDebugMipLevel.store(
            previous->GpuOcclusionDebugMipLevel.load(std::memory_order_acquire), std::memory_order_relaxed);
        replacement->GpuOcclusionDebugMode.store(previous->GpuOcclusionDebugMode.load(std::memory_order_acquire),
                                                 std::memory_order_relaxed);
        replacement->Lifetime = std::make_shared<RenderSurfaceEpochLease>(replacement->Id, replacement->Epoch);
        {
            std::scoped_lock lock(SurfaceMutex);
            const auto found = std::ranges::find_if(Surfaces, [&previous](const RenderSurfaceRegistryEntry& entry)
                                                    { return entry.Current && entry.State == previous; });
            if (found == Surfaces.end())
                throw std::logic_error("Render surface epoch changed during resize.");
            found->Current = false;
            Surfaces.push_back({replacement, true});
        }
        return replacement;
    }

    void RenderSharedState::RequestSurfaceRetirement(const std::shared_ptr<RenderSurfaceState>& surface) noexcept
    {
        if (!surface)
            return;
        {
            std::scoped_lock lock(SurfaceMutex);
            if (const auto found = std::ranges::find_if(Surfaces, [&surface](const RenderSurfaceRegistryEntry& entry)
                                                        { return entry.State == surface; });
                found != Surfaces.end())
            {
                found->Current = false;
            }
        }
        try
        {
            DispatchRender([self = shared_from_this()] { self->CollectRetiredSurfaceEpochs(); });
        }
        catch (...)
        {
            surface->Owner.reset();
        }
    }

    void RenderSharedState::CollectRetiredSurfaceEpochs() noexcept
    {
        std::vector<std::shared_ptr<RenderSurfaceState>> retired;
        {
            std::scoped_lock lock(SurfaceMutex);
            for (auto iterator = Surfaces.begin(); iterator != Surfaces.end();)
            {
                if (!iterator->Current && iterator->State && iterator->State->Lifetime.use_count() == 1)
                {
                    retired.push_back(std::move(iterator->State));
                    iterator = Surfaces.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }
        for (const auto& surface : retired)
            RetireSurface(*surface);
    }

    void RenderSharedState::ReleaseResources(SurfaceResources& resources) noexcept
    {
        if (!Device)
        {
            resources = {};
            return;
        }
        for (auto& workset : resources.Worksets)
        {
            for (auto& frame : workset.GpuOcclusionFrames)
                ReleaseGpuOcclusionFrameResources(frame);
            ReleaseForwardPlusResources(workset.ForwardPlus);
            ReleaseDynamicUploadResources(workset.DynamicUploads);
            if (workset.LocalShadow)
                SDL_ReleaseGPUTexture(Device, workset.LocalShadow);
            if (workset.DirectionalShadow)
                SDL_ReleaseGPUTexture(Device, workset.DirectionalShadow);
            if (workset.Depth)
                SDL_ReleaseGPUTexture(Device, workset.Depth);
            if (workset.SampledDepth)
                SDL_ReleaseGPUTexture(Device, workset.SampledDepth);
            if (workset.MultisampleHdrColor)
                SDL_ReleaseGPUTexture(Device, workset.MultisampleHdrColor);
            for (auto* texture : workset.TransientTextures)
                if (texture)
                    SDL_ReleaseGPUTexture(Device, texture);
        }
        for (auto* texture : resources.FinalOutputs)
            if (texture)
                SDL_ReleaseGPUTexture(Device, texture);
        resources = {};
    }

    SDL_GPUShader* RenderSharedState::CreateShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = Detail::BuiltinShaderUniformBufferCount(vertex);
        information.num_samplers = vertex ? 0U : 2U;

        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinUnlitVertexDxil : Detail::BuiltinUnlitFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinUnlitVertexDxilSize : Detail::BuiltinUnlitFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinUnlitVertexMsl : Detail::BuiltinUnlitFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinUnlitVertexMslSize : Detail::BuiltinUnlitFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinUnlitVertexSpirV : Detail::BuiltinUnlitFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinUnlitVertexSpirVSize : Detail::BuiltinUnlitFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported built-in shader format.");

        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateSkyShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 0U : 1U;
        information.num_samplers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinSkyVertexDxil : Detail::BuiltinSkyFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinSkyVertexDxilSize : Detail::BuiltinSkyFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinSkyVertexMsl : Detail::BuiltinSkyFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinSkyVertexMslSize : Detail::BuiltinSkyFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinSkyVertexSpirV : Detail::BuiltinSkyFragmentSpirV;
            information.code_size = vertex ? Detail::BuiltinSkyVertexSpirVSize : Detail::BuiltinSkyFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported sky shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(sky) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateGridShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinGridVertexDxil : Detail::BuiltinGridFragmentDxil;
            information.code_size = vertex ? Detail::BuiltinGridVertexDxilSize : Detail::BuiltinGridFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinGridVertexMsl : Detail::BuiltinGridFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinGridVertexMslSize : Detail::BuiltinGridFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinGridVertexSpirV : Detail::BuiltinGridFragmentSpirV;
            information.code_size = vertex ? Detail::BuiltinGridVertexSpirVSize : Detail::BuiltinGridFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported grid shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(grid) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateShadowShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 1U : 0U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinShadowVertexDxil : Detail::BuiltinShadowFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinShadowVertexDxilSize : Detail::BuiltinShadowFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinShadowVertexMsl : Detail::BuiltinShadowFragmentMsl;
            information.code_size = vertex ? Detail::BuiltinShadowVertexMslSize : Detail::BuiltinShadowFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinShadowVertexSpirV : Detail::BuiltinShadowFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinShadowVertexSpirVSize : Detail::BuiltinShadowFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported shadow shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(shadow) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateToneMapShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_samplers = vertex ? 0U : 1U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinToneMapVertexDxil : Detail::BuiltinToneMapFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexDxilSize : Detail::BuiltinToneMapFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinToneMapVertexMsl : Detail::BuiltinToneMapFragmentMsl;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexMslSize : Detail::BuiltinToneMapFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinToneMapVertexSpirV : Detail::BuiltinToneMapFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinToneMapVertexSpirVSize : Detail::BuiltinToneMapFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported tone-map shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(tone map) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUShader* RenderSharedState::CreateRuntimeUiShader(const bool vertex) const
    {
        SDL_GPUShaderCreateInfo information{};
        information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        information.num_uniform_buffers = vertex ? 1U : 0U;
        const auto formats = SDL_GetGPUShaderFormats(Device);
        if (formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            information.format = SDL_GPU_SHADERFORMAT_DXIL;
            information.code = vertex ? Detail::BuiltinRuntimeUiVertexDxil : Detail::BuiltinRuntimeUiFragmentDxil;
            information.code_size =
                vertex ? Detail::BuiltinRuntimeUiVertexDxilSize : Detail::BuiltinRuntimeUiFragmentDxilSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            information.format = SDL_GPU_SHADERFORMAT_MSL;
            information.code = vertex ? Detail::BuiltinRuntimeUiVertexMsl : Detail::BuiltinRuntimeUiFragmentMsl;
            information.code_size =
                vertex ? Detail::BuiltinRuntimeUiVertexMslSize : Detail::BuiltinRuntimeUiFragmentMslSize;
        }
        else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            information.format = SDL_GPU_SHADERFORMAT_SPIRV;
            information.entrypoint = vertex ? "VSMain" : "PSMain";
            information.code = vertex ? Detail::BuiltinRuntimeUiVertexSpirV : Detail::BuiltinRuntimeUiFragmentSpirV;
            information.code_size =
                vertex ? Detail::BuiltinRuntimeUiVertexSpirVSize : Detail::BuiltinRuntimeUiFragmentSpirVSize;
        }
        else
            throw std::runtime_error("The active SDL_GPU backend exposes no supported runtime UI shader format.");
        SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
        if (!shader)
            throw std::runtime_error("SDL_CreateGPUShader(runtime UI) failed: " + LastSdlError());
        return shader;
    }

    SDL_GPUGraphicsPipeline* RenderSharedState::CreateRuntimeUiPipeline()
    {
        SDL_GPUShader* vertex = nullptr;
        SDL_GPUShader* fragment = nullptr;
        try
        {
            vertex = CreateRuntimeUiShader(true);
            fragment = CreateRuntimeUiShader(false);

            const SDL_GPUVertexBufferDescription buffer{0, sizeof(RuntimeUiVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
            const std::array attributes{
                SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(RuntimeUiVertex, Position)},
                SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(RuntimeUiVertex, ColorValue)},
            };
            SDL_GPUVertexInputState input{};
            input.vertex_buffer_descriptions = &buffer;
            input.num_vertex_buffers = 1;
            input.vertex_attributes = attributes.data();
            input.num_vertex_attributes = static_cast<std::uint32_t>(attributes.size());

            SDL_GPUColorTargetDescription color{};
            color.format = SDL_GetGPUSwapchainTextureFormat(Device, NativeWindow);
            color.blend_state.enable_blend = true;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                                 SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
            SDL_GPUGraphicsPipelineTargetInfo target{};
            target.color_target_descriptions = &color;
            target.num_color_targets = 1;

            SDL_GPUGraphicsPipelineCreateInfo information{};
            information.vertex_shader = vertex;
            information.fragment_shader = fragment;
            information.vertex_input_state = input;
            information.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            information.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            information.target_info = target;
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
            if (!pipeline)
                throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(runtime UI) failed: " + LastSdlError());
            SDL_ReleaseGPUShader(Device, fragment);
            SDL_ReleaseGPUShader(Device, vertex);
            return pipeline;
        }
        catch (...)
        {
            RethrowIfDeviceLost("runtime UI pipeline creation");
            if (fragment)
                SDL_ReleaseGPUShader(Device, fragment);
            if (vertex)
                SDL_ReleaseGPUShader(Device, vertex);
            throw;
        }
    }

    void RenderSharedState::EnsureFrameUploadContext()
    {
        if (FrameUploadPass)
            return;
        if (FrameUploadCommands)
            throw std::logic_error("The frame upload command buffer is active without a copy pass.");

        FrameUploadCommands = SDL_AcquireGPUCommandBuffer(Device);
        if (!FrameUploadCommands)
            throw std::runtime_error("SDL_AcquireGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
        FrameUploadPass = SDL_BeginGPUCopyPass(FrameUploadCommands);
        if (!FrameUploadPass)
        {
            (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
            FrameUploadCommands = nullptr;
            throw std::runtime_error("SDL_BeginGPUCopyPass(frame uploads) failed: " + LastSdlError());
        }
    }

    SDL_GPUBuffer* RenderSharedState::UploadBuffer(const std::span<const std::byte> bytes,
                                                   const SDL_GPUBufferUsageFlags usage)
    {
        return UploadBuffer(nullptr, bytes, usage);
    }

    SDL_GPUBuffer* RenderSharedState::UploadBuffer(SDL_GPUCommandBuffer* commands,
                                                   const std::span<const std::byte> bytes,
                                                   const SDL_GPUBufferUsageFlags usage)
    {
        if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("GPU buffer payload is empty or exceeds the 32-bit SDL limit.");
        const auto byteSize = static_cast<std::uint32_t>(bytes.size());
        SDL_GPUBufferCreateInfo bufferInformation{};
        bufferInformation.usage = usage;
        bufferInformation.size = byteSize;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(Device, &bufferInformation);
        if (!buffer)
            throw std::runtime_error("SDL_CreateGPUBuffer failed: " + LastSdlError());

        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = byteSize;
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer failed: " + LastSdlError());

            void* mapped = SDL_MapGPUTransferBuffer(Device, transfer, false);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer failed: " + LastSdlError());
            std::memcpy(mapped, bytes.data(), bytes.size());
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            SDL_GPUTransferBufferLocation source{transfer, 0};
            SDL_GPUBufferRegion destination{buffer, 0, byteSize};
            if (commands)
            {
                auto* copy = SDL_BeginGPUCopyPass(commands);
                if (!copy)
                    throw std::runtime_error("SDL_BeginGPUCopyPass(frame-local upload) failed: " + LastSdlError());
                SDL_UploadToGPUBuffer(copy, &source, &destination, false);
                SDL_EndGPUCopyPass(copy);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return buffer;
            }
            if (FrameExecutionActive && !FrameUploadPass)
                EnsureFrameUploadContext();
            if (FrameUploadPass)
            {
                SDL_UploadToGPUBuffer(FrameUploadPass, &source, &destination, false);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return buffer;
            }

            SDL_GPUCommandBuffer* uploadCommands = SDL_AcquireGPUCommandBuffer(Device);
            if (!uploadCommands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(uploadCommands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                throw std::runtime_error("SDL_BeginGPUCopyPass failed: " + LastSdlError());
            }
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(upload) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            transfer = nullptr;
            return buffer;
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU buffer upload");
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            SDL_ReleaseGPUBuffer(Device, buffer);
            throw;
        }
    }

    SDL_GPUBuffer* RenderSharedState::UploadVertexBuffer(const std::span<const RenderVertex> vertices)
    {
        return UploadVertexBuffer(nullptr, vertices);
    }

    SDL_GPUBuffer* RenderSharedState::UploadVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                         const std::span<const RenderVertex> vertices)
    {
        std::vector<GpuRenderVertex> gpuVertices;
        gpuVertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            gpuVertices.push_back({
                {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                {vertex.Color.X, vertex.Color.Y, vertex.Color.Z, 1.0F},
                {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F},
            });
        }
        return UploadBuffer(commands, std::as_bytes(std::span(gpuVertices)), SDL_GPU_BUFFERUSAGE_VERTEX);
    }

    SDL_GPUBuffer* RenderSharedState::UploadMeshVertexBuffer(const std::span<const MeshVertex> vertices)
    {
        return UploadMeshVertexBuffer(nullptr, vertices);
    }

    SDL_GPUBuffer* RenderSharedState::UploadMeshVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                             const std::span<const MeshVertex> vertices)
    {
        std::vector<GpuMeshVertex> gpuVertices;
        gpuVertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            gpuVertices.push_back({
                {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F},
                {vertex.UV0.X, vertex.UV0.Y, 0.0F, 0.0F},
                {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue, vertex.VertexColor.Alpha},
                vertex.Tangent,
                {vertex.UV1.X, vertex.UV1.Y, 0.0F, 0.0F},
            });
        }
        return UploadBuffer(commands, std::as_bytes(std::span(gpuVertices)),
                            SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
    }

    SDL_GPUSampler* RenderSharedState::ResolveSampler(const SamplerDescription& description)
    {
        const auto found = std::ranges::find(SamplerCache, description, &decltype(SamplerCache)::value_type::first);
        if (found != SamplerCache.end())
            return found->second;
        const auto filter = [](const TextureFilter value)
        { return value == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR; };
        const auto address = [](const TextureAddressMode value)
        {
            switch (value)
            {
            case TextureAddressMode::Clamp:
                return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            case TextureAddressMode::Mirror:
                return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
            case TextureAddressMode::Repeat:
            default:
                return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            }
        };
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = filter(description.Minimum);
        sampler.mag_filter = filter(description.Magnification);
        sampler.mipmap_mode = description.Mip == TextureFilter::Nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
                                                                        : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u = address(description.AddressU);
        sampler.address_mode_v = address(description.AddressV);
        sampler.address_mode_w = address(description.AddressW);
        sampler.max_anisotropy = static_cast<float>(description.Anisotropy);
        sampler.max_lod = 32.0F;
        sampler.enable_anisotropy = description.Anisotropy > 1;
        SDL_GPUSampler* created = SDL_CreateGPUSampler(Device, &sampler);
        if (!created)
            throw std::runtime_error("SDL_CreateGPUSampler failed: " + LastSdlError());
        SamplerCache.emplace_back(description, created);
        return created;
    }

    GpuTextureResources RenderSharedState::CreateTextureResources(const Texture2DAsset& asset)
    {
        const auto mips = asset.Mips();
        if (mips.empty() || mips.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Texture GPU upload requires a bounded mip chain.");
        std::size_t totalBytes = 0;
        for (const auto& mip : mips)
        {
            if (mip.Pixels.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                throw std::invalid_argument("Texture GPU upload exceeds SDL's 32-bit transfer limit.");
            totalBytes += mip.Pixels.size();
        }

        GpuTextureResources result;
        result.EstimatedBytes = totalBytes;
        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTextureCreateInfo texture{};
            texture.type = SDL_GPU_TEXTURETYPE_2D;
            texture.format = asset.Settings().ColorSpace == TextureColorSpace::Srgb
                                 ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                                 : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture.width = asset.Width();
            texture.height = asset.Height();
            texture.layer_count_or_depth = 1;
            texture.num_levels = static_cast<std::uint32_t>(mips.size());
            texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
            result.Texture = SDL_CreateGPUTexture(Device, &texture);
            if (!result.Texture)
                throw std::runtime_error("SDL_CreateGPUTexture(asset) failed: " + LastSdlError());

            result.Sampler = ResolveSampler(asset.Settings().Sampler);
            result.HdrEncoded = asset.Settings().HighDynamicRange;
            result.EnvironmentLayout = asset.Settings().EnvironmentLayout;
            result.MipLevels = static_cast<std::uint32_t>(mips.size());
            if (asset.Settings().Semantic == TextureSemantic::Environment)
            {
                const auto irradiance = BakeDiffuseIrradiance(asset);
                for (std::size_t index = 0; index < irradiance.Coefficients.size(); ++index)
                {
                    const auto& coefficient = irradiance.Coefficients[index];
                    result.DiffuseIrradiance[index] = {coefficient.X, coefficient.Y, coefficient.Z, 0.0F};
                }
                result.HasDiffuseIrradiance = true;
            }

            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = static_cast<std::uint32_t>(totalBytes);
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(texture) failed: " + LastSdlError());
            auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(texture) failed: " + LastSdlError());
            std::size_t offset = 0;
            for (const auto& mip : mips)
            {
                std::memcpy(mapped + offset, mip.Pixels.data(), mip.Pixels.size());
                offset += mip.Pixels.size();
            }
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            if (FrameActive)
            {
                EnsureFrameUploadContext();
                offset = 0;
                for (std::size_t index = 0; index < mips.size(); ++index)
                {
                    const auto& mip = mips[index];
                    SDL_GPUTextureTransferInfo source{transfer, static_cast<std::uint32_t>(offset), mip.Width,
                                                      mip.Height};
                    SDL_GPUTextureRegion destination{
                        result.Texture, static_cast<std::uint32_t>(index), 0, 0, 0, 0, mip.Width, mip.Height, 1};
                    SDL_UploadToGPUTexture(FrameUploadPass, &source, &destination, false);
                    offset += mip.Pixels.size();
                }
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return result;
            }

            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(texture) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(texture) failed: " + LastSdlError());
            }
            offset = 0;
            for (std::size_t index = 0; index < mips.size(); ++index)
            {
                const auto& mip = mips[index];
                SDL_GPUTextureTransferInfo source{transfer, static_cast<std::uint32_t>(offset), mip.Width, mip.Height};
                SDL_GPUTextureRegion destination{
                    result.Texture, static_cast<std::uint32_t>(index), 0, 0, 0, 0, mip.Width, mip.Height, 1};
                SDL_UploadToGPUTexture(copy, &source, &destination, false);
                offset += mip.Pixels.size();
            }
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(texture) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU texture upload");
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (result.Texture)
                SDL_ReleaseGPUTexture(Device, result.Texture);
            throw;
        }
    }

    GpuTextureResources RenderSharedState::CreateLightingTextureResources(const LightingTextureArrayAsset& asset)
    {
        const auto& definition = asset.Definition();
        if (definition.Mips.empty() || definition.Mips.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Baked-lighting GPU upload requires a bounded mip chain.");
        std::size_t totalBytes = 0;
        for (const auto& mip : definition.Mips)
        {
            if (mip.Pixels.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                throw std::invalid_argument("Baked-lighting GPU upload exceeds SDL's transfer limit.");
            totalBytes += mip.Pixels.size();
        }

        GpuTextureResources result;
        result.EstimatedBytes = totalBytes;
        SDL_GPUTransferBuffer* transfer = nullptr;
        try
        {
            SDL_GPUTextureCreateInfo texture{};
            texture.type = definition.Target == LightingTextureTarget::CubeArray ? SDL_GPU_TEXTURETYPE_CUBE_ARRAY
                                                                                 : SDL_GPU_TEXTURETYPE_2D_ARRAY;
            texture.format = definition.Encoding == LightingTextureEncoding::Rgba16Float
                                 ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
                                 : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture.width = definition.Mips.front().Width;
            texture.height = definition.Mips.front().Height;
            texture.layer_count_or_depth = definition.Mips.front().Layers;
            texture.num_levels = static_cast<std::uint32_t>(definition.Mips.size());
            texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
            result.Texture = SDL_CreateGPUTexture(Device, &texture);
            if (!result.Texture)
                throw std::runtime_error("SDL_CreateGPUTexture(baked lighting) failed: " + LastSdlError());
            SamplerDescription sampler;
            sampler.AddressU = TextureAddressMode::Clamp;
            sampler.AddressV = TextureAddressMode::Clamp;
            sampler.AddressW = TextureAddressMode::Clamp;
            result.Sampler = ResolveSampler(sampler);
            result.HdrEncoded = definition.Encoding == LightingTextureEncoding::Rgbe8;
            result.MipLevels = static_cast<std::uint32_t>(definition.Mips.size());

            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = static_cast<std::uint32_t>(totalBytes);
            transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(baked lighting) failed: " + LastSdlError());
            auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(baked lighting) failed: " + LastSdlError());
            std::size_t offset = 0;
            for (const auto& mip : definition.Mips)
            {
                std::memcpy(mapped + offset, mip.Pixels.data(), mip.Pixels.size());
                offset += mip.Pixels.size();
            }
            SDL_UnmapGPUTransferBuffer(Device, transfer);

            const auto upload = [&](SDL_GPUCopyPass* copy)
            {
                std::size_t sourceOffset = 0;
                for (std::size_t index = 0; index < definition.Mips.size(); ++index)
                {
                    const auto& mip = definition.Mips[index];
                    const auto layerBytes = mip.Pixels.size() / mip.Layers;
                    for (std::uint32_t layer = 0; layer < mip.Layers; ++layer)
                    {
                        SDL_GPUTextureTransferInfo source{transfer,
                                                          static_cast<std::uint32_t>(sourceOffset + layerBytes * layer),
                                                          mip.Width, mip.Height};
                        SDL_GPUTextureRegion destination{result.Texture,
                                                         static_cast<std::uint32_t>(index),
                                                         layer,
                                                         0,
                                                         0,
                                                         0,
                                                         mip.Width,
                                                         mip.Height,
                                                         1};
                        SDL_UploadToGPUTexture(copy, &source, &destination, false);
                    }
                    sourceOffset += mip.Pixels.size();
                }
            };
            if (FrameActive)
            {
                EnsureFrameUploadContext();
                upload(FrameUploadPass);
                FrameUploadTransfers.push_back(transfer);
                transfer = nullptr;
                return result;
            }
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(baked lighting) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(baked lighting) failed: " + LastSdlError());
            }
            upload(copy);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw std::runtime_error("SDL_SubmitGPUCommandBuffer(baked lighting) failed: " + LastSdlError());
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("baked-lighting texture upload");
            if (transfer)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (result.Texture)
                SDL_ReleaseGPUTexture(Device, result.Texture);
            throw;
        }
    }

    GpuMeshResources RenderSharedState::CreateMeshResources(const MeshAsset& mesh)
    {
        std::vector<RenderVertex> vertices;
        vertices.reserve(mesh.Vertices().size());
        for (const auto& vertex : mesh.Vertices())
        {
            vertices.push_back({vertex.Position,
                                {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                                vertex.Normal});
        }
        GpuMeshResources result;
        result.EstimatedBytes = static_cast<std::uint64_t>(vertices.size()) * sizeof(RenderVertex) +
                                static_cast<std::uint64_t>(mesh.Vertices().size()) * sizeof(MeshVertex) +
                                static_cast<std::uint64_t>(mesh.Indices().size()) * sizeof(std::uint32_t);
        try
        {
            result.Vertices = UploadVertexBuffer(vertices);
            result.AssetVertices = UploadMeshVertexBuffer(mesh.Vertices());
            result.Indices = UploadBuffer(std::as_bytes(mesh.Indices()), SDL_GPU_BUFFERUSAGE_INDEX);
            result.IndexCount = static_cast<std::uint32_t>(mesh.Indices().size());
            result.Submeshes.assign(mesh.Submeshes().begin(), mesh.Submeshes().end());
            result.Lods.assign(mesh.Lods().begin(), mesh.Lods().end());
            result.Bounds = mesh.Bounds();
            const auto boundsEnclose =
                [&](const MeshBounds bounds, const std::uint32_t first, const std::uint32_t count)
            {
                const auto submeshes = std::span(result.Submeshes).subspan(first, count);
                return !submeshes.empty() &&
                       std::ranges::all_of(submeshes, [&](const MeshSubmesh& submesh)
                                           { return GeometryDetail::Encloses(bounds, submesh.Bounds); });
            };
            result.BoundsEncloseSubmeshes =
                boundsEnclose(result.Bounds, 0, static_cast<std::uint32_t>(result.Submeshes.size()));
            result.LodBoundsEncloseSubmeshes.reserve(result.Lods.size());
            for (const auto& lod : result.Lods)
                result.LodBoundsEncloseSubmeshes.push_back(
                    boundsEnclose(lod.Bounds, lod.FirstSubmesh, lod.SubmeshCount));
            result.DefaultMaterials.reserve(mesh.MaterialSlots().size());
            for (const auto& slot : mesh.MaterialSlots())
                result.DefaultMaterials.push_back(slot.DefaultMaterial);
            result.ShapeSamples.reserve(mesh.Indices().size() / 3U);
            double cumulativeArea = 0.0;
            for (std::size_t index = 0; index + 2U < mesh.Indices().size(); index += 3U)
            {
                const auto& a = mesh.Vertices()[mesh.Indices()[index]].Position;
                const auto& b = mesh.Vertices()[mesh.Indices()[index + 1U]].Position;
                const auto& c = mesh.Vertices()[mesh.Indices()[index + 2U]].Position;
                const auto edge0 = Vector3{b.X - a.X, b.Y - a.Y, b.Z - a.Z};
                const auto edge1 = Vector3{c.X - a.X, c.Y - a.Y, c.Z - a.Z};
                const auto cross = Vector3{edge0.Y * edge1.Z - edge0.Z * edge1.Y, edge0.Z * edge1.X - edge0.X * edge1.Z,
                                           edge0.X * edge1.Y - edge0.Y * edge1.X};
                const auto area =
                    0.5 * std::sqrt(static_cast<double>(cross.X) * cross.X + static_cast<double>(cross.Y) * cross.Y +
                                    static_cast<double>(cross.Z) * cross.Z);
                if (!std::isfinite(area) || area <= 0.0)
                    continue;
                cumulativeArea += area;
                if (!std::isfinite(cumulativeArea) || cumulativeArea > std::numeric_limits<float>::max())
                    throw std::runtime_error("Mesh surface area exceeds the VFX sampling range.");
                result.ShapeSamples.push_back({{a.X, a.Y, a.Z, 0.0F},
                                               {b.X, b.Y, b.Z, 0.0F},
                                               {c.X, c.Y, c.Z, static_cast<float>(cumulativeArea)}});
            }
            result.ShapeSampleWeight = static_cast<float>(cumulativeArea);
            result.Revision = 1;
            return result;
        }
        catch (...)
        {
            RethrowIfDeviceLost("GPU VFX volume resource creation");
            if (result.Indices)
                SDL_ReleaseGPUBuffer(Device, result.Indices);
            if (result.Vertices)
                SDL_ReleaseGPUBuffer(Device, result.Vertices);
            if (result.AssetVertices)
                SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
            throw;
        }
    }
} // namespace Keire::RenderBackend
