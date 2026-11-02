#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <utility>

namespace Keire::Internal
{
    [[nodiscard]] inline float
    NativePixelScaleForRuntimeUi(const float renderScale, const std::span<const RuntimeUiRenderSubmission> submissions,
                                 const bool screenOverlayUsesSurface) noexcept
    {
        for (const auto& submission : submissions)
            if (submission.Target == RuntimeUiRenderTarget::CameraOverlay ||
                (screenOverlayUsesSurface && submission.Target == RuntimeUiRenderTarget::ScreenOverlay))
                return 1.0F;
        return renderScale;
    }

    class DynamicResolutionController final
    {
      public:
        [[nodiscard]] float Update(const RenderEnvironmentSettings& settings, const RenderFeatureSelection& selection,
                                   const RenderStatistics& statistics) noexcept
        {
            const float authoredScale = std::clamp(settings.RenderScale, 0.5F, 1.0F);
            if (selection.EffectiveDynamicResolution != DynamicResolutionMode::Automatic)
            {
                Reset();
                m_Scale = authoredScale;
                m_LastStatisticsFrame = statistics.Frame;
                return m_Scale;
            }

            const float minimum =
                std::min(std::clamp(settings.MinimumDynamicResolutionScale, 0.5F, 1.0F), authoredScale);
            const float maximum = std::max(minimum, std::min(authoredScale, settings.MaximumDynamicResolutionScale));
            if (!m_Initialized)
            {
                m_Scale = maximum;
                m_Initialized = true;
            }
            m_Scale = std::clamp(m_Scale, minimum, maximum);

            // GPU timing is asynchronous: multiple statistics publications can contain
            // the same completed interval. Count that measurement only once.
            const bool hasGpuSample = statistics.GpuTimingSupported && statistics.GpuFrameMilliseconds > 0.0F &&
                                      statistics.GpuTimingFrame != 0;
            const auto sampleFrame = hasGpuSample ? statistics.GpuTimingFrame : statistics.Frame;
            auto& lastSampleFrame = hasGpuSample ? m_LastGpuTimingFrame : m_LastStatisticsFrame;
            if (sampleFrame == 0 || sampleFrame <= lastSampleFrame)
            {
                return m_Scale;
            }
            lastSampleFrame = sampleFrame;
            if (m_SettlingSamples > 0)
            {
                --m_SettlingSamples;
                return m_Scale;
            }

            const float sampleMilliseconds = EstimateFrameCost(statistics);
            if (!std::isfinite(sampleMilliseconds) || sampleMilliseconds <= 0.0F)
            {
                return m_Scale;
            }
            m_FrameCosts[m_CompletedSamples++] = sampleMilliseconds;
            if (m_CompletedSamples < AdjustmentInterval)
            {
                return m_Scale;
            }
            m_CompletedSamples = 0;

            // A resize or an asset upload must not become sustained GPU pressure. Use the
            // median of completed samples instead of whichever frame ends the interval.
            auto orderedCosts = m_FrameCosts;
            std::sort(orderedCosts.begin(), orderedCosts.end());
            const float measuredMilliseconds =
                std::midpoint(orderedCosts[AdjustmentInterval / 2 - 1], orderedCosts[AdjustmentInterval / 2]);
            const float previousScale = m_Scale;
            const float targetMilliseconds = std::clamp(settings.DynamicResolutionTargetMilliseconds, 8.0F, 50.0F);
            const float ratio = measuredMilliseconds / targetMilliseconds;
            if (ratio > DownscaleThreshold)
            {
                const float desired = m_Scale * std::sqrt(targetMilliseconds / measuredMilliseconds);
                m_Scale = Quantize(std::lerp(m_Scale, desired, DownscaleResponse), minimum, maximum);
            }
            else if (ratio < UpscaleThreshold)
            {
                const float desired = m_Scale * std::sqrt(targetMilliseconds / measuredMilliseconds);
                m_Scale = Quantize(std::lerp(m_Scale, desired, UpscaleResponse), minimum, maximum);
            }
            if (m_Scale != previousScale)
            {
                // Submitted timing results can still describe the previous extent; let the
                // render queue and new targets settle before measuring the next adjustment.
                m_SettlingSamples = SettlingInterval;
            }
            return m_Scale;
        }

        void Reset() noexcept
        {
            m_Scale = 1.0F;
            m_LastStatisticsFrame = 0;
            m_LastGpuTimingFrame = 0;
            m_CompletedSamples = 0;
            m_SettlingSamples = 0;
            m_Initialized = false;
        }

        [[nodiscard]] float Scale() const noexcept { return m_Scale; }

        [[nodiscard]] static float EstimateFrameCost(const RenderStatistics& statistics) noexcept
        {
            if (statistics.GpuTimingSupported && statistics.GpuFrameMilliseconds > 0.0F)
                return statistics.GpuFrameMilliseconds;
            if (statistics.GpuCompletionLatencyMilliseconds > 0.0F)
            {
                return statistics.GpuCompletionLatencyMilliseconds /
                       static_cast<float>(std::max(statistics.OutstandingFrames, 1U));
            }
            return std::max(statistics.RenderCpuMilliseconds, statistics.SubmitToPresentMilliseconds);
        }

      private:
        [[nodiscard]] static float Quantize(const float scale, const float minimum, const float maximum) noexcept
        {
            constexpr float steps = 32.0F;
            return std::clamp(std::round(scale * steps) / steps, minimum, maximum);
        }

        static constexpr std::uint32_t AdjustmentInterval = 8;
        static constexpr std::uint32_t SettlingInterval = 16;
        static constexpr float DownscaleThreshold = 1.05F;
        static constexpr float UpscaleThreshold = 0.82F;
        static constexpr float DownscaleResponse = 0.65F;
        static constexpr float UpscaleResponse = 0.12F;

        float m_Scale = 1.0F;
        std::uint64_t m_LastStatisticsFrame = 0;
        std::uint64_t m_LastGpuTimingFrame = 0;
        std::array<float, AdjustmentInterval> m_FrameCosts{};
        std::uint32_t m_CompletedSamples = 0;
        std::uint32_t m_SettlingSamples = 0;
        bool m_Initialized = false;
    };

    [[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t>
    ScaledRenderSurfaceExtent(const float logicalWidth, const float logicalHeight, const float displayScale,
                              const float renderScale) noexcept
    {
        const float scale = std::max(displayScale, 1.0F) * std::clamp(renderScale, 0.5F, 1.0F);
        const auto width =
            static_cast<std::uint32_t>(std::round(std::clamp(std::max(logicalWidth, 1.0F) * scale, 1.0F, 16384.0F)));
        const auto height =
            static_cast<std::uint32_t>(std::round(std::clamp(std::max(logicalHeight, 1.0F) * scale, 1.0F, 16384.0F)));
        return {width, height};
    }
} // namespace Keire::Internal
