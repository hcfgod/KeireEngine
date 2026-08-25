#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

void EditorWorkspaceLayer::DrawPerformanceOverlay(Keire::UiFrame& ui, const Keire::UiItemRect viewport,
                                                  const std::string_view label)
{
    if ((!m_ShowPerformanceOverlay && !m_ShowAdvancedPerformanceOverlay) || viewport.Size().Width < 220.0F ||
        viewport.Size().Height < 80.0F)
        return;
    const auto profiler = Owner().GetProfiler();
    if (!profiler || !profiler->IsOpen())
        return;
    const auto frame = profiler->LatestSummary();
    if (frame.Sequence == 0 || frame.DurationMicroseconds <= 0.0)
        return;

    const auto decimal = [](const double value)
    {
        const auto scaled = static_cast<std::int64_t>(std::lround(std::max(0.0, value) * 10.0));
        return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
    };
    const double frameMilliseconds = frame.DurationMicroseconds / 1000.0;
    const auto framesPerSecond = static_cast<std::uint32_t>(std::lround(1'000'000.0 / frame.DurationMicroseconds));
    const auto performanceColor =
        frameMilliseconds <= 16.7 ? m_Theme.Success : (frameMilliseconds <= 33.3 ? m_Theme.Warning : m_Theme.Error);
    if (m_ShowAdvancedPerformanceOverlay && viewport.Size().Width >= 380.0F && viewport.Size().Height >= 254.0F)
    {
        constexpr float width = 318.0F;
        constexpr float height = 194.0F;
        const Keire::UiItemRect overlay{{viewport.Maximum.X - width - 12.0F, viewport.Minimum.Y + 48.0F},
                                        {viewport.Maximum.X - 12.0F, viewport.Minimum.Y + 48.0F + height}};
        ui.DrawFilledRectangle(overlay, {0.018F, 0.024F, 0.035F, 0.92F}, 7.0F);
        ui.DrawRectangle(overlay, {performanceColor.Red, performanceColor.Green, performanceColor.Blue, 0.72F}, 1.0F,
                         7.0F);
        ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 8.0F}, m_Theme.MutedText,
                           std::string(label) + " PERFORMANCE", 10.0F, overlay);
        ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 25.0F}, performanceColor,
                           std::to_string(framesPerSecond) + " FPS", 20.0F, overlay);
        ui.DrawOverlayText({overlay.Minimum.X + 104.0F, overlay.Minimum.Y + 30.0F}, m_Theme.Text,
                           decimal(frameMilliseconds) + " ms  |  frame " + std::to_string(frame.Sequence), 12.0F,
                           overlay);

        const auto row =
            [&](const float y, const std::string_view name, const double microseconds, const Keire::UiColor color)
        {
            const double milliseconds = microseconds / 1000.0;
            const float fraction = static_cast<float>(std::clamp(microseconds / frame.DurationMicroseconds, 0.0, 1.0));
            ui.DrawOverlayText({overlay.Minimum.X + 12.0F, y}, m_Theme.MutedText, name, 11.0F, overlay);
            const Keire::UiItemRect track{{overlay.Minimum.X + 86.0F, y + 3.0F},
                                          {overlay.Maximum.X - 58.0F, y + 10.0F}};
            ui.DrawFilledRectangle(track, {0.11F, 0.13F, 0.17F, 0.92F}, 3.0F);
            if (fraction > 0.0F)
            {
                const Keire::UiItemRect fill{track.Minimum,
                                             {track.Minimum.X + track.Size().Width * fraction, track.Maximum.Y}};
                ui.DrawFilledRectangle(fill, color, 3.0F);
            }
            ui.DrawOverlayText({overlay.Maximum.X - 52.0F, y}, m_Theme.Text, decimal(milliseconds), 11.0F, overlay);
        };
        row(overlay.Minimum.Y + 57.0F, "Rendering", frame.RenderingMicroseconds, m_Theme.Accent);
        row(overlay.Minimum.Y + 77.0F, "Scripting", frame.ScriptingMicroseconds, {0.68F, 0.48F, 0.96F, 1.0F});
        row(overlay.Minimum.Y + 97.0F, "Physics", frame.PhysicsMicroseconds, {0.30F, 0.78F, 0.68F, 1.0F});
        row(overlay.Minimum.Y + 117.0F, "Animation", frame.AnimationMicroseconds, {0.95F, 0.62F, 0.28F, 1.0F});
        row(overlay.Minimum.Y + 137.0F, "Assets + audio", frame.AssetsMicroseconds + frame.AudioMicroseconds,
            {0.42F, 0.66F, 0.94F, 1.0F});
        if (const auto renderer = Owner().Renderer())
        {
            const auto occlusion =
                KeireEditor::BuildGpuOcclusionDiagnostics(renderer->Capabilities(), renderer->Statistics());
            const auto occlusionColor = occlusion.Warning ? m_Theme.Warning
                                        : occlusion.State == KeireEditor::GpuOcclusionDiagnosticState::Active
                                            ? m_Theme.Success
                                            : m_Theme.MutedText;
            ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 163.0F}, occlusionColor,
                               occlusion.Status, 10.0F, overlay);
            ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 178.0F}, m_Theme.MutedText,
                               occlusion.Visibility, 10.0F, overlay);
        }
        return;
    }
    const Keire::UiItemRect overlay{{viewport.Minimum.X + 12.0F, viewport.Minimum.Y + 12.0F},
                                    {viewport.Minimum.X + 178.0F, viewport.Minimum.Y + 56.0F}};
    ui.DrawFilledRectangle(overlay, {0.018F, 0.024F, 0.035F, 0.88F}, 6.0F);
    ui.DrawRectangle(overlay, {performanceColor.Red, performanceColor.Green, performanceColor.Blue, 0.72F}, 1.0F, 6.0F);
    ui.DrawOverlayText({overlay.Minimum.X + 10.0F, overlay.Minimum.Y + 6.0F}, m_Theme.MutedText,
                       std::string(label) + " VIEW", 10.0F, overlay);
    ui.DrawOverlayText({overlay.Minimum.X + 10.0F, overlay.Minimum.Y + 21.0F}, performanceColor,
                       std::to_string(framesPerSecond) + " FPS  |  " + decimal(frameMilliseconds) + " ms", 15.0F,
                       overlay);
}
