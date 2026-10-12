#pragma once

#include "Keire/Jobs/JobSystem.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Keire::RenderBackend
{
    // A device must outlive its unpublished batch. Publication transfers the entire set to the render thread.
    class GpuVfxPipelineBatch final
    {
      public:
        static constexpr std::size_t Count = 16;
        using Handles = std::array<SDL_GPUComputePipeline*, Count>;
        using ReleaseFunction = void (*)(SDL_GPUDevice*, SDL_GPUComputePipeline*) noexcept;

        explicit GpuVfxPipelineBatch(SDL_GPUDevice* device, ReleaseFunction release = ReleasePipeline) noexcept
            : m_Device(device), m_Release(release)
        {
        }

        ~GpuVfxPipelineBatch() { Reset(); }

        void Reset() noexcept
        {
            for (auto*& pipeline : Pipelines)
            {
                if (pipeline)
                    m_Release(m_Device, std::exchange(pipeline, nullptr));
            }
        }

        GpuVfxPipelineBatch(const GpuVfxPipelineBatch&) = delete;
        GpuVfxPipelineBatch& operator=(const GpuVfxPipelineBatch&) = delete;

        [[nodiscard]] Handles Release() noexcept { return std::exchange(Pipelines, {}); }

        // Lost-device handles follow the renderer's abandon policy; never call back into that device.
        void Abandon() noexcept { Pipelines.fill(nullptr); }

        Handles Pipelines{};

      private:
        static void ReleasePipeline(SDL_GPUDevice* device, SDL_GPUComputePipeline* pipeline) noexcept
        {
            SDL_ReleaseGPUComputePipeline(device, pipeline);
        }

        SDL_GPUDevice* m_Device;
        ReleaseFunction m_Release;
    };
    // Accessed by the render thread; the worker owns only the captured batch until its job completes.
    struct GpuVfxPipelineWarmupWork final
    {
        JobHandle Job;
        std::shared_ptr<GpuVfxPipelineBatch> Batch;
        std::uint32_t Generation = 0;
        std::atomic<std::uint64_t> Microseconds{0};
        std::string Failure;
        std::chrono::steady_clock::time_point Started;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        JobFunction CompileForTest;
#endif
    };
} // namespace Keire::RenderBackend
