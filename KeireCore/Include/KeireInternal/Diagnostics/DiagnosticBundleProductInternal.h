#pragma once

#include "Keire/Log.h"
#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleInternal.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Keire::Internal
{
    struct SystemHardwareIdentity;

    struct DiagnosticBundlePackageVersion final
    {
        std::string Id;
        std::string Version;
    };

    struct DiagnosticBundleFailureSummary final
    {
        std::string Code;
        std::uint64_t Count = 0;
        std::string Kind = "handled-exception";
    };

    struct DiagnosticBundleLastFailureRecord final
    {
        std::string Kind;
        std::string Code;
        std::optional<std::uint64_t> Frame;
        std::optional<std::uint32_t> DeviceGeneration;
    };

    struct DiagnosticBundleProjectMetadata final
    {
        std::string Kind;
        std::optional<std::uint32_t> SchemaVersion;
        std::optional<std::uint64_t> AssetCount;
        std::optional<std::uint64_t> RecentProjectCount;
        std::optional<std::uint64_t> PinnedProjectCount;
        bool Writable = false;
        bool StartupSceneConfigured = false;
        bool DefaultInputConfigured = false;
    };

    struct DiagnosticBundleProductSnapshot final
    {
        std::string Product;
        bool RendererAvailable = false;
        RenderMode RendererMode = RenderMode::Disabled;
        RenderDeviceIdentity RendererIdentity;
        RenderCapabilities RendererCapabilities;
        RenderStatistics RendererStatistics;
        std::vector<RenderFrameTimeline> RendererTimelines;
        std::optional<GpuDeviceLossDiagnostic> LastDeviceLoss;
        DiagnosticBundleProjectMetadata Project;
        std::vector<DiagnosticBundlePackageVersion> Packages;
        std::vector<DiagnosticBundleFailureSummary> Failures;
        std::filesystem::path LogRoot;
        std::vector<std::string> LogFiles;
        std::optional<DiagnosticBundleLastFailureRecord> LastFailure;
    };

    [[nodiscard]] DiagnosticBundleRequest
    CreateProductDiagnosticBundleRequest(const DiagnosticBundleProductSnapshot& snapshot);
    [[nodiscard]] DiagnosticBundleRequest
    CreateProductDiagnosticBundleRequestForTesting(const DiagnosticBundleProductSnapshot& snapshot,
                                                   const SystemHardwareIdentity& hardware);
} // namespace Keire::Internal
