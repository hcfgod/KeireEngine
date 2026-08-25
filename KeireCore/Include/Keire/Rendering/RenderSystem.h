#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"
#include "Keire/Rendering/FrameGraphSnapshot.h"
#include "Keire/Vfx/VfxSystem.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace Keire
{
    class StreamingSystem;
    class JobSystem;
    class AssetSystem;
    class Scene;
    class RuntimeUiTree;
    class Window;
    class WindowSystem;

    enum class RenderMode : std::uint8_t
    {
        Automatic,
        Disabled,
        Headless,
        Rendered
    };

    enum class RenderPresentMode : std::uint8_t
    {
        VSync,
        Mailbox,
        Immediate
    };

    enum class RenderSampleCount : std::uint8_t
    {
        One = 1,
        Two = 2,
        Four = 4,
        Eight = 8
    };

    enum class GpuOcclusionMode : std::uint8_t
    {
        Disabled,
        Automatic,
        Forced
    };

    enum class GpuOcclusionDebugView : std::uint8_t
    {
        None,
        VisibilityBounds,
        HierarchicalDepth
    };

    enum class GpuOcclusionSurfaceState : std::uint8_t
    {
        Disabled,
        Unsupported,
        Idle,
        Active,
        Fallback
    };

    enum class GpuOcclusionFallbackReason : std::uint8_t
    {
        None,
        DisabledBySetting,
        UnsupportedBackend,
        PipelineUnavailable,
        ResourceAllocationFailed,
        NoSafeOccluders,
        BelowAutomaticThreshold,
        NoEligibleCandidates,
        LegacyShaderAbi,
        OversizedBatch,
        ReadbackValidationFailed
    };

    struct GpuOcclusionSurfaceDiagnostics
    {
        GpuOcclusionMode RequestedMode = GpuOcclusionMode::Automatic;
        GpuOcclusionMode EffectiveMode = GpuOcclusionMode::Disabled;
        GpuOcclusionSurfaceState State = GpuOcclusionSurfaceState::Disabled;
        GpuOcclusionFallbackReason FallbackReason = GpuOcclusionFallbackReason::None;
        std::uint64_t SourceFrame = 0;
        std::uint32_t ReadbackAge = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t Candidates = 0;
        std::uint32_t Visible = 0;
        std::uint32_t Culled = 0;
        std::uint32_t SafeOccluders = 0;
        std::uint32_t PyramidMipCount = 0;
        bool PyramidValid = false;
        bool ReadbackValid = false;

        auto operator<=>(const GpuOcclusionSurfaceDiagnostics&) const noexcept = default;
    };

    struct RenderSpecification
    {
        RenderMode Mode = RenderMode::Automatic;
        RenderPresentMode PresentMode = RenderPresentMode::VSync;
        Color SwapchainClearColor{0.08F, 0.09F, 0.11F, 1.0F};
        RenderSampleCount PreferredSampleCount = RenderSampleCount::Four;
        std::uint32_t MaximumFramesInFlight = 3;
        bool EnableGpuValidation = false;
    };

    struct RenderSurfaceSpecification
    {
        std::string Name = "Render Surface";
        std::uint32_t Width = 1;
        std::uint32_t Height = 1;
        Color ClearColor{0.08F, 0.09F, 0.11F, 1.0F};
        RenderSampleCount SampleCount = RenderSampleCount::Four;
        bool Depth = true;
    };

    class KEIRE_API RenderSurface final : public RefCounted
    {
      public:
        class Impl;
        ~RenderSurface() override;

        [[nodiscard]] std::string Name() const;
        [[nodiscard]] std::uint32_t Width() const noexcept;
        [[nodiscard]] std::uint32_t Height() const noexcept;
        [[nodiscard]] RenderSampleCount SampleCount() const noexcept;
        [[nodiscard]] Color ClearColor() const noexcept;
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] bool SampledDepthAvailable() const noexcept;
        [[nodiscard]] GpuOcclusionSurfaceDiagnostics OcclusionDiagnostics() const noexcept;
        [[nodiscard]] GpuOcclusionDebugView OcclusionDebugView() const noexcept;
        [[nodiscard]] std::uint32_t OcclusionDebugMip() const noexcept;

        void RequestSize(std::uint32_t width, std::uint32_t height);
        void SetClearColor(Color color);
        void SetOcclusionDebugView(GpuOcclusionDebugView view, std::uint32_t mip = 0);

      private:
        friend class RenderSystem;
        friend class RenderSystemInternalAccess;
        friend class UiFrame;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit RenderSurface(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct RenderCamera
    {
        Matrix4 View;
        Matrix4 Projection;
        Color ClearColor{0.08F, 0.09F, 0.11F, 1.0F};
        // SetCamera requires finite clip planes satisfying 0 < NearPlane < FarPlane <= 10,000,000.
        float NearPlane = 0.1F;
        float FarPlane = 1000.0F;
    };

    struct RenderEnvironmentSettings
    {
        std::uint32_t SchemaVersion = 3;
        Color AmbientColor{0.20F, 0.22F, 0.26F, 1.0F};
        float AmbientIntensity = 0.75F;
        float Exposure = 1.0F;
        AssetId Environment;
        float EnvironmentRotationDegrees = 0.0F;
        float EnvironmentDiffuseIntensity = 1.0F;
        float EnvironmentSpecularIntensity = 1.0F;
        bool SkyVisible = true;
        float DirectionalShadowDistance = 100.0F;
        std::uint32_t DirectionalShadowCascadeCount = 4;
        std::uint32_t DirectionalShadowResolution = 2048;
        float DirectionalShadowSplitLambda = 0.65F;
        GpuOcclusionMode GpuOcclusion = GpuOcclusionMode::Automatic;

        auto operator<=>(const RenderEnvironmentSettings&) const noexcept = default;
    };

    [[nodiscard]] KEIRE_API RenderEnvironmentSettings
    LoadRenderEnvironmentSettings(const std::filesystem::path& projectRoot);
    KEIRE_API void ValidateRenderEnvironmentSettings(const RenderEnvironmentSettings& settings);
    KEIRE_API void SaveRenderEnvironmentSettings(const std::filesystem::path& projectRoot,
                                                 const RenderEnvironmentSettings& settings);

    class KEIRE_API RenderView final : public RefCounted
    {
      public:
        class Impl;
        ~RenderView() override;

        [[nodiscard]] Ref<RenderSurface> Surface() const noexcept;
        [[nodiscard]] RenderCamera Camera() const noexcept;
        void SetCamera(RenderCamera camera);

      private:
        friend class RenderSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit RenderView(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct SceneRenderRequest
    {
        Ref<Keire::Scene> Scene;
        Ref<RenderView> View;
        bool DrawGrid = false;
        RenderEnvironmentSettings Environment;
        std::map<std::string, MaterialPropertyValue, std::less<>> GlobalMaterialProperties;
        VfxRenderSnapshot Vfx;
        float MaterialTimeSeconds = 0.0F;
        float MaterialDeltaSeconds = 0.0F;
        std::uint64_t FrameIndex = 0;
    };

    struct RenderCapabilities
    {
        bool CpuVfxSimulation = true;
        bool GpuVfxSimulation = false;
        bool TransparentPass = false;
        bool DynamicSpritePackets = false;
        bool TexturedSpritePackets = false;
        bool DynamicMeshPackets = false;
        bool SampledResolvedDepth = false;
        // GPU particle simulation can collide with sampled scene depth.
        bool GpuDepthCollision = false;
        bool GpuOcclusionCulling = false;
    };

    struct RenderStatistics
    {
        std::uint64_t Frame = 0;
        std::uint32_t Passes = 0;
        std::uint32_t Surfaces = 0;
        std::uint32_t DrawCalls = 0;
        std::uint32_t DepthDrawCalls = 0;
        std::uint32_t ShadowDrawCalls = 0;
        std::uint32_t Triangles = 0;
        std::uint32_t VisibleSubmeshes = 0;
        std::uint32_t CulledSubmeshes = 0;
        std::uint32_t CulledShadowSubmeshes = 0;
        std::uint32_t InstanceBatches = 0;
        std::uint32_t VisibleLocalLights = 0;
        std::uint32_t CulledLocalLights = 0;
        std::uint32_t OverflowedLightTiles = 0;
        std::uint32_t DirectionalShadowCascades = 0;
        std::uint32_t VfxSpriteParticles = 0;
        std::uint32_t VfxMeshParticles = 0;
        std::uint32_t VfxRibbonParticles = 0;
        std::uint32_t VfxVolumetricParticles = 0;
        std::uint32_t PlannedFrameGraphPasses = 0;
        std::uint32_t ExecutedFrameGraphPasses = 0;
        std::uint32_t FrameGraphTransitions = 0;
        std::uint32_t TransientResourceAllocations = 0;
        std::uint32_t RendererQueueHighWaterMark = 0;
        std::uint32_t ForwardPlusBufferReallocations = 0;
        std::uint32_t DynamicUploadBufferReallocations = 0;
        std::uint32_t CpuVfxDrawBatches = 0;
        std::uint32_t ForwardPlusCacheHits = 0;
        std::uint32_t FrameUploadSubmissions = 0;
        std::uint32_t AllowedFramesInFlight = 0;
        std::uint32_t GpuOcclusionCandidates = 0;
        std::uint32_t GpuOcclusionVisible = 0;
        std::uint32_t GpuOcclusionCulled = 0;
        std::uint32_t GpuOcclusionSafeOccluders = 0;
        std::uint32_t GpuOcclusionIndirectDraws = 0;
        std::uint32_t GpuOcclusionPyramidMipCount = 0;
        std::uint32_t GpuOcclusionDispatches = 0;
        std::uint32_t GpuOcclusionReadbackAge = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t GpuOcclusionFallbacks = 0;
        // Last completed frame's submitted surfaces. Partial-fallback surfaces are a subset of active surfaces;
        // fallback surfaces are terminal direct-draw/unsupported surfaces and exclude disabled or idle surfaces.
        std::uint32_t GpuOcclusionActiveSurfaces = 0;
        std::uint32_t GpuOcclusionFallbackSurfaces = 0;
        std::uint32_t GpuOcclusionPartialFallbackSurfaces = 0;
        std::uint64_t ForwardPlusUploadBytes = 0;
        std::uint64_t DynamicUploadBytes = 0;
        std::uint64_t DepthTriangles = 0;
        std::uint64_t ShadowTriangles = 0;
        std::uint64_t CulledCpuVfxParticles = 0;
        std::uint64_t DroppedVfxParticles = 0;
        std::uint64_t VfxGpuBufferBytes = 0;
        std::uint64_t FenceRetiredBytes = 0;
        std::uint64_t ActiveTransientBytes = 0;
        std::uint64_t TheoreticalUnaliasedBytes = 0;
        std::uint64_t SavedAliasingBytes = 0;
        std::uint64_t VfxComputeThreadGroups = 0;
        std::uint64_t GpuOcclusionCandidateTriangles = 0;
        std::uint64_t GpuOcclusionCulledTriangles = 0;
        std::uint64_t GpuOcclusionDepthTriangles = 0;
        std::uint32_t VfxComputeDispatches = 0;
        std::uint32_t VfxIndirectDraws = 0;
        std::uint32_t VfxGpuWorlds = 0;
        std::uint32_t VfxGpuParticleCapacity = 0;
        bool VfxPipelineWarmupPending = false;
        bool VfxPipelinesReady = false;
        bool GpuTimingSupported = false;
        bool ForwardPlusGpuCullingSupported = false;
        bool SampledResolvedDepthAvailable = false;
        bool GpuOcclusionEnabled = false;
        bool GpuOcclusionReadbackValid = false;
        bool GpuOcclusionFallbackActive = false;
        float CpuPreparationMilliseconds = 0.0F;
        float CpuPreparationP95Milliseconds = 0.0F;
        float CommandRecordingMilliseconds = 0.0F;
        float SkinningPreparationMilliseconds = 0.0F;
        float VfxPreparationMilliseconds = 0.0F;
        float DrawPreparationMilliseconds = 0.0F;
        float ShadowRecordingMilliseconds = 0.0F;
        float ForwardPlusCullingMilliseconds = 0.0F;
        float ScenePassMilliseconds = 0.0F;
        float DepthPassMilliseconds = 0.0F;
        float ToneMapMilliseconds = 0.0F;
        float CommandRecordingUnattributedMilliseconds = 0.0F;
        float FrameUploadMilliseconds = 0.0F;
        float GpuFenceWaitMilliseconds = 0.0F;
        float SwapchainWaitMilliseconds = 0.0F;
        float UiRecordingMilliseconds = 0.0F;
        float GpuSubmissionMilliseconds = 0.0F;
        float GpuFrameMilliseconds = 0.0F;
        float GpuCompletionLatencyMilliseconds = 0.0F;
        float VfxGpuCompletionLatencyMilliseconds = 0.0F;
        float RendererLatencyMilliseconds = 0.0F;
        float VfxPipelineWarmupMilliseconds = 0.0F;
        /// CPU time spent recording the occlusion depth pass; SDL_GPU does not expose GPU timestamps.
        float GpuOcclusionDepthPassMilliseconds = 0.0F;
        /// CPU time spent recording depth-pyramid dispatches; this is not GPU execution time.
        float GpuOcclusionPyramidRecordingMilliseconds = 0.0F;
        /// CPU time spent recording culling/compaction dispatches; this is not GPU execution time.
        float GpuOcclusionCullingRecordingMilliseconds = 0.0F;
    };

    class KEIRE_API RenderSystem final : public RefCounted
    {
      public:
        ~RenderSystem() override;

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;

        [[nodiscard]] Ref<RenderSurface> CreateSurface(const RenderSurfaceSpecification& specification = {});
        [[nodiscard]] Ref<RenderView> CreateView(const RenderSurfaceSpecification& specification = {});
        void Submit(const SceneRenderRequest& request);
        void Submit(SceneRenderRequest&& request);
        void SubmitRuntimeUi(const Ref<RuntimeUiTree>& tree);
        void RequestGpuVfxPipelineWarmup();

        [[nodiscard]] RenderMode Mode() const noexcept;
        [[nodiscard]] RenderCapabilities Capabilities() const noexcept;
        [[nodiscard]] RenderStatistics Statistics() const noexcept;
        [[nodiscard]] FrameGraphSnapshot CaptureFrameGraph() const;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        friend class Application;
        friend class RenderSystemInternalAccess;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        RenderSystem(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                     Ref<AssetSystem> assets, Ref<JobSystem> jobs, Ref<StreamingSystem> streaming);

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
