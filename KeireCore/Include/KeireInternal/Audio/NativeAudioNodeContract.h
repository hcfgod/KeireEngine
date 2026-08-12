#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace Keire::Detail
{
    class NativeAudioNodeFrameCounts final
    {
      public:
        NativeAudioNodeFrameCounts(std::uint32_t* input, std::uint32_t* output) noexcept
            : m_Input(input), m_Output(output)
        {
            assert(m_Input != nullptr);
            assert(m_Output != nullptr);
        }

        [[nodiscard]] std::uint32_t Capacity() const noexcept { return std::min(*m_Input, *m_Output); }

        void Commit(const std::uint32_t frames) noexcept
        {
            assert(frames <= Capacity());
            *m_Input = frames;
            *m_Output = frames;
        }

      private:
        // miniaudio supplies one shared input-frame count even when a node owns multiple input buses.
        std::uint32_t* m_Input = nullptr;
        std::uint32_t* m_Output = nullptr;
    };
} // namespace Keire::Detail
