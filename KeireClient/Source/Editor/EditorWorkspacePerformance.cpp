#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"
#include "KeireClient/Editor/SceneViewportLayout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::string> WrapOverlayText(Keire::UiFrame& ui, const std::string_view text,
                                                           const float fontSize, const float maximumWidth)
    {
        std::vector<std::string> lines;
        std::string line;
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const auto separator = text.find(' ', offset);
            const auto word =
                text.substr(offset, separator == std::string_view::npos ? text.size() - offset : separator - offset);
            offset = separator == std::string_view::npos ? text.size() : separator + 1U;
            if (word.empty())
                continue;

            auto candidate = line;
            if (!candidate.empty())
                candidate += ' ';
            candidate.append(word);
            if (!line.empty() && ui.MeasureText(candidate, fontSize).Width > maximumWidth)
            {
                lines.push_back(std::move(line));
                line.assign(word);
            }
            else
                line = std::move(candidate);
        }
        if (!line.empty())
            lines.push_back(std::move(line));
        return lines;
    }
} // namespace

std::optional<Keire::UiItemRect> EditorWorkspaceLayer::DrawSceneViewportPerformanceOverlay(
    Keire::UiFrame& ui, const Keire::UiItemRect viewport,
    const std::optional<Keire::GpuOcclusionSurfaceDiagnostics> occlusionSurface,
    const std::optional<Keire::UiItemRect> occupied)
{
    return DrawPerformanceOverlay(ui, viewport, "EDITOR CAMERA", occlusionSurface, true, occupied);
}

std::optional<Keire::UiItemRect> EditorWorkspaceLayer::DrawPerformanceOverlay(
    Keire::UiFrame& ui, const Keire::UiItemRect viewport, const std::string_view label,
    const std::optional<Keire::GpuOcclusionSurfaceDiagnostics> occlusionSurface, const bool reserveToolbar,
    const std::optional<Keire::UiItemRect> occupied)
{
    if ((!m_ShowPerformanceOverlay && !m_ShowAdvancedPerformanceOverlay) || viewport.Size().Width < 220.0F ||
        viewport.Size().Height < 80.0F)
        return std::nullopt;
    const auto profiler = Owner().GetProfiler();
    if (!profiler || !profiler->IsOpen())
        return std::nullopt;
    const auto frame = profiler->LatestSummary();
    if (frame.Sequence == 0 || frame.DurationMicroseconds <= 0.0)
        return std::nullopt;

    const auto decimal = [](const double value)
    {
        const auto scaled = static_cast<std::int64_t>(std::lround(std::max(0.0, value) * 10.0));
        return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
    };
    const double frameMilliseconds = frame.DurationMicroseconds / 1000.0;
    const auto framesPerSecond = static_cast<std::uint32_t>(std::lround(1'000'000.0 / frame.DurationMicroseconds));
    const auto performanceColor =
        frameMilliseconds <= 16.7 ? m_Theme.Success : (frameMilliseconds <= 33.3 ? m_Theme.Warning : m_Theme.Error);
    if (m_ShowAdvancedPerformanceOverlay && viewport.Size().Width >= 380.0F)
    {
        std::optional<KeireEditor::GpuOcclusionDiagnostics> occlusion;
        std::optional<Keire::RenderStatistics> rendererStatistics;
        if (const auto renderer = Owner().Renderer())
        {
            rendererStatistics = renderer->Statistics();
            occlusion = KeireEditor::BuildGpuOcclusionPanelDiagnostics(renderer->Capabilities(), *rendererStatistics,
                                                                       occlusionSurface);
        }

        constexpr float minimumWidth = 318.0F;
        constexpr float maximumPreferredWidth = 520.0F;
        constexpr float horizontalPadding = 12.0F;
        constexpr float diagnosticsFontSize = 10.0F;
        const float maximumWidth = std::min(maximumPreferredWidth, viewport.Size().Width - 24.0F);
        float desiredWidth = minimumWidth;
        if (occlusion)
        {
            desiredWidth = std::max(
                {desiredWidth, ui.MeasureText(occlusion->Status, diagnosticsFontSize).Width + horizontalPadding * 2.0F,
                 ui.MeasureText(occlusion->Visibility, diagnosticsFontSize).Width + horizontalPadding * 2.0F});
        }
        const float width = std::clamp(desiredWidth, minimumWidth, maximumWidth);
        const float diagnosticsWidth = width - horizontalPadding * 2.0F;
        const auto statusLines = occlusion
                                     ? WrapOverlayText(ui, occlusion->Status, diagnosticsFontSize, diagnosticsWidth)
                                     : std::vector<std::string>{};
        const auto visibilityLines =
            occlusion ? WrapOverlayText(ui, occlusion->Visibility, diagnosticsFontSize, diagnosticsWidth)
                      : std::vector<std::string>{};
        const auto pyramidLines = occlusion
                                      ? WrapOverlayText(ui, occlusion->Pyramid, diagnosticsFontSize, diagnosticsWidth)
                                      : std::vector<std::string>{};
        const auto readbackLines = occlusion
                                       ? WrapOverlayText(ui, occlusion->Readback, diagnosticsFontSize, diagnosticsWidth)
                                       : std::vector<std::string>{};
        const auto recordingLines =
            occlusion ? WrapOverlayText(ui, occlusion->Recording, diagnosticsFontSize, diagnosticsWidth)
                      : std::vector<std::string>{};
        const std::string cpuPreparation =
            rendererStatistics ? "Renderer CPU preparation " + decimal(rendererStatistics->CpuPreparationMilliseconds) +
                                     " ms / P95 " + decimal(rendererStatistics->CpuPreparationP95Milliseconds) + " ms"
                               : std::string{};
        const auto cpuPreparationLines =
            cpuPreparation.empty() ? std::vector<std::string>{}
                                   : WrapOverlayText(ui, cpuPreparation, diagnosticsFontSize, diagnosticsWidth);
        const float diagnosticsLineHeight = std::max(13.0F, ui.MeasureText("Ag", diagnosticsFontSize).Height + 3.0F);
        const auto diagnosticsLineCount = statusLines.size() + visibilityLines.size() + pyramidLines.size() +
                                          readbackLines.size() + recordingLines.size() + cpuPreparationLines.size();
        const float diagnosticsHeight =
            203.0F + diagnosticsLineHeight * static_cast<float>(diagnosticsLineCount) + 8.0F;
        const float height = std::max(234.0F, diagnosticsHeight);

        if (viewport.Size().Height >= height + 60.0F)
        {
            const auto placement =
                KeireEditor::PlaceViewportPerformanceOverlay(viewport, {width, height}, reserveToolbar ? 48.0F : 12.0F,
                                                             true, occupied, reserveToolbar ? 34.0F : 12.0F);
            if (placement)
            {
                const Keire::UiItemRect overlay = *placement;
                ui.DrawFilledRectangle(overlay, {0.018F, 0.024F, 0.035F, 0.92F}, 7.0F);
                ui.DrawRectangle(overlay, {performanceColor.Red, performanceColor.Green, performanceColor.Blue, 0.72F},
                                 1.0F, 7.0F);
                ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 8.0F}, m_Theme.MutedText,
                                   std::string(label) + " PERFORMANCE", 10.0F, overlay);
                ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 25.0F}, performanceColor,
                                   std::to_string(framesPerSecond) + " FPS", 20.0F, overlay);
                ui.DrawOverlayText({overlay.Minimum.X + 104.0F, overlay.Minimum.Y + 30.0F}, m_Theme.Text,
                                   decimal(frameMilliseconds) + " ms  |  frame " + std::to_string(frame.Sequence),
                                   12.0F, overlay);

                const auto row = [&](const float y, const std::string_view name, const double microseconds,
                                     const Keire::UiColor color)
                {
                    const double milliseconds = microseconds / 1000.0;
                    const float fraction =
                        static_cast<float>(std::clamp(microseconds / frame.DurationMicroseconds, 0.0, 1.0));
                    ui.DrawOverlayText({overlay.Minimum.X + 12.0F, y}, m_Theme.MutedText, name, 11.0F, overlay);
                    const Keire::UiItemRect track{{overlay.Minimum.X + 86.0F, y + 3.0F},
                                                  {overlay.Maximum.X - 58.0F, y + 10.0F}};
                    ui.DrawFilledRectangle(track, {0.11F, 0.13F, 0.17F, 0.92F}, 3.0F);
                    if (fraction > 0.0F)
                    {
                        const Keire::UiItemRect fill{
                            track.Minimum, {track.Minimum.X + track.Size().Width * fraction, track.Maximum.Y}};
                        ui.DrawFilledRectangle(fill, color, 3.0F);
                    }
                    ui.DrawOverlayText({overlay.Maximum.X - 52.0F, y}, m_Theme.Text, decimal(milliseconds), 11.0F,
                                       overlay);
                };
                row(overlay.Minimum.Y + 57.0F, "Rendering", frame.RenderingMicroseconds, m_Theme.Accent);
                row(overlay.Minimum.Y + 77.0F, "Scripting", frame.ScriptingMicroseconds, {0.68F, 0.48F, 0.96F, 1.0F});
                row(overlay.Minimum.Y + 97.0F, "Physics", frame.PhysicsMicroseconds, {0.30F, 0.78F, 0.68F, 1.0F});
                row(overlay.Minimum.Y + 117.0F, "Animation", frame.AnimationMicroseconds, {0.95F, 0.62F, 0.28F, 1.0F});
                row(overlay.Minimum.Y + 137.0F, "Assets + audio", frame.AssetsMicroseconds + frame.AudioMicroseconds,
                    {0.42F, 0.66F, 0.94F, 1.0F});
                row(overlay.Minimum.Y + 157.0F, "Application", frame.ApplicationMicroseconds,
                    {0.96F, 0.46F, 0.38F, 1.0F});
                row(overlay.Minimum.Y + 177.0F, "Editor/User", frame.UserMicroseconds, {0.90F, 0.74F, 0.30F, 1.0F});
                if (occlusion)
                {
                    const auto occlusionColor = occlusion->Warning ? m_Theme.Warning
                                                : occlusion->State == KeireEditor::GpuOcclusionDiagnosticState::Active
                                                    ? m_Theme.Success
                                                    : m_Theme.MutedText;
                    float diagnosticsY = overlay.Minimum.Y + 203.0F;
                    for (const auto& line : statusLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, occlusionColor, line,
                                           diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                    for (const auto& line : visibilityLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, m_Theme.MutedText,
                                           line, diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                    for (const auto& line : pyramidLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, m_Theme.MutedText,
                                           line, diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                    for (const auto& line : readbackLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, m_Theme.MutedText,
                                           line, diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                    for (const auto& line : recordingLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, m_Theme.MutedText,
                                           line, diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                    for (const auto& line : cpuPreparationLines)
                    {
                        ui.DrawOverlayText({overlay.Minimum.X + horizontalPadding, diagnosticsY}, m_Theme.MutedText,
                                           line, diagnosticsFontSize, overlay);
                        diagnosticsY += diagnosticsLineHeight;
                    }
                }
                return overlay;
            }
        }
    }
    const auto placement = KeireEditor::PlaceViewportPerformanceOverlay(
        viewport, {166.0F, 44.0F}, reserveToolbar ? 48.0F : 12.0F, false, occupied, reserveToolbar ? 34.0F : 12.0F);
    if (!placement)
        return std::nullopt;
    const Keire::UiItemRect overlay = *placement;
    ui.DrawFilledRectangle(overlay, {0.018F, 0.024F, 0.035F, 0.88F}, 6.0F);
    ui.DrawRectangle(overlay, {performanceColor.Red, performanceColor.Green, performanceColor.Blue, 0.72F}, 1.0F, 6.0F);
    ui.DrawOverlayText({overlay.Minimum.X + 10.0F, overlay.Minimum.Y + 6.0F}, m_Theme.MutedText,
                       std::string(label) + " VIEW", 10.0F, overlay);
    ui.DrawOverlayText({overlay.Minimum.X + 10.0F, overlay.Minimum.Y + 21.0F}, performanceColor,
                       std::to_string(framesPerSecond) + " FPS  |  " + decimal(frameMilliseconds) + " ms", 15.0F,
                       overlay);
    return overlay;
}
