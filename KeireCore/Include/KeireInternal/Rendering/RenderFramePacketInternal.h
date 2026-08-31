#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Ui/RuntimeUi.h"
#include "Keire/Ui/UiFontAssets.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

struct ImDrawData;
struct SDL_GPUDevice;

namespace Keire
{
    class Texture2DAsset;
}

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

    struct PendingRuntimeUiSubmission final
    {
        RuntimeUiRenderSubmission Submission;
        RenderSurfaceToken Surface;
        std::uint64_t Sequence = 0;
    };

    struct CapturedRuntimeUiWorldPanel final
    {
        std::vector<RuntimeUiDrawCommand> Commands;
        RenderSurfaceToken Surface;
        Matrix4 World;
        Matrix4 ViewProjection;
        Vector2 Viewport;
        Vector2 ReferenceResolution;
        Vector2 Pivot;
        Vector2 WorldUnitsPerPixel;
        float LayoutScale = 1.0F;
        std::int32_t SortingOrder = 0;
        std::uint64_t Sequence = 0;
        bool DepthTest = true;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
    };

    struct CapturedRuntimeUiCameraPanel final
    {
        std::vector<RuntimeUiDrawCommand> Commands;
        RenderSurfaceToken Surface;
        Vector2 Viewport;
        std::int32_t SortingOrder = 0;
        std::uint64_t Sequence = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
    };

    struct CapturedRuntimeUiRenderTexture final
    {
        std::vector<RuntimeUiDrawCommand> Commands;
        AssetId Target;
        Vector2 ReferenceResolution;
        std::int32_t SortingOrder = 0;
        std::uint64_t Sequence = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
    };

    struct CapturedRuntimeUiImageLease final
    {
        AssetId Asset;
        AssetHandle<Texture2DAsset> Handle;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
    };

    struct CapturedRuntimeUiFontLease final
    {
        AssetId Font;
        std::shared_ptr<const RuntimeUiGlyphAtlasCpuData> Atlas;
        AssetHandle<UiFontFamilyAsset> Family;
        AssetHandle<UiFontFaceAsset> Face;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
    };

    struct RuntimeUiTextureRun final
    {
        AssetId Asset;
        RuntimeUiRect ClipRect;
        std::size_t FirstCommand = 0;
        std::size_t CommandCount = 0;
    };

    [[nodiscard]] inline bool IsRuntimeUiDrawable(const RuntimeUiDrawCommand& command) noexcept
    {
        return command.Type == RuntimeUiDrawType::Quad || command.Type == RuntimeUiDrawType::Image ||
               command.Type == RuntimeUiDrawType::Text;
    }

    [[nodiscard]] inline AssetId RuntimeUiTextureAsset(const RuntimeUiDrawCommand& command) noexcept
    {
        if (command.Type == RuntimeUiDrawType::Image)
            return command.RenderTexture ? command.RenderTexture : command.Asset;
        return command.Type == RuntimeUiDrawType::Text
                   ? (command.PreparedFontBinding ? command.PreparedFontBinding : RuntimeUiFontBindingId(command.Asset))
                   : AssetId{};
    }

    [[nodiscard]] inline std::vector<RuntimeUiTextureRun>
    BuildRuntimeUiTextureRuns(const std::span<const RuntimeUiDrawCommand> commands)
    {
        std::vector<RuntimeUiTextureRun> result;
        result.reserve(commands.size());
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            const auto& command = commands[index];
            if (!IsRuntimeUiDrawable(command))
                continue;
            const auto asset = RuntimeUiTextureAsset(command);
            if (!result.empty() && result.back().Asset == asset && result.back().ClipRect == command.ClipRect)
            {
                result.back().CommandCount = index + 1U - result.back().FirstCommand;
                continue;
            }
            result.push_back({asset, command.ClipRect, index, 1U});
        }
        return result;
    }

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
        RenderFramePacket();
        ~RenderFramePacket();

        std::uint64_t Id = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
        std::vector<QueuedSceneRequest> Requests;
        std::vector<RuntimeUiDrawCommand> RuntimeUiCommands;
        std::vector<CapturedRuntimeUiCameraPanel> RuntimeUiCameraPanels;
        std::vector<CapturedRuntimeUiWorldPanel> RuntimeUiWorldPanels;
        std::vector<CapturedRuntimeUiRenderTexture> RuntimeUiRenderTextures;
        std::vector<CapturedRuntimeUiImageLease> RuntimeUiImageLeases;
        std::vector<CapturedRuntimeUiFontLease> RuntimeUiFontLeases;
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

    [[nodiscard]] inline bool RuntimeUiWorldPanelOwnershipValid(const CapturedRuntimeUiWorldPanel& panel,
                                                                const RenderFramePacket& frame) noexcept
    {
        return panel.Surface && panel.DeviceGeneration == frame.DeviceGeneration && panel.FrameSlot == frame.FrameSlot;
    }

    inline void QualifyRuntimeUiWorldPanels(RenderFramePacket& frame) noexcept
    {
        for (auto& panel : frame.RuntimeUiWorldPanels)
        {
            panel.DeviceGeneration = frame.DeviceGeneration;
            panel.FrameSlot = frame.FrameSlot;
        }
    }

    [[nodiscard]] inline bool RuntimeUiCameraPanelOwnershipValid(const CapturedRuntimeUiCameraPanel& panel,
                                                                 const RenderFramePacket& frame) noexcept
    {
        return panel.Surface && panel.DeviceGeneration == frame.DeviceGeneration && panel.FrameSlot == frame.FrameSlot;
    }

    inline void QualifyRuntimeUiCameraPanels(RenderFramePacket& frame) noexcept
    {
        for (auto& panel : frame.RuntimeUiCameraPanels)
        {
            panel.DeviceGeneration = frame.DeviceGeneration;
            panel.FrameSlot = frame.FrameSlot;
        }
    }

    [[nodiscard]] inline bool RuntimeUiRenderTextureOwnershipValid(const CapturedRuntimeUiRenderTexture& target,
                                                                   const RenderFramePacket& frame) noexcept
    {
        return target.Target && target.DeviceGeneration == frame.DeviceGeneration &&
               target.FrameSlot == frame.FrameSlot;
    }

    inline void QualifyRuntimeUiRenderTextures(RenderFramePacket& frame) noexcept
    {
        for (auto& target : frame.RuntimeUiRenderTextures)
        {
            target.DeviceGeneration = frame.DeviceGeneration;
            target.FrameSlot = frame.FrameSlot;
        }
    }

    [[nodiscard]] inline bool RuntimeUiRenderTextureProduced(const RenderFramePacket& frame,
                                                             const AssetId asset) noexcept
    {
        return asset && std::ranges::any_of(frame.RuntimeUiRenderTextures,
                                            [asset](const auto& target) { return target.Target == asset; });
    }

    [[nodiscard]] inline std::vector<AssetId> BuildRuntimeUiRenderTextureOrder(const RenderFramePacket& frame)
    {
        std::vector<AssetId> targets;
        for (const auto& panel : frame.RuntimeUiRenderTextures)
        {
            if (!panel.Target || std::ranges::find(targets, panel.Target) != targets.end())
                continue;
            targets.push_back(panel.Target);
        }

        enum class VisitState : std::uint8_t
        {
            Unvisited,
            Visiting,
            Visited
        };
        std::vector<VisitState> states(targets.size());
        std::vector<AssetId> result;
        result.reserve(targets.size());
        const auto visit = [&](const auto& self, const std::size_t index) -> void
        {
            if (states[index] == VisitState::Visited)
                return;
            if (states[index] == VisitState::Visiting)
            {
                throw std::logic_error(
                    "Runtime UI RenderTexture targets contain a same-frame sampling dependency cycle.");
            }
            states[index] = VisitState::Visiting;
            const auto target = targets[index];
            for (const auto& panel : frame.RuntimeUiRenderTextures)
            {
                if (panel.Target != target)
                    continue;
                for (const auto& command : panel.Commands)
                {
                    const auto dependency = command.RenderTexture;
                    const auto found = std::ranges::find(targets, dependency);
                    if (found != targets.end())
                        self(self, static_cast<std::size_t>(std::distance(targets.begin(), found)));
                }
            }
            states[index] = VisitState::Visited;
            result.push_back(target);
        };
        for (std::size_t index = 0; index < targets.size(); ++index)
            visit(visit, index);
        return result;
    }

    [[nodiscard]] inline bool RuntimeUiImageLeaseOwnershipValid(const CapturedRuntimeUiImageLease& lease,
                                                                const RenderFramePacket& frame) noexcept
    {
        return lease.Asset && lease.DeviceGeneration == frame.DeviceGeneration && lease.FrameSlot == frame.FrameSlot;
    }

    inline void QualifyRuntimeUiImageLeases(RenderFramePacket& frame) noexcept
    {
        for (auto& lease : frame.RuntimeUiImageLeases)
        {
            lease.DeviceGeneration = frame.DeviceGeneration;
            lease.FrameSlot = frame.FrameSlot;
        }
    }

    [[nodiscard]] inline bool RuntimeUiFontLeaseOwnershipValid(const CapturedRuntimeUiFontLease& lease,
                                                               const RenderFramePacket& frame) noexcept
    {
        return lease.Font && lease.Atlas && lease.DeviceGeneration == frame.DeviceGeneration &&
               lease.FrameSlot == frame.FrameSlot;
    }

    inline void QualifyRuntimeUiFontLeases(RenderFramePacket& frame) noexcept
    {
        for (auto& lease : frame.RuntimeUiFontLeases)
        {
            lease.DeviceGeneration = frame.DeviceGeneration;
            lease.FrameSlot = frame.FrameSlot;
        }
    }

    [[nodiscard]] inline const CapturedRuntimeUiImageLease* FindRuntimeUiImageLease(const RenderFramePacket& frame,
                                                                                    const AssetId asset) noexcept
    {
        for (const auto& lease : frame.RuntimeUiImageLeases)
        {
            if (lease.Asset == asset)
                return &lease;
        }
        return nullptr;
    }

    [[nodiscard]] inline const CapturedRuntimeUiFontLease* FindRuntimeUiFontLease(const RenderFramePacket& frame,
                                                                                  const AssetId font) noexcept
    {
        for (const auto& lease : frame.RuntimeUiFontLeases)
        {
            if (lease.Font == font)
                return &lease;
        }
        return nullptr;
    }
} // namespace Keire::RenderBackend
