#pragma once

#include "Keire/Ref.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    class FrameUsage final : public RefCounted
    {
      public:
        std::atomic<std::uint64_t> Attachments{0};
    };

    // Owns only a usage counter, not the frame registry. Safe to release after the world has closed.
    class FrameAttachment final
    {
      public:
        FrameAttachment() = default;

        ~FrameAttachment() { Reset(); }

        FrameAttachment(const FrameAttachment&) = delete;
        FrameAttachment& operator=(const FrameAttachment&) = delete;

        FrameAttachment(FrameAttachment&& other) noexcept : m_Usage(std::move(other.m_Usage)) {}

        FrameAttachment& operator=(FrameAttachment&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Usage = std::move(other.m_Usage);
            }
            return *this;
        }

        void Reset() noexcept
        {
            if (m_Usage)
            {
                m_Usage->Attachments.fetch_sub(1, std::memory_order_release);
                m_Usage = {};
            }
        }

      private:
        friend class FrameRegistry;
        explicit FrameAttachment(Ref<FrameUsage> usage) : m_Usage(std::move(usage))
        {
            auto count = m_Usage->Attachments.load(std::memory_order_relaxed);
            do
            {
                if (count == std::numeric_limits<std::uint64_t>::max())
                {
                    throw std::overflow_error("Frame attachment count exhausted.");
                }
            } while (!m_Usage->Attachments.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel,
                                                                 std::memory_order_relaxed));
        }

        Ref<FrameUsage> m_Usage;
    };
} // namespace Keire::Detail
