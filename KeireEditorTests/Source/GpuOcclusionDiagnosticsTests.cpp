#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"

#include <doctest/doctest.h>

#include <limits>
#include <optional>
#include <string_view>

TEST_CASE("GPU occlusion project modes have explicit editor labels and fallback policy")
{
    using Keire::GpuOcclusionMode;

    CHECK(KeireEditor::GpuOcclusionModeName(GpuOcclusionMode::Disabled) == "Disabled");
    CHECK(KeireEditor::GpuOcclusionModeName(GpuOcclusionMode::Automatic) == "Automatic");
    CHECK(KeireEditor::GpuOcclusionModeName(GpuOcclusionMode::Forced) == "Forced");
    CHECK(KeireEditor::GpuOcclusionDebugViewName(Keire::GpuOcclusionDebugView::None) == "None");
    CHECK(KeireEditor::GpuOcclusionDebugViewName(Keire::GpuOcclusionDebugView::VisibilityBounds) ==
          "Visibility Bounds");
    CHECK(KeireEditor::GpuOcclusionDebugViewName(Keire::GpuOcclusionDebugView::HierarchicalDepth) ==
          "Hierarchical Depth");
    CHECK(KeireEditor::GpuOcclusionModeDescription(GpuOcclusionMode::Disabled).find("without GPU") !=
          std::string_view::npos);
    CHECK(KeireEditor::GpuOcclusionModeDescription(GpuOcclusionMode::Automatic).find("deterministic") !=
          std::string_view::npos);
    CHECK(KeireEditor::GpuOcclusionModeDescription(GpuOcclusionMode::Automatic).find("Visibility Bounds") !=
          std::string_view::npos);
    CHECK(KeireEditor::GpuOcclusionModeDescription(GpuOcclusionMode::Forced).find("explicit fallback") !=
          std::string_view::npos);
}

TEST_CASE("GPU occlusion diagnostics distinguish unavailable disabled and pending states")
{
    Keire::RenderCapabilities capabilities;
    Keire::RenderStatistics statistics;

    auto presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Unavailable);
    CHECK(presentation.Status.find("unavailable") != std::string::npos);
    CHECK_FALSE(presentation.Warning);

    capabilities.GpuOcclusionCulling = true;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Disabled);
    CHECK(presentation.Pyramid == "HZB idle");
    CHECK(presentation.Readback == "Visibility readback unavailable while GPU occlusion is disabled");
    CHECK(presentation.Readback.find("pending") == std::string::npos);

    statistics.GpuOcclusionEnabled = true;
    statistics.GpuOcclusionCandidates = 250;
    statistics.GpuOcclusionPyramidMipCount = 7;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::WaitingForReadback);
    CHECK(presentation.Visibility.find("250 candidates") != std::string::npos);
    CHECK(presentation.Pyramid.find("7 HZB mips") != std::string::npos);
    CHECK(presentation.Readback.find("pending") != std::string::npos);
    CHECK_FALSE(presentation.ReadbackFresh);
}

TEST_CASE("GPU occlusion diagnostics expose readback age accounting and fallback without hiding direct draws")
{
    Keire::RenderCapabilities capabilities;
    capabilities.GpuOcclusionCulling = true;
    Keire::RenderStatistics statistics;
    statistics.GpuOcclusionEnabled = true;
    statistics.GpuOcclusionReadbackValid = true;
    statistics.GpuOcclusionCandidates = 100;
    statistics.GpuOcclusionVisible = 36;
    statistics.GpuOcclusionCulled = 64;
    statistics.GpuOcclusionSafeOccluders = 12;
    statistics.GpuOcclusionIndirectDraws = 5;
    statistics.GpuOcclusionDispatches = 4;
    statistics.GpuOcclusionPyramidMipCount = 8;
    statistics.GpuOcclusionReadbackAge = 1;
    statistics.GpuOcclusionDepthPassMilliseconds = 0.14F;
    statistics.GpuOcclusionPyramidRecordingMilliseconds = 0.24F;
    statistics.GpuOcclusionCullingRecordingMilliseconds = 0.34F;

    auto presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.CountersConsistent);
    CHECK(presentation.ReadbackFresh);
    CHECK(presentation.Visibility.find("64 culled (64%)") != std::string::npos);
    CHECK(presentation.Pyramid.find("12 safe occluders") != std::string::npos);
    CHECK(presentation.Recording ==
          "Last completed frame occlusion recording 0.1 ms depth / 0.2 ms pyramid / 0.3 ms cull");

    statistics.GpuOcclusionDepthPassMilliseconds = std::numeric_limits<float>::infinity();
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.Recording.find("n/a ms depth") != std::string::npos);
    statistics.GpuOcclusionDepthPassMilliseconds = 0.14F;

    statistics.GpuOcclusionCulled = 63;
    statistics.GpuOcclusionReadbackAge = 5;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK_FALSE(presentation.CountersConsistent);
    CHECK_FALSE(presentation.ReadbackFresh);
    CHECK(presentation.Warning);
    CHECK(presentation.Readback.find("inconsistent") != std::string::npos);

    statistics.GpuOcclusionFallbackActive = true;
    statistics.GpuOcclusionFallbacks = 2;
    statistics.GpuOcclusionActiveSurfaces = 2;
    statistics.GpuOcclusionFallbackSurfaces = 1;
    statistics.GpuOcclusionPartialFallbackSurfaces = 1;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.Status.find("active on 2 surfaces") != std::string::npos);
    CHECK(presentation.Status.find("direct-draw fallback on 1 surface") != std::string::npos);
    CHECK(presentation.Status.find("partial direct draws on 1 active surface") != std::string::npos);
    CHECK(presentation.Status.find("2 fallback events") != std::string::npos);

    statistics.GpuOcclusionReadbackValid = false;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::WaitingForReadback);
    CHECK(presentation.Warning);
    CHECK(presentation.Status.find("active on 2 surfaces") != std::string::npos);
    CHECK(presentation.Status.find("direct-draw fallback on 1 surface") != std::string::npos);

    statistics = {};
    statistics.GpuOcclusionFallbackActive = true;
    statistics.GpuOcclusionFallbacks = 1;
    presentation = KeireEditor::BuildGpuOcclusionDiagnostics(capabilities, statistics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Fallback);
    CHECK(presentation.Status.find("direct draws") != std::string::npos);
    CHECK(presentation.Status.find("1 fallback event") != std::string::npos);
    CHECK(presentation.Readback == "Visibility readback unavailable while direct-draw fallback is active");
    CHECK(presentation.Readback.find("pending") == std::string::npos);
}

TEST_CASE("GPU occlusion panels prefer active Scene surface diagnostics over reset frame aggregates")
{
    Keire::RenderCapabilities capabilities;
    capabilities.GpuOcclusionCulling = true;
    Keire::RenderStatistics aggregate;
    Keire::GpuOcclusionSurfaceDiagnostics surface;
    surface.RequestedMode = Keire::GpuOcclusionMode::Forced;
    surface.EffectiveMode = Keire::GpuOcclusionMode::Forced;
    surface.State = Keire::GpuOcclusionSurfaceState::Active;
    surface.PyramidMipCount = 7;
    surface.PyramidValid = true;
    surface.ReadbackValid = true;
    surface.ReadbackAge = 1;
    surface.Candidates = 20;
    surface.Visible = 6;
    surface.Culled = 14;

    auto presentation = KeireEditor::BuildGpuOcclusionPanelDiagnostics(capabilities, aggregate, std::optional(surface));
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.Status.find("requested Forced, effective Forced") != std::string::npos);
    CHECK(presentation.Pyramid.find("7 HZB mips") != std::string::npos);

    aggregate.GpuOcclusionEnabled = true;
    aggregate.GpuOcclusionFallbackActive = true;
    aggregate.GpuOcclusionActiveSurfaces = 2;
    aggregate.GpuOcclusionFallbackSurfaces = 1;
    aggregate.GpuOcclusionDispatches = 28;
    aggregate.GpuOcclusionIndirectDraws = 4;
    aggregate.GpuOcclusionCandidates = 322;
    aggregate.GpuOcclusionVisible = 34;
    aggregate.GpuOcclusionCulled = 288;
    aggregate.GpuOcclusionReadbackValid = true;
    aggregate.GpuOcclusionReadbackAge = 1;
    presentation = KeireEditor::BuildGpuOcclusionPanelDiagnostics(capabilities, aggregate, std::nullopt);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.Status.find("active on 2 surfaces") != std::string::npos);
    CHECK(presentation.Status.find("direct-draw fallback on 1 surface") != std::string::npos);
    CHECK(presentation.Pyramid.find("28 dispatches / 4 indirect draws") != std::string::npos);

    surface.RequestedMode = Keire::GpuOcclusionMode::Automatic;
    surface.State = Keire::GpuOcclusionSurfaceState::Fallback;
    surface.EffectiveMode = Keire::GpuOcclusionMode::Disabled;
    surface.FallbackReason = Keire::GpuOcclusionFallbackReason::BelowAutomaticThreshold;
    surface.ReadbackValid = false;
    surface.EligibleCandidates = 6;
    surface.EligibleSafeOccluders = 4;
    surface.EligibleCandidateTriangles = 26'664;
    presentation = KeireEditor::BuildGpuOcclusionPanelDiagnostics(capabilities, aggregate, surface);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Disabled);
    CHECK_FALSE(presentation.Warning);
    CHECK(presentation.Status.find("below the Automatic activation threshold") != std::string::npos);
    CHECK(presentation.Status.find("use Forced to validate this scene") != std::string::npos);
    CHECK(presentation.Visibility.find("6 eligible candidates / 26664 candidate triangles") != std::string::npos);
    CHECK(presentation.Pyramid.find("4 eligible safe occluders") != std::string::npos);
    CHECK(presentation.Pyramid.find("28 dispatches") == std::string::npos);
}

TEST_CASE("GPU occlusion mode transitions do not mix reset surface state with prior frame workload")
{
    Keire::RenderCapabilities capabilities;
    capabilities.GpuOcclusionCulling = true;
    Keire::RenderStatistics priorFrame;
    priorFrame.GpuOcclusionEnabled = true;
    priorFrame.GpuOcclusionDispatches = 8;
    priorFrame.GpuOcclusionIndirectDraws = 3;
    priorFrame.GpuOcclusionDepthPassMilliseconds = 0.1F;

    Keire::GpuOcclusionSurfaceDiagnostics disabledSurface;
    disabledSurface.RequestedMode = Keire::GpuOcclusionMode::Disabled;
    disabledSurface.EffectiveMode = Keire::GpuOcclusionMode::Disabled;
    disabledSurface.State = Keire::GpuOcclusionSurfaceState::Disabled;
    disabledSurface.FallbackReason = Keire::GpuOcclusionFallbackReason::DisabledBySetting;

    const auto presentation = KeireEditor::BuildGpuOcclusionPanelDiagnostics(capabilities, priorFrame, disabledSurface);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Disabled);
    CHECK(presentation.Readback == "Visibility readback unavailable while GPU occlusion is disabled");
    CHECK(presentation.Pyramid.find("frame aggregate") == std::string::npos);
    CHECK(presentation.Pyramid.find("8 dispatches") == std::string::npos);
    CHECK(presentation.Recording == "Occlusion recording unavailable for this surface");
}

TEST_CASE("per-surface GPU occlusion diagnostics preserve typed fallback and source-frame context")
{
    Keire::GpuOcclusionSurfaceDiagnostics diagnostics;
    diagnostics.RequestedMode = Keire::GpuOcclusionMode::Forced;
    diagnostics.EffectiveMode = Keire::GpuOcclusionMode::Forced;
    diagnostics.State = Keire::GpuOcclusionSurfaceState::Active;
    diagnostics.SourceFrame = 91;
    diagnostics.ReadbackAge = 2;
    diagnostics.Candidates = 30;
    diagnostics.Visible = 8;
    diagnostics.Culled = 22;
    diagnostics.SafeOccluders = 4;
    diagnostics.PyramidMipCount = 7;
    diagnostics.PyramidValid = true;
    diagnostics.ReadbackValid = true;
    Keire::RenderStatistics aggregate;
    aggregate.GpuOcclusionDispatches = 8;
    aggregate.GpuOcclusionIndirectDraws = 3;

    auto presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics, &aggregate);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.Status.find("requested Forced, effective Forced") != std::string::npos);
    CHECK(presentation.Readback == "Visibility source frame 91 (2 frames old)");
    CHECK(presentation.Visibility.find("22 culled (73%)") != std::string::npos);
    CHECK(presentation.Pyramid.find("last completed frame aggregate: 8 dispatches / 3 indirect draws") !=
          std::string::npos);

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Fallback;
    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::ResourceAllocationFailed;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Fallback);
    CHECK(presentation.Warning);
    CHECK(presentation.Status.find("GPU resource allocation failed") != std::string::npos);
    CHECK(presentation.Status.find("direct draws") != std::string::npos);
    CHECK(presentation.Readback == "Visibility readback unavailable while direct-draw fallback is active");
    CHECK(presentation.Readback.find("pending") == std::string::npos);

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Unsupported;
    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::UnsupportedBackend;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Unavailable);
    CHECK(presentation.Warning);
    CHECK(presentation.Status.find("unsupported GPU backend") != std::string::npos);
    CHECK(presentation.Readback == "Visibility readback unavailable on the active renderer backend");
    CHECK(presentation.Readback.find("pending") == std::string::npos);

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Idle;
    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::BelowAutomaticThreshold;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Disabled);
    CHECK(presentation.Readback == "Visibility readback unavailable while GPU occlusion is idle");
    CHECK(presentation.Readback.find("pending") == std::string::npos);

    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::None;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.Status == "GPU occlusion idle; surface was not rendered in the last completed frame");

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Fallback;
    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::OversizedBatch;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.Status.find("instance batch exceeds the occlusion limit") != std::string::npos);

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Active;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.State == KeireEditor::GpuOcclusionDiagnosticState::Active);
    CHECK(presentation.Warning);
    CHECK(presentation.Status.find("partial fallback: instance batch exceeds the occlusion limit") !=
          std::string::npos);
    CHECK(presentation.Status.find("some direct draws") != std::string::npos);

    diagnostics.State = Keire::GpuOcclusionSurfaceState::Fallback;
    diagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::ReadbackValidationFailed;
    presentation = KeireEditor::BuildGpuOcclusionSurfaceDiagnostics(diagnostics);
    CHECK(presentation.Warning);
    CHECK(presentation.Status.find("GPU visibility readback failed validation") != std::string::npos);
}
