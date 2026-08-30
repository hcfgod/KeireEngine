#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/UiContextAccessInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] bool RuntimeUiCommandWithin(const RuntimeUiTree& tree, const RuntimeUiElementId root,
                                                  RuntimeUiElementId element) noexcept
        {
            if (!root)
                return true;
            try
            {
                while (element)
                {
                    if (element == root)
                        return true;
                    const auto state = tree.State(element);
                    element = state ? state->Parent : RuntimeUiElementId{};
                }
            }
            catch (...)
            {
            }
            return false;
        }

        [[nodiscard]] std::vector<RuntimeUiDrawCommand>
        CopyRuntimeUiCommands(const RuntimeUiRenderSubmission& submission)
        {
            std::vector<RuntimeUiDrawCommand> result;
            if (!submission.Tree)
                return result;
            const auto commands = submission.Tree->DrawCommands();
            result.reserve(commands.size());
            for (const auto& command : commands)
            {
                if (RuntimeUiCommandWithin(*submission.Tree, submission.Root, command.Element))
                    result.push_back(command);
            }
            return result;
        }

        void PublishIdleGpuOcclusionSurface(RenderSurfaceState& surface) noexcept
        {
            auto& diagnostics = surface.GpuOcclusionDiagnostics;
            if (diagnostics.State == GpuOcclusionSurfaceState::Idle)
                return;

            surface.GpuOcclusionSubmissionEpoch =
                surface.GpuOcclusionSubmissionEpoch == std::numeric_limits<std::uint64_t>::max()
                    ? 1U
                    : surface.GpuOcclusionSubmissionEpoch + 1U;
            diagnostics.RequestedMode = surface.GpuOcclusionSubmittedMode;
            diagnostics.EffectiveMode = GpuOcclusionMode::Disabled;
            diagnostics.State = GpuOcclusionSurfaceState::Idle;
            diagnostics.FallbackReason = GpuOcclusionFallbackReason::None;
            ClearGpuOcclusionFrameEvidence(diagnostics);
            surface.GpuOcclusionLatestCandidateTriangles = 0;
            surface.GpuOcclusionLatestVisibleTriangles = 0;
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0;
            surface.GpuOcclusionAutomaticMinimumFrames = 0;
            surface.GpuOcclusionAutomaticCooldownFrames = 0;
            surface.GpuOcclusionValidationCooldown = false;
            surface.GpuOcclusionValidationFallbackEventPending = false;
            surface.GpuOcclusionDebugMipLevel = 0;
        }
    } // namespace

    void RenderSharedState::EndFrame(ImDrawData* drawData)
    {
        RequireOwner("EndFrame");
        if (!FrameActive)
            throw std::logic_error("No render frame is active.");
        FrameActive = false;
        auto frame = std::make_shared<RenderFramePacket>();
        frame->Id = CaptureFrameId;
        frame->Timeline.OwnerUpdateMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CaptureFrameStartedAt).count();
        try
        {
            EnqueueFrame(
                frame,
                [this, frame, drawData]
                {
                    constexpr std::size_t maximumRuntimeUiDrawCommands = 131'072U;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    if (InjectCaptureFailureAtNextFrame.exchange(false, std::memory_order_acq_rel))
                        throw std::runtime_error("Injected immutable frame capture failure.");
#endif
                    frame->CaptureStarted = std::chrono::steady_clock::now();
                    CpuPreparation.BeginFrame();
                    for (auto& pending : PendingSceneRequests)
                        CapturePendingSceneRequest(std::move(pending), frame->Id);
                    frame->Requests = std::move(CaptureRequests);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    RecordVfxSnapshotSignatureForTest(frame, false);
#endif

                    std::size_t runtimeUiDrawCommandCount = 0;
                    for (const auto& pending : PendingRuntimeUiSubmissions)
                    {
                        const auto& submission = pending.Submission;
                        if (!submission.Tree)
                            continue;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                        RuntimeUiCaptureEnumerationCount.fetch_add(1U, std::memory_order_relaxed);
#endif
                        auto commands = CopyRuntimeUiCommands(submission);
                        if (commands.size() > maximumRuntimeUiDrawCommands - runtimeUiDrawCommandCount)
                            throw std::length_error("Runtime UI submissions exceed the per-frame draw-command bound.");
                        runtimeUiDrawCommandCount += commands.size();
                        if (submission.Target == RuntimeUiRenderTarget::RenderTexture)
                        {
                            const auto existing =
                                std::ranges::find(frame->RuntimeUiRenderTextures, submission.RenderTexture,
                                                  &CapturedRuntimeUiRenderTexture::Target);
                            if (existing != frame->RuntimeUiRenderTextures.end() &&
                                existing->ReferenceResolution != submission.ReferenceResolution)
                            {
                                throw std::invalid_argument(
                                    "Same-frame runtime UI RenderTexture submissions must use one reference "
                                    "resolution per logical target ID.");
                            }
                            frame->RuntimeUiRenderTextures.push_back(
                                {.Commands = std::move(commands),
                                 .Target = submission.RenderTexture,
                                 .ReferenceResolution = submission.ReferenceResolution,
                                 .SortingOrder = submission.SortingOrder,
                                 .Sequence = pending.Sequence});
                            continue;
                        }
                        if (submission.Target == RuntimeUiRenderTarget::WorldSurface)
                        {
                            const auto surface = ResolveSurface(pending.Surface);
                            if (!surface)
                            {
                                throw std::logic_error(
                                    "A world-surface runtime UI surface epoch expired during frame capture.");
                            }
                            const auto camera = submission.View->Camera();
                            if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection))
                                throw std::invalid_argument("World-surface runtime UI camera is invalid.");
                            frame->RuntimeUiWorldPanels.push_back(
                                {.Commands = std::move(commands),
                                 .Surface = pending.Surface,
                                 .World = submission.World,
                                 .ViewProjection = Math::Multiply(camera.Projection, camera.View),
                                 .Viewport = submission.Viewport,
                                 .ReferenceResolution = submission.ReferenceResolution,
                                 .Pivot = submission.Pivot,
                                 .WorldUnitsPerPixel = submission.WorldUnitsPerPixel,
                                 .SortingOrder = submission.SortingOrder,
                                 .Sequence = pending.Sequence,
                                 .DepthTest = submission.DepthTest});
                            continue;
                        }
                        if (submission.Target == RuntimeUiRenderTarget::CameraOverlay)
                        {
                            if (!ResolveSurface(pending.Surface))
                            {
                                throw std::logic_error(
                                    "A camera-overlay runtime UI surface epoch expired during frame capture.");
                            }
                            frame->RuntimeUiCameraPanels.push_back({.Commands = std::move(commands),
                                                                    .Surface = pending.Surface,
                                                                    .Viewport = submission.Viewport,
                                                                    .SortingOrder = submission.SortingOrder,
                                                                    .Sequence = pending.Sequence});
                            continue;
                        }
                        CaptureRuntimeUiCommands.insert(CaptureRuntimeUiCommands.end(),
                                                        std::make_move_iterator(commands.begin()),
                                                        std::make_move_iterator(commands.end()));
                    }
                    frame->RuntimeUiCommands = std::move(CaptureRuntimeUiCommands);
                    CaptureRuntimeUiImageLeases(*frame);
                    frame->Surfaces = CaptureLiveSurfaceTokens();
                    if (PresentationSurfaceId)
                    {
                        if (const auto presentation =
                                std::ranges::find(frame->Surfaces, *PresentationSurfaceId, &RenderSurfaceToken::Id);
                            presentation != frame->Surfaces.end())
                        {
                            frame->PresentationSurface = *presentation;
                        }
                    }
                    std::vector<CapturedSurfaceTextureBinding> surfaceTextureBindings;
                    surfaceTextureBindings = std::move(PendingUiSurfaceTextureBindings);
                    surfaceTextureBindings.reserve(surfaceTextureBindings.size() + frame->Surfaces.size());
                    for (const auto& token : frame->Surfaces)
                    {
                        const auto surface = ResolveSurface(token);
                        if (!surface)
                            throw std::logic_error("A render-surface epoch expired during frame capture.");
                        const auto textureIdentity =
                            reinterpret_cast<std::uintptr_t>(surface->PublishedTexture.load(std::memory_order_acquire));
                        if (textureIdentity != 0U &&
                            std::ranges::none_of(surfaceTextureBindings,
                                                 [&token, textureIdentity](const auto& binding)
                                                 {
                                                     return binding.Surface.Id == token.Id &&
                                                            binding.Surface.Epoch == token.Epoch &&
                                                            binding.TextureIdentity == textureIdentity;
                                                 }))
                        {
                            surfaceTextureBindings.push_back({token, textureIdentity});
                        }
                    }
                    if (!drawData && PendingUiTextureRetirements.empty())
                    {
                        frame->EditorUi.reset();
                    }
                    else
                    {
                        const auto contextAccess = EditorUiContextAccess.load(std::memory_order_acquire);
                        const auto contextLock = Keire::Detail::AcquireRequiredUiContext(
                            contextAccess,
                            "Dear ImGui packet capture requires the renderer's live UI context binding.");
                        frame->EditorUi =
                            OwnedImGuiDrawData::Capture(drawData, surfaceTextureBindings, PendingUiTextureRetirements);
                    }
                    CpuPreparation.EndFrame();
                    frame->CpuPreparationMilliseconds = CpuPreparation.CompletedMilliseconds();
                    frame->CpuPreparationP95Milliseconds = CpuPreparation.P95Milliseconds();
                    frame->CapturedAt = std::chrono::steady_clock::now();
                    frame->Timeline.Frame = frame->Id;
                    frame->Timeline.CaptureMilliseconds =
                        std::chrono::duration<float, std::milli>(frame->CapturedAt - frame->CaptureStarted).count();
                    frame->CapturedStatistics = CaptureStatistics;
                    frame->CapturedStatistics.CpuPreparationMilliseconds = frame->CpuPreparationMilliseconds;
                    frame->CapturedStatistics.CpuPreparationP95Milliseconds = frame->CpuPreparationP95Milliseconds;
                    frame->CapturedStatistics.OwnerUpdateMilliseconds = frame->Timeline.OwnerUpdateMilliseconds;
                    frame->CapturedStatistics.FrameCaptureMilliseconds = frame->Timeline.CaptureMilliseconds;
                    frame->DeviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
                    QualifyRuntimeUiCameraPanels(*frame);
                    QualifyRuntimeUiWorldPanels(*frame);
                    QualifyRuntimeUiRenderTextures(*frame);
                    QualifyRuntimeUiImageLeases(*frame);
                    QualifyRuntimeUiFontLeases(*frame);
                });
        }
        catch (...)
        {
            CpuPreparation.CancelFrame();
            PendingSceneRequests.clear();
            PendingRuntimeUiSubmissions.clear();
            PendingUiSurfaceTextureBindings.clear();
            CaptureRequests.clear();
            CaptureRuntimeUiCommands.clear();
            throw;
        }
        PendingSceneRequests.clear();
        PendingRuntimeUiSubmissions.clear();
        PendingUiSurfaceTextureBindings.clear();
        PendingUiTextureRetirements.clear();
    }

    void RenderSharedState::ExecuteAcceptedFrame(const std::shared_ptr<RenderFramePacket>& frame) noexcept
    {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (const auto delay = DelayNextAcceptedFrameMilliseconds.exchange(0U, std::memory_order_acq_rel); delay != 0U)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        {
            std::unique_lock lock(RenderQueueMutex);
            if (BlockNextAcceptedFrame)
            {
                BlockNextAcceptedFrame = false;
                AcceptedFrameBlocked = true;
                FramesRetired.notify_all();
                (void)FramesRetired.wait_for(lock, std::chrono::seconds(2),
                                             [this]
                                             {
                                                 return ReleaseAcceptedFrame ||
                                                        DeviceLifecycle.load(std::memory_order_acquire) ==
                                                            RenderDeviceState::Closing;
                                             });
                AcceptedFrameBlocked = false;
                ReleaseAcceptedFrame = false;
                FramesRetired.notify_all();
            }
        }
#endif
        for (;;)
        {
            try
            {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (InjectTerminalFailureAtNextAcceptedFrame.exchange(false, std::memory_order_acq_rel))
                    throw std::runtime_error("Injected accepted-frame terminal failure.");
#endif
                if (Specification.Mode != RenderMode::Rendered)
                {
                    Statistics = frame->CapturedStatistics;
                    Statistics.Surfaces = static_cast<std::uint32_t>(frame->Surfaces.size());
                    Statistics.Passes = Statistics.Surfaces;
                    CompleteFrame(frame, false);
                    return;
                }
                ExecuteFrame(frame);
                if (frame->RetriedAfterDeviceLoss &&
                    DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Recovering &&
                    !CompleteDeviceRecoveryAfterRetry())
                {
                    const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
                    if (lifecycle != RenderDeviceState::Closing && lifecycle != RenderDeviceState::Closed)
                    {
                        RecordTerminalFailure(std::make_exception_ptr(std::runtime_error(
                                                  "GPU recovery could not publish the successfully retried frame.")),
                                              frame);
                    }
                }
                return;
            }
            catch (const GpuDeviceLostError& error)
            {
                const auto failure = std::current_exception();
                auto recovery = DeviceRecoveryResult::Failed;
                try
                {
                    if (!frame->RetriedAfterDeviceLoss)
                        recovery = RecoverDevice(frame, error.Diagnostic());
                }
                catch (...)
                {
                    RecordTerminalFailure(std::current_exception(), frame);
                    return;
                }
                if (recovery == DeviceRecoveryResult::Recovered)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    RecordVfxSnapshotSignatureForTest(frame, true);
#endif
                    frame->RetriedAfterDeviceLoss = true;
                    continue;
                }
                if (recovery == DeviceRecoveryResult::CancelledByShutdown)
                {
                    CompleteFrame(frame, true);
                    return;
                }
                RecordTerminalFailure(failure, frame);
                return;
            }
            catch (const std::exception& error)
            {
                const auto failure = std::current_exception();
                const auto diagnostic = ClassifyDeviceFailure("SDL GPU frame execution", error.what());
                auto recovery = DeviceRecoveryResult::Failed;
                try
                {
                    if (diagnostic && !frame->RetriedAfterDeviceLoss)
                        recovery = RecoverDevice(frame, *diagnostic);
                }
                catch (...)
                {
                    RecordTerminalFailure(std::current_exception(), frame);
                    return;
                }
                if (recovery == DeviceRecoveryResult::Recovered)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    RecordVfxSnapshotSignatureForTest(frame, true);
#endif
                    frame->RetriedAfterDeviceLoss = true;
                    continue;
                }
                if (recovery == DeviceRecoveryResult::CancelledByShutdown)
                {
                    CompleteFrame(frame, true);
                    return;
                }
                RecordTerminalFailure(failure, frame);
                return;
            }
            catch (...)
            {
                RecordTerminalFailure(std::current_exception(), frame);
                return;
            }
        }
    }

    void RenderSharedState::ExecuteFrame(const std::shared_ptr<RenderFramePacket>& frame)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("Execute render frame");
        thread_local bool telemetryThreadNamed = false;
        if (!telemetryThreadNamed)
        {
            Keire::Internal::TelemetrySetThreadName("Render owner");
            telemetryThreadNamed = true;
        }
        ActiveFrame = frame;
        frame->RenderStartedAt = std::chrono::steady_clock::now();
        frame->Timeline.QueueDelayMilliseconds =
            std::chrono::duration<float, std::milli>(frame->RenderStartedAt - frame->AcceptedAt).count();
        PrepareFrameForExecution(frame);
        Statistics.CommandRecordingMilliseconds = 0.0F;
        Statistics.SkinningPreparationMilliseconds = 0.0F;
        Statistics.VfxPreparationMilliseconds = 0.0F;
        Statistics.DrawPreparationMilliseconds = 0.0F;
        Statistics.ShadowRecordingMilliseconds = 0.0F;
        Statistics.ForwardPlusCullingMilliseconds = 0.0F;
        Statistics.ScenePassMilliseconds = 0.0F;
        Statistics.DepthPassMilliseconds = 0.0F;
        Statistics.ToneMapMilliseconds = 0.0F;
        Statistics.CommandRecordingUnattributedMilliseconds = 0.0F;
        Statistics.FrameUploadMilliseconds = 0.0F;
        Statistics.SwapchainWaitMilliseconds = 0.0F;
        Statistics.UiRecordingMilliseconds = 0.0F;
        Statistics.GpuSubmissionMilliseconds = 0.0F;
        Statistics.GpuFrameMilliseconds = 0.0F;
        Statistics.GpuOcclusionDepthPassMilliseconds = 0.0F;
        Statistics.GpuOcclusionPyramidRecordingMilliseconds = 0.0F;
        Statistics.GpuOcclusionCullingRecordingMilliseconds = 0.0F;
        Statistics.RuntimeUiRenderer = {};
        // Editor UI is built after BeginFrame but before execution. Retain the finalized previous-frame workload until
        // this point so diagnostics never mistake a reset aggregate for the frame that actually reached the GPU.
        Statistics.GpuOcclusionStaticMeshCandidates = 0;
        Statistics.GpuOcclusionSkinnedMeshCandidates = 0;
        Statistics.GpuOcclusionMeshVfxCandidates = 0;
        Statistics.GpuOcclusionLocalLightCandidates = 0;
        Statistics.GpuOcclusionSpatialVolumeCandidates = 0;
        Statistics.GpuOcclusionForcedVisibleCandidates = 0;
        Statistics.GpuOcclusionSafeOccluders = 0;
        Statistics.GpuOcclusionIndirectDraws = 0;
        Statistics.GpuOcclusionPyramidMipCount = 0;
        Statistics.GpuOcclusionDispatches = 0;
        Statistics.GpuOcclusionFallbacks = 0;
        Statistics.GpuOcclusionActiveSurfaces = 0;
        Statistics.GpuOcclusionFallbackSurfaces = 0;
        Statistics.GpuOcclusionPartialFallbackSurfaces = 0;
        Statistics.GpuOcclusionDepthTriangles = 0;
        Statistics.GpuOcclusionEnabled = false;
        Statistics.GpuOcclusionFallbackActive = false;
        Statistics.ForwardPlusCacheHits = 0;
        Statistics.FrameUploadSubmissions = 0;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (InjectDeviceLossAtNextFrame.exchange(false, std::memory_order_acq_rel))
        {
            MarkInjectedDeviceLossForTest();
            throw GpuDeviceLostError(DeviceLossDiagnostic("test frame injection", "Injected GPU device loss."));
        }
#endif
        if (FrameUploadCommands || FrameUploadPass || !FrameUploadTransfers.empty())
            throw std::logic_error("A previous frame left the GPU upload context active.");

        if (GpuSubmissionSerial == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("GPU submission serial exhausted.");
        SDL_GPUCommandBuffer* commands = nullptr;
        SDL_GPUFence* submittedFence = nullptr;
        std::vector<SDL_GPUCommandBuffer*> surfaceCommands;
        surfaceCommands.reserve(frame->Requests.size());
        std::vector<std::shared_ptr<RenderSurfaceState>> publicationSurfaces;
        publicationSurfaces.reserve(frame->Requests.size());
        std::shared_ptr<ResolvedImGuiDrawData> resolvedEditorUi;
        ActiveGpuSubmissionSerial = GpuSubmissionSerial + 1U;
        FrameExecutionActive = true;
        bool gpuWorkSubmitted = false;

        try
        {
            PrepareRuntimeUiRenderTextures(*frame);
            PrepareRuntimeUiTextureBindings(*frame);
            const auto publicationWriterIndex = static_cast<std::size_t>(frame->FrameSlot) + 1U;
            for (const auto& request : frame->Requests)
            {
                auto surface = ResolveSurface(request.Surface);
                if (!surface)
                    throw std::logic_error("A requested render-surface epoch expired before recording.");
                if (publicationWriterIndex >= surface->Resources.FinalOutputs.size())
                    throw std::logic_error(
                        "A captured render-surface epoch has no output for the accepted frame slot.");
                publicationSurfaces.push_back(std::move(surface));
            }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
            if (InjectDeviceLossWithActiveResourcesAtNextFrame.exchange(false, std::memory_order_acq_rel))
            {
                const std::array<std::byte, 16> payload{};
                FrameTransientBuffers.push_back(
                    UploadBuffer(std::span<const std::byte>(payload), SDL_GPU_BUFFERUSAGE_VERTEX));
                MarkInjectedDeviceLossForTest();
                throw GpuDeviceLostError(
                    DeviceLossDiagnostic("test active-resource frame injection", "Injected GPU device loss."));
            }
#endif

            const auto recordingStarted = std::chrono::steady_clock::now();
            RecordRuntimeUiRenderTextures(surfaceCommands);
            for (const auto& request : frame->Requests)
            {
                const auto surface = ResolveSurface(request.Surface);
                if (!surface)
                    throw std::logic_error("A requested render-surface epoch expired before recording.");
                auto* surfaceCommandBuffer = SDL_AcquireGPUCommandBuffer(Device);
                if (!surfaceCommandBuffer)
                    throw std::runtime_error("SDL_AcquireGPUCommandBuffer(surface) failed: " + LastSdlError());
                surfaceCommands.push_back(surfaceCommandBuffer);
                RecordSurface(surfaceCommandBuffer, *surface, surfaceCommands);
                const auto& diagnostics = surface->GpuOcclusionDiagnostics;
                if (diagnostics.State == GpuOcclusionSurfaceState::Active)
                {
                    ++Statistics.GpuOcclusionActiveSurfaces;
                    if (diagnostics.FallbackReason != GpuOcclusionFallbackReason::None)
                        ++Statistics.GpuOcclusionPartialFallbackSurfaces;
                }
                else if (diagnostics.State == GpuOcclusionSurfaceState::Fallback ||
                         diagnostics.State == GpuOcclusionSurfaceState::Unsupported)
                {
                    ++Statistics.GpuOcclusionFallbackSurfaces;
                }
            }
            // Surface diagnostics describe the most recently completed surface work. Invalidate a live surface after
            // its first completed frame without a scene request so editor overlays cannot report stale active work.
            for (const auto& token : frame->Surfaces)
            {
                const auto surface = ResolveSurface(token);
                if (!surface)
                    throw std::logic_error("A captured render-surface epoch expired before idle publication.");
                if (!surface->Submitted)
                    PublishIdleGpuOcclusionSurface(*surface);
            }
            // RecordSurface and the idle transition above can invalidate readbacks that were still valid at
            // BeginFrame. Publish the completed execution state as one coherent aggregate for profiling and tools.
            PublishGpuOcclusionReadbackStatistics();
            Statistics.CommandRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordingStarted).count();
            const auto attributedRecordingMilliseconds =
                Statistics.SkinningPreparationMilliseconds + Statistics.VfxPreparationMilliseconds +
                Statistics.DrawPreparationMilliseconds + Statistics.ShadowRecordingMilliseconds +
                Statistics.ForwardPlusCullingMilliseconds + Statistics.ScenePassMilliseconds +
                Statistics.DepthPassMilliseconds + Statistics.ToneMapMilliseconds +
                Statistics.GpuOcclusionDepthPassMilliseconds + Statistics.GpuOcclusionPyramidRecordingMilliseconds +
                Statistics.GpuOcclusionCullingRecordingMilliseconds;
            Statistics.CommandRecordingUnattributedMilliseconds =
                std::max(0.0F, Statistics.CommandRecordingMilliseconds - attributedRecordingMilliseconds);

            const auto uploadStarted = std::chrono::steady_clock::now();
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                auto* uploadCommands = std::exchange(FrameUploadCommands, nullptr);
                if (FrameUploadTransfers.empty())
                {
                    (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                }
                else if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                {
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
                }
                else
                {
                    gpuWorkSubmitted = true;
                    ++Statistics.FrameUploadSubmissions;
                }
            }
            Statistics.FrameUploadMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStarted).count();

            const auto submissionStarted = std::chrono::steady_clock::now();
            // SDL's D3D12 backend owns bounded descriptor heaps per command buffer. Keeping independent editor
            // surfaces in one command buffer allows the Scene, Game, and preview views to exhaust a shared heap.
            // Submitting each offscreen surface separately preserves queue order while resetting that backend-local
            // allocation. The final swapchain fence still retires every earlier submission on the same GPU queue.
            for (auto& surfaceCommandBuffer : surfaceCommands)
            {
                auto* submitted = std::exchange(surfaceCommandBuffer, nullptr);
                if (!SDL_SubmitGPUCommandBuffer(submitted))
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(surface) failed: " + LastSdlError());
                gpuWorkSubmitted = true;
            }

            commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(swapchain) failed: " + LastSdlError());
            if (frame->EditorUi)
            {
                resolvedEditorUi =
                    frame->EditorUi->ResolveForRender(EditorUiTextures, frame->DeviceGeneration,
                                                      [this](const RenderSurfaceToken& token)
                                                      {
                                                          const auto surface = ResolveSurface(token);
                                                          return reinterpret_cast<std::uintptr_t>(
                                                              surface ? surface->Resources.PublishedColor() : nullptr);
                                                      });
            }
            RecordSwapchain(commands, resolvedEditorUi ? resolvedEditorUi->Data() : nullptr);

            submittedFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            commands = nullptr;
            if (!submittedFence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
            gpuWorkSubmitted = true;
            PublishRuntimeUiRenderTextures(*frame);
            if (resolvedEditorUi)
                resolvedEditorUi->CommitGpuTextures(EditorUiTextures, frame->DeviceGeneration);
            Statistics.GpuSubmissionMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - submissionStarted).count();
            frame->SubmittedAt = submissionStarted;
            frame->Timeline.RenderCpuMilliseconds =
                std::chrono::duration<float, std::milli>(frame->SubmittedAt - frame->RenderStartedAt).count();
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            if (InjectPostSubmitFailureAtNextFrame.exchange(false, std::memory_order_acq_rel))
                throw std::runtime_error("Injected post-submit publication failure.");
#endif
            // InFlight is reserved to the admission bound before device creation. After a successful GPU submit this
            // move therefore cannot allocate, and every submitted fence acquires exactly one retirement owner.
            InFlight.push_back({submittedFence, frame, std::move(resolvedEditorUi), std::move(PendingRetired),
                                std::move(PendingRetiredMeshes), std::move(PendingRetiredSkins),
                                std::move(PendingRetiredTextures), std::move(PendingRetiredPipelines),
                                std::move(PendingRetiredForwardPlus), std::move(FrameTransientBuffers),
                                std::move(FrameUploadTransfers), std::move(FrameGpuOcclusionReadbacks),
                                submissionStarted, Statistics.VfxGpuWorlds != 0, PendingRetiredBytes});
            submittedFence = nullptr;
            PendingRetiredBytes = 0;
            PendingRetired.clear();
            PendingRetiredMeshes.clear();
            PendingRetiredSkins.clear();
            PendingRetiredTextures.clear();
            PendingRetiredPipelines.clear();
            PendingRetiredForwardPlus.clear();
            FrameTransientBuffers.clear();
            FrameUploadTransfers.clear();
            FrameGpuOcclusionReadbacks.clear();
            PreparedRuntimeUiTextures.clear();
            FrameRuntimeUiRenderTextureTargets.clear();
            GpuSubmissionSerial = ActiveGpuSubmissionSerial;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            for (const auto& surface : publicationSurfaces)
            {
                std::swap(surface->Resources.FinalOutputs.front(),
                          surface->Resources.FinalOutputs[publicationWriterIndex]);
                surface->HasOutput = true;
                surface->PublishedWorksetSlot.store(frame->FrameSlot, std::memory_order_release);
                surface->PublishedDepthAvailable.store(surface->SampledDepthValid, std::memory_order_release);
                surface->PublishedTexture.store(surface->Resources.PublishedColor(), std::memory_order_release);
            }
            const auto firstPresentation = frame->PresentedAt == std::chrono::steady_clock::time_point{};
            frame->PresentedAt = std::chrono::steady_clock::now();
            if (firstPresentation)
                PresentedFrameCount.fetch_add(1U, std::memory_order_relaxed);
            auto lastPresentedFrame = LastPresentedFrameId.load(std::memory_order_relaxed);
            while (lastPresentedFrame < frame->Id &&
                   !LastPresentedFrameId.compare_exchange_weak(lastPresentedFrame, frame->Id, std::memory_order_release,
                                                               std::memory_order_relaxed))
            {
            }
            frame->Timeline.RenderCpuMilliseconds =
                std::chrono::duration<float, std::milli>(frame->PresentedAt - frame->RenderStartedAt).count();
            frame->Timeline.SubmitToPresentMilliseconds =
                std::chrono::duration<float, std::milli>(frame->PresentedAt - frame->SubmittedAt).count();
            Statistics.SubmitToPresentMilliseconds = frame->Timeline.SubmitToPresentMilliseconds;
            PublishStatistics();
            ActiveFrame.reset();
        }
        catch (...)
        {
            try
            {
                throw;
            }
            catch (const GpuDeviceLostError&)
            {
                DeviceLost = true;
            }
            catch (const std::exception& error)
            {
                DeviceLost = ClassifyDeviceFailure("SDL GPU frame execution", error.what()).has_value();
            }
            catch (...)
            {
            }
            // Once any command buffer has been submitted, failure cleanup cannot prove that the generation will
            // become idle. Waiting for idle here would prevent the accepted frame from publishing its terminal
            // completion and can block the owner in Flush forever. Treat that generation as abandoned even when the
            // original exception is not itself classified as device loss; ExecuteAcceptedFrame still records and
            // rethrows the original terminal failure.
            const bool abandonGeneration = DeviceLost || gpuWorkSubmitted;
            if (abandonGeneration)
            {
                DeviceLost = true;
                // A lost or indeterminate submitted generation is opaque and unusable. Sever every CPU reference
                // without calling SDL on copy passes, command buffers, transfers, transient buffers, textures,
                // fences, or the device itself.
                const auto abandonedHandles = static_cast<std::uint64_t>(FrameUploadPass != nullptr) +
                                              static_cast<std::uint64_t>(FrameUploadCommands != nullptr) +
                                              static_cast<std::uint64_t>(commands != nullptr) +
                                              static_cast<std::uint64_t>(submittedFence != nullptr) +
                                              static_cast<std::uint64_t>(FrameUploadTransfers.size()) +
                                              static_cast<std::uint64_t>(FrameTransientBuffers.size()) +
                                              static_cast<std::uint64_t>(std::ranges::count_if(
                                                  surfaceCommands, [](const auto* value) { return value != nullptr; }));
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                LostGenerationAbandonedHandleCount.fetch_add(abandonedHandles, std::memory_order_relaxed);
#else
                (void)abandonedHandles;
#endif
                FrameUploadPass = nullptr;
                FrameUploadCommands = nullptr;
                std::ranges::fill(surfaceCommands, nullptr);
                commands = nullptr;
                submittedFence = nullptr;
                FrameUploadTransfers.clear();
                FrameTransientBuffers.clear();
                FrameGpuOcclusionReadbacks.clear();
                if (resolvedEditorUi)
                    resolvedEditorUi->ReleaseGpuTextures(nullptr, true);
                EditorUiTextures.ReleaseGpuTextures(nullptr, true);
            }
            else
            {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                const auto recordGpuCleanupCall = [this]
                {
                    if (DeviceLost)
                        LostGenerationGpuCleanupCallCount.fetch_add(1U, std::memory_order_relaxed);
                };
#endif
                if (FrameUploadPass)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    SDL_EndGPUCopyPass(FrameUploadPass);
                    FrameUploadPass = nullptr;
                }
                if (FrameUploadCommands)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
                    FrameUploadCommands = nullptr;
                }
                for (auto*& surfaceCommandBuffer : surfaceCommands)
                    if (surfaceCommandBuffer)
                    {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                        recordGpuCleanupCall();
#endif
                        (void)SDL_CancelGPUCommandBuffer(std::exchange(surfaceCommandBuffer, nullptr));
                    }
                if (gpuWorkSubmitted && Device)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    (void)SDL_WaitForGPUIdle(Device);
                }
                if (submittedFence)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    SDL_ReleaseGPUFence(Device, submittedFence);
                    submittedFence = nullptr;
                }
                for (auto* transfer : FrameUploadTransfers)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    SDL_ReleaseGPUTransferBuffer(Device, transfer);
                }
                FrameUploadTransfers.clear();
                if (commands)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    (void)SDL_CancelGPUCommandBuffer(commands);
                }
                for (auto* buffer : FrameTransientBuffers)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    recordGpuCleanupCall();
#endif
                    SDL_ReleaseGPUBuffer(Device, buffer);
                }
                FrameTransientBuffers.clear();
                FrameGpuOcclusionReadbacks.clear();
                if (resolvedEditorUi)
                    resolvedEditorUi->ReleaseGpuTextures(Device, false);
            }
            // A canceled command buffer invalidates the emitter sequencing recorded for every world it touched.
            for (auto& [worldId, resources] : GpuVfxWorlds)
            {
                (void)worldId;
                if (resources.LastPreparedFrame != Statistics.Frame)
                    continue;
                resources.InvalidateSequencing();
            }
            for (const auto& request : frame->Requests)
            {
                if (const auto surface = ResolveSurface(request.Surface))
                    surface->SampledDepthValid = false;
            }
            FrameActive = false;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            PreparedRuntimeUiTextures.clear();
            FrameRuntimeUiRenderTextureTargets.clear();
            ActiveFrame.reset();
            throw;
        }
    }

    void RenderSharedState::Close() noexcept
    {
        if (!Open)
            return;
        {
            std::scoped_lock lock(RenderQueueMutex);
            DeviceLifecycle.store(RenderDeviceState::Closing, std::memory_order_release);
            RecoveryOwnerBoundary = true;
        }
        FramesRetired.notify_all();
        Open = false;
        FrameActive = false;
        if (VfxPipelineWarmupJob)
        {
            VfxPipelineWarmupJob.Cancel();
            (void)VfxPipelineWarmupJob.Wait();
            VfxPipelineWarmupJob = {};
        }
        if (RenderJobs)
        {
            RenderJobs->Cancel();
            RenderJobs->Wait();
        }
        bool cleanupCompleted = false;
        try
        {
            DispatchRender(
                [this]
                {
                    bool abandon = DeviceLost;
                    {
                        std::scoped_lock lock(FailureMutex);
                        abandon = abandon || static_cast<bool>(TerminalFailure);
                    }
                    DestroyDeviceAndResources(abandon);
                });
            cleanupCompleted = true;
        }
        catch (...)
        {
            DeviceLost = true;
            RecordTerminalFailure(std::current_exception());
        }
        StopRenderThread();
        FrameExecutionActive = false;
        ActiveGpuSubmissionSerial = 0;
        if (!cleanupCompleted)
        {
            // Never call SDL GPU APIs from the owner thread. A failed render-thread cleanup intentionally abandons
            // the lost or unreachable native handles and only severs CPU-side ownership.
            AbandonLostDeviceResources();
            for (const auto& surface : AllSurfaceEpochs())
            {
                surface->ResourcesAvailable.store(false, std::memory_order_release);
                surface->PublishedTexture.store(nullptr, std::memory_order_release);
                surface->Owner.reset();
                surface->Width = 0;
                surface->Height = 0;
                surface->PublishSurfacePropertiesSnapshot();
            }
            WindowClaimed = false;
            Device = nullptr;
        }
        PendingSceneRequests.clear();
        PendingRuntimeUiSubmissions.clear();
        PendingUiSurfaceTextureBindings.clear();
        CaptureRequests.clear();
        CaptureRuntimeUiCommands.clear();
        ActiveFrame.reset();
        NativeWindow = nullptr;
        Window.Reset();
        Windows.Reset();
        Assets.Reset();
        DeviceLifecycle.store(RenderDeviceState::Closed, std::memory_order_release);
        FramesRetired.notify_all();
    }
} // namespace Keire::RenderBackend
