#include "Keire/Streaming/StreamingSystem.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Diagnostics/Diagnostic.h"
#include "Keire/Memory/MemorySystem.h"
#include "KeireInternal/StableHandleTable.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::size_t StreamingClassCount = 5;

        [[nodiscard]] constexpr std::size_t ClassIndex(const StreamingClass value) noexcept
        {
            return static_cast<std::size_t>(value);
        }

        [[nodiscard]] constexpr bool ValidClass(const StreamingClass value) noexcept
        {
            return ClassIndex(value) < StreamingClassCount;
        }

        [[nodiscard]] StreamingClassBudget BudgetFor(const StreamingBudgetSpecification& specification,
                                                     const StreamingClass value)
        {
            switch (value)
            {
            case StreamingClass::General:
                return specification.General;
            case StreamingClass::Texture:
                return specification.Texture;
            case StreamingClass::Mesh:
                return specification.Mesh;
            case StreamingClass::Audio:
                return specification.Audio;
            case StreamingClass::Animation:
                return specification.Animation;
            }
            return {};
        }

        [[nodiscard]] bool IsTerminal(const AssetStreamState state) noexcept
        {
            return state == AssetStreamState::Succeeded || state == AssetStreamState::Failed ||
                   state == AssetStreamState::Cancelled;
        }

        template <typename T> void SaturatingAdd(T& target, const T value) noexcept
        {
            target =
                value > (std::numeric_limits<T>::max)() - target ? (std::numeric_limits<T>::max)() : target + value;
        }

        [[nodiscard]] std::size_t ScaledBudget(const std::size_t bytes, const float scale) noexcept
        {
            if (bytes == 0)
                return 0;
            const auto scaled = static_cast<long double>(bytes) * static_cast<long double>(scale);
            return scaled >= static_cast<long double>((std::numeric_limits<std::size_t>::max)())
                       ? (std::numeric_limits<std::size_t>::max)()
                       : static_cast<std::size_t>(scaled);
        }

        [[nodiscard]] AssetStreamSegment ResolveSegment(const AssetSystem& assets, const AssetId asset,
                                                        const StreamingSegmentKind kind, const std::uint32_t segment)
        {
            const auto layout = assets.TryGetStreamLayout(asset);
            if (!layout)
                throw std::invalid_argument("Streaming request references an unavailable asset.");
            const auto found =
                std::ranges::find_if(layout->Segments, [kind, segment](const AssetStreamSegment& candidate)
                                     { return candidate.Kind == kind && candidate.Segment == segment; });
            if (found != layout->Segments.end())
                return *found;
            if (layout->MonolithicCompatibility)
            {
                const auto fallback = std::ranges::find_if(layout->Segments, [](const AssetStreamSegment& candidate)
                                                           { return candidate.Kind == AssetStreamSegmentKind::Data; });
                if (fallback != layout->Segments.end())
                    return *fallback;
            }
            throw std::out_of_range("Requested streamed asset segment is unavailable.");
        }
    } // namespace

    class StreamingSystem::Impl final
    {
      public:
        struct RequestRecord final
        {
            StreamedAssetRequest Request;
            Ref<AssetStreamOperation> Operation;
            ResidencyState State = ResidencyState::Requested;
            std::vector<std::byte> Data;
            std::chrono::steady_clock::time_point Started;
            std::uint64_t LastTouch = 0;
        };

        Impl(StreamingBudgetSpecification specification, Ref<AssetSystem> assets, Ref<DiagnosticSink> diagnostics,
             Ref<MemorySystem> memory)
            : Specification(specification), Assets(std::move(assets)), Diagnostics(std::move(diagnostics)),
              Memory(std::move(memory)), Owner(std::this_thread::get_id())
        {
            if (!Assets)
                throw std::invalid_argument("StreamingSystem requires an asset system.");
            if (!std::isfinite(Specification.EvictionThreshold) || !std::isfinite(Specification.EvictionTarget) ||
                Specification.EvictionThreshold < 1.0F || Specification.EvictionTarget <= 0.0F ||
                Specification.EvictionTarget >= Specification.EvictionThreshold || Specification.MaximumRequests == 0)
            {
                throw std::invalid_argument("Streaming budget thresholds or request capacity are invalid.");
            }
            for (std::size_t index = 0; index < StreamingClassCount; ++index)
                Statistics[index].Class = static_cast<StreamingClass>(index);
            Requests.Reserve(Specification.MaximumRequests);
            Active.reserve(Specification.MaximumRequests);
            if (Memory)
                MemoryDomain = Memory->RegisterDomain("Streaming");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("StreamingSystem::") + operation + " must run on the owner thread.");
        }

        void UpdateMemory() noexcept
        {
            if (!Memory || !MemoryDomain)
                return;
            std::size_t bytes = 0;
            for (const auto& statistics : Statistics)
                bytes += statistics.ResidentCpuBytes + statistics.InFlightBytes + statistics.RetiredCpuBytes;
            Memory->ReportExternal(MemoryDomain, bytes);
        }

        StreamingBudgetSpecification Specification;
        Ref<AssetSystem> Assets;
        Ref<DiagnosticSink> Diagnostics;
        Ref<MemorySystem> Memory;
        Keire::MemoryDomain MemoryDomain;
        std::thread::id Owner;
        mutable std::mutex Mutex;
        Internal::StableHandleTable<ResidencyRequestTag, RequestRecord> Requests;
        std::vector<ResidencyRequestHandle> Active;
        std::array<StreamingClassStatistics, StreamingClassCount> Statistics{};
        std::atomic_uint64_t AudioUnderruns{0};
        std::uint64_t ReportedAudioUnderruns = 0;
        std::uint64_t Sequence = 0;
        std::atomic_bool Open = true;
    };

    StreamingSystem::StreamingSystem(StreamingBudgetSpecification specification, Ref<AssetSystem> assets,
                                     Ref<DiagnosticSink> diagnostics, Ref<MemorySystem> memory)
        : m_Impl(std::make_unique<Impl>(specification, std::move(assets), std::move(diagnostics), std::move(memory)))
    {
    }

    StreamingSystem::~StreamingSystem() { Close(); }

    ResidencyRequestHandle StreamingSystem::Request(StreamedAssetRequest request)
    {
        m_Impl->RequireOwner("Request");
        std::scoped_lock stateLock(m_Impl->Mutex);
        if (!m_Impl->Open.load(std::memory_order_acquire))
            throw std::logic_error("StreamingSystem is closed.");
        if (!request.Asset || !ValidClass(request.Class) || request.Range.Bytes == 0 ||
            !std::isfinite(request.WindowStartSeconds) || !std::isfinite(request.WindowEndSeconds) ||
            request.WindowStartSeconds < 0.0F || request.WindowEndSeconds < request.WindowStartSeconds)
        {
            throw std::invalid_argument("Streamed asset request is invalid.");
        }
        if (m_Impl->Active.size() >= m_Impl->Specification.MaximumRequests)
            throw std::runtime_error("Streaming request capacity was exhausted.");

        auto operation =
            m_Impl->Assets->ReadRangeAsync(request.Asset, request.Range.Offset, request.Range.Bytes, request.Priority);
        if (!operation)
            throw std::runtime_error("The asset system returned no streaming operation.");
        auto operationGuard = operation;
        const auto requestClass = request.Class;
        const auto bytes = request.Range.Bytes;
        ResidencyRequestHandle handle;
        try
        {
            handle = m_Impl->Requests.Emplace(Impl::RequestRecord{request,
                                                                  std::move(operation),
                                                                  ResidencyState::Loading,
                                                                  {},
                                                                  std::chrono::steady_clock::now(),
                                                                  ++m_Impl->Sequence});
        }
        catch (...)
        {
            operationGuard->Cancel();
            throw;
        }
        m_Impl->Active.push_back(handle);
        auto& statistics = m_Impl->Statistics[ClassIndex(requestClass)];
        SaturatingAdd(statistics.RequestedBytes, bytes);
        SaturatingAdd(statistics.InFlightBytes, bytes);
        SaturatingAdd(statistics.Requests, std::uint64_t{1});
        SaturatingAdd(statistics.Misses, std::uint64_t{1});
        m_Impl->UpdateMemory();
        return handle;
    }

    ResidencyRequestHandle StreamingSystem::RequestTextureMip(const AssetId asset, const std::uint32_t mip,
                                                              const StreamingRange range, const AssetPriority priority,
                                                              const bool pinned)
    {
        return Request({asset, StreamingClass::Texture, StreamingSegmentKind::TextureMip, range, priority, mip, 0.0F,
                        0.0F, pinned});
    }

    ResidencyRequestHandle StreamingSystem::RequestTextureMip(const AssetId asset, const std::uint32_t mip,
                                                              const AssetPriority priority, const bool pinned)
    {
        m_Impl->RequireOwner("RequestTextureMip");
        const auto segment = ResolveSegment(*m_Impl->Assets, asset, StreamingSegmentKind::TextureMip, mip);
        return RequestTextureMip(asset, mip, {segment.Offset, segment.Bytes, segment.Bytes}, priority, pinned);
    }

    ResidencyRequestHandle StreamingSystem::RequestMeshLod(const AssetId asset, const std::uint32_t lod,
                                                           const StreamingRange range, const AssetPriority priority,
                                                           const bool pinned)
    {
        return Request(
            {asset, StreamingClass::Mesh, StreamingSegmentKind::MeshLod, range, priority, lod, 0.0F, 0.0F, pinned});
    }

    ResidencyRequestHandle StreamingSystem::RequestMeshLod(const AssetId asset, const std::uint32_t lod,
                                                           const AssetPriority priority, const bool pinned)
    {
        m_Impl->RequireOwner("RequestMeshLod");
        const auto segment = ResolveSegment(*m_Impl->Assets, asset, StreamingSegmentKind::MeshLod, lod);
        return RequestMeshLod(asset, lod, {segment.Offset, segment.Bytes, segment.Bytes}, priority, pinned);
    }

    ResidencyRequestHandle StreamingSystem::RequestAudioPage(const AssetId asset, const std::uint32_t page,
                                                             const StreamingRange range, const AssetPriority priority,
                                                             const bool pinned)
    {
        return Request(
            {asset, StreamingClass::Audio, StreamingSegmentKind::AudioPage, range, priority, page, 0.0F, 0.0F, pinned});
    }

    ResidencyRequestHandle StreamingSystem::RequestAudioPage(const AssetId asset, const std::uint32_t page,
                                                             const AssetPriority priority, const bool pinned)
    {
        m_Impl->RequireOwner("RequestAudioPage");
        const auto segment = ResolveSegment(*m_Impl->Assets, asset, StreamingSegmentKind::AudioPage, page);
        return Request({asset,
                        StreamingClass::Audio,
                        StreamingSegmentKind::AudioPage,
                        {segment.Offset, segment.Bytes, 0},
                        priority,
                        page,
                        segment.WindowStartSeconds,
                        segment.WindowEndSeconds,
                        pinned});
    }

    ResidencyRequestHandle StreamingSystem::RequestAnimationWindow(const AssetId asset, const float startSeconds,
                                                                   const float endSeconds, const std::uint32_t window,
                                                                   const StreamingRange range,
                                                                   const AssetPriority priority, const bool pinned)
    {
        return Request({asset, StreamingClass::Animation, StreamingSegmentKind::AnimationWindow, range, priority,
                        window, startSeconds, endSeconds, pinned});
    }

    ResidencyRequestHandle StreamingSystem::RequestAnimationWindow(const AssetId asset, const std::uint32_t window,
                                                                   const AssetPriority priority, const bool pinned)
    {
        m_Impl->RequireOwner("RequestAnimationWindow");
        const auto segment = ResolveSegment(*m_Impl->Assets, asset, StreamingSegmentKind::AnimationWindow, window);
        return RequestAnimationWindow(asset, segment.WindowStartSeconds, segment.WindowEndSeconds, window,
                                      {segment.Offset, segment.Bytes, 0}, priority, pinned);
    }

    bool StreamingSystem::Cancel(const ResidencyRequestHandle handle) noexcept
    {
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            bool cancelled = false;
            (void)m_Impl->Requests.With(
                handle,
                [&](Impl::RequestRecord& record)
                {
                    if (record.State == ResidencyState::Requested || record.State == ResidencyState::Loading)
                    {
                        record.Operation->Cancel();
                        record.Operation.Reset();
                        record.State = ResidencyState::Cancelled;
                        auto& statistics = m_Impl->Statistics[ClassIndex(record.Request.Class)];
                        statistics.InFlightBytes -= std::min(statistics.InFlightBytes, record.Request.Range.Bytes);
                        statistics.RequestedBytes -= std::min(statistics.RequestedBytes, record.Request.Range.Bytes);
                        SaturatingAdd(statistics.CancelledRequests, std::uint64_t{1});
                        cancelled = true;
                    }
                });
            m_Impl->UpdateMemory();
            return cancelled;
        }
        catch (...)
        {
            return false;
        }
    }

    bool StreamingSystem::Release(const ResidencyRequestHandle handle) noexcept
    {
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            (void)m_Impl->Requests.With(
                handle,
                [&](Impl::RequestRecord& record)
                {
                    auto& statistics = m_Impl->Statistics[ClassIndex(record.Request.Class)];
                    if (record.State == ResidencyState::Requested || record.State == ResidencyState::Loading)
                    {
                        record.Operation->Cancel();
                        record.Operation.Reset();
                        record.State = ResidencyState::Cancelled;
                        statistics.InFlightBytes -= std::min(statistics.InFlightBytes, record.Request.Range.Bytes);
                        statistics.RequestedBytes -= std::min(statistics.RequestedBytes, record.Request.Range.Bytes);
                        SaturatingAdd(statistics.CancelledRequests, std::uint64_t{1});
                    }
                    else if (record.State == ResidencyState::Resident)
                    {
                        statistics.ResidentCpuBytes -= std::min(statistics.ResidentCpuBytes, record.Data.size());
                        statistics.ResidentGpuBytes -=
                            std::min(statistics.ResidentGpuBytes, record.Request.Range.EstimatedGpuBytes);
                        statistics.RequestedBytes -= std::min(statistics.RequestedBytes, record.Request.Range.Bytes);
                    }
                });
            std::erase(m_Impl->Active, handle);
            const auto erased = m_Impl->Requests.Erase(handle);
            m_Impl->UpdateMemory();
            return erased;
        }
        catch (...)
        {
            return false;
        }
    }

    bool StreamingSystem::SetPinned(const ResidencyRequestHandle handle, const bool pinned) noexcept
    {
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            return m_Impl->Requests.With(handle,
                                         [pinned](Impl::RequestRecord& record) { record.Request.Pinned = pinned; });
        }
        catch (...)
        {
            return false;
        }
    }

    bool StreamingSystem::Touch(const ResidencyRequestHandle handle) noexcept
    {
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            return m_Impl->Requests.With(handle,
                                         [&](Impl::RequestRecord& record) { record.LastTouch = ++m_Impl->Sequence; });
        }
        catch (...)
        {
            return false;
        }
    }

    ResidencySnapshot StreamingSystem::Snapshot(const ResidencyRequestHandle handle) const
    {
        std::scoped_lock stateLock(m_Impl->Mutex);
        ResidencySnapshot result;
        if (!m_Impl->Requests.With(
                handle,
                [&](const Impl::RequestRecord& record)
                {
                    result = {handle,
                              record.Request.Asset,
                              record.Request.Class,
                              record.Request.Kind,
                              record.State,
                              record.Request.Segment,
                              record.Data.size(),
                              record.State == ResidencyState::Resident ? record.Request.Range.EstimatedGpuBytes : 0,
                              record.Request.Pinned};
                }))
        {
            throw std::invalid_argument("Streaming residency handle is stale or foreign.");
        }
        return result;
    }

    std::vector<std::byte> StreamingSystem::ResidentData(const ResidencyRequestHandle handle) const
    {
        std::scoped_lock stateLock(m_Impl->Mutex);
        std::vector<std::byte> result;
        if (!m_Impl->Requests.With(handle, [&](const Impl::RequestRecord& record) { result = record.Data; }))
            throw std::invalid_argument("Streaming residency handle is stale or foreign.");
        return result;
    }

    std::size_t StreamingSystem::Pump()
    {
        m_Impl->RequireOwner("Pump");
        std::size_t completed = 0;
        bool reportUnderrun = false;
        Ref<DiagnosticSink> diagnostics;
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            if (!m_Impl->Open.load(std::memory_order_acquire))
                return 0;
            for (const auto handle : m_Impl->Active)
            {
                (void)m_Impl->Requests.With(
                    handle,
                    [&](Impl::RequestRecord& record)
                    {
                        if (record.State != ResidencyState::Loading)
                            return;
                        const auto operationState = record.Operation->State();
                        if (!IsTerminal(operationState))
                            return;
                        std::vector<std::byte> result;
                        if (operationState == AssetStreamState::Succeeded)
                            result = record.Operation->Result();
                        auto& statistics = m_Impl->Statistics[ClassIndex(record.Request.Class)];
                        statistics.InFlightBytes -= std::min(statistics.InFlightBytes, record.Request.Range.Bytes);
                        record.Operation.Reset();
                        if (operationState == AssetStreamState::Succeeded)
                        {
                            record.Data = std::move(result);
                            record.State = ResidencyState::Resident;
                            SaturatingAdd(statistics.ResidentCpuBytes, record.Data.size());
                            SaturatingAdd(statistics.ResidentGpuBytes, record.Request.Range.EstimatedGpuBytes);
                            SaturatingAdd(statistics.CompletedRequests, std::uint64_t{1});
                            const auto elapsed = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - record.Started);
                            statistics.AverageLatencyMilliseconds +=
                                (elapsed.count() - statistics.AverageLatencyMilliseconds) /
                                static_cast<double>(statistics.CompletedRequests);
                        }
                        else
                        {
                            record.State = operationState == AssetStreamState::Cancelled ? ResidencyState::Cancelled
                                                                                         : ResidencyState::Failed;
                            statistics.RequestedBytes -=
                                std::min(statistics.RequestedBytes, record.Request.Range.Bytes);
                            if (record.State == ResidencyState::Failed)
                                SaturatingAdd(statistics.Failures, std::uint64_t{1});
                            else
                                SaturatingAdd(statistics.CancelledRequests, std::uint64_t{1});
                        }
                        ++completed;
                    });
            }

            const auto underruns = m_Impl->AudioUnderruns.load(std::memory_order_relaxed);
            if (underruns != m_Impl->ReportedAudioUnderruns)
            {
                m_Impl->Statistics[ClassIndex(StreamingClass::Audio)].AudioUnderruns = underruns;
                m_Impl->ReportedAudioUnderruns = underruns;
                reportUnderrun = true;
                diagnostics = m_Impl->Diagnostics;
            }
            m_Impl->UpdateMemory();
        }
        if (reportUnderrun && diagnostics)
        {
            try
            {
                diagnostics->Report({DiagnosticId("KEIRE-AUDIO-0001"), DiagnosticSeverity::Warning,
                                     "Audio streaming underrun; silence was emitted while preserving the logical "
                                     "sample cursor."});
            }
            catch (...)
            {
            }
        }
        completed += EvictToBudgets();
        return completed;
    }

    std::size_t StreamingSystem::EvictToBudgets()
    {
        m_Impl->RequireOwner("EvictToBudgets");
        std::scoped_lock stateLock(m_Impl->Mutex);
        if (!m_Impl->Open.load(std::memory_order_acquire))
            return 0;
        std::size_t evicted = 0;
        for (std::size_t classIndex = 0; classIndex < StreamingClassCount; ++classIndex)
        {
            const auto assetClass = static_cast<StreamingClass>(classIndex);
            const auto budget = BudgetFor(m_Impl->Specification, assetClass);
            auto& statistics = m_Impl->Statistics[classIndex];
            const auto cpuThreshold = ScaledBudget(budget.CpuBytes, m_Impl->Specification.EvictionThreshold);
            const auto gpuThreshold = ScaledBudget(budget.GpuBytes, m_Impl->Specification.EvictionThreshold);
            const bool cpuExceeded = budget.CpuBytes != 0 && statistics.ResidentCpuBytes > cpuThreshold;
            const bool gpuExceeded = budget.GpuBytes != 0 && statistics.ResidentGpuBytes > gpuThreshold;
            if (!cpuExceeded && !gpuExceeded)
                continue;
            const auto cpuTarget = ScaledBudget(budget.CpuBytes, m_Impl->Specification.EvictionTarget);
            const auto gpuTarget = ScaledBudget(budget.GpuBytes, m_Impl->Specification.EvictionTarget);
            std::vector<std::pair<std::uint64_t, ResidencyRequestHandle>> candidates;
            candidates.reserve(m_Impl->Active.size());
            for (const auto handle : m_Impl->Active)
            {
                (void)m_Impl->Requests.With(handle,
                                            [&](const Impl::RequestRecord& record)
                                            {
                                                if (record.Request.Class == assetClass &&
                                                    record.State == ResidencyState::Resident && !record.Request.Pinned)
                                                    candidates.emplace_back(record.LastTouch, handle);
                                            });
            }
            std::ranges::sort(candidates);
            for (const auto& [lastTouch, handle] : candidates)
            {
                (void)lastTouch;
                if ((budget.CpuBytes == 0 || statistics.ResidentCpuBytes <= cpuTarget) &&
                    (budget.GpuBytes == 0 || statistics.ResidentGpuBytes <= gpuTarget))
                    break;
                (void)m_Impl->Requests.With(
                    handle,
                    [&](Impl::RequestRecord& record)
                    {
                        statistics.ResidentCpuBytes -= std::min(statistics.ResidentCpuBytes, record.Data.size());
                        statistics.ResidentGpuBytes -=
                            std::min(statistics.ResidentGpuBytes, record.Request.Range.EstimatedGpuBytes);
                        statistics.RequestedBytes -= std::min(statistics.RequestedBytes, record.Request.Range.Bytes);
                        std::vector<std::byte>().swap(record.Data);
                        record.State = ResidencyState::Evicted;
                        SaturatingAdd(statistics.Evictions, std::uint64_t{1});
                        ++evicted;
                    });
            }
        }
        m_Impl->UpdateMemory();
        return evicted;
    }

    std::vector<StreamingClassStatistics> StreamingSystem::Statistics() const
    {
        std::scoped_lock stateLock(m_Impl->Mutex);
        auto result = std::vector<StreamingClassStatistics>(m_Impl->Statistics.begin(), m_Impl->Statistics.end());
        result[ClassIndex(StreamingClass::Audio)].AudioUnderruns =
            m_Impl->AudioUnderruns.load(std::memory_order_relaxed);
        return result;
    }

    void StreamingSystem::ReportRetired(const StreamingClass assetClass, const std::size_t cpuBytes,
                                        const std::size_t gpuBytes) noexcept
    {
        if (!ValidClass(assetClass))
            return;
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            if (!m_Impl->Open.load(std::memory_order_acquire))
                return;
            auto& statistics = m_Impl->Statistics[ClassIndex(assetClass)];
            SaturatingAdd(statistics.RetiredCpuBytes, cpuBytes);
            SaturatingAdd(statistics.RetiredGpuBytes, gpuBytes);
            m_Impl->UpdateMemory();
        }
        catch (...)
        {
        }
    }

    void StreamingSystem::ReleaseRetired(const StreamingClass assetClass, const std::size_t cpuBytes,
                                         const std::size_t gpuBytes) noexcept
    {
        if (!ValidClass(assetClass))
            return;
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            if (!m_Impl->Open.load(std::memory_order_acquire))
                return;
            auto& statistics = m_Impl->Statistics[ClassIndex(assetClass)];
            statistics.RetiredCpuBytes -= std::min(statistics.RetiredCpuBytes, cpuBytes);
            statistics.RetiredGpuBytes -= std::min(statistics.RetiredGpuBytes, gpuBytes);
            m_Impl->UpdateMemory();
        }
        catch (...)
        {
        }
    }

    void StreamingSystem::ReportAudioUnderrun() noexcept
    {
        if (m_Impl && m_Impl->Open.load(std::memory_order_acquire))
            m_Impl->AudioUnderruns.fetch_add(1, std::memory_order_relaxed);
    }

    bool StreamingSystem::IsOpen() const noexcept { return m_Impl && m_Impl->Open.load(std::memory_order_acquire); }

    void StreamingSystem::Close() noexcept
    {
        if (!m_Impl)
            return;
        try
        {
            std::scoped_lock stateLock(m_Impl->Mutex);
            if (!m_Impl->Open.exchange(false, std::memory_order_acq_rel))
                return;
            for (const auto handle : m_Impl->Active)
            {
                (void)m_Impl->Requests.With(
                    handle,
                    [&](Impl::RequestRecord& record)
                    {
                        if (record.State == ResidencyState::Requested || record.State == ResidencyState::Loading)
                        {
                            auto& statistics = m_Impl->Statistics[ClassIndex(record.Request.Class)];
                            SaturatingAdd(statistics.CancelledRequests, std::uint64_t{1});
                        }
                        if (record.Operation)
                            record.Operation->Cancel();
                        record.Operation.Reset();
                    });
                (void)m_Impl->Requests.Erase(handle);
            }
            m_Impl->Active.clear();
            for (auto& statistics : m_Impl->Statistics)
            {
                statistics.RequestedBytes = 0;
                statistics.InFlightBytes = 0;
                statistics.ResidentCpuBytes = 0;
                statistics.ResidentGpuBytes = 0;
                statistics.RetiredCpuBytes = 0;
                statistics.RetiredGpuBytes = 0;
            }
            m_Impl->UpdateMemory();
        }
        catch (...)
        {
        }
    }
} // namespace Keire
