#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"

#include "Keire/BuildInfo.h"
#include "KeireInternal/Diagnostics/SystemHardwareIdentityInternal.h"

#include "KeireInternal/Diagnostics/DiagnosticBundleSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace Keire::Internal
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string_view RenderModeName(const RenderMode mode) noexcept
        {
            switch (mode)
            {
            case RenderMode::Automatic:
                return "automatic";
            case RenderMode::Disabled:
                return "disabled";
            case RenderMode::Headless:
                return "headless";
            case RenderMode::Rendered:
                return "rendered";
            }
            return "unknown";
        }

        [[nodiscard]] DiagnosticBundleTextSource TextSource(const DiagnosticBundleSection section, std::string path,
                                                            Json value)
        {
            return {.Section = section, .ArchivePath = std::move(path), .Contents = value.dump(2) + '\n'};
        }

        [[nodiscard]] Json BuildIdentity(const DiagnosticBundleProductSnapshot& snapshot)
        {
            const auto& build = GetBuildInfo();
            return {{"product", snapshot.Product},          {"keireVersion", build.Version},
                    {"gitCommit", build.GitCommit},         {"dirtyBuild", build.Dirty},
                    {"configuration", build.Configuration}, {"compiler", build.Compiler},
                    {"platform", build.Platform},           {"architecture", build.Architecture}};
        }

        [[nodiscard]] Json RendererReport(const DiagnosticBundleProductSnapshot& snapshot)
        {
            const auto& capabilities = snapshot.RendererCapabilities;
            const auto& identity = snapshot.RendererIdentity;
            return {{"available", snapshot.RendererAvailable},
                    {"backendFamily", snapshot.RendererAvailable ? "SDL_GPU" : "none"},
                    {"adapterIdentityAvailable", identity.Available && !identity.Adapter.empty()},
                    {"backend", identity.Backend},
                    {"adapter", identity.Adapter},
                    {"driverName", identity.DriverName},
                    {"driverVersion", identity.DriverVersion},
                    {"driverInformation", identity.DriverInformation},
                    {"deviceGeneration", identity.DeviceGeneration},
                    {"mode", RenderModeName(snapshot.RendererMode)},
                    {"capabilities",
                     {{"cpuVfxSimulation", capabilities.CpuVfxSimulation},
                      {"gpuVfxSimulation", capabilities.GpuVfxSimulation},
                      {"transparentPass", capabilities.TransparentPass},
                      {"dynamicSpritePackets", capabilities.DynamicSpritePackets},
                      {"texturedSpritePackets", capabilities.TexturedSpritePackets},
                      {"dynamicMeshPackets", capabilities.DynamicMeshPackets},
                      {"sampledResolvedDepth", capabilities.SampledResolvedDepth},
                      {"gpuDepthCollision", capabilities.GpuDepthCollision},
                      {"gpuOcclusionCulling", capabilities.GpuOcclusionCulling}}}};
        }

        [[nodiscard]] Json HardwareReport(const SystemHardwareIdentity& hardware)
        {
            const bool operatingSystemIdentityAvailable =
                !hardware.OperatingSystemDescription.empty() || !hardware.OperatingSystemVersion.empty();
            const bool cpuIdentityAvailable = !hardware.CpuModel.empty();
            const bool logicalProcessorCountAvailable = hardware.LogicalProcessorCount != 0U;
            const bool physicalMemoryAvailable = hardware.PhysicalMemoryBytes != 0U;
            return {{"available", operatingSystemIdentityAvailable || cpuIdentityAvailable ||
                                      logicalProcessorCountAvailable || physicalMemoryAvailable},
                    {"operatingSystemIdentityAvailable", operatingSystemIdentityAvailable},
                    {"operatingSystem", hardware.OperatingSystemDescription},
                    {"operatingSystemDescription", hardware.OperatingSystemDescription},
                    {"operatingSystemVersion", hardware.OperatingSystemVersion},
                    {"cpuIdentityAvailable", cpuIdentityAvailable},
                    {"cpuModel", hardware.CpuModel},
                    {"cpuArchitecture", GetBuildInfo().Architecture},
                    {"logicalProcessorCountAvailable", logicalProcessorCountAvailable},
                    {"logicalProcessorCount", hardware.LogicalProcessorCount},
                    {"physicalMemoryAvailable", physicalMemoryAvailable},
                    {"physicalMemoryBytes", hardware.PhysicalMemoryBytes}};
        }

        [[nodiscard]] Json RendererStatisticsReport(const DiagnosticBundleProductSnapshot& snapshot)
        {
            const auto& statistics = snapshot.RendererStatistics;
            Json timelines = Json::array();
            constexpr std::size_t maximumTimelineRecords = 256U;
            const auto first = snapshot.RendererTimelines.size() > maximumTimelineRecords
                                   ? snapshot.RendererTimelines.size() - maximumTimelineRecords
                                   : 0U;
            for (std::size_t index = first; index < snapshot.RendererTimelines.size(); ++index)
            {
                const auto& timeline = snapshot.RendererTimelines[index];
                timelines.push_back({{"frame", timeline.Frame},
                                     {"ownerUpdateMilliseconds", timeline.OwnerUpdateMilliseconds},
                                     {"captureMilliseconds", timeline.CaptureMilliseconds},
                                     {"admissionWaitMilliseconds", timeline.AdmissionWaitMilliseconds},
                                     {"queueDelayMilliseconds", timeline.QueueDelayMilliseconds},
                                     {"renderCpuMilliseconds", timeline.RenderCpuMilliseconds},
                                     {"gpuRetirementMilliseconds", timeline.GpuRetirementMilliseconds},
                                     {"submitToPresentMilliseconds", timeline.SubmitToPresentMilliseconds},
                                     {"outstandingAtAdmission", timeline.OutstandingAtAdmission},
                                     {"retriedAfterDeviceLoss", timeline.RetriedAfterDeviceLoss},
                                     {"presented", timeline.Presented},
                                     {"cancelled", timeline.Cancelled}});
            }
            return {{"frame", statistics.Frame},
                    {"surfaces", statistics.Surfaces},
                    {"drawCalls", statistics.DrawCalls},
                    {"triangles", statistics.Triangles},
                    {"allowedFramesInFlight", statistics.AllowedFramesInFlight},
                    {"outstandingFrames", statistics.OutstandingFrames},
                    {"framesInFlightHighWaterMark", statistics.FramesInFlightHighWaterMark},
                    {"queueHighWaterMark", statistics.RendererQueueHighWaterMark},
                    {"acceptedFrames", statistics.AcceptedFrames},
                    {"presentedFrames", statistics.PresentedFrames},
                    {"retiredFrames", statistics.RetiredFrames},
                    {"cancelledFrames", statistics.CancelledFrames},
                    {"lastAcceptedFrame", statistics.LastAcceptedFrame},
                    {"lastPresentedFrame", statistics.LastPresentedFrame},
                    {"lastRetiredFrame", statistics.LastRetiredFrame},
                    {"ownerUpdateMilliseconds", statistics.OwnerUpdateMilliseconds},
                    {"frameCaptureMilliseconds", statistics.FrameCaptureMilliseconds},
                    {"frameAdmissionWaitMilliseconds", statistics.FrameAdmissionWaitMilliseconds},
                    {"rendererQueueDelayMilliseconds", statistics.RendererQueueDelayMilliseconds},
                    {"renderCpuMilliseconds", statistics.RenderCpuMilliseconds},
                    {"gpuRetirementMilliseconds", statistics.GpuRetirementMilliseconds},
                    {"submitToPresentMilliseconds", statistics.SubmitToPresentMilliseconds},
                    {"cpuPreparationMilliseconds", statistics.CpuPreparationMilliseconds},
                    {"commandRecordingMilliseconds", statistics.CommandRecordingMilliseconds},
                    {"gpuSubmissionMilliseconds", statistics.GpuSubmissionMilliseconds},
                    {"gpuCompletionLatencyMilliseconds", statistics.GpuCompletionLatencyMilliseconds},
                    {"gpuTimingSupported", statistics.GpuTimingSupported},
                    {"vfxPipelinesReady", statistics.VfxPipelinesReady},
                    {"recentTimelines", std::move(timelines)},
                    {"omittedTimelineCount", first}};
        }

        [[nodiscard]] Json ProjectReport(const DiagnosticBundleProjectMetadata& project)
        {
            Json result{{"kind", project.Kind},
                        {"writable", project.Writable},
                        {"startupSceneConfigured", project.StartupSceneConfigured},
                        {"defaultInputConfigured", project.DefaultInputConfigured}};
            if (project.SchemaVersion)
                result["schemaVersion"] = *project.SchemaVersion;
            if (project.AssetCount)
                result["assetCount"] = *project.AssetCount;
            if (project.RecentProjectCount)
                result["recentProjectCount"] = *project.RecentProjectCount;
            if (project.PinnedProjectCount)
                result["pinnedProjectCount"] = *project.PinnedProjectCount;
            return result;
        }

        [[nodiscard]] Json PackageReport(std::vector<DiagnosticBundlePackageVersion> packages)
        {
            std::ranges::sort(packages, [](const auto& first, const auto& second)
                              { return std::tie(first.Id, first.Version) < std::tie(second.Id, second.Version); });
            packages.erase(std::unique(packages.begin(), packages.end(), [](const auto& first, const auto& second)
                                       { return first.Id == second.Id && first.Version == second.Version; }),
                           packages.end());
            Json result = Json::array();
            for (const auto& package : packages)
                result.push_back({{"id", package.Id}, {"version", package.Version}});
            return {{"packages", std::move(result)}};
        }

        [[nodiscard]] Json FailureReport(std::vector<DiagnosticBundleFailureSummary> failures,
                                         const std::optional<GpuDeviceLossDiagnostic>& deviceLoss)
        {
            const auto operationCode = [](const std::string_view operation) noexcept -> std::string_view
            {
                constexpr std::array knownOperations{
                    std::pair{"SDL_AcquireGPUCommandBuffer", "sdl.acquire-command-buffer"},
                    std::pair{"SDL_BeginGPUCopyPass", "sdl.begin-copy-pass"},
                    std::pair{"SDL_BeginGPURenderPass", "sdl.begin-render-pass"},
                    std::pair{"SDL_CancelGPUCommandBuffer", "sdl.cancel-command-buffer"},
                    std::pair{"SDL_ClaimWindowForGPUDevice", "sdl.claim-window"},
                    std::pair{"SDL_CreateGPU", "sdl.create-resource"},
                    std::pair{"SDL_MapGPUTransferBuffer", "sdl.map-transfer-buffer"},
                    std::pair{"SDL_QueryGPUFence(timeout)", "sdl.fence-retirement-timeout"},
                    std::pair{"SDL_QueryGPUFence", "sdl.query-fence"},
                    std::pair{"SDL_SubmitGPUCommandBuffer", "sdl.submit-command-buffer"},
                    std::pair{"SDL_WaitAndAcquireGPUSwapchainTexture", "sdl.acquire-swapchain"},
                    std::pair{"SDL_WaitForGPUFences", "sdl.wait-fence"}};
                const auto match = std::ranges::find_if(knownOperations, [operation](const auto& known)
                                                        { return operation.starts_with(known.first); });
                return match == knownOperations.end() ? "unknown" : match->second;
            };
            const auto sanitized = [](const std::string_view value)
            { return DiagnosticBundleDetail::SanitizeText(value).Contents; };
            std::ranges::sort(failures, [](const auto& first, const auto& second)
                              { return std::tie(first.Kind, first.Code) < std::tie(second.Kind, second.Code); });
            std::vector<DiagnosticBundleFailureSummary> combined;
            for (auto& failure : failures)
            {
                if (!combined.empty() && combined.back().Kind == failure.Kind && combined.back().Code == failure.Code)
                {
                    const auto available = (std::numeric_limits<std::uint64_t>::max)() - combined.back().Count;
                    combined.back().Count += (std::min)(failure.Count, available);
                }
                else
                {
                    combined.push_back(std::move(failure));
                }
            }

            constexpr std::size_t maximumFailureRecords = 64U;
            Json result = Json::array();
            if (deviceLoss)
            {
                const bool retirementTimeout = deviceLoss->Operation.starts_with("SDL_QueryGPUFence(timeout)");
                result.push_back(
                    {{"kind", retirementTimeout ? "gpu-retirement-timeout" : "device-loss"},
                     {"code", retirementTimeout ? "renderer.gpu-retirement-timeout" : "renderer.device-loss"},
                     {"operationCode", operationCode(deviceLoss->Operation)},
                     {"backend", sanitized(deviceLoss->Backend)},
                     {"adapter", sanitized(deviceLoss->Adapter)},
                     {"driverName", sanitized(deviceLoss->DriverName)},
                     {"driverVersion", sanitized(deviceLoss->DriverVersion)},
                     {"driverInformation", sanitized(deviceLoss->DriverInformation)},
                     {"driverDetail", sanitized(deviceLoss->DriverDetail)},
                     {"frame", deviceLoss->Frame},
                     {"deviceGeneration", deviceLoss->DeviceGeneration},
                     {"recoveryAttempt", deviceLoss->RecoveryAttempt},
                     {"recoveredDeviceGeneration", deviceLoss->RecoveredDeviceGeneration},
                     {"recoveryElapsedMilliseconds", deviceLoss->RecoveryElapsedMilliseconds},
                     {"recoverySucceeded", deviceLoss->RecoverySucceeded}});
            }
            const auto available = maximumFailureRecords - result.size();
            const auto included = (std::min)(available, combined.size());
            for (std::size_t index = 0; index < included; ++index)
            {
                const auto& failure = combined[index];
                result.push_back({{"kind", failure.Kind}, {"code", failure.Code}, {"count", failure.Count}});
            }
            return {{"bounded", true},
                    {"maximumRecords", maximumFailureRecords},
                    {"omittedRecordCount", combined.size() - included},
                    {"messageTextIncluded", false},
                    {"records", std::move(result)}};
        }

        [[nodiscard]] Json LastFailureReport(const DiagnosticBundleProductSnapshot& snapshot)
        {
            Json result{{"recognizedKeireRecordAvailable", snapshot.LastFailure.has_value()},
                        {"nativeDumpIncluded", false},
                        {"messageTextIncluded", false}};
            if (!snapshot.LastFailure)
                return result;
            Json record{{"kind", snapshot.LastFailure->Kind}, {"code", snapshot.LastFailure->Code}};
            if (snapshot.LastFailure->Frame)
                record["frame"] = *snapshot.LastFailure->Frame;
            if (snapshot.LastFailure->DeviceGeneration)
                record["deviceGeneration"] = *snapshot.LastFailure->DeviceGeneration;
            result["record"] = std::move(record);
            return result;
        }
    } // namespace

    DiagnosticBundleRequest
    CreateProductDiagnosticBundleRequestForTesting(const DiagnosticBundleProductSnapshot& snapshot,
                                                   const SystemHardwareIdentity& hardware)
    {
        DiagnosticBundleRequest result;
        result.TextSources = {
            TextSource(DiagnosticBundleSection::System, "system/build.json", BuildIdentity(snapshot)),
            TextSource(DiagnosticBundleSection::System, "system/hardware.json", HardwareReport(hardware)),
            TextSource(DiagnosticBundleSection::Renderer, "renderer/capabilities.json", RendererReport(snapshot)),
            TextSource(DiagnosticBundleSection::Renderer, "renderer/statistics.json",
                       RendererStatisticsReport(snapshot)),
            TextSource(DiagnosticBundleSection::Project, "project/metadata.json", ProjectReport(snapshot.Project)),
            TextSource(DiagnosticBundleSection::Packages, "packages/versions.json", PackageReport(snapshot.Packages)),
            TextSource(DiagnosticBundleSection::Failures, "failures/recent.json",
                       FailureReport(snapshot.Failures, snapshot.LastDeviceLoss)),
            TextSource(DiagnosticBundleSection::Crash, "crash/last-failure.json", LastFailureReport(snapshot))};

        for (std::size_t index = 0; index < snapshot.LogFiles.size(); ++index)
        {
            const auto& file = snapshot.LogFiles[index];
            const auto archiveName = index == 0U   ? std::string("core.log")
                                     : index == 1U ? std::string("client.log")
                                                   : "additional-" + std::to_string(index - 1U) + ".log";
            result.LogSources.push_back(
                {.ArchivePath = "logs/" + archiveName, .TrustedRoot = snapshot.LogRoot, .RelativePath = file});
        }
        return result;
    }

    DiagnosticBundleRequest CreateProductDiagnosticBundleRequest(const DiagnosticBundleProductSnapshot& snapshot)
    {
        return CreateProductDiagnosticBundleRequestForTesting(snapshot, QuerySystemHardwareIdentity());
    }
} // namespace Keire::Internal
