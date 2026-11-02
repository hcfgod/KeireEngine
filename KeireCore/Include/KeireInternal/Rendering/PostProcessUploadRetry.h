#pragma once

#include <cstdint>

namespace Keire::Detail
{
    class PostProcessUploadRetry final
    {
      public:
        [[nodiscard]] bool ShouldAttempt(const std::uint64_t frame, const bool sourceChanged,
                                         const bool sourceReady) const noexcept
        {
            if (sourceChanged || !m_Attempted)
            {
                return true;
            }
            if (sourceReady)
            {
                return false;
            }
            // Retry once after a counter reset, then resume the normal interval in that timeline.
            return frame < m_LastAttemptFrame || frame - m_LastAttemptFrame >= RetryIntervalFrames;
        }

        void RecordAttempt(const std::uint64_t frame) noexcept
        {
            m_LastAttemptFrame = frame;
            m_Attempted = true;
        }

        static constexpr std::uint64_t RetryIntervalFrames = 60;

      private:
        std::uint64_t m_LastAttemptFrame = 0;
        bool m_Attempted = false;
    };
} // namespace Keire::Detail
