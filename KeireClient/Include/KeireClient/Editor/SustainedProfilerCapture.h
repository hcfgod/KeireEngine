#pragma once

#include "Keire/Diagnostics/Profiler.h"
#include "Keire/Rendering/RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace KeireEditor
{
    // Application summaries with optional asynchronous renderer observations, never displayed-frame cadence.
    class SustainedProfilerCapture final
    {
      public:
        struct RendererObservation
        {
            bool Available = false;
            std::uint64_t Frame = 0, GpuTimingFrame = 0;
            bool GpuTimingSupported = false;
            float Admission = 0, Queue = 0, CpuPreparation = 0, Swapchain = 0, GpuDuration = 0, Completion = 0;
            std::uint32_t Outstanding = 0;
        };
        enum class State
        {
            Idle,
            Warming,
            Recording,
            Complete,
            Stopped,
            Gap,
            Overflow,
            InvalidClock
        };
        SustainedProfilerCapture() : SustainedProfilerCapture(50000) {}
        explicit SustainedProfilerCapture(std::size_t capacity) : m_Capacity(capacity) {}
        void Start(double seconds, const Keire::ProfileFrameSummary& baseline, double warmupSeconds = 2,
                   bool rendererTelemetry = false)
        {
            if ((seconds != 10 && seconds != 30) || !std::isfinite(warmupSeconds) || warmupSeconds < 0 ||
                !Valid(baseline) || m_Capacity == 0)
                throw std::invalid_argument("Invalid sustained profiler capture settings or baseline");
            const double readyAt = baseline.StartMicroseconds + baseline.DurationMicroseconds + warmupSeconds * 1e6;
            if (!std::isfinite(readyAt))
                throw std::invalid_argument("Sustained profiler warmup deadline overflow");
            std::vector<Keire::ProfileFrameSummary> storage;
            storage.reserve(m_Capacity);
            std::vector<RendererObservation> rendererStorage;
            if (rendererTelemetry)
                rendererStorage.reserve(m_Capacity);
            m_Frames.swap(storage);
            m_Renderer.swap(rendererStorage);
            m_RendererEnabled = rendererTelemetry;
            m_RendererAttached = false;
            m_Last = baseline;
            m_Duration = seconds * 1e6;
            m_Warmup = warmupSeconds;
            m_ReadyAt = readyAt;
            m_State = State::Warming;
        }
        bool Observe(const Keire::ProfileFrameSummary& frame)
        {
            if (!Active() || frame.Sequence == m_Last.Sequence)
                return false;
            if (!Valid(frame) || frame.StartMicroseconds < m_Last.StartMicroseconds + m_Last.DurationMicroseconds)
            {
                m_State = State::InvalidClock;
                return false;
            }
            if (frame.Sequence != m_Last.Sequence + 1)
            {
                m_State = State::Gap;
                return false;
            }
            m_Last = frame;
            if (m_State == State::Warming)
            {
                if (frame.StartMicroseconds < m_ReadyAt)
                    return false;
                m_State = State::Recording;
            }
            if (m_Frames.size() == m_Capacity)
            {
                m_State = State::Overflow;
                return false;
            }
            m_Frames.push_back(frame);
            if (m_RendererEnabled)
                m_Renderer.emplace_back();
            m_RendererAttached = false;
            if (ElapsedMicroseconds() >= m_Duration)
                m_State = State::Complete;
            return true;
        }
        [[nodiscard]] bool RendererTelemetryEnabled() const noexcept { return m_RendererEnabled; }
        [[nodiscard]] const std::vector<RendererObservation>& RendererObservations() const noexcept
        {
            return m_Renderer;
        }
        // One optional observation after an accepted summary. These are asynchronous IDs, never a frame join.
        void ObserveRenderer(std::uint64_t summarySequence, const Keire::RenderStatistics* statistics)
        {
            if ((m_State != State::Recording && m_State != State::Complete) || !m_RendererEnabled || m_Frames.empty() ||
                m_Frames.back().Sequence != summarySequence || m_RendererAttached)
                throw std::logic_error("Renderer observation does not match an unobserved accepted summary");
            m_RendererAttached = true;
            if (!statistics || statistics->Frame == 0)
                return;
            auto& r = m_Renderer.back();
            r = {true,
                 statistics->Frame,
                 statistics->GpuTimingFrame,
                 statistics->GpuTimingSupported,
                 statistics->FrameAdmissionWaitMilliseconds,
                 statistics->RendererQueueDelayMilliseconds,
                 statistics->CpuPreparationMilliseconds,
                 statistics->SwapchainWaitMilliseconds,
                 statistics->GpuFrameMilliseconds,
                 statistics->GpuCompletionLatencyMilliseconds,
                 statistics->OutstandingFrames};
        }
        void Stop() noexcept
        {
            if (Active())
                m_State = State::Stopped;
            m_RendererAttached = true;
        }
        [[nodiscard]] bool Active() const noexcept { return m_State == State::Warming || m_State == State::Recording; }
        [[nodiscard]] State Status() const noexcept { return m_State; }
        [[nodiscard]] const std::vector<Keire::ProfileFrameSummary>& Frames() const noexcept { return m_Frames; }
        [[nodiscard]] double ElapsedMicroseconds() const noexcept
        {
            return m_Frames.empty() ? 0 : m_Frames.back().StartMicroseconds - m_Frames.front().StartMicroseconds;
        }
        [[nodiscard]] const char* Label() const noexcept
        {
            switch (m_State)
            {
            case State::Idle:
                return "Idle";
            case State::Warming:
                return "Warmup (2 seconds by default)";
            case State::Recording:
                return "Recording application frames";
            case State::Complete:
                return "Complete";
            case State::Stopped:
                return "Stopped: incomplete";
            case State::Gap:
                return "Invalid: frame sequence gap";
            case State::Overflow:
                return "Invalid: summary capacity exceeded";
            case State::InvalidClock:
                return "Invalid: frame timing";
            }
            return "Invalid";
        }
        [[nodiscard]] std::string Csv() const
        {
            if (Active())
                return {};
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::setprecision(17);
            out << "# scope=application_start_intervals_not_displayed_fps\n# status=" << Label()
                << "\n# requested_seconds=" << m_Duration / 1e6 << "\n# warmup_seconds=" << m_Warmup
                << "\n# elapsed_seconds=" << ElapsedMicroseconds() / 1e6;
            std::vector<double> durations;
            double total = 0;
            std::size_t above2 = 0, above25 = 0;
            for (std::size_t i = 1; i < m_Frames.size(); ++i)
            {
                const double interval = m_Frames[i].StartMicroseconds - m_Frames[i - 1].StartMicroseconds;
                durations.push_back(interval);
                total += interval;
                above2 += interval > 2000;
                above25 += interval > 2500;
            }
            if (!durations.empty())
            {
                std::sort(durations.begin(), durations.end());
                const auto percentile = [&](double q)
                { return durations[static_cast<std::size_t>(std::ceil(q * durations.size())) - 1]; };
                out << "\n# measured_intervals=" << durations.size()
                    << "\n# average_application_fps=" << 1e6 * durations.size() / total
                    << "\n# p95_us=" << percentile(.95) << "\n# p99_us=" << percentile(.99)
                    << "\n# maximum_us=" << durations.back() << "\n# frames_above_2ms=" << above2
                    << "\n# frames_above_2_5ms=" << above25;
            }
            out << "\n# renderer_telemetry=" << m_RendererEnabled
                << "\n# renderer_scope=asynchronous_observation_not_application_frame_join";
            out << "\nsequence,start_us,profiled_body_us,dropped_spans,dropped_counters,truncated,application_us,"
                   "scripting_us,physics_us,rendering_us";
            if (m_RendererEnabled)
                out << ",renderer_available,observed_renderer_frame,observed_gpu_timing_frame,gpu_timing_supported,"
                       "admission_ms,queue_ms,renderer_cpu_preparation_ms,swapchain_ms,gpu_duration_ms,completion_ms,"
                       "outstanding";
            out << '\n';
            for (std::size_t index = 0; index < m_Frames.size(); ++index)
            {
                const auto& f = m_Frames[index];
                out << f.Sequence << ',' << f.StartMicroseconds << ',' << f.DurationMicroseconds << ','
                    << f.DroppedSpans << ',' << f.DroppedCounters << ',' << f.Truncated << ','
                    << f.ApplicationMicroseconds << ',' << f.ScriptingMicroseconds << ',' << f.PhysicsMicroseconds
                    << ',' << f.RenderingMicroseconds;
                if (m_RendererEnabled)
                {
                    const auto& r = m_Renderer[index];
                    out << ',' << r.Available;
                    if (r.Available)
                    {
                        out << ',' << r.Frame << ',' << r.GpuTimingFrame << ',' << r.GpuTimingSupported << ','
                            << r.Admission << ',' << r.Queue << ',' << r.CpuPreparation << ',' << r.Swapchain << ',';
                        if (r.GpuTimingSupported && r.GpuTimingFrame != 0)
                            out << r.GpuDuration;
                        out << ',' << r.Completion << ',' << r.Outstanding;
                    }
                    else
                        out << ",,,,,,,,,,";
                }
                out << '\n';
            }
            return out.str();
        }

      private:
        static bool Valid(const Keire::ProfileFrameSummary& f) noexcept
        {
            return f.Sequence != 0 && std::isfinite(f.StartMicroseconds) && std::isfinite(f.DurationMicroseconds) &&
                   f.StartMicroseconds >= 0 && f.DurationMicroseconds > 0 &&
                   std::isfinite(f.StartMicroseconds + f.DurationMicroseconds);
        }
        std::size_t m_Capacity;
        State m_State = State::Idle;
        Keire::ProfileFrameSummary m_Last;
        std::vector<Keire::ProfileFrameSummary> m_Frames;
        std::vector<RendererObservation> m_Renderer;
        bool m_RendererEnabled = false, m_RendererAttached = false;
        double m_Duration = 0, m_ReadyAt = 0, m_Warmup = 0;
    };
} // namespace KeireEditor
