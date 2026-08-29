#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Ui/RuntimeUi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>

struct ImDrawData;
struct SDL_GPUDevice;

namespace Keire::RenderBackend
{
    struct QueuedSceneRequest;
    struct RenderSurfaceEpochLease final
    {
        RenderSurfaceEpochLease(const std::uint64_t id, const std::uint64_t epoch) : Id(id), Epoch(epoch) {}
        const std::uint64_t Id;
        const std::uint64_t Epoch;
    };

    struct RenderSurfaceToken final
    {
        std::uint64_t Id = 0;
        std::uint64_t Epoch = 0;
        std::shared_ptr<const RenderSurfaceEpochLease> Lifetime;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Id != 0 && Epoch != 0 && static_cast<bool>(Lifetime);
        }
    };

    struct CapturedSurfaceTextureBinding final
    {
        RenderSurfaceToken Surface;
        std::uintptr_t TextureIdentity = 0;
    };

    struct PendingSceneRequest final
    {
        SceneRenderRequest Request;
        RenderSurfaceToken Surface;
    };

    class ImGuiTextureCache final
    {
      public:
        ImGuiTextureCache();
        ~ImGuiTextureCache();

        ImGuiTextureCache(const ImGuiTextureCache&) = delete;
        ImGuiTextureCache& operator=(const ImGuiTextureCache&) = delete;

        void ReleaseGpuTextures(SDL_GPUDevice* device, bool abandon) noexcept;

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        [[nodiscard]] std::size_t TextureCountForTest() const noexcept;
        [[nodiscard]] std::size_t GpuTextureCountForTest(std::uint32_t deviceGeneration) const noexcept;
#endif

      private:
        friend class OwnedImGuiDrawData;
        friend class ResolvedImGuiDrawData;
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ResolvedImGuiDrawData final
    {
      public:
        ~ResolvedImGuiDrawData();

        ResolvedImGuiDrawData(const ResolvedImGuiDrawData&) = delete;
        ResolvedImGuiDrawData& operator=(const ResolvedImGuiDrawData&) = delete;

        [[nodiscard]] ImDrawData* Data() noexcept;
        void CommitGpuTextures(ImGuiTextureCache& cache, std::uint32_t deviceGeneration) noexcept;
        void ReleaseGpuTextures(SDL_GPUDevice* device, bool abandon) noexcept;

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        [[nodiscard]] std::size_t PendingGpuTextureRetirementCountForTest() const noexcept;
#endif

      private:
        friend class OwnedImGuiDrawData;
        class Impl;
        explicit ResolvedImGuiDrawData(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class OwnedImGuiDrawData final
    {
      public:
        ~OwnedImGuiDrawData();

        OwnedImGuiDrawData(const OwnedImGuiDrawData&) = delete;
        OwnedImGuiDrawData& operator=(const OwnedImGuiDrawData&) = delete;

        [[nodiscard]] static std::shared_ptr<OwnedImGuiDrawData>
        Capture(ImDrawData* drawData, std::span<const CapturedSurfaceTextureBinding> surfaces,
                std::span<const std::uintptr_t> retiredTextureIds = {});
        [[nodiscard]] std::shared_ptr<ResolvedImGuiDrawData>
        ResolveForRender(ImGuiTextureCache& cache, std::uint32_t deviceGeneration,
                         const std::function<std::uintptr_t(const RenderSurfaceToken&)>& resolveTexture) const;

      private:
        class Impl;
        explicit OwnedImGuiDrawData(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct RenderFramePacket final
    {
        std::uint64_t Id = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
        std::vector<QueuedSceneRequest> Requests;
        std::vector<RuntimeUiDrawCommand> RuntimeUiCommands;
        std::vector<RenderSurfaceToken> Surfaces;
        RenderSurfaceToken PresentationSurface;
        std::shared_ptr<OwnedImGuiDrawData> EditorUi;
        RenderFrameTimeline Timeline;
        std::chrono::steady_clock::time_point CaptureStarted;
        std::chrono::steady_clock::time_point CapturedAt;
        std::chrono::steady_clock::time_point AcceptedAt;
        std::chrono::steady_clock::time_point RenderStartedAt;
        std::chrono::steady_clock::time_point SubmittedAt;
        std::chrono::steady_clock::time_point PresentedAt;
        std::chrono::steady_clock::time_point RetiredAt;
        float CpuPreparationMilliseconds = 0.0F;
        float CpuPreparationP95Milliseconds = 0.0F;
        RenderStatistics CapturedStatistics;
        bool RetriedAfterDeviceLoss = false;
        std::atomic<bool> CompletionPublished{false};
    };
} // namespace Keire::RenderBackend
