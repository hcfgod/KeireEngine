#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>

namespace Keire::RenderBackend
{
    class CpuPreparationTracker final
    {
      public:
        void BeginFrame() noexcept
        {
            m_CurrentMilliseconds = 0.0F;
            m_FrameStarted = true;
        }

        void EndFrame()
        {
            if (!m_FrameStarted)
                return;
            m_CompletedMilliseconds = m_CurrentMilliseconds;
            m_Samples.push_back(m_CompletedMilliseconds);
            if (m_Samples.size() > SampleWindow)
                m_Samples.pop_front();

            std::array<float, SampleWindow> ordered{};
            std::ranges::copy(m_Samples, ordered.begin());
            std::ranges::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(m_Samples.size()));
            const auto percentileIndex = (m_Samples.size() * 95U + 99U) / 100U - 1U;
            m_P95Milliseconds = ordered[percentileIndex];
            m_FrameStarted = false;
        }

        void CancelFrame() noexcept
        {
            m_CurrentMilliseconds = 0.0F;
            m_FrameStarted = false;
        }

        void Accumulate(const float milliseconds) noexcept { m_CurrentMilliseconds += milliseconds; }

        [[nodiscard]] float CurrentMilliseconds() const noexcept { return m_CurrentMilliseconds; }
        [[nodiscard]] float CompletedMilliseconds() const noexcept { return m_CompletedMilliseconds; }
        [[nodiscard]] float P95Milliseconds() const noexcept { return m_P95Milliseconds; }

      private:
        static constexpr std::size_t SampleWindow = 120;

        std::deque<float> m_Samples;
        float m_CurrentMilliseconds = 0.0F;
        float m_CompletedMilliseconds = 0.0F;
        float m_P95Milliseconds = 0.0F;
        bool m_FrameStarted = false;
    };
} // namespace Keire::RenderBackend
