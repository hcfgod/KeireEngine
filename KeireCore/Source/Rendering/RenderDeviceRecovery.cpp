#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/Log.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace Keire::RenderBackend
{
#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void RenderSharedState::RecordVfxSnapshotSignatureForTest(const std::shared_ptr<RenderFramePacket>& frame,
                                                              const bool retried) noexcept
    {
        constexpr std::uint64_t offsetBasis = 1469598103934665603ULL;
        std::uint64_t signature = offsetBasis;
        std::uint64_t count = 0;
        const auto mix = [&signature](const std::uint64_t value) noexcept
        {
            signature ^= value;
            signature *= 1099511628211ULL;
        };
        if (frame)
        {
            for (const auto& request : frame->Requests)
            {
                for (const auto& snapshot : request.Packet.VfxSnapshots)
                {
                    ++count;
                    mix(snapshot.WorldId());
                    mix(snapshot.Revision());
                    mix(snapshot.ResetRevision());
                    mix(snapshot.SimulationStepRevision());
                    mix(snapshot.ParticleCapacity());
                    mix(snapshot.Particles().size());
                    mix(snapshot.GpuEmitters().size());
                    for (const auto& emitter : snapshot.GpuEmitters())
                    {
                        mix(emitter.System.High());
                        mix(emitter.System.Low());
                        mix(emitter.Revision);
                        mix(emitter.SpawnSequence);
                        mix(emitter.Seed);
                        mix(emitter.Capacity);
                        mix(emitter.SimulationRevision);
                        mix(emitter.SimulationStep);
                        mix(std::bit_cast<std::uint32_t>(emitter.EffectTime));
                    }
                }
            }
        }
        if (retried)
        {
            LastRetriedVfxSnapshotCount.store(count, std::memory_order_release);
            LastRetriedVfxSnapshotSignature.store(signature, std::memory_order_release);
        }
        else
        {
            LastCapturedVfxSnapshotSignature.store(signature, std::memory_order_release);
        }
    }
#endif

    std::optional<GpuDeviceLossDiagnostic> RenderSharedState::ClassifyDeviceFailure(std::string operation,
                                                                                    std::string detail) const
    {
        if (operation == "SDL GPU frame execution" && detail.starts_with("SDL_"))
        {
            const auto end = detail.find(" failed:");
            if (end != std::string::npos && end <= 96U &&
                std::ranges::all_of(std::string_view(detail).substr(0U, end), [](const unsigned char value)
                                    { return std::isalnum(value) || value == '_' || value == '(' || value == ')'; }))
            {
                operation = detail.substr(0U, end);
            }
        }
        std::string normalized = detail;
        std::ranges::transform(normalized, normalized.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        constexpr std::string_view deviceLossMarkers[]{"device lost",         "device removed", "device reset",
                                                       "device hung",         "gpu lost",       "dxgi_error_device",
                                                       "vk_error_device_lost"};
        if (std::ranges::none_of(deviceLossMarkers, [&normalized](const std::string_view marker)
                                 { return normalized.find(marker) != std::string::npos; }))
        {
            return std::nullopt;
        }
        return DeviceLossDiagnostic(std::move(operation), std::move(detail));
    }

    void RenderSharedState::ThrowIfDeviceLost(std::string operation, std::string detail) const
    {
        if (const auto diagnostic = ClassifyDeviceFailure(std::move(operation), std::move(detail)))
            throw GpuDeviceLostError(*diagnostic);
    }

    void RenderSharedState::RethrowIfDeviceLost(const std::string_view operation) const
    {
        const auto failure = std::current_exception();
        if (!failure)
            return;
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost(std::string(operation), error.what());
        }
        catch (...)
        {
        }
    }

    void RenderSharedState::HandleRenderThreadFailure(std::exception_ptr failure) noexcept
    {
        if (!failure)
            return;
        const auto interrupted = InFlight.empty() ? std::shared_ptr<RenderFramePacket>{} : InFlight.front().Frame;
        try
        {
            std::optional<GpuDeviceLossDiagnostic> diagnostic;
            try
            {
                std::rethrow_exception(failure);
            }
            catch (const GpuDeviceLostError& error)
            {
                diagnostic = error.Diagnostic();
            }
            catch (const std::exception& error)
            {
                diagnostic = ClassifyDeviceFailure("SDL GPU retirement", error.what());
            }
            catch (...)
            {
            }
            if (!diagnostic)
            {
                RecordTerminalFailure(std::move(failure), interrupted);
                return;
            }

            if (interrupted && interrupted->RetriedAfterDeviceLoss)
            {
                RecordTerminalFailure(std::move(failure), interrupted);
                return;
            }
            const auto recovery = RecoverDevice(interrupted, *diagnostic);
            if (recovery == DeviceRecoveryResult::CancelledByShutdown)
            {
                CompleteFrame(interrupted, true);
                return;
            }
            if (recovery != DeviceRecoveryResult::Recovered)
            {
                RecordTerminalFailure(std::move(failure), interrupted);
                return;
            }
            if (interrupted)
            {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                RecordVfxSnapshotSignatureForTest(interrupted, true);
#endif
                interrupted->RetriedAfterDeviceLoss = true;
                ExecuteAcceptedFrame(interrupted);
            }
        }
        catch (...)
        {
            RecordTerminalFailure(std::current_exception(), interrupted);
        }
    }

    void RenderSharedState::AbandonLostDeviceResources(const std::shared_ptr<RenderFramePacket>& interrupted) noexcept
    {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        std::uint64_t abandonedHandles = 0;
        for (const auto& frame : InFlight)
        {
            abandonedHandles += static_cast<std::uint64_t>(frame.Fence != nullptr);
            abandonedHandles += frame.RetiredPipelines.size() + frame.TransientBuffers.size() +
                                frame.TransientTransferBuffers.size() + frame.GpuOcclusionReadbacks.size();
        }
        LostGenerationAbandonedHandleCount.fetch_add(abandonedHandles, std::memory_order_relaxed);
#endif
        EditorUiTextures.ReleaseGpuTextures(nullptr, true);
        for (auto& frame : InFlight)
        {
            if (frame.ResolvedEditorUi)
                frame.ResolvedEditorUi->ReleaseGpuTextures(nullptr, true);
            if (frame.Frame != interrupted)
                CompleteFrame(frame.Frame, true);
        }
        InFlight.clear();

        for (const auto& surface : AllSurfaceEpochs())
        {
            surface->Resources = {};
            surface->ResourcesAvailable.store(false, std::memory_order_release);
            surface->PublishedTexture.store(nullptr, std::memory_order_release);
            surface->PresentationFallbackLifetime.store({}, std::memory_order_release);
            surface->PublishedDepthAvailable.store(false, std::memory_order_release);
            surface->Width = 0;
            surface->Height = 0;
            surface->FailedWidth = 0;
            surface->FailedHeight = 0;
            surface->HasOutput = false;
            surface->SampledDepthValid = false;
            ++surface->Generation;
            surface->PublishSurfacePropertiesSnapshot();
        }

        PendingRetired.clear();
        PendingRetiredMeshes.clear();
        PendingRetiredSkins.clear();
        PendingRetiredTextures.clear();
        PendingRetiredPipelines.clear();
        PendingRetiredForwardPlus.clear();
        PendingRetiredBytes = 0;
        FrameTransientBuffers.clear();
        FrameUploadTransfers.clear();
        FrameGpuOcclusionReadbacks.clear();
        FrameUploadCommands = nullptr;
        FrameUploadPass = nullptr;

        Pipelines.clear();
        MeshCache.clear();
        SkinCache.clear();
        TextureCache.clear();
        ReleaseRuntimeUiFontAtlas(true);
        ReleaseRuntimeUiRenderTextureCache(true);
        LightingTextureCache.clear();
        LightingSetCache.clear();
        LightProbeVolumeCache.clear();
        MaterialCache.clear();
        ShaderCache.clear();
        SamplerCache.clear();
        VfxVolumeCache.clear();
        GpuVfxWorlds.clear();
        CookieAtlasAssets.fill({});
        CookieAtlasHandles.fill({});
        CookieAtlasRevisions.fill(0);

        ShadowPipeline = nullptr;
        SceneDepthPipeline = nullptr;
        ToneMapPipeline = nullptr;
        RuntimeUiPipeline = nullptr;
        RuntimeUiCameraOverlayPipeline = nullptr;
        RuntimeUiRenderTexturePipeline = nullptr;
        GpuOcclusionDepthPipelines.fill(nullptr);
        GpuOcclusionBuildBasePipeline = nullptr;
        GpuOcclusionReducePipeline = nullptr;
        GpuOcclusionClassifyPipeline = nullptr;
        GpuOcclusionScanBlocksPipeline = nullptr;
        GpuOcclusionScanBatchesPipeline = nullptr;
        GpuOcclusionScatterPipeline = nullptr;
        ForwardPlusVisibilityPipeline = nullptr;
        SpatialSelectionPipeline = nullptr;
        GpuOcclusionDebugPyramidPipeline = nullptr;
        GpuOcclusionDebugBoundsPipeline = nullptr;
        ShadowSampler = nullptr;
        ToneMapSampler = nullptr;
        GpuOcclusionSampler = nullptr;
        EmptyShadowTexture = nullptr;
        SpatialSelectionFallbackBuffer = nullptr;
        SpatialSelectionFallbackDeviceGeneration = 0;
        DefaultMesh = {};
        ErrorMesh = {};
        CheckerboardTexture = {};
        DefaultSkyTexture = {};
        BrdfIntegrationLut = {};
        WhiteTexture = {};
        FlatNormalTexture = {};
        NeutralOrmTexture = {};
        BlackTexture = {};
        BlackDataTexture = {};
        WhiteDataTexture = {};
        DefaultLightingArray = {};
        DefaultLightingMaskArray = {};
        DefaultReflectionCubeArray = {};
        CookieAtlas = {};
        SkinningPipeline = nullptr;
        SkinningPipelineAttempted = false;
        GpuOcclusionPipelineFailure.clear();
        GpuOcclusionPipelinesAttempted = false;
        SpatialSelectionPipelineAttempted = false;
        VfxInitializePipeline = nullptr;
        VfxResetPipeline = nullptr;
        VfxKillPipeline = nullptr;
        VfxTransformPipeline = nullptr;
        VfxSimulatePipeline = nullptr;
        VfxSimulateOutputPipeline = nullptr;
        VfxSpawnPipeline = nullptr;
        VfxSpawnInitializePipeline = nullptr;
        VfxSpawnOutputPipeline = nullptr;
        VfxMapStripsPipeline = nullptr;
        VfxLinkStripsPipeline = nullptr;
        VfxFinalizePipeline = nullptr;
        VfxResetRenderPipeline = nullptr;
        VfxFilterRenderPipeline = nullptr;
        VfxBuildVisibilityPipeline = nullptr;
        VfxCompactVisibilityPipeline = nullptr;
        VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::NotStarted, std::memory_order_release);
        VfxPipelineWarmupFailure.clear();
        Statistics.FenceRetiredBytes = 0;
    }

    void RenderSharedState::CreateDeviceAndMandatoryResources(const bool recovering,
                                                              const std::uint32_t resourceGeneration)
    {
        RequireRenderThread("CreateDeviceAndMandatoryResources");
        GpuCreationThread = std::this_thread::get_id();
        constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB);
        const char* requestedDriver = recovering && !DeviceDriver.empty() ? DeviceDriver.c_str() : nullptr;
        Device = SDL_CreateGPUDevice(formats, Specification.EnableGpuValidation, requestedDriver);
        if (!Device)
            throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_CreateGPUDevice", LastSdlError()));
        const std::string recreatedDriver = SDL_GetGPUDeviceDriver(Device) ? SDL_GetGPUDeviceDriver(Device) : "unknown";
        if (recovering && !DeviceDriver.empty() && recreatedDriver != DeviceDriver)
            throw GpuDeviceLostError(
                DeviceLossDiagnostic("SDL_CreateGPUDevice", "Recreated backend changed unexpectedly."));
        DeviceDriver = recreatedDriver;
        const auto deviceProperties = SDL_GetGPUDeviceProperties(Device);
        const auto property = [deviceProperties](const char* name) -> std::string
        {
            const char* value = deviceProperties ? SDL_GetStringProperty(deviceProperties, name, "") : "";
            return value ? std::string(value) : std::string{};
        };
        RenderDeviceIdentity identity{.Available = true,
                                      .Backend = DeviceDriver,
                                      .Adapter = property(SDL_PROP_GPU_DEVICE_NAME_STRING),
                                      .DriverName = property(SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING),
                                      .DriverVersion = property(SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING),
                                      .DriverInformation = property(SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING),
                                      .DeviceGeneration = resourceGeneration};
        if (identity.Adapter.empty())
            identity.Adapter = "unknown SDL GPU adapter";
        if (identity.DriverName.empty())
            identity.DriverName = DeviceDriver;
        {
            std::scoped_lock lock(DeviceIdentityMutex);
            DeviceIdentitySnapshot = identity;
        }
        KEIRE_CORE_INFO("Created SDL_GPU device (driver={}, shader formats=0x{:x}).", DeviceDriver,
                        static_cast<std::uint32_t>(SDL_GetGPUShaderFormats(Device)));

        if (!SDL_ClaimWindowForGPUDevice(Device, NativeWindow))
        {
            const auto initialClaimFailure = LastSdlError();
            if (!recovering)
                throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_ClaimWindowForGPUDevice", initialClaimFailure));
            try
            {
                NativeWindow = RequestWindowRecreationAtOwnerBoundary();
            }
            catch (const std::exception& error)
            {
                throw GpuDeviceLostError(
                    DeviceLossDiagnostic("recreate native window after SDL_ClaimWindowForGPUDevice",
                                         initialClaimFailure + "; native-window replacement failed: " + error.what()));
            }
            if (!SDL_ClaimWindowForGPUDevice(Device, NativeWindow))
            {
                throw GpuDeviceLostError(
                    DeviceLossDiagnostic("SDL_ClaimWindowForGPUDevice(recreated window)",
                                         initialClaimFailure + "; replacement claim failed: " + LastSdlError()));
            }
        }
        WindowClaimed = true;
        Statistics.AllowedFramesInFlight = SdlAllowedFramesInFlight(Specification.MaximumFramesInFlight);
        if (!SDL_SetGPUAllowedFramesInFlight(Device, Statistics.AllowedFramesInFlight))
            throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_SetGPUAllowedFramesInFlight", LastSdlError()));
        if (!SDL_WindowSupportsGPUPresentMode(Device, NativeWindow, PresentMode))
        {
            if (recovering)
            {
                throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_WindowSupportsGPUPresentMode",
                                                              "The configured present mode disappeared."));
            }
            KEIRE_CORE_WARN("Requested render present mode is unavailable; falling back to VSync.");
            PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
            if (!SDL_WindowSupportsGPUPresentMode(Device, NativeWindow, PresentMode))
            {
                throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_WindowSupportsGPUPresentMode",
                                                              "The VSync fallback present mode is unavailable."));
            }
        }
        if (!SDL_SetGPUSwapchainParameters(Device, NativeWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode))
            throw GpuDeviceLostError(DeviceLossDiagnostic("SDL_SetGPUSwapchainParameters", LastSdlError()));

        ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        const SDL_GPUTextureUsageFlags colorUsage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (!SDL_GPUTextureSupportsFormat(Device, ColorFormat, SDL_GPU_TEXTURETYPE_2D, colorUsage))
            ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (!SDL_GPUTextureSupportsFormat(Device, SceneColorFormat, SDL_GPU_TEXTURETYPE_2D, colorUsage))
            throw GpuDeviceLostError(
                DeviceLossDiagnostic("SDL_GPUTextureSupportsFormat", "RGBA16F scene attachments are unavailable."));

        DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        ShadowDepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        constexpr SDL_GPUTextureFormat depthCandidates[]{
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTUREFORMAT_D16_UNORM};
        for (const auto candidate : depthCandidates)
        {
            if (DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID &&
                SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                             SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            {
                DepthFormat = candidate;
            }
            if (ShadowDepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID &&
                SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D_ARRAY,
                                             SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                                 SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
                SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                             SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
            {
                ShadowDepthFormat = candidate;
            }
        }
        if (DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID || ShadowDepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
            throw GpuDeviceLostError(
                DeviceLossDiagnostic("SDL_GPUTextureSupportsFormat", "No compatible depth format is available."));

        const auto shaderFormats = SDL_GetGPUShaderFormats(Device);
        const bool occlusionShaderFormat =
            (shaderFormats & (SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)) != 0;
        GpuOcclusionCapability.store(
            occlusionShaderFormat &&
                SDL_GPUTextureSupportsFormat(Device, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                             SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE),
            std::memory_order_release);
        CreateGeometryResources(resourceGeneration);
        (void)PipelinesFor(SDL_GPU_SAMPLECOUNT_1);
        RuntimeUiPipeline = CreateRuntimeUiPipeline();
        KEIRE_CORE_INFO("Selected GPU attachment formats (output={}, scene={}, depth={}, shadowDepth={}).",
                        static_cast<std::uint32_t>(ColorFormat), static_cast<std::uint32_t>(SceneColorFormat),
                        static_cast<std::uint32_t>(DepthFormat), static_cast<std::uint32_t>(ShadowDepthFormat));
        KEIRE_CORE_INFO("Configured {} total accepted GPU frame(s) in flight.", Statistics.AllowedFramesInFlight);
        if (recovering)
        {
            for (const auto& surface : AllSurfaceEpochs())
            {
                EnsureSurface(*surface);
                if (surface->RequestedWidth != 0U && surface->RequestedHeight != 0U &&
                    !surface->Resources.PublishedColor())
                {
                    throw GpuDeviceLostError(DeviceLossDiagnostic("rebuild render surface",
                                                                  "GPU resources could not be recreated for surface '" +
                                                                      surface->Specification.Name + "'."));
                }
                (void)PipelinesFor(ToSdlSampleCount(surface->ActualSamples));
            }
        }
    }

    DeviceRecoveryResult RenderSharedState::RecoverDevice(const std::shared_ptr<RenderFramePacket>& interrupted,
                                                          const GpuDeviceLossDiagnostic& diagnostic)
    {
        const auto recoveryStarted = std::chrono::steady_clock::now();
        RecoveryStartedAt = recoveryStarted;
        const auto cleanupHealthyCandidate = [this]() noexcept
        {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            HealthyRecoveryCandidateCleanupCount.fetch_add(1U, std::memory_order_relaxed);
#endif
            DestroyDeviceAndResources(false, true);
        };
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        const auto consumeInjectedFailure = [](std::atomic<std::uint32_t>& remaining) noexcept
        {
            auto observed = remaining.load(std::memory_order_acquire);
            while (observed != 0U && !remaining.compare_exchange_weak(
                                         observed, observed - 1U, std::memory_order_acq_rel, std::memory_order_acquire))
            {
            }
            return observed != 0U;
        };
#endif
        const auto publishIncident = [this, &diagnostic, recoveryStarted](const std::uint32_t attempt,
                                                                          const bool succeeded) -> float
        {
            auto incident = diagnostic;
            incident.RecoveryAttempt = attempt;
            incident.RecoveredDeviceGeneration = succeeded ? DeviceGeneration.load(std::memory_order_acquire) : 0U;
            const auto elapsed =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recoveryStarted).count();
            incident.RecoveryElapsedMilliseconds = elapsed;
            incident.RecoverySucceeded = succeeded;
            std::scoped_lock lock(FailureMutex);
            LastDeviceLossDiagnostic = std::move(incident);
            return elapsed;
        };
        bool cancelledByShutdown = false;
        {
            std::scoped_lock lock(RenderQueueMutex);
            if (!TryBeginDeviceRecovery(DeviceLifecycle))
            {
                const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
                cancelledByShutdown = lifecycle == RenderDeviceState::Closing || lifecycle == RenderDeviceState::Closed;
                if (!cancelledByShutdown)
                    return DeviceRecoveryResult::Failed;
            }
            else
            {
                RecoveryOwnerBoundary = false;
            }
        }
        if (cancelledByShutdown)
        {
            // A loss first observed after ordinary shutdown begins must not start recovery or turn Close into a
            // throwing operation. Abandon the lost generation on its render thread and let shutdown cancel the
            // accepted packet exactly once.
            DeviceLost = true;
            publishIncident(0U, false);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            if (!ReleaseInjectedLostDeviceForTest())
#endif
                AbandonLostDeviceResources();
            WindowClaimed = false;
            Device = nullptr;
            FramesRetired.notify_all();
            return DeviceRecoveryResult::CancelledByShutdown;
        }
        DeviceLost = true;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        LastRecoveryBackoffMillisecondsForTest.store(0.0F, std::memory_order_release);
#endif
        {
            std::scoped_lock lock(FailureMutex);
            LastDeviceLossDiagnostic = diagnostic;
        }
        FramesRetired.notify_all();
        if (Specification.DeviceLossRecoveryAttempts == 0U)
        {
            publishIncident(0U, false);
            return DeviceRecoveryResult::Failed;
        }

        {
            std::unique_lock lock(RenderQueueMutex);
            FramesRetired.wait(lock,
                               [this]
                               {
                                   return RecoveryOwnerBoundary ||
                                          DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing;
                               });
            if (DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing)
                return DeviceRecoveryResult::Failed;
        }

        try
        {
            std::function<void()> before;
            {
                std::scoped_lock lock(DeviceCallbackMutex);
                before = BeforeDeviceRecovery;
            }
            if (before)
                before();
        }
        catch (...)
        {
            publishIncident(RecoveryAttemptsUsed.load(std::memory_order_acquire), false);
            return DeviceRecoveryResult::Failed;
        }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (!ReleaseInjectedLostDeviceForTest(interrupted))
#endif
            AbandonLostDeviceResources(interrupted);
        WindowClaimed = false;
        // Never release claims or destroy the lost generation. SDL GPU handles are unusable after loss; recovery
        // intentionally abandons them and creates an independent generation.
        Device = nullptr;

        while (RecoveryAttemptsUsed.load(std::memory_order_acquire) < Specification.DeviceLossRecoveryAttempts)
        {
            auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
            while (lifecycle == RenderDeviceState::RecoveryPending || lifecycle == RenderDeviceState::Recovering)
            {
                if (DeviceLifecycle.compare_exchange_weak(lifecycle, RenderDeviceState::Recovering,
                                                          std::memory_order_acq_rel))
                {
                    break;
                }
            }
            if (lifecycle != RenderDeviceState::RecoveryPending && lifecycle != RenderDeviceState::Recovering)
                return DeviceRecoveryResult::Failed;
            const auto attempt = RecoveryAttemptsUsed.fetch_add(1U, std::memory_order_acq_rel) + 1U;
            if (attempt > 1U)
            {
                std::unique_lock lock(RenderQueueMutex);
                const auto backoffStarted = std::chrono::steady_clock::now();
                const bool closing = FramesRetired.wait_for(
                    lock, std::chrono::milliseconds(250),
                    [this] { return DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing; });
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                LastRecoveryBackoffMillisecondsForTest.store(
                    std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - backoffStarted).count(),
                    std::memory_order_release);
#endif
                if (closing)
                {
                    return DeviceRecoveryResult::Failed;
                }
            }
            if (DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing)
                return DeviceRecoveryResult::Failed;
            try
            {
                const auto activeGeneration = DeviceGeneration.load(std::memory_order_acquire);
                const auto candidateGeneration = RecoveryCandidateDeviceGeneration(activeGeneration);
                if (!candidateGeneration)
                    throw std::overflow_error("GPU device generation is exhausted.");
                CreateDeviceAndMandatoryResources(true, *candidateGeneration);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (consumeInjectedFailure(InjectHealthyRecoveryCandidateFailures))
                    throw std::runtime_error("Injected healthy recovery-candidate failure.");
                if (consumeInjectedFailure(InjectLostRecoveryCandidateFailures))
                {
                    MarkInjectedDeviceLossForTest();
                    throw GpuDeviceLostError(
                        DeviceLossDiagnostic("test recovery candidate", "Injected candidate device lost."));
                }
#endif
                if (DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing)
                {
                    cleanupHealthyCandidate();
                    return DeviceRecoveryResult::Failed;
                }
                std::function<void(SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)> after;
                {
                    std::scoped_lock lock(DeviceCallbackMutex);
                    after = AfterDeviceRecovery;
                }
                if (after)
                    after(Device, ColorFormat, PresentMode);
                if (DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing)
                {
                    cleanupHealthyCandidate();
                    return DeviceRecoveryResult::Failed;
                }
                DeviceGeneration.store(*candidateGeneration, std::memory_order_release);
                const auto recoveredGeneration = *candidateGeneration;
                if (interrupted)
                {
                    interrupted->DeviceGeneration = recoveredGeneration;
                    QualifyRuntimeUiCameraPanels(*interrupted);
                    QualifyRuntimeUiWorldPanels(*interrupted);
                    QualifyRuntimeUiRenderTextures(*interrupted);
                    QualifyRuntimeUiImageLeases(*interrupted);
                    QualifyRuntimeUiFontLeases(*interrupted);
                }
                if (!interrupted && !CompleteDeviceRecoveryAfterRetry())
                {
                    cleanupHealthyCandidate();
                    return DeviceRecoveryResult::CancelledByShutdown;
                }
                return DeviceRecoveryResult::Recovered;
            }
            catch (const GpuDeviceLostError& error)
            {
                KEIRE_CORE_ERROR("GPU device recreation attempt {} failed: {}", attempt, error.what());
                const bool candidateLost =
                    ClassifyDeviceFailure(error.Diagnostic().Operation, error.Diagnostic().DriverDetail).has_value();
                if (candidateLost)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    if (!ReleaseInjectedLostDeviceForTest(interrupted))
#endif
                        AbandonLostDeviceResources(interrupted);
                    WindowClaimed = false;
                    Device = nullptr;
                }
                else
                {
                    cleanupHealthyCandidate();
                }
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_ERROR("GPU device recreation attempt {} failed: {}", attempt, error.what());
                if (ClassifyDeviceFailure("GPU recovery candidate", error.what()))
                {
                    AbandonLostDeviceResources(interrupted);
                    WindowClaimed = false;
                    Device = nullptr;
                }
                else
                {
                    cleanupHealthyCandidate();
                }
            }
            catch (...)
            {
                // An unknown failure cannot prove the candidate generation healthy. Abandon it without SDL calls.
                AbandonLostDeviceResources(interrupted);
                WindowClaimed = false;
                Device = nullptr;
            }
        }
        const auto recoveryAttempts = RecoveryAttemptsUsed.load(std::memory_order_acquire);
        publishIncident(recoveryAttempts, false);
        KEIRE_CORE_ERROR(
            "GPU recovery exhausted after {} attempt(s): operation='{}', backend='{}', adapter='{}', frame={}, "
            "deviceGeneration={}, driver='{}', driverVersion='{}'.",
            recoveryAttempts, std::string_view(diagnostic.Operation).substr(0U, 160U),
            std::string_view(diagnostic.Backend).substr(0U, 160U),
            std::string_view(diagnostic.Adapter).substr(0U, 160U), diagnostic.Frame, diagnostic.DeviceGeneration,
            std::string_view(diagnostic.DriverName).substr(0U, 160U),
            std::string_view(diagnostic.DriverVersion).substr(0U, 160U));
        PublishTerminalDeviceFailure(DeviceLifecycle);
        FramesRetired.notify_all();
        return DeviceRecoveryResult::Failed;
    }

    bool RenderSharedState::CompleteDeviceRecoveryAfterRetry() noexcept
    {
        try
        {
            std::unique_lock queueLock(RenderQueueMutex);
            if (DeviceLifecycle.load(std::memory_order_acquire) != RenderDeviceState::Recovering)
                return false;

            const auto completedAt = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<float, std::milli>(completedAt - RecoveryStartedAt).count();
            std::uint32_t previousGeneration = 0U;
            std::uint32_t recoveredGeneration = DeviceGeneration.load(std::memory_order_acquire);
            std::uint32_t attempt = RecoveryAttemptsUsed.load(std::memory_order_acquire);
            std::string_view operation = "unknown";
            std::string_view backend = "unknown";
            std::string_view adapter = "unknown";
            {
                std::scoped_lock failureLock(FailureMutex);
                if (LastDeviceLossDiagnostic)
                {
                    LastDeviceLossDiagnostic->RecoveryAttempt = attempt;
                    LastDeviceLossDiagnostic->RecoveredDeviceGeneration = recoveredGeneration;
                    LastDeviceLossDiagnostic->RecoveryElapsedMilliseconds = elapsed;
                    LastDeviceLossDiagnostic->RecoverySucceeded = true;
                    previousGeneration = LastDeviceLossDiagnostic->DeviceGeneration;
                    operation = LastDeviceLossDiagnostic->Operation;
                    backend = LastDeviceLossDiagnostic->Backend;
                    adapter = LastDeviceLossDiagnostic->Adapter;
                }
            }
            DeviceLost = false;
            LastRecoveryCompletedAt = completedAt;
            RetiredFramesSinceRecovery = 0;
            try
            {
                KEIRE_CORE_WARN(
                    "Recovered SDL GPU device generation {} -> {} after loss in '{}' on backend '{}' adapter '{}' "
                    "(attempt {}, {:.2f} ms).",
                    previousGeneration, recoveredGeneration, operation.substr(0U, 160U), backend.substr(0U, 160U),
                    adapter.substr(0U, 160U), attempt, elapsed);
            }
            catch (...)
            {
            }
            // Publish Running last. The owner boundary may resume immediately after this store, so every recovery
            // diagnostic and retained-generation field must already describe the successfully resubmitted frame.
            DeviceLifecycle.store(RenderDeviceState::Running, std::memory_order_release);
            queueLock.unlock();
            FramesRetired.notify_all();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace Keire::RenderBackend
