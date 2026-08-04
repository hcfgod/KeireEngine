#include "Keire/Memory/MemorySystem.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        struct MemoryDomainRecord final
        {
            MemoryDomain Domain;
            MemoryDomain Parent;
            std::string Name;
            std::atomic<std::size_t> Current{0};
            std::atomic<std::size_t> Peak{0};
            std::atomic<std::size_t> External{0};
            std::atomic<std::uint64_t> Allocations{0};
            std::atomic<std::uint64_t> Deallocations{0};
        };

        struct MemoryState final
        {
            mutable std::mutex Mutex;
            std::vector<std::unique_ptr<MemoryDomainRecord>> Domains;
            std::atomic<std::uint64_t> SnapshotSequence{1};
        };

        [[nodiscard]] MemoryDomainRecord* FindRecord(const std::shared_ptr<MemoryState>& state,
                                                     const MemoryDomain domain) noexcept
        {
            if (!state || !domain.IsValid())
                return nullptr;
            std::scoped_lock lock(state->Mutex);
            const auto index = static_cast<std::size_t>(domain.Value() - 1);
            return index < state->Domains.size() ? state->Domains[index].get() : nullptr;
        }

        void UpdatePeak(MemoryDomainRecord& record, const std::size_t current) noexcept
        {
            auto peak = record.Peak.load(std::memory_order_relaxed);
            while (peak < current && !record.Peak.compare_exchange_weak(peak, current, std::memory_order_relaxed,
                                                                        std::memory_order_relaxed))
            {
            }
        }
    } // namespace

    class TrackedMemoryResource::Impl final
    {
      public:
        Impl(std::shared_ptr<MemoryState> value, const MemoryDomain domain, std::pmr::memory_resource* resource)
            : State(std::move(value)), DomainId(domain), Upstream(resource)
        {
        }

        std::shared_ptr<MemoryState> State;
        MemoryDomain DomainId;
        std::pmr::memory_resource* Upstream = nullptr;
    };

    TrackedMemoryResource::TrackedMemoryResource(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    TrackedMemoryResource::~TrackedMemoryResource() = default;

    MemoryDomain TrackedMemoryResource::Domain() const noexcept { return m_Impl->DomainId; }

    void* TrackedMemoryResource::do_allocate(const std::size_t bytes, const std::size_t alignment)
    {
        void* pointer = m_Impl->Upstream->allocate(bytes, alignment);
        if (auto* record = FindRecord(m_Impl->State, m_Impl->DomainId))
        {
            const auto current = record->Current.fetch_add(bytes, std::memory_order_relaxed) + bytes;
            record->Allocations.fetch_add(1, std::memory_order_relaxed);
            UpdatePeak(*record, current + record->External.load(std::memory_order_relaxed));
        }
        return pointer;
    }

    void TrackedMemoryResource::do_deallocate(void* pointer, const std::size_t bytes, const std::size_t alignment)
    {
        if (auto* record = FindRecord(m_Impl->State, m_Impl->DomainId))
        {
            record->Current.fetch_sub(bytes, std::memory_order_relaxed);
            record->Deallocations.fetch_add(1, std::memory_order_relaxed);
        }
        m_Impl->Upstream->deallocate(pointer, bytes, alignment);
    }

    bool TrackedMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
    {
        return this == std::addressof(other);
    }

    class FrameArena::Impl final
    {
      public:
        Impl(std::shared_ptr<MemoryState> state, const MemoryDomain domain, const std::size_t capacity)
            : Owner(std::this_thread::get_id()),
              Upstream(std::make_unique<TrackedMemoryResource>(std::make_unique<TrackedMemoryResource::Impl>(
                  std::move(state), domain, std::pmr::new_delete_resource()))),
              Buffer(capacity, std::byte{}, Upstream.get()), Arena(Buffer.data(), Buffer.size(), Upstream.get())
        {
        }

        std::thread::id Owner;
        std::unique_ptr<TrackedMemoryResource> Upstream;
        std::pmr::vector<std::byte> Buffer;
        std::pmr::monotonic_buffer_resource Arena;
        std::uint64_t Generation = 1;
    };

    FrameArena::FrameArena(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    FrameArena::~FrameArena() = default;

    std::pmr::memory_resource* FrameArena::Resource() noexcept { return std::addressof(m_Impl->Arena); }

    std::size_t FrameArena::Capacity() const noexcept { return m_Impl->Buffer.size(); }

    std::uint64_t FrameArena::Generation() const noexcept { return m_Impl->Generation; }

    bool FrameArena::IsCurrent(const std::uint64_t generation) const noexcept
    {
        return generation != 0 && generation == m_Impl->Generation;
    }

    void FrameArena::Validate(const std::uint64_t generation) const
    {
        if (!IsCurrent(generation))
            throw std::logic_error("Frame-arena data escaped its safe frame generation.");
    }

    void FrameArena::Reset()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("A frame arena may only be reset by its owner thread.");
        m_Impl->Arena.release();
        ++m_Impl->Generation;
        if (m_Impl->Generation == 0)
            ++m_Impl->Generation;
    }

    class MemorySystem::Impl final
    {
      public:
        explicit Impl(const MemorySystemSpecification& specification) : State(std::make_shared<MemoryState>())
        {
            auto root = std::make_unique<MemoryDomainRecord>();
            root->Domain = MemoryDomain(1);
            root->Name = "Kéire";
            State->Domains.push_back(std::move(root));

            auto frame = std::make_unique<MemoryDomainRecord>();
            frame->Domain = MemoryDomain(2);
            frame->Parent = MemoryDomain(1);
            frame->Name = "Frame";
            State->Domains.push_back(std::move(frame));
            FrameStorage = std::unique_ptr<FrameArena>(new FrameArena(
                std::make_unique<FrameArena::Impl>(State, MemoryDomain(2), specification.FrameArenaBytes)));
        }

        std::shared_ptr<MemoryState> State;
        std::unique_ptr<FrameArena> FrameStorage;
    };

    MemorySystem::MemorySystem(const MemorySystemSpecification specification)
        : m_Impl(std::make_unique<Impl>(specification))
    {
        if (specification.FrameArenaBytes == 0)
            throw std::invalid_argument("The frame arena capacity must be greater than zero.");
    }

    MemorySystem::~MemorySystem() = default;

    MemoryDomain MemorySystem::RootDomain() const noexcept { return MemoryDomain(1); }

    MemoryDomain MemorySystem::RegisterDomain(const std::string_view name, MemoryDomain parent)
    {
        if (name.empty())
            throw std::invalid_argument("A memory domain requires a name.");
        if (!parent)
            parent = RootDomain();
        std::scoped_lock lock(m_Impl->State->Mutex);
        const auto parentIndex = static_cast<std::size_t>(parent.Value() - 1);
        if (parentIndex >= m_Impl->State->Domains.size())
            throw std::invalid_argument("The parent memory domain is not registered.");
        for (const auto& record : m_Impl->State->Domains)
        {
            if (record->Parent == parent && record->Name == name)
                throw std::invalid_argument("A sibling memory domain already uses this name.");
        }
        if (m_Impl->State->Domains.size() >= std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("The memory domain table is full.");
        auto record = std::make_unique<MemoryDomainRecord>();
        record->Domain = MemoryDomain(static_cast<std::uint32_t>(m_Impl->State->Domains.size() + 1));
        record->Parent = parent;
        record->Name = name;
        const auto result = record->Domain;
        m_Impl->State->Domains.push_back(std::move(record));
        return result;
    }

    std::unique_ptr<TrackedMemoryResource> MemorySystem::CreateTrackedResource(const MemoryDomain domain,
                                                                               std::pmr::memory_resource* upstream)
    {
        if (!FindRecord(m_Impl->State, domain))
            throw std::invalid_argument("The memory domain is not registered.");
        if (!upstream)
            upstream = std::pmr::get_default_resource();
        return std::unique_ptr<TrackedMemoryResource>(
            new TrackedMemoryResource(std::make_unique<TrackedMemoryResource::Impl>(m_Impl->State, domain, upstream)));
    }

    std::unique_ptr<FrameArena> MemorySystem::CreateArena(const MemoryDomain domain, const std::size_t capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument("An arena capacity must be greater than zero.");
        if (!FindRecord(m_Impl->State, domain))
            throw std::invalid_argument("The memory domain is not registered.");
        return std::unique_ptr<FrameArena>(
            new FrameArena(std::make_unique<FrameArena::Impl>(m_Impl->State, domain, capacity)));
    }

    FrameArena& MemorySystem::Frame() noexcept { return *m_Impl->FrameStorage; }

    const FrameArena& MemorySystem::Frame() const noexcept { return *m_Impl->FrameStorage; }

    void MemorySystem::ResetFrameArena() { m_Impl->FrameStorage->Reset(); }

    void MemorySystem::ReportExternal(const MemoryDomain domain, const std::size_t bytes) noexcept
    {
        if (auto* record = FindRecord(m_Impl->State, domain))
        {
            record->External.store(bytes, std::memory_order_relaxed);
            UpdatePeak(*record, record->Current.load(std::memory_order_relaxed) + bytes);
        }
    }

    MemorySnapshot MemorySystem::Snapshot() const
    {
        MemorySnapshot result;
        result.Sequence = m_Impl->State->SnapshotSequence.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_Impl->State->Mutex);
        result.Domains.reserve(m_Impl->State->Domains.size());
        for (const auto& record : m_Impl->State->Domains)
        {
            MemoryDomainStatistics statistics;
            statistics.Domain = record->Domain;
            statistics.Parent = record->Parent;
            statistics.Name = record->Name;
            statistics.CurrentBytes = record->Current.load(std::memory_order_relaxed);
            statistics.PeakBytes = record->Peak.load(std::memory_order_relaxed);
            statistics.ExternalBytes = record->External.load(std::memory_order_relaxed);
            statistics.RetainedBytes = statistics.CurrentBytes + statistics.ExternalBytes;
            statistics.AllocationCount = record->Allocations.load(std::memory_order_relaxed);
            statistics.DeallocationCount = record->Deallocations.load(std::memory_order_relaxed);
            result.Domains.push_back(std::move(statistics));
        }
        return result;
    }
} // namespace Keire
