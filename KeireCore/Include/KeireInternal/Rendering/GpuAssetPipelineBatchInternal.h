#pragma once

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    // The device outlives the batch. Only its worker accesses it until the job completes;
    // the render thread then publishes or classifies failure before resetting/abandoning it.
    class GpuAssetPipelineBatch final
    {
      public:
        struct Slot final
        {
            SDL_GPUShader* Vertex = nullptr;
            SDL_GPUShader* Fragment = nullptr;
            SDL_GPUGraphicsPipeline* Pipeline = nullptr;
        };

        GpuAssetPipelineBatch(SDL_GPUDevice* device, std::size_t count) : m_Device(device), m_Slots(count) {}

        ~GpuAssetPipelineBatch() { Reset(); }

        GpuAssetPipelineBatch(const GpuAssetPipelineBatch&) = delete;
        GpuAssetPipelineBatch& operator=(const GpuAssetPipelineBatch&) = delete;

        [[nodiscard]] Slot& At(std::size_t index) { return m_Slots.at(index); }

        template <typename Build, typename StopRequested> void Prepare(Build&& build, StopRequested&& stopRequested)
        {
            for (std::size_t index = 0; index < m_Slots.size(); ++index)
            {
                if (stopRequested())
                {
                    return;
                }
                build(index, m_Slots[index]);
            }
        }

        [[nodiscard]] bool IsComplete() const noexcept
        {
            return !m_Slots.empty() &&
                   std::ranges::all_of(m_Slots, [](const Slot& slot) { return slot.Pipeline != nullptr; });
        }

        [[nodiscard]] SDL_GPUGraphicsPipeline* ReleasePipeline(std::size_t index)
        {
            return std::exchange(m_Slots.at(index).Pipeline, nullptr);
        }

        void Reset() noexcept
        {
            for (auto& slot : m_Slots)
            {
                if (slot.Pipeline)
                {
                    SDL_ReleaseGPUGraphicsPipeline(m_Device, std::exchange(slot.Pipeline, nullptr));
                }
                if (slot.Fragment)
                {
                    SDL_ReleaseGPUShader(m_Device, std::exchange(slot.Fragment, nullptr));
                }
                if (slot.Vertex)
                {
                    SDL_ReleaseGPUShader(m_Device, std::exchange(slot.Vertex, nullptr));
                }
            }
        }

        void Abandon() noexcept
        {
            for (auto& slot : m_Slots)
            {
                slot = {};
            }
        }

      private:
        SDL_GPUDevice* m_Device;
        std::vector<Slot> m_Slots;
    };
} // namespace Keire::RenderBackend
