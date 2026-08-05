#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Ref.h"
#include "Keire/StableHandle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire
{
    class AssetSystem;
    class DiagnosticSink;
    class MemorySystem;

    enum class StreamingClass : std::uint8_t
    {
        General,
        Texture,
        Mesh,
        Audio,
        Animation
    };

    using StreamingSegmentKind = AssetStreamSegmentKind;

    enum class ResidencyState : std::uint8_t
    {
        Requested,
        Loading,
        Resident,
        Evicted,
        Failed,
        Cancelled
    };

    struct StreamingClassBudget
    {
        std::size_t CpuBytes = 0;
        std::size_t GpuBytes = 0;
    };

    struct StreamingBudgetSpecification
    {
        StreamingClassBudget General{std::size_t{256} * 1024U * 1024U, 0};
        StreamingClassBudget Texture{std::size_t{64} * 1024U * 1024U, std::size_t{512} * 1024U * 1024U};
        StreamingClassBudget Mesh{std::size_t{96} * 1024U * 1024U, std::size_t{256} * 1024U * 1024U};
        StreamingClassBudget Audio{std::size_t{128} * 1024U * 1024U, 0};
        StreamingClassBudget Animation{std::size_t{96} * 1024U * 1024U, 0};
        float EvictionThreshold = 1.0F;
        float EvictionTarget = 0.9F;
        std::size_t MaximumRequests = 16384;
    };

    struct StreamingRange
    {
        std::uint64_t Offset = 0;
        std::size_t Bytes = 0;
        std::size_t EstimatedGpuBytes = 0;
    };

    struct StreamedAssetRequest
    {
        AssetId Asset;
        StreamingClass Class = StreamingClass::General;
        StreamingSegmentKind Kind = StreamingSegmentKind::Data;
        StreamingRange Range;
        AssetPriority Priority = AssetPriority::Normal;
        std::uint32_t Segment = 0;
        float WindowStartSeconds = 0.0F;
        float WindowEndSeconds = 0.0F;
        bool Pinned = false;
    };

    struct ResidencyRequestTag;
    using ResidencyRequestHandle = StableHandle<ResidencyRequestTag>;

    struct ResidencySnapshot
    {
        ResidencyRequestHandle Handle;
        AssetId Asset;
        StreamingClass Class = StreamingClass::General;
        StreamingSegmentKind Kind = StreamingSegmentKind::Data;
        ResidencyState State = ResidencyState::Requested;
        std::uint32_t Segment = 0;
        std::size_t CpuBytes = 0;
        std::size_t GpuBytes = 0;
        bool Pinned = false;
    };

    struct StreamingClassStatistics
    {
        StreamingClass Class = StreamingClass::General;
        std::size_t RequestedBytes = 0;
        std::size_t InFlightBytes = 0;
        std::size_t ResidentCpuBytes = 0;
        std::size_t ResidentGpuBytes = 0;
        std::size_t RetiredCpuBytes = 0;
        std::size_t RetiredGpuBytes = 0;
        std::uint64_t Requests = 0;
        std::uint64_t CompletedRequests = 0;
        std::uint64_t CancelledRequests = 0;
        std::uint64_t Misses = 0;
        std::uint64_t Evictions = 0;
        std::uint64_t Failures = 0;
        std::uint64_t AudioUnderruns = 0;
        double AverageLatencyMilliseconds = 0.0;
    };

    class KEIRE_API StreamingSystem final : public RefCounted
    {
      public:
        StreamingSystem(StreamingBudgetSpecification specification, Ref<AssetSystem> assets,
                        Ref<DiagnosticSink> diagnostics = {}, Ref<MemorySystem> memory = {});
        ~StreamingSystem() override;

        StreamingSystem(const StreamingSystem&) = delete;
        StreamingSystem& operator=(const StreamingSystem&) = delete;

        [[nodiscard]] ResidencyRequestHandle Request(StreamedAssetRequest request);
        [[nodiscard]] ResidencyRequestHandle RequestTextureMip(AssetId asset, std::uint32_t mip, StreamingRange range,
                                                               AssetPriority priority = AssetPriority::Normal,
                                                               bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestTextureMip(AssetId asset, std::uint32_t mip,
                                                               AssetPriority priority = AssetPriority::Normal,
                                                               bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestMeshLod(AssetId asset, std::uint32_t lod, StreamingRange range,
                                                            AssetPriority priority = AssetPriority::Normal,
                                                            bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestMeshLod(AssetId asset, std::uint32_t lod,
                                                            AssetPriority priority = AssetPriority::Normal,
                                                            bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestAudioPage(AssetId asset, std::uint32_t page, StreamingRange range,
                                                              AssetPriority priority = AssetPriority::High,
                                                              bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestAudioPage(AssetId asset, std::uint32_t page,
                                                              AssetPriority priority = AssetPriority::High,
                                                              bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestAnimationWindow(AssetId asset, float startSeconds, float endSeconds,
                                                                    std::uint32_t window, StreamingRange range,
                                                                    AssetPriority priority = AssetPriority::Normal,
                                                                    bool pinned = false);
        [[nodiscard]] ResidencyRequestHandle RequestAnimationWindow(AssetId asset, std::uint32_t window,
                                                                    AssetPriority priority = AssetPriority::Normal,
                                                                    bool pinned = false);
        [[nodiscard]] bool Cancel(ResidencyRequestHandle handle) noexcept;
        [[nodiscard]] bool Release(ResidencyRequestHandle handle) noexcept;
        [[nodiscard]] bool SetPinned(ResidencyRequestHandle handle, bool pinned) noexcept;
        [[nodiscard]] bool Touch(ResidencyRequestHandle handle) noexcept;
        [[nodiscard]] ResidencySnapshot Snapshot(ResidencyRequestHandle handle) const;
        [[nodiscard]] std::vector<std::byte> ResidentData(ResidencyRequestHandle handle) const;
        [[nodiscard]] std::size_t Pump();
        [[nodiscard]] std::size_t EvictToBudgets();
        [[nodiscard]] std::vector<StreamingClassStatistics> Statistics() const;
        void ReportRetired(StreamingClass assetClass, std::size_t cpuBytes, std::size_t gpuBytes) noexcept;
        void ReleaseRetired(StreamingClass assetClass, std::size_t cpuBytes, std::size_t gpuBytes) noexcept;
        void ReportAudioUnderrun() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
