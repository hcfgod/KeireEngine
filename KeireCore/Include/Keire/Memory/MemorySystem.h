#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Keire
{
    class MemorySystem;

    class MemoryDomain final
    {
      public:
        constexpr MemoryDomain() noexcept = default;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] constexpr bool operator==(const MemoryDomain&) const noexcept = default;

      private:
        friend class MemorySystem;

        explicit constexpr MemoryDomain(const std::uint32_t value) noexcept : m_Value(value) {}
        std::uint32_t m_Value = 0;
    };

    struct MemorySystemSpecification
    {
        std::size_t FrameArenaBytes = std::size_t{4} * 1024U * 1024U;
    };

    struct MemoryDomainStatistics
    {
        MemoryDomain Domain;
        MemoryDomain Parent;
        std::string Name;
        std::size_t CurrentBytes = 0;
        std::size_t PeakBytes = 0;
        std::size_t ExternalBytes = 0;
        std::size_t RetainedBytes = 0;
        std::uint64_t AllocationCount = 0;
        std::uint64_t DeallocationCount = 0;
    };

    struct MemorySnapshot
    {
        std::uint64_t Sequence = 0;
        std::vector<MemoryDomainStatistics> Domains;
    };

    class KEIRE_API TrackedMemoryResource final : public std::pmr::memory_resource
    {
      public:
        class Impl;

        explicit TrackedMemoryResource(std::unique_ptr<Impl> implementation);
        ~TrackedMemoryResource() override;

        TrackedMemoryResource(const TrackedMemoryResource&) = delete;
        TrackedMemoryResource& operator=(const TrackedMemoryResource&) = delete;

        [[nodiscard]] MemoryDomain Domain() const noexcept;

      private:
        friend class MemorySystem;

        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API FrameArena final
    {
      public:
        ~FrameArena();

        FrameArena(const FrameArena&) = delete;
        FrameArena& operator=(const FrameArena&) = delete;

        [[nodiscard]] std::pmr::memory_resource* Resource() noexcept;
        [[nodiscard]] std::size_t Capacity() const noexcept;
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;
        void Validate(std::uint64_t generation) const;
        void Reset();

      private:
        friend class MemorySystem;

        class Impl;
        explicit FrameArena(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API MemorySystem final : public RefCounted
    {
      public:
        explicit MemorySystem(MemorySystemSpecification specification = {});
        ~MemorySystem() override;

        MemorySystem(const MemorySystem&) = delete;
        MemorySystem& operator=(const MemorySystem&) = delete;

        [[nodiscard]] MemoryDomain RootDomain() const noexcept;
        [[nodiscard]] MemoryDomain RegisterDomain(std::string_view name, MemoryDomain parent = {});
        [[nodiscard]] std::unique_ptr<TrackedMemoryResource>
        CreateTrackedResource(MemoryDomain domain, std::pmr::memory_resource* upstream = nullptr);
        [[nodiscard]] std::unique_ptr<FrameArena> CreateArena(MemoryDomain domain, std::size_t capacity);
        [[nodiscard]] FrameArena& Frame() noexcept;
        [[nodiscard]] const FrameArena& Frame() const noexcept;
        void ResetFrameArena();
        void ReportExternal(MemoryDomain domain, std::size_t bytes) noexcept;
        [[nodiscard]] MemorySnapshot Snapshot() const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
