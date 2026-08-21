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

        void RequestSize(std::uint32_t width, std::uint32_t height);
        void SetClearColor(Color color);

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
        float NearPlane = 0.1F;
        float FarPlane = 1000.0F;
    };

    struct RenderEnvironmentSettings
    {
        std::uint32_t SchemaVersion = 2;
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
        bool GpuDepthCollision = false;
    };

    struct RenderStatistics
    {
        std::uint64_t Frame = 0;
        std::uint32_t Passes = 0;
        std::uint32_t Surfaces = 0;
        std::uint32_t DrawCalls = 0;
        std::uint32_t Triangles = 0;
        std::uint32_t VisibleSubmeshes = 0;
        std::uint32_t CulledSubmeshes = 0;
        std::uint32_t InstanceBatches = 0;
        std::uint32_t VisibleLocalLights = 0;
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
        std::uint32_t ForwardPlusCacheHits = 0;
        std::uint32_t FrameUploadSubmissions = 0;
        std::uint32_t AllowedFramesInFlight = 0;
        std::uint64_t ForwardPlusUploadBytes = 0;
        std::uint64_t DroppedVfxParticles = 0;
        std::uint64_t VfxGpuBufferBytes = 0;
        std::uint64_t FenceRetiredBytes = 0;
        std::uint64_t ActiveTransientBytes = 0;
        std::uint64_t TheoreticalUnaliasedBytes = 0;
        std::uint64_t SavedAliasingBytes = 0;
        std::uint64_t VfxComputeThreadGroups = 0;
        std::uint32_t VfxComputeDispatches = 0;
        std::uint32_t VfxIndirectDraws = 0;
        std::uint32_t VfxGpuWorlds = 0;
        std::uint32_t VfxGpuParticleCapacity = 0;
        bool VfxPipelineWarmupPending = false;
        bool VfxPipelinesReady = false;
        bool GpuTimingSupported = false;
        bool ForwardPlusGpuCullingSupported = false;
        bool SampledResolvedDepthAvailable = false;
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
