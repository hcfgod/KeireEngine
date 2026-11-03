#include "KeireRuntimeInternal/RuntimeRenderBenchmark.h"
#include "KeireRuntimeInternal/RuntimeBenchmarkIntervals.h"

#include "KeireInternal/Diagnostics/SystemHardwareIdentityInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL_gpu.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireRuntime
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string_view PresentModeName(const Keire::RenderPresentMode mode) noexcept
        {
            return mode == Keire::RenderPresentMode::Immediate ? "immediate" : "vsync";
        }

        [[nodiscard]] SDL_GPUPresentMode SdlPresentMode(const Keire::RenderPresentMode mode) noexcept
        {
            return mode == Keire::RenderPresentMode::Immediate ? SDL_GPU_PRESENTMODE_IMMEDIATE
                                                               : SDL_GPU_PRESENTMODE_VSYNC;
        }

        template <typename Value> [[nodiscard]] Value Percentile(std::vector<Value> values, const double percentile)
        {
            if (values.empty())
            {
                return Value{};
            }
            std::ranges::sort(values);
            const auto rank = static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
            return values[(std::max)(std::size_t{1}, rank) - 1U];
        }

        template <typename Projection>
        [[nodiscard]] Json MetricSummary(const std::span<const Keire::RenderFrameTimeline> timelines,
                                         Projection projection)
        {
            std::vector<float> values;
            values.reserve(timelines.size());
            for (const auto& timeline : timelines)
                values.push_back(projection(timeline));
            return {{"median", Percentile(values, 0.50)},
                    {"p95", Percentile(values, 0.95)},
                    {"p99", Percentile(std::move(values), 0.99)}};
        }

        [[nodiscard]] Json TimelineReport(const Keire::RenderFrameTimeline& timeline)
        {
            return {{"frame", timeline.Frame},
                    {"ownerUpdateMilliseconds", timeline.OwnerUpdateMilliseconds},
                    {"captureMilliseconds", timeline.CaptureMilliseconds},
                    {"admissionWaitMilliseconds", timeline.AdmissionWaitMilliseconds},
                    {"queueDelayMilliseconds", timeline.QueueDelayMilliseconds},
                    {"renderCpuMilliseconds", timeline.RenderCpuMilliseconds},
                    {"gpuRetirementMilliseconds", timeline.GpuRetirementMilliseconds},
                    {"submitToPresentMilliseconds", timeline.SubmitToPresentMilliseconds},
                    {"outstandingAtAdmission", timeline.OutstandingAtAdmission}};
        }
    } // namespace

    class RuntimeRenderBenchmark::Impl final
    {
      public:
        Impl(std::filesystem::path output, const Keire::RenderPresentMode presentMode)
            : Output(std::move(output)), PresentMode(presentMode)
        {
            const auto configuration = Keire::GetBuildInfo().Configuration;
            if (!Output.empty() && !RuntimeRenderBenchmark::SupportsConfiguration(configuration))
            {
                throw Keire::CommandLineError("Render benchmark requires a Release, DevDist or Dist runtime build.");
            }
        }

        void Update(Keire::Application& application, const Keire::Ref<Keire::RenderSystem>& renderer)
        {
            if (Output.empty() || Complete)
                return;
            const auto now = std::chrono::steady_clock::now();
            const auto window = application.MainWindow();
            if (!window)
            {
                throw std::runtime_error("Render benchmark requires a main window.");
            }
            const auto pixels = window->PixelSize();
            if (pixels.Width == 0 || pixels.Height == 0)
            {
                throw std::runtime_error("Render benchmark requires a nonzero drawable resolution.");
            }
            if (!BaselineInitialized)
            {
                PixelWidth = pixels.Width;
                PixelHeight = pixels.Height;
            }
            else if (pixels.Width != PixelWidth || pixels.Height != PixelHeight)
            {
                const Json failure{{"schemaVersion", 1},
                                   {"status", "failed"},
                                   {"reason", "drawable_resolution_changed"},
                                   {"expectedWidth", PixelWidth},
                                   {"expectedHeight", PixelHeight},
                                   {"observedWidth", pixels.Width},
                                   {"observedHeight", pixels.Height},
                                   {"measuredIntervalCount", ApplicationIntervals.Values().size()}};
                Keire::Detail::WriteTextFileAtomically(Output, failure.dump(2) + '\n');
                throw std::runtime_error("Render benchmark resolution changed; repeat with a fixed window size.");
            }
            if (!renderer || renderer->Mode() != Keire::RenderMode::Rendered)
                throw std::runtime_error("Render benchmark requires the rendered GPU backend.");
            if (Keire::RenderSystemInternalAccess::PresentMode(*renderer) != SdlPresentMode(PresentMode))
            {
                throw std::runtime_error("The selected adapter does not expose the requested " +
                                         std::string(PresentModeName(PresentMode)) + " presentation mode.");
            }

            // During asynchronous VFX compilation the owner keeps pumping events without
            // admitting every render submission. Those loops are startup, not rendered throughput.
            const auto currentStatistics = renderer->Statistics();
            if (!AdmissionGate.Ready(currentStatistics.VfxPipelineWarmupPending, currentStatistics.LastAcceptedFrame))
            {
                ApplicationIntervals.RestartWarmup();
                GpuObservations.Reset();
                Timelines.clear();
                CloudCacheObservations = {};
                BaselineInitialized = false;
                BaselineFrame = 0;
                PipelineWarmupObserved = true;
                constexpr auto maximumStartupWait = std::chrono::minutes(3);
                if (now - Started > maximumStartupWait)
                {
                    throw std::runtime_error("Render benchmark pipeline warmup exceeded its startup deadline.");
                }
                return;
            }
            ApplicationIntervals.Observe(std::chrono::duration<double, std::milli>(now - Started).count());

            const auto recent = renderer->RecentFrameTimelines();
            if (!BaselineInitialized)
            {
                StartupMilliseconds = std::chrono::duration<double, std::milli>(now - Started).count();
                // Retiring startup frames can still be present after compilation finishes.
                // Fence all already accepted work, not only the published completion history.
                BaselineFrame = currentStatistics.LastAcceptedFrame;
                for (const auto& timeline : recent)
                    BaselineFrame = (std::max)(BaselineFrame, timeline.Frame);
                BaselineInitialized = true;
                return;
            }
            if (currentStatistics.Frame > BaselineFrame + RuntimeRenderBenchmark::WarmupFrames &&
                currentStatistics.Frame <=
                    BaselineFrame + RuntimeRenderBenchmark::WarmupFrames + RuntimeRenderBenchmark::MeasuredFrames)
            {
                CloudCacheObservations.Observe(currentStatistics.Frame, currentStatistics.CloudLightCacheBuilds,
                                               currentStatistics.CloudLightCacheReuses,
                                               currentStatistics.CloudLightCacheCells);
            }
            GpuObservations.Observe(
                currentStatistics.GpuTimingSupported, currentStatistics.GpuTimingFrame,
                currentStatistics.GpuFrameMilliseconds, BaselineFrame + RuntimeRenderBenchmark::WarmupFrames + 1,
                BaselineFrame + RuntimeRenderBenchmark::WarmupFrames + RuntimeRenderBenchmark::MeasuredFrames);
            for (const auto& timeline : recent)
            {
                if (timeline.Frame <= BaselineFrame ||
                    std::ranges::any_of(Timelines, [frame = timeline.Frame](const auto& observed)
                                        { return observed.Frame == frame; }))
                {
                    continue;
                }
                if (timeline.Cancelled)
                    throw std::runtime_error("Render benchmark observed a cancelled frame.");
                Timelines.push_back(timeline);
            }
            constexpr auto required = RuntimeRenderBenchmark::WarmupFrames + RuntimeRenderBenchmark::MeasuredFrames;
            if (Timelines.size() < required || !ApplicationIntervals.Complete())
                return;
            std::ranges::sort(Timelines, [](const auto& left, const auto& right) { return left.Frame < right.Frame; });
            Timelines.resize(required);
            for (std::size_t index = 1; index < Timelines.size(); ++index)
            {
                if (Timelines[index].Frame != Timelines[index - 1U].Frame + 1U)
                {
                    Json observed = Json::array();
                    for (const auto& timeline : Timelines)
                    {
                        observed.push_back(TimelineReport(timeline));
                    }
                    const auto intervals = ApplicationIntervals.Values();
                    const Json failure{
                        {"schemaVersion", 1},
                        {"status", "failed"},
                        {"reason", "frame_timeline_gap"},
                        {"gapIndex", index},
                        {"previousFrame", Timelines[index - 1U].Frame},
                        {"nextFrame", Timelines[index].Frame},
                        {"baselineFrame", BaselineFrame},
                        {"applicationIntervalsMilliseconds", std::vector<double>(intervals.begin(), intervals.end())},
                        {"timelines", std::move(observed)}};
                    Keire::Detail::WriteTextFileAtomically(Output, failure.dump(2) + '\n');
                    throw std::runtime_error("Render benchmark observed a missing or reordered frame timeline; "
                                             "diagnostics saved to the requested output.");
                }
            }

            const auto measured =
                std::span<const Keire::RenderFrameTimeline>(Timelines).subspan(RuntimeRenderBenchmark::WarmupFrames);
            const auto build = Keire::GetBuildInfo();
            const auto hardware = Keire::Internal::QuerySystemHardwareIdentity();
            const auto device = renderer->DeviceIdentity();
            const auto statistics = renderer->Statistics();
            Json measuredTimelines = Json::array();
            for (const auto& timeline : measured)
                measuredTimelines.push_back(TimelineReport(timeline));
            const auto summary =
                Json{{"ownerUpdateMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.OwnerUpdateMilliseconds; })},
                     {"captureMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.CaptureMilliseconds; })},
                     {"admissionWaitMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.AdmissionWaitMilliseconds; })},
                     {"queueDelayMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.QueueDelayMilliseconds; })},
                     {"renderCpuMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.RenderCpuMilliseconds; })},
                     {"gpuRetirementMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.GpuRetirementMilliseconds; })},
                     {"submitToPresentMilliseconds",
                      MetricSummary(measured, [](const auto& value) { return value.SubmitToPresentMilliseconds; })}};
            const auto intervals = ApplicationIntervals.Values();
            const double elapsed = std::accumulate(intervals.begin(), intervals.end(), 0.0);
            std::vector<double> sortedIntervals(intervals.begin(), intervals.end());
            const Json applicationTiming{
                {"scope", "application_render_submission_intervals_not_displayed_fps"},
                {"count", intervals.size()},
                {"elapsedMilliseconds", elapsed},
                {"averageMilliseconds", elapsed / static_cast<double>(intervals.size())},
                {"medianMilliseconds", Percentile(sortedIntervals, 0.50)},
                {"p95Milliseconds", Percentile(sortedIntervals, 0.95)},
                {"p99Milliseconds", Percentile(sortedIntervals, 0.99)},
                {"maximumMilliseconds", *std::ranges::max_element(intervals)},
                {"intervalsMilliseconds", std::vector<double>(intervals.begin(), intervals.end())}};
            Json gpuSamples = Json::array();
            std::vector<float> gpuDurations;
            for (const auto& sample : GpuObservations.Values())
            {
                gpuSamples.push_back({{"frame", sample.Frame}, {"milliseconds", sample.Milliseconds}});
                gpuDurations.push_back(sample.Milliseconds);
            }
            Json gpuTiming{{"scope", "sampled_completed_gpu_frames_not_complete_timeline"},
                           {"available", !gpuDurations.empty()},
                           {"count", gpuDurations.size()},
                           {"samples", std::move(gpuSamples)}};
            if (!gpuDurations.empty())
            {
                gpuTiming["averageMilliseconds"] = std::accumulate(gpuDurations.begin(), gpuDurations.end(), 0.0) /
                                                   static_cast<double>(gpuDurations.size());
                gpuTiming["medianMilliseconds"] = Percentile(gpuDurations, 0.50);
                gpuTiming["p95Milliseconds"] = Percentile(gpuDurations, 0.95);
                gpuTiming["p99Milliseconds"] = Percentile(gpuDurations, 0.99);
                gpuTiming["maximumMilliseconds"] = *std::ranges::max_element(gpuDurations);
            }
            const Json report{{"schemaVersion", 1},
                              {"status", "passed"},
                              {"workload",
                               {{"warmupFrames", RuntimeRenderBenchmark::WarmupFrames},
                                {"measuredFrames", RuntimeRenderBenchmark::MeasuredFrames},
                                {"pixelWidth", PixelWidth},
                                {"pixelHeight", PixelHeight}}},
                              {"presentMode", std::string(PresentModeName(PresentMode))},
                              {"startup",
                               {{"millisecondsToCaptureBaseline", StartupMilliseconds},
                                {"pipelineWarmupObserved", PipelineWarmupObserved},
                                {"vfxPipelineWarmupMilliseconds", statistics.VfxPipelineWarmupMilliseconds}}},
                              {"build",
                               {{"version", std::string(build.Version)},
                                {"gitCommit", std::string(build.GitCommit)},
                                {"dirty", build.Dirty},
                                {"configuration", std::string(build.Configuration)},
                                {"compiler", std::string(build.Compiler)},
                                {"platform", std::string(build.Platform)},
                                {"architecture", std::string(build.Architecture)}}},
                              {"hardware",
                               {{"operatingSystemDescription", hardware.OperatingSystemDescription},
                                {"operatingSystemVersion", hardware.OperatingSystemVersion},
                                {"cpuModel", hardware.CpuModel},
                                {"logicalProcessorCount", hardware.LogicalProcessorCount},
                                {"physicalMemoryBytes", hardware.PhysicalMemoryBytes},
                                {"rendererBackend", device.Backend},
                                {"gpuAdapter", device.Adapter},
                                {"driverName", device.DriverName},
                                {"driverVersion", device.DriverVersion},
                                {"driverInformation", device.DriverInformation},
                                {"deviceGeneration", device.DeviceGeneration}}},
                              {"pipeline",
                               {{"allowedFramesInFlight", statistics.AllowedFramesInFlight},
                                {"framesInFlightHighWaterMark", statistics.FramesInFlightHighWaterMark},
                                {"rendererQueueHighWaterMark", statistics.RendererQueueHighWaterMark}}},
                              {"applicationTiming", applicationTiming},
                              {"gpuExecutionTiming", std::move(gpuTiming)},
                              {"cloudLightCacheObservations",
                               {{"scope", "sampled_published_frames_not_complete_timeline"},
                                {"frames", CloudCacheObservations.Frames},
                                {"builds", CloudCacheObservations.Builds},
                                {"reuses", CloudCacheObservations.Reuses},
                                {"cells", CloudCacheObservations.Cells}}},
                              {"summary", summary},
                              {"timelines", std::move(measuredTimelines)}};
            Keire::Detail::WriteTextFileAtomically(Output, report.dump(2) + '\n');
            Complete = true;
            application.RequestExit();
        }

        const std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
        RuntimeBenchmarkIntervals ApplicationIntervals{RuntimeRenderBenchmark::WarmupFrames,
                                                       RuntimeRenderBenchmark::MeasuredFrames};
        RuntimeBenchmarkAdmissionGate AdmissionGate;
        RuntimeBenchmarkCloudCacheObservations CloudCacheObservations;
        RuntimeBenchmarkGpuObservations GpuObservations{RuntimeRenderBenchmark::MeasuredFrames};
        std::filesystem::path Output;
        Keire::RenderPresentMode PresentMode = Keire::RenderPresentMode::VSync;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uint64_t BaselineFrame = 0;
        std::uint32_t PixelWidth = 0;
        std::uint32_t PixelHeight = 0;
        bool BaselineInitialized = false;
        bool PipelineWarmupObserved = false;
        double StartupMilliseconds = 0.0;
        bool Complete = false;
    };

    RuntimeRenderBenchmark::RuntimeRenderBenchmark(std::filesystem::path output,
                                                   const Keire::RenderPresentMode presentMode)
        : m_Impl(std::make_unique<Impl>(std::move(output), presentMode))
    {
    }

    RuntimeRenderBenchmark::~RuntimeRenderBenchmark() = default;

    bool RuntimeRenderBenchmark::Enabled() const noexcept { return !m_Impl->Output.empty(); }

    void RuntimeRenderBenchmark::Update(Keire::Application& application,
                                        const Keire::Ref<Keire::RenderSystem>& renderer)
    {
        m_Impl->Update(application, renderer);
    }
} // namespace KeireRuntime
