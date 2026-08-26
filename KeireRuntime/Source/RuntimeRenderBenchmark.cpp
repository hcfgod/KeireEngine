#include "KeireRuntimeInternal/RuntimeRenderBenchmark.h"

#include "KeireInternal/Diagnostics/SystemHardwareIdentityInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL_gpu.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

        [[nodiscard]] float Percentile(std::vector<float> values, const double percentile)
        {
            if (values.empty())
                return 0.0F;
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
            if (!Output.empty() && Keire::GetBuildInfo().Configuration != "Release")
                throw Keire::CommandLineError("Render benchmark requires a Release runtime build.");
        }

        void Update(Keire::Application& application, const Keire::Ref<Keire::RenderSystem>& renderer)
        {
            if (Output.empty() || Complete)
                return;
            if (!renderer || renderer->Mode() != Keire::RenderMode::Rendered)
                throw std::runtime_error("Render benchmark requires the rendered GPU backend.");
            if (Keire::RenderSystemInternalAccess::PresentMode(*renderer) != SdlPresentMode(PresentMode))
            {
                throw std::runtime_error("The selected adapter does not expose the requested " +
                                         std::string(PresentModeName(PresentMode)) + " presentation mode.");
            }

            const auto recent = renderer->RecentFrameTimelines();
            if (!BaselineInitialized)
            {
                for (const auto& timeline : recent)
                    BaselineFrame = (std::max)(BaselineFrame, timeline.Frame);
                BaselineInitialized = true;
                return;
            }
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
            if (Timelines.size() < required)
                return;
            std::ranges::sort(Timelines, [](const auto& left, const auto& right) { return left.Frame < right.Frame; });
            Timelines.resize(required);
            for (std::size_t index = 1; index < Timelines.size(); ++index)
                if (Timelines[index].Frame != Timelines[index - 1U].Frame + 1U)
                    throw std::runtime_error("Render benchmark observed a missing or reordered frame timeline.");

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
            const Json report{{"schemaVersion", 1},
                              {"status", "passed"},
                              {"workload",
                               {{"warmupFrames", RuntimeRenderBenchmark::WarmupFrames},
                                {"measuredFrames", RuntimeRenderBenchmark::MeasuredFrames}}},
                              {"presentMode", std::string(PresentModeName(PresentMode))},
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
                              {"summary", summary},
                              {"timelines", std::move(measuredTimelines)}};
            Keire::Detail::WriteTextFileAtomically(Output, report.dump(2) + '\n');
            Complete = true;
            application.RequestExit();
        }

        std::filesystem::path Output;
        Keire::RenderPresentMode PresentMode = Keire::RenderPresentMode::VSync;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uint64_t BaselineFrame = 0;
        bool BaselineInitialized = false;
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
