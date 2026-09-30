#include "Keire/Core.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    struct TimestampDevice final
    {
        Keire::Ref<Keire::WindowSystem> Windows = Keire::CreateRef<Keire::WindowSystem>();
        SDL_GPUDevice* Device = nullptr;
        SDL_GPUTimestampQuery* Query = nullptr;
        std::vector<SDL_GPUFence*> Fences;

        explicit TimestampDevice(const bool debug = true)
        {
            (void)RenderTestSpecification();
            constexpr auto formats = static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_METALLIB);
            Device = SDL_CreateGPUDevice(formats, debug, nullptr);
        }

        ~TimestampDevice()
        {
            if (Device)
            {
                (void)SDL_WaitForGPUIdle(Device);
                for (auto* fence : Fences)
                    SDL_ReleaseGPUFence(Device, fence);
                SDL_ReleaseGPUTimestampQuery(Device, Query);
                SDL_DestroyGPUDevice(Device);
            }
            while (Windows->PollEvent())
            {
            }
            Windows->Shutdown();
        }
    };

    class TimestampLayer final : public Keire::Layer
    {
      public:
        explicit TimestampLayer(std::vector<Keire::RenderStatistics>& samples)
            : Layer("GPU timestamp publication"), m_Samples(samples)
        {
        }

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            // Flush previously submitted frames so the assertion is independent of GPU/polling speed.
            Owner().Renderer()->Flush();
            m_Samples.push_back(Owner().Renderer()->Statistics());
            if (m_Samples.size() == 12)
                Owner().RequestExit();
        }

      private:
        std::vector<Keire::RenderStatistics>& m_Samples;
    };
} // namespace

TEST_CASE("GPU timestamp queries retain results until reuse and reject invalid recording")
{
    bool debug = true;
    SUBCASE("GPU validation enabled") {}
    SUBCASE("GPU validation disabled") { debug = false; }
    TimestampDevice native(debug);
    REQUIRE(native.Device);
    if (!SDL_GPUSupportsTimestampQueries(native.Device))
    {
        CHECK(SDL_CreateGPUTimestampQuery(native.Device) == nullptr);
        MESSAGE("Native device does not expose GPU timestamp queries; unavailable is preserved.");
        return;
    }
    native.Query = SDL_CreateGPUTimestampQuery(native.Device);
    REQUIRE(native.Query);
    for (std::uint32_t iteration = 0; iteration < 6; ++iteration)
    {
        auto* begin = SDL_AcquireGPUCommandBuffer(native.Device);
        REQUIRE(begin);
        CHECK_FALSE(SDL_WriteGPUTimestamp(begin, native.Query, 1));
        CHECK_FALSE(SDL_WriteGPUTimestamp(begin, native.Query, 2));
        REQUIRE(SDL_WriteGPUTimestamp(begin, native.Query, 0));
        auto* started = SDL_SubmitGPUCommandBufferAndAcquireFence(begin);
        REQUIRE(started);
        native.Fences.push_back(started);
        double elapsed = -1.0;
        CHECK_FALSE(SDL_GetGPUTimestampQueryResult(native.Device, native.Query, started, &elapsed));
        CHECK(elapsed == -1.0);

        auto* end = SDL_AcquireGPUCommandBuffer(native.Device);
        REQUIRE(end);
        auto* pass = SDL_BeginGPUCopyPass(end);
        REQUIRE(pass);
        // SDL's pass lifecycle flags are maintained only with GPU validation enabled.
        if (debug)
            CHECK_FALSE(SDL_WriteGPUTimestamp(end, native.Query, 1));
        SDL_EndGPUCopyPass(pass);
        REQUIRE(SDL_WriteGPUTimestamp(end, native.Query, 1));
        auto* completed = SDL_SubmitGPUCommandBufferAndAcquireFence(end);
        REQUIRE(completed);
        native.Fences.push_back(completed);
        REQUIRE(SDL_WaitForGPUFences(native.Device, true, &completed, 1));
        CHECK_FALSE(SDL_GetGPUTimestampQueryResult(native.Device, native.Query, completed, nullptr));
        REQUIRE(SDL_GetGPUTimestampQueryResult(native.Device, native.Query, completed, &elapsed));
        CHECK(std::isfinite(elapsed));
        CHECK(elapsed >= 0.0);
        double repeated = -1.0;
        REQUIRE(SDL_GetGPUTimestampQueryResult(native.Device, native.Query, completed, &repeated));
        CHECK(std::isfinite(repeated));
        CHECK(repeated >= 0.0);
    }
    // Canceled recordings never enter the queue; they do not prevent query reuse or final release.
    auto* canceled = SDL_AcquireGPUCommandBuffer(native.Device);
    REQUIRE(canceled);
    REQUIRE(SDL_WriteGPUTimestamp(canceled, native.Query, 0));
    REQUIRE(SDL_CancelGPUCommandBuffer(canceled));
}

TEST_CASE("GPU timing publishes completed frame identities across bounded query slot reuse")
{
    bool supported = false;
    {
        TimestampDevice native;
        REQUIRE(native.Device);
        supported = SDL_GPUSupportsTimestampQueries(native.Device);
    }
    std::vector<Keire::RenderStatistics> samples;
    auto specification = RenderTestSpecification();
    specification.Render.MaximumFramesInFlight = 3;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<TimestampLayer>(samples));
    REQUIRE(application.Run() == 0);
    REQUIRE(samples.size() == 12);
    std::uint64_t lastSample = 0;
    for (const auto& sample : samples)
    {
        if (sample.LastRetiredFrame == 0 || !supported)
        {
            CHECK_FALSE(sample.GpuTimingSupported);
            CHECK(sample.GpuTimingFrame == 0);
            CHECK(sample.GpuFrameMilliseconds == 0.0F);
        }
        else
        {
            CHECK(sample.GpuTimingSupported);
            CHECK(sample.GpuTimingFrame == sample.LastRetiredFrame);
            CHECK(sample.GpuTimingFrame > lastSample);
            CHECK(std::isfinite(sample.GpuFrameMilliseconds));
            CHECK(sample.GpuFrameMilliseconds >= 0.0F);
            lastSample = sample.GpuTimingFrame;
        }
    }
    if (supported)
        CHECK(lastSample > 3);
}
