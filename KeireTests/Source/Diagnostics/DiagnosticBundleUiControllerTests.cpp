#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleUiInternal.h"
#include "KeireInternal/Diagnostics/SystemHardwareIdentityInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Keire::Internal
{
    struct DiagnosticBundleDialogControllerTestAccess final
    {
        static void CompleteSaveDialog(DiagnosticBundleDialogController& controller, const SaveFileDialogStatus status,
                                       const std::filesystem::path& selectedPath, std::string diagnostic)
        {
            controller.CompleteSaveDialog(status, selectedPath, std::move(diagnostic));
        }
    };
} // namespace Keire::Internal

namespace
{
    class TestDirectory final
    {
      public:
        explicit TestDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }

        ~TestDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }

        std::filesystem::path Path;
    };
} // namespace

TEST_CASE("diagnostic product snapshots expose only the allowlisted metadata contract")
{
    Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
    snapshot.Product = "Kéire Editor";
    snapshot.RendererAvailable = true;
    snapshot.RendererMode = Keire::RenderMode::Rendered;
    snapshot.RendererIdentity = {.Available = true,
                                 .Backend = "direct3d12",
                                 .Adapter = "Allowlisted Test GPU",
                                 .DriverName = "Test Driver",
                                 .DriverVersion = "31.2.3",
                                 .DriverInformation = "WHQL",
                                 .DeviceGeneration = 4U};
    snapshot.RendererCapabilities.GpuVfxSimulation = true;
    snapshot.RendererStatistics.Frame = 42U;
    snapshot.RendererStatistics.AllowedFramesInFlight = 2U;
    snapshot.RendererStatistics.OutstandingFrames = 1U;
    snapshot.RendererStatistics.FramesInFlightHighWaterMark = 2U;
    snapshot.RendererStatistics.AcceptedFrames = 9U;
    snapshot.RendererStatistics.RetiredFrames = 8U;
    snapshot.RendererStatistics.FrameCaptureMilliseconds = 0.25F;
    snapshot.RendererStatistics.FrameAdmissionWaitMilliseconds = 0.5F;
    snapshot.RendererStatistics.RendererQueueDelayMilliseconds = 0.75F;
    snapshot.RendererStatistics.RenderCpuMilliseconds = 1.0F;
    snapshot.RendererStatistics.GpuRetirementMilliseconds = 1.25F;
    snapshot.RendererStatistics.SubmitToPresentMilliseconds = 1.5F;
    snapshot.RendererTimelines.push_back({.Frame = 42U,
                                          .CaptureMilliseconds = 0.25F,
                                          .AdmissionWaitMilliseconds = 0.5F,
                                          .QueueDelayMilliseconds = 0.75F,
                                          .RenderCpuMilliseconds = 1.0F,
                                          .GpuRetirementMilliseconds = 1.25F,
                                          .SubmitToPresentMilliseconds = 1.5F,
                                          .OutstandingAtAdmission = 2U});
    snapshot.Project = {.Kind = "active-editor-project",
                        .SchemaVersion = 4U,
                        .AssetCount = 17U,
                        .Writable = true,
                        .StartupSceneConfigured = true};
    snapshot.Packages = {{"z.package", "2.0.0"}, {"a.package", "1.0.0"}};
    snapshot.Failures = {{"KEIRE-TEST-0001", 2U}};
    for (std::uint32_t index = 0; index < 70U; ++index)
        snapshot.Failures.push_back({"KEIRE-BOUNDED-" + std::to_string(index), 1U});
    snapshot.LastDeviceLoss = {.Operation = "private operation text must not be copied",
                               .Backend = "direct3d12",
                               .Adapter = "Allowlisted Test GPU",
                               .DriverName = "Test Driver",
                               .DriverVersion = "31.2.3",
                               .DriverDetail = "C:/Users/Private/driver-detail.txt",
                               .Frame = 41U,
                               .DeviceGeneration = 3U,
                               .RecoveryAttempt = 1U,
                               .RecoveredDeviceGeneration = 4U,
                               .RecoveryElapsedMilliseconds = 12.5F,
                               .RecoverySucceeded = true};
    snapshot.LastFailure = Keire::Internal::DiagnosticBundleLastFailureRecord{
        .Kind = "handled-exception", .Code = "KEIRE-LAST-0001", .Frame = 40U};
    snapshot.LogRoot = "C:/Users/Private/SecretProject/Logs";
    snapshot.LogFiles = {"Keith-private-core.log", "SecretProject-editor.log"};

    const auto request = Keire::Internal::CreateProductDiagnosticBundleRequest(snapshot);
    const auto findText = [&request](const std::string_view path) -> std::string_view
    {
        const auto found =
            std::ranges::find(request.TextSources, path, &Keire::Internal::DiagnosticBundleTextSource::ArchivePath);
        return found == request.TextSources.end() ? std::string_view{} : found->Contents;
    };

    REQUIRE_FALSE(findText("project/metadata.json").empty());
    CHECK(findText("project/metadata.json").find("SecretProject") == std::string::npos);
    CHECK(findText("project/metadata.json").find("projectName") == std::string::npos);
    CHECK(findText("project/metadata.json").find("projectPath") == std::string::npos);
    CHECK(findText("packages/versions.json").find("a.package") < findText("packages/versions.json").find("z.package"));
    CHECK(findText("packages/versions.json").find("sourceUrl") == std::string::npos);
    CHECK(findText("failures/recent.json").find("messageTextIncluded\": false") != std::string::npos);
    const auto renderer = nlohmann::json::parse(findText("renderer/capabilities.json"));
    CHECK(renderer.at("adapterIdentityAvailable") == true);
    CHECK(renderer.at("adapter") == "Allowlisted Test GPU");
    CHECK(renderer.at("driverVersion") == "31.2.3");
    const auto statistics = nlohmann::json::parse(findText("renderer/statistics.json"));
    CHECK(statistics.at("submitToPresentMilliseconds").get<double>() == doctest::Approx(1.5));
    CHECK(statistics.at("outstandingFrames") == 1U);
    CHECK(statistics.at("framesInFlightHighWaterMark") == 2U);
    REQUIRE(statistics.at("recentTimelines").size() == 1U);
    CHECK(statistics.at("recentTimelines").front().at("frame") == 42U);
    const auto failures = nlohmann::json::parse(findText("failures/recent.json"));
    CHECK(failures.at("maximumRecords") == 64U);
    CHECK(failures.at("records").size() == 64U);
    CHECK(failures.at("omittedRecordCount") == 8U);
    CHECK(failures.at("records").front().at("kind") == "device-loss");
    CHECK(failures.at("records").front().at("operationCode") == "unknown");
    CHECK(failures.at("records").front().at("driverDetail") == "<redacted:path>");
    CHECK(findText("failures/recent.json").find("private operation") == std::string::npos);
    CHECK(findText("failures/recent.json").find("driver-detail") == std::string::npos);
    const auto lastFailure = nlohmann::json::parse(findText("crash/last-failure.json"));
    CHECK(lastFailure.at("recognizedKeireRecordAvailable") == true);
    CHECK(lastFailure.at("record").at("code") == "KEIRE-LAST-0001");
    const auto hardware = nlohmann::json::parse(findText("system/hardware.json"));
    CHECK(hardware.contains("operatingSystemDescription"));
    CHECK(hardware.contains("operatingSystemVersion"));
    CHECK(hardware.contains("cpuModel"));
    for (const auto& source : request.TextSources)
    {
        CHECK(source.Contents.find("C:/Users/Private") == std::string::npos);
        CHECK(source.Contents.find("environment") == std::string::npos);
        CHECK(source.Contents.find("email") == std::string::npos);
    }
    REQUIRE(request.LogSources.size() == 2U);
    CHECK(request.LogSources[0].ArchivePath == "logs/core.log");
    CHECK(request.LogSources[0].RelativePath == "Keith-private-core.log");
    CHECK(request.LogSources[1].ArchivePath == "logs/client.log");
    CHECK(request.LogSources[1].RelativePath == "SecretProject-editor.log");
    CHECK(request.LogSources[0].ArchivePath.find("Keith") == std::string::npos);
    CHECK(request.LogSources[1].ArchivePath.find("SecretProject") == std::string::npos);
}

TEST_CASE("diagnostic product snapshots distinguish bounded fence timeouts from confirmed device loss")
{
    Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
    snapshot.Product = "Kéire Editor";
    snapshot.LastDeviceLoss = {.Operation = "SDL_QueryGPUFence(timeout)",
                               .Backend = "vulkan",
                               .Adapter = "Test GPU",
                               .DriverName = "Test Driver",
                               .DriverVersion = "1.2.3",
                               .DriverDetail = "retirement timeout",
                               .Frame = 9U,
                               .DeviceGeneration = 2U};
    const auto request = Keire::Internal::CreateProductDiagnosticBundleRequest(snapshot);
    const auto found = std::ranges::find(request.TextSources, std::string("failures/recent.json"),
                                         &Keire::Internal::DiagnosticBundleTextSource::ArchivePath);
    REQUIRE(found != request.TextSources.end());
    const auto failures = nlohmann::json::parse(found->Contents);
    REQUIRE(failures.at("records").size() == 1U);
    CHECK(failures.at("records").front().at("kind") == "gpu-retirement-timeout");
    CHECK(failures.at("records").front().at("code") == "renderer.gpu-retirement-timeout");
    CHECK(failures.at("records").front().at("operationCode") == "sdl.fence-retirement-timeout");
}

TEST_CASE("diagnostic product snapshots label unavailable hardware and renderer providers")
{
    Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
    snapshot.Product = "Kéire Editor";
    Keire::Internal::SystemHardwareIdentity hardware;
    hardware.LogicalProcessorCount = 0U;
    const auto request = Keire::Internal::CreateProductDiagnosticBundleRequestForTesting(snapshot, hardware);
    const auto findText = [&request](const std::string_view path) -> std::string_view
    {
        const auto found =
            std::ranges::find(request.TextSources, path, &Keire::Internal::DiagnosticBundleTextSource::ArchivePath);
        return found == request.TextSources.end() ? std::string_view{} : found->Contents;
    };

    const auto hardwareReport = nlohmann::json::parse(findText("system/hardware.json"));
    CHECK(hardwareReport.at("available") == false);
    CHECK(hardwareReport.at("operatingSystemIdentityAvailable") == false);
    CHECK(hardwareReport.at("cpuIdentityAvailable") == false);
    CHECK(hardwareReport.at("logicalProcessorCountAvailable") == false);
    CHECK(hardwareReport.at("physicalMemoryAvailable") == false);
    const auto rendererReport = nlohmann::json::parse(findText("renderer/capabilities.json"));
    CHECK(rendererReport.at("available") == false);
    CHECK(rendererReport.at("backendFamily") == "none");
    CHECK(rendererReport.at("adapterIdentityAvailable") == false);
}

TEST_CASE("diagnostic bundle dialog selection rebuilds the exact frozen preview")
{
    Keire::Internal::DiagnosticBundleRequest request;
    request.TextSources = {{.Section = Keire::Internal::DiagnosticBundleSection::System,
                            .ArchivePath = "system/build.json",
                            .Contents = "{}"},
                           {.Section = Keire::Internal::DiagnosticBundleSection::Project,
                            .ArchivePath = "project/metadata.json",
                            .Contents = "{}"},
                           {.Section = Keire::Internal::DiagnosticBundleSection::Packages,
                            .ArchivePath = "packages/versions.json",
                            .Contents = "{}"},
                           {.Section = Keire::Internal::DiagnosticBundleSection::Crash,
                            .ArchivePath = "crash/last-failure.json",
                            .Contents = "{}"}};

    Keire::Internal::DiagnosticBundleDialogController controller;
    controller.Open(std::move(request), "Keire-Diagnostics.zip");
    REQUIRE(controller.Bundle());
    CHECK(controller.Bundle()->Preview().size() == 5U);

    auto selection = controller.Selection();
    selection.IncludeProjectMetadata = false;
    selection.IncludePackageVersions = false;
    selection.IncludeCrashInformation = false;
    controller.SetSelection(selection);
    REQUIRE(controller.Bundle());
    CHECK(controller.Bundle()->Preview().size() == 2U);
    CHECK(controller.Bundle()->Omissions().size() == 3U);
    CHECK(std::ranges::none_of(controller.Bundle()->Preview(),
                               [](const auto& entry) { return entry.ArchivePath == "project/metadata.json"; }));

    controller.Shutdown();
    controller.Shutdown();
    CHECK(controller.Bundle() == nullptr);
    CHECK(controller.Status().empty());
    CHECK(controller.Error().empty());
}

TEST_CASE("diagnostic bundle dialog cancellation never publishes and shutdown stays idempotent")
{
    TestDirectory directory("diagnostic-bundle-dialog-cancel");
    Keire::Internal::DiagnosticBundleRequest request;
    request.TextSources = {{.Section = Keire::Internal::DiagnosticBundleSection::System,
                            .ArchivePath = "system/build.json",
                            .Contents = "{}"}};

    Keire::Internal::DiagnosticBundleDialogController controller;
    controller.Open(std::move(request), "Keire-Diagnostics.zip");
    REQUIRE(controller.Bundle());
    const auto output = directory.Path / "must-not-exist.zip";
    Keire::Internal::DiagnosticBundleDialogControllerTestAccess::CompleteSaveDialog(
        controller, Keire::SaveFileDialogStatus::Cancelled, output, "ignored cancellation diagnostic");

    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(controller.Status() == "Save cancelled. No archive was written.");
    CHECK(controller.Error().empty());
    CHECK(controller.Bundle() != nullptr);
    controller.Shutdown();
    controller.Shutdown();
    CHECK(controller.Bundle() == nullptr);
    CHECK(controller.Status().empty());
    CHECK(controller.Error().empty());
}
