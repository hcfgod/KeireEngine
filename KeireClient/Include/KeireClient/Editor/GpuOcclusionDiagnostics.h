#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    enum class GpuOcclusionDiagnosticState : std::uint8_t
    {
        Unavailable,
        Disabled,
        WaitingForReadback,
        Active,
        Fallback
    };

    struct GpuOcclusionDiagnostics final
    {
        GpuOcclusionDiagnosticState State = GpuOcclusionDiagnosticState::Unavailable;
        std::string Status;
        std::string Visibility;
        std::string Pyramid;
        std::string Readback;
        std::string Recording;
        bool CountersConsistent = true;
        bool ReadbackFresh = false;
        bool Warning = false;
    };

    [[nodiscard]] inline constexpr std::string_view GpuOcclusionModeName(const Keire::GpuOcclusionMode mode) noexcept
    {
        switch (mode)
        {
        case Keire::GpuOcclusionMode::Disabled:
            return "Disabled";
        case Keire::GpuOcclusionMode::Automatic:
            return "Automatic";
        case Keire::GpuOcclusionMode::Forced:
            return "Forced";
        }
        return "Unknown";
    }

    [[nodiscard]] inline constexpr std::string_view
    GpuOcclusionDebugViewName(const Keire::GpuOcclusionDebugView view) noexcept
    {
        switch (view)
        {
        case Keire::GpuOcclusionDebugView::None:
            return "None";
        case Keire::GpuOcclusionDebugView::VisibilityBounds:
            return "Visibility Bounds";
        case Keire::GpuOcclusionDebugView::HierarchicalDepth:
            return "Hierarchical Depth";
        }
        return "Unknown";
    }

    [[nodiscard]] inline constexpr std::string_view
    GpuOcclusionModeDescription(const Keire::GpuOcclusionMode mode) noexcept
    {
        switch (mode)
        {
        case Keire::GpuOcclusionMode::Disabled:
            return "Submit CPU-frustum-visible geometry without GPU occlusion culling.";
        case Keire::GpuOcclusionMode::Automatic:
            return "Use GPU occlusion when the backend supports it, with a deterministic direct-draw fallback.";
        case Keire::GpuOcclusionMode::Forced:
            return "Bypass profitability thresholds but retain safety checks; failures are an explicit fallback.";
        }
        return "The project contains an unsupported GPU occlusion mode.";
    }

    [[nodiscard]] inline constexpr std::string_view
    GpuOcclusionFallbackReasonName(const Keire::GpuOcclusionFallbackReason reason) noexcept
    {
        switch (reason)
        {
        case Keire::GpuOcclusionFallbackReason::None:
            return "none";
        case Keire::GpuOcclusionFallbackReason::DisabledBySetting:
            return "disabled by project setting";
        case Keire::GpuOcclusionFallbackReason::UnsupportedBackend:
            return "unsupported GPU backend";
        case Keire::GpuOcclusionFallbackReason::PipelineUnavailable:
            return "compute pipeline unavailable";
        case Keire::GpuOcclusionFallbackReason::ResourceAllocationFailed:
            return "GPU resource allocation failed";
        case Keire::GpuOcclusionFallbackReason::NoSafeOccluders:
            return "no safe occluders";
        case Keire::GpuOcclusionFallbackReason::BelowAutomaticThreshold:
            return "below the Automatic activation threshold";
        case Keire::GpuOcclusionFallbackReason::NoEligibleCandidates:
            return "no eligible candidates";
        case Keire::GpuOcclusionFallbackReason::LegacyShaderAbi:
            return "legacy shader ABI requires direct draws";
        case Keire::GpuOcclusionFallbackReason::OversizedBatch:
            return "instance batch exceeds the occlusion limit";
        case Keire::GpuOcclusionFallbackReason::ReadbackValidationFailed:
            return "GPU visibility readback failed validation";
        }
        return "unknown fallback reason";
    }

    namespace Detail
    {
        [[nodiscard]] inline std::string OcclusionMilliseconds(const float value)
        {
            if (!std::isfinite(value) || value < 0.0F)
                return "n/a";
            constexpr double maximum = 1'000'000'000'000.0;
            const auto tenths =
                static_cast<std::uint64_t>(std::llround(std::min(static_cast<double>(value), maximum) * 10.0));
            return std::to_string(tenths / 10U) + "." + std::to_string(tenths % 10U);
        }

        [[nodiscard]] inline std::string FallbackEventSummary(const std::uint32_t count)
        {
            return std::to_string(count) + (count == 1U ? " fallback event" : " fallback events");
        }
    } // namespace Detail

    [[nodiscard]] inline GpuOcclusionDiagnostics
    BuildGpuOcclusionDiagnostics(const Keire::RenderCapabilities capabilities,
                                 const Keire::RenderStatistics& statistics)
    {
        GpuOcclusionDiagnostics result;
        if (!capabilities.GpuOcclusionCulling)
        {
            result.Status = "GPU occlusion unavailable on the active renderer backend";
            result.Visibility = "Direct draws remain active";
            result.Pyramid = "HZB not allocated";
            result.Readback = "Visibility readback unavailable";
            result.Recording = "Occlusion recording 0.0 ms";
            result.Warning = statistics.GpuOcclusionEnabled || statistics.GpuOcclusionFallbackActive;
            return result;
        }

        if (statistics.GpuOcclusionFallbackActive && !statistics.GpuOcclusionEnabled)
        {
            result.State = GpuOcclusionDiagnosticState::Fallback;
            result.Status = "GPU occlusion fallback active (direct draws)";
            result.Visibility = "Direct draws remain active";
            result.Pyramid = "HZB unavailable during direct-draw fallback";
            result.Readback = "Visibility readback unavailable while direct-draw fallback is active";
            result.Recording = "Occlusion recording 0.0 ms";
            result.Warning = true;
            if (statistics.GpuOcclusionFallbacks != 0U)
                result.Status += " / " + Detail::FallbackEventSummary(statistics.GpuOcclusionFallbacks);
            return result;
        }

        if (!statistics.GpuOcclusionEnabled)
        {
            result.State = GpuOcclusionDiagnosticState::Disabled;
            result.Status = "GPU occlusion disabled";
            result.Visibility = "Direct draws remain active";
            result.Pyramid = "HZB idle";
            result.Readback = "Visibility readback unavailable while GPU occlusion is disabled";
            result.Recording = "Occlusion recording 0.0 ms";
            result.Warning = statistics.GpuOcclusionFallbackActive;
            return result;
        }

        const auto classified = static_cast<std::uint64_t>(statistics.GpuOcclusionVisible) +
                                static_cast<std::uint64_t>(statistics.GpuOcclusionCulled);
        result.CountersConsistent =
            !statistics.GpuOcclusionReadbackValid || classified == statistics.GpuOcclusionCandidates;
        const auto culledPercent =
            statistics.GpuOcclusionCandidates == 0
                ? 0U
                : static_cast<std::uint32_t>(static_cast<std::uint64_t>(statistics.GpuOcclusionCulled) * 100U /
                                             statistics.GpuOcclusionCandidates);
        result.Visibility = std::to_string(statistics.GpuOcclusionCandidates) + " candidates / " +
                            std::to_string(statistics.GpuOcclusionVisible) + " visible / " +
                            std::to_string(statistics.GpuOcclusionCulled) + " culled (" +
                            std::to_string(culledPercent) + "%)";
        result.Pyramid = "Last completed frame: " + std::to_string(statistics.GpuOcclusionPyramidMipCount) +
                         " HZB mips / " + std::to_string(statistics.GpuOcclusionSafeOccluders) + " safe occluders / " +
                         std::to_string(statistics.GpuOcclusionDispatches) + " dispatches / " +
                         std::to_string(statistics.GpuOcclusionIndirectDraws) + " indirect draws";
        result.Recording =
            "Last completed frame occlusion recording " +
            Detail::OcclusionMilliseconds(statistics.GpuOcclusionDepthPassMilliseconds) + " ms depth / " +
            Detail::OcclusionMilliseconds(statistics.GpuOcclusionPyramidRecordingMilliseconds) + " ms pyramid / " +
            Detail::OcclusionMilliseconds(statistics.GpuOcclusionCullingRecordingMilliseconds) + " ms cull";

        if (statistics.GpuOcclusionReadbackValid &&
            statistics.GpuOcclusionReadbackAge != std::numeric_limits<std::uint32_t>::max())
        {
            result.ReadbackFresh = statistics.GpuOcclusionReadbackAge <= 1U;
            result.Readback = statistics.GpuOcclusionReadbackAge == 0U
                                  ? "Visibility readback current"
                                  : "Visibility readback " + std::to_string(statistics.GpuOcclusionReadbackAge) +
                                        (statistics.GpuOcclusionReadbackAge == 1U ? " frame old" : " frames old");
        }
        else
        {
            result.Readback = "Visibility readback pending; counters are not synchronized";
        }

        if (statistics.GpuOcclusionFallbackActive)
        {
            result.State = statistics.GpuOcclusionReadbackValid ? GpuOcclusionDiagnosticState::Active
                                                                : GpuOcclusionDiagnosticState::WaitingForReadback;
            result.Status = "GPU occlusion active";
            if (statistics.GpuOcclusionActiveSurfaces != 0U)
            {
                result.Status += " on " + std::to_string(statistics.GpuOcclusionActiveSurfaces) +
                                 (statistics.GpuOcclusionActiveSurfaces == 1U ? " surface" : " surfaces");
            }
            if (statistics.GpuOcclusionFallbackSurfaces != 0U)
            {
                result.Status += " / direct-draw fallback on " +
                                 std::to_string(statistics.GpuOcclusionFallbackSurfaces) +
                                 (statistics.GpuOcclusionFallbackSurfaces == 1U ? " surface" : " surfaces");
            }
            if (statistics.GpuOcclusionPartialFallbackSurfaces != 0U)
            {
                result.Status += " / partial direct draws on " +
                                 std::to_string(statistics.GpuOcclusionPartialFallbackSurfaces) + " active " +
                                 (statistics.GpuOcclusionPartialFallbackSurfaces == 1U ? "surface" : "surfaces");
            }
            if (statistics.GpuOcclusionFallbackSurfaces == 0U && statistics.GpuOcclusionPartialFallbackSurfaces == 0U)
            {
                result.Status += " / some draws use direct-draw fallback";
            }
            result.Warning = true;
        }
        else if (!statistics.GpuOcclusionReadbackValid)
        {
            result.State = GpuOcclusionDiagnosticState::WaitingForReadback;
            result.Status = "GPU occlusion active; waiting for asynchronous visibility readback";
        }
        else
        {
            result.State = GpuOcclusionDiagnosticState::Active;
            result.Status = "GPU occlusion active";
            result.Warning = !result.CountersConsistent || statistics.GpuOcclusionReadbackAge > 3U;
        }

        if (!result.CountersConsistent)
        {
            result.Readback += "; candidate accounting is inconsistent";
            result.Warning = true;
        }
        if (statistics.GpuOcclusionFallbacks != 0U)
            result.Status += " / " + Detail::FallbackEventSummary(statistics.GpuOcclusionFallbacks);
        return result;
    }

    [[nodiscard]] inline GpuOcclusionDiagnostics
    BuildGpuOcclusionSurfaceDiagnostics(const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics,
                                        const Keire::RenderStatistics* aggregate = nullptr)
    {
        GpuOcclusionDiagnostics result;
        const auto requested = std::string(GpuOcclusionModeName(diagnostics.RequestedMode));
        const auto effective = std::string(GpuOcclusionModeName(diagnostics.EffectiveMode));
        const auto classified =
            static_cast<std::uint64_t>(diagnostics.Visible) + static_cast<std::uint64_t>(diagnostics.Culled);
        result.CountersConsistent = !diagnostics.ReadbackValid || classified == diagnostics.Candidates;
        const auto culledPercent = diagnostics.Candidates == 0
                                       ? 0U
                                       : static_cast<std::uint32_t>(static_cast<std::uint64_t>(diagnostics.Culled) *
                                                                    100U / diagnostics.Candidates);
        result.Visibility = std::to_string(diagnostics.Candidates) + " candidates / " +
                            std::to_string(diagnostics.Visible) + " visible / " + std::to_string(diagnostics.Culled) +
                            " culled (" + std::to_string(culledPercent) + "%)";
        result.Pyramid = std::to_string(diagnostics.PyramidMipCount) + " HZB mips / " +
                         std::to_string(diagnostics.SafeOccluders) + " safe occluders";
        if (!diagnostics.ReadbackValid)
        {
            result.Visibility = std::to_string(diagnostics.EligibleCandidates) + " eligible candidates / " +
                                std::to_string(diagnostics.EligibleCandidateTriangles) + " candidate triangles";
            result.Pyramid =
                std::to_string(diagnostics.EligibleSafeOccluders) + " eligible safe occluders / no completed HZB";
        }
        if (aggregate && diagnostics.State == Keire::GpuOcclusionSurfaceState::Active)
        {
            result.Pyramid +=
                " / last completed frame aggregate: " + std::to_string(aggregate->GpuOcclusionDispatches) +
                " dispatches / " + std::to_string(aggregate->GpuOcclusionIndirectDraws) + " indirect draws";
            result.Recording =
                "Last completed frame occlusion recording " +
                Detail::OcclusionMilliseconds(aggregate->GpuOcclusionDepthPassMilliseconds) + " ms depth / " +
                Detail::OcclusionMilliseconds(aggregate->GpuOcclusionPyramidRecordingMilliseconds) + " ms pyramid / " +
                Detail::OcclusionMilliseconds(aggregate->GpuOcclusionCullingRecordingMilliseconds) + " ms cull";
        }
        else
            result.Recording = "Occlusion recording unavailable for this surface";

        if (diagnostics.ReadbackValid && diagnostics.ReadbackAge != std::numeric_limits<std::uint32_t>::max())
        {
            result.ReadbackFresh = diagnostics.ReadbackAge <= 1U;
            result.Readback =
                "Visibility source frame " + std::to_string(diagnostics.SourceFrame) +
                (diagnostics.ReadbackAge == 0U ? " (current)"
                                               : " (" + std::to_string(diagnostics.ReadbackAge) +
                                                     (diagnostics.ReadbackAge == 1U ? " frame old)" : " frames old)"));
        }
        else
            result.Readback = "Visibility readback pending; counters are not synchronized";

        const auto reason = std::string(GpuOcclusionFallbackReasonName(diagnostics.FallbackReason));
        switch (diagnostics.State)
        {
        case Keire::GpuOcclusionSurfaceState::Disabled:
            result.State = GpuOcclusionDiagnosticState::Disabled;
            result.Status = "GPU occlusion disabled (requested " + requested + ")";
            result.Readback = "Visibility readback unavailable while GPU occlusion is disabled";
            break;
        case Keire::GpuOcclusionSurfaceState::Unsupported:
            result.State = GpuOcclusionDiagnosticState::Unavailable;
            result.Status = "GPU occlusion unsupported: " + reason;
            result.Readback = "Visibility readback unavailable on the active renderer backend";
            result.Warning = diagnostics.RequestedMode == Keire::GpuOcclusionMode::Forced;
            break;
        case Keire::GpuOcclusionSurfaceState::Idle:
            result.State = GpuOcclusionDiagnosticState::Disabled;
            result.Status = diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::None
                                ? "GPU occlusion idle; surface was not rendered in the last completed frame"
                                : "GPU occlusion idle: " + reason;
            result.Readback = "Visibility readback unavailable while GPU occlusion is idle";
            break;
        case Keire::GpuOcclusionSurfaceState::Active:
            result.State = diagnostics.ReadbackValid ? GpuOcclusionDiagnosticState::Active
                                                     : GpuOcclusionDiagnosticState::WaitingForReadback;
            result.Status = "GPU occlusion active (requested " + requested + ", effective " + effective + ")";
            result.Warning = !result.CountersConsistent || (diagnostics.ReadbackValid && diagnostics.ReadbackAge > 3U);
            if (diagnostics.FallbackReason != Keire::GpuOcclusionFallbackReason::None)
            {
                result.Status += " / partial fallback: " + reason + " (some direct draws)";
                result.Warning = true;
            }
            break;
        case Keire::GpuOcclusionSurfaceState::Fallback:
            if (diagnostics.RequestedMode == Keire::GpuOcclusionMode::Automatic &&
                diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::BelowAutomaticThreshold)
            {
                result.State = GpuOcclusionDiagnosticState::Disabled;
                result.Status = "GPU occlusion idle: " + reason + " (direct draws; use Forced to validate this scene)";
            }
            else
            {
                result.State = GpuOcclusionDiagnosticState::Fallback;
                result.Status = "GPU occlusion fallback: " + reason + " (direct draws)";
                result.Warning = true;
            }
            result.Readback = "Visibility readback unavailable while direct-draw fallback is active";
            break;
        }
        if (!diagnostics.PyramidValid && diagnostics.State == Keire::GpuOcclusionSurfaceState::Active)
        {
            result.Pyramid += " / invalid";
            result.Warning = true;
        }
        if (!result.CountersConsistent)
        {
            result.Readback += "; candidate accounting is inconsistent";
            result.Warning = true;
        }
        return result;
    }

    [[nodiscard]] inline GpuOcclusionDiagnostics
    BuildGpuOcclusionPanelDiagnostics(const Keire::RenderCapabilities capabilities,
                                      const Keire::RenderStatistics& aggregate,
                                      const std::optional<Keire::GpuOcclusionSurfaceDiagnostics>& preferredSurface)
    {
        return preferredSurface ? BuildGpuOcclusionSurfaceDiagnostics(*preferredSurface, &aggregate)
                                : BuildGpuOcclusionDiagnostics(capabilities, aggregate);
    }
} // namespace KeireEditor
