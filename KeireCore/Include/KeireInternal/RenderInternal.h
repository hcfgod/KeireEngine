#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Rendering/RenderSystem.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

struct ImDrawData;
struct SDL_Window;

namespace Keire::Detail
{
    class UiContextAccess;

    [[nodiscard]] constexpr std::uint32_t BuiltinShaderUniformBufferCount(const bool) noexcept { return 3U; }

    [[nodiscard]] constexpr std::uint32_t DeferredGBufferShaderUniformBufferCount(const bool vertex) noexcept
    {
        return vertex ? 3U : 0U;
    }

    [[nodiscard]] constexpr std::uint32_t DeferredGBufferShaderStorageBufferCount(const bool vertex) noexcept
    {
        return vertex ? 1U : 0U;
    }

    [[nodiscard]] constexpr std::uint32_t DeferredLightingShaderUniformBufferCount(const bool vertex) noexcept
    {
        return vertex ? 0U : 3U;
    }

    [[nodiscard]] constexpr std::uint32_t DeferredLightingShaderSamplerCount(const bool vertex) noexcept
    {
        return vertex ? 0U : 16U;
    }

    [[nodiscard]] constexpr std::uint32_t DeferredLightingShaderStorageTextureCount(const bool) noexcept { return 0U; }

    [[nodiscard]] constexpr std::uint32_t DeferredLightingShaderStorageBufferCount(const bool vertex) noexcept
    {
        return vertex ? 0U : 4U;
    }

    [[nodiscard]] constexpr std::uint32_t IrradynShaderUniformBufferCount(const bool vertex) noexcept
    {
        return vertex ? 0U : 2U;
    }

    [[nodiscard]] constexpr std::uint32_t IrradynShaderSamplerCount(const bool vertex) noexcept
    {
        return vertex ? 0U : 7U;
    }
} // namespace Keire::Detail

namespace Keire
{
#if defined(KEIRE_ENABLE_TEST_HOOKS)
    enum class RenderRecoveryCandidateFault : std::uint8_t
    {
        HealthyFailure,
        DeviceLoss
    };

    struct AdditiveSceneCaptureSummary final
    {
        AssetId PrimaryScene;
        AssetId PrimaryBakedLighting;
        RenderCamera Camera;
        RenderEnvironmentSettings Environment;
        Color ClearColor;
        std::vector<std::uint32_t> DrawContributionOrder;
        std::vector<EntityId> DrawEntities;
        std::vector<AssetId> SpatialScenes;
        std::vector<AssetId> SpatialBakedLighting;
        std::vector<std::uint32_t> PreparedOpaqueContributionOrder;
        std::vector<EntityId> PreparedOpaqueEntities;
        std::vector<std::uint32_t> PreparedTransparentContributionOrder;
        std::vector<EntityId> PreparedTransparentEntities;
        std::size_t LocalLights = 0;
        std::size_t ReflectionProbes = 0;
        std::size_t LightProbeVolumes = 0;
    };

    struct RenderRecoveryResourceCounts final
    {
        std::size_t Meshes = 0;
        std::size_t Textures = 0;
        std::size_t Materials = 0;
        std::size_t Shaders = 0;
        std::size_t GpuVfxWorlds = 0;
        std::uint64_t RenderedEditorUiFrames = 0;
    };
#endif

    class RenderRecoveryBoundaryRequired final : public std::runtime_error
    {
      public:
        RenderRecoveryBoundaryRequired() : std::runtime_error("GPU recovery requires an application safe boundary.") {}
    };

    class RenderSystemInternalAccess final
    {
      public:
        [[nodiscard]] static SDL_GPUDevice* Device(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_Window* NativeWindow(RenderSystem& renderer) noexcept;
        [[nodiscard]] static SDL_GPUPresentMode PresentMode(RenderSystem& renderer) noexcept;
        // Convert the raw texture used while authoring Dear ImGui commands into a logical surface lease before
        // queue admission can allow the render thread to publish another N+1 output.
        [[nodiscard]] static SDL_GPUTexture* CaptureUiSurfaceTexture(const RenderSurface& surface);
        [[nodiscard]] static std::vector<std::uint8_t> ReadbackRGBA8(RenderSystem& renderer,
                                                                     const RenderSurface& surface);
        [[nodiscard]] static std::vector<float>
        ReadbackDirectionalShadow(RenderSystem& renderer, const RenderSurface& surface, std::uint32_t layer);
        [[nodiscard]] static std::vector<float> ReadbackLocalShadow(RenderSystem& renderer,
                                                                    const RenderSurface& surface, std::uint32_t layer);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        static void InjectDeviceLoss(RenderSystem& renderer);
        static void InjectDeviceLossAtRetirement(RenderSystem& renderer, std::uint32_t minimumInFlight = 1U);
        static void InjectDeviceLossWithActiveResources(RenderSystem& renderer);
        static void InjectCaptureFailure(RenderSystem& renderer) noexcept;
        static void InjectRecoveryAtAdmissionBarrier(RenderSystem& renderer) noexcept;
        static void InjectPostSubmitFailure(RenderSystem& renderer) noexcept;
        static void InjectTerminalFailureAtNextAcceptedFrame(RenderSystem& renderer) noexcept;
        static void InjectRecoveryCandidateFailure(RenderSystem& renderer, RenderRecoveryCandidateFault fault,
                                                   std::uint32_t count = 1U);
        [[nodiscard]] static std::uint32_t SaturateRendererQueue(RenderSystem& renderer);
        static void DelayNextAcceptedFrame(RenderSystem& renderer, std::uint32_t milliseconds) noexcept;
        [[nodiscard]] static bool StartThreadedHeadlessForTest(RenderSystem& renderer);
        static void BlockNextAcceptedFrame(RenderSystem& renderer) noexcept;
        [[nodiscard]] static bool WaitForAcceptedFrameBlock(RenderSystem& renderer);
        [[nodiscard]] static bool WaitForFrameAdmissionWaiter(RenderSystem& renderer);
        static void ReleaseAcceptedFrameBlock(RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t SceneCaptureEnumerationCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t RuntimeUiCaptureEnumerationCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t LastCapturedDirectionalLightEntity(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static AdditiveSceneCaptureSummary LastCapturedAdditiveScene(RenderSystem& renderer);
        [[nodiscard]] static std::size_t AvailableFrameSlotCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t LostGenerationAbandonedHandleCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t LostGenerationGpuCleanupCallCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t HealthyRecoveryCandidateCleanupCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t
        ReleasedInjectedLostDeviceCountForTest(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t LastRetriedVfxSnapshotCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::array<std::uint64_t, 2>
        LastVfxRetrySignaturesForTest(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static RenderRecoveryResourceCounts RecoveryResourceCountsForTest(RenderSystem& renderer);
        [[nodiscard]] static std::uint64_t SurfaceResourceGenerationForTest(const RenderSurface& surface);
        [[nodiscard]] static std::uint32_t RecoveryAttemptCountForTest(RenderSystem& renderer);
        [[nodiscard]] static float LastRecoveryBackoffMillisecondsForTest(RenderSystem& renderer);
        static void SatisfyRecoveryStabilityWindowForTest(RenderSystem& renderer);
        [[nodiscard]] static bool CompleteFrameTwiceForTest(RenderSystem& renderer);
        [[nodiscard]] static std::optional<GpuDeviceLossDiagnostic>
        ClassifyDeviceFailureForTest(const RenderSystem& renderer, std::string operation, std::string detail);
#endif
        static void RequestSurfaceSize(RenderSurface& surface, std::uint32_t width, std::uint32_t height);
        static void SetPresentationSurface(RenderSystem& renderer, const Ref<RenderSurface>& surface);
        [[nodiscard]] static std::size_t RuntimeUiCommandCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::size_t SceneContributionCount(const RenderSystem& renderer,
                                                                const RenderSurface& surface) noexcept;
        [[nodiscard]] static std::size_t SceneDrawItemCount(const RenderSystem& renderer,
                                                            const RenderSurface& surface) noexcept;
        [[nodiscard]] static void* SurfaceState(RenderSurface& surface) noexcept;
        [[nodiscard]] static std::shared_ptr<void> SurfaceLease(const RenderSurface& surface) noexcept;
        static void
        SetDeviceRecoveryCallbacks(RenderSystem& renderer, std::function<void()> before,
                                   std::function<void(SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)> after);
        static void SetUiContextAccess(RenderSystem& renderer, std::shared_ptr<Detail::UiContextAccess> contextAccess);
        static void QueueUiTextureRetirements(RenderSystem& renderer,
                                              std::span<const std::uintptr_t> logicalTextureIds);
        static void RunOnRenderThread(RenderSystem& renderer, std::function<void()> work);
        [[nodiscard]] static bool GpuLifecycleThreadAffinityValid(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static bool WaitForDeviceRecovery(RenderSystem& renderer, std::function<bool()> pumpWindowEvents);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        static void SetDeviceRecoveryStateForTest(RenderSystem& renderer, RenderDeviceState state) noexcept;
#endif
        static void WaitIdle(RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::exception_ptr TerminalFailure(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t MaterialBindingBuildCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t MaterialDependencyCheckCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t SkinningStaticBuildCount(const RenderSystem& renderer) noexcept;
        [[nodiscard]] static std::uint64_t SkinningOutputBuildCount(const RenderSystem& renderer) noexcept;
        static void BeginFrame(RenderSystem& renderer);
        static void CancelFrame(RenderSystem& renderer) noexcept;
        static void EndFrame(RenderSystem& renderer, ImDrawData* drawData);
    };
} // namespace Keire
