#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace KeireRuntime
{
    struct RuntimeBenchmarkGpuSample final
    {
        std::uint64_t Frame = 0;
        float Milliseconds = 0.0F;
    };

    class RuntimeBenchmarkGpuObservations final
    {
      public:
        explicit RuntimeBenchmarkGpuObservations(const std::size_t capacity) { m_Samples.reserve(capacity); }

        void Observe(const bool supported, const std::uint64_t frame, const float milliseconds,
                     const std::uint64_t firstFrame, const std::uint64_t lastFrame)
        {
            if (!supported || frame == 0 || frame < firstFrame || frame > lastFrame ||
                (!m_Samples.empty() && frame <= m_Samples.back().Frame))
            {
                return;
            }
            if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
            {
                throw std::invalid_argument("GPU benchmark duration must be finite and nonnegative.");
            }
            m_Samples.push_back({frame, milliseconds});
        }

        [[nodiscard]] std::span<const RuntimeBenchmarkGpuSample> Values() const noexcept { return m_Samples; }

        void Reset() noexcept { m_Samples.clear(); }

      private:
        std::vector<RuntimeBenchmarkGpuSample> m_Samples;
    };

    // Published renderer statistics may repeat or skip frames at the owner-loop boundary.
    // Count unique observations without presenting them as a complete render timeline.
    struct RuntimeBenchmarkCloudCacheObservations final
    {
        std::uint64_t LastFrame = 0;
        std::uint64_t Frames = 0;
        std::uint64_t Builds = 0;
        std::uint64_t Reuses = 0;
        std::uint64_t Cells = 0;

        void Observe(const std::uint64_t frame, const std::uint64_t builds, const std::uint64_t reuses,
                     const std::uint64_t cells) noexcept
        {
            if (frame <= LastFrame)
            {
                return;
            }
            LastFrame = frame;
            ++Frames;
            Builds += builds;
            Reuses += reuses;
            Cells += cells;
        }
    };

    class RuntimeBenchmarkAdmissionGate final
    {
      public:
        [[nodiscard]] bool Ready(const bool compiling, const std::uint64_t lastAcceptedFrame) noexcept
        {
            if (compiling)
            {
                m_WarmupLastAcceptedFrame = lastAcceptedFrame;
                return false;
            }
            return !m_WarmupLastAcceptedFrame || lastAcceptedFrame > *m_WarmupLastAcceptedFrame;
        }

      private:
        std::optional<std::uint64_t> m_WarmupLastAcceptedFrame;
    };

    // Measures successive visits to the same application-loop boundary, independently
    // of asynchronous renderer completion. Storage is allocated before measurement.
    class RuntimeBenchmarkIntervals final
    {
      public:
        RuntimeBenchmarkIntervals(const std::size_t warmup, const std::size_t measured)
            : m_Warmup(warmup), m_WarmupRemaining(warmup), m_Required(measured)
        {
            if (measured == 0)
            {
                throw std::invalid_argument("A benchmark requires at least one measured interval.");
            }
            m_Intervals.reserve(measured);
        }

        void Observe(const double milliseconds)
        {
            if (!std::isfinite(milliseconds) || (m_Previous && milliseconds <= *m_Previous))
            {
                throw std::invalid_argument("Benchmark timestamps must be finite and strictly increasing.");
            }
            if (Complete())
            {
                return;
            }
            if (m_Previous)
            {
                const double interval = milliseconds - *m_Previous;
                if (!std::isfinite(interval))
                {
                    throw std::out_of_range("Benchmark interval is not representable.");
                }
                if (m_WarmupRemaining != 0)
                {
                    --m_WarmupRemaining;
                }
                else
                {
                    m_Intervals.push_back(interval);
                }
            }
            m_Previous = milliseconds;
        }

        [[nodiscard]] bool Complete() const noexcept { return m_Intervals.size() == m_Required; }

        void RestartWarmup()
        {
            if (!m_Intervals.empty())
            {
                throw std::logic_error("Cannot discard benchmark samples after measurement has started.");
            }
            m_WarmupRemaining = m_Warmup;
            m_Previous.reset();
        }

        [[nodiscard]] std::span<const double> Values() const noexcept { return m_Intervals; }

      private:
        const std::size_t m_Warmup;
        std::size_t m_WarmupRemaining;
        std::size_t m_Required;
        std::optional<double> m_Previous;
        std::vector<double> m_Intervals;
    };
} // namespace KeireRuntime
