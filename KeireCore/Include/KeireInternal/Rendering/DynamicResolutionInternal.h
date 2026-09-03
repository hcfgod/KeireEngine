#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace Keire::Internal
{
    class DynamicResolutionController final
    {
      public:
        [[nodiscard]] float Update(const RenderEnvironmentSettings& settings, const RenderFeatureSelection& selection,
                                   const RenderStatistics& statistics) noexcept
        {
            const float authoredScale = std::clamp(settings.RenderScale, 0.5F, 1.0F);
            if (selection.EffectiveDynamicResolution != DynamicResolutionMode::Automatic)
            {
                m_Scale = authoredScale;
                m_LastStatisticsFrame = statistics.Frame;
                m_CompletedSamples = 0;
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

            if (statistics.Frame == 0 || statistics.Frame <= m_LastStatisticsFrame)
                return m_Scale;
            m_LastStatisticsFrame = statistics.Frame;
            if (++m_CompletedSamples < AdjustmentInterval)
                return m_Scale;
            m_CompletedSamples = 0;

            const float measuredMilliseconds = EstimateFrameCost(statistics);
            if (!std::isfinite(measuredMilliseconds) || measuredMilliseconds <= 0.0F)
                return m_Scale;
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
            return m_Scale;
        }

        void Reset() noexcept
        {
            m_Scale = 1.0F;
            m_LastStatisticsFrame = 0;
            m_CompletedSamples = 0;
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
        static constexpr float DownscaleThreshold = 1.05F;
        static constexpr float UpscaleThreshold = 0.82F;
        static constexpr float DownscaleResponse = 0.65F;
        static constexpr float UpscaleResponse = 0.12F;

        float m_Scale = 1.0F;
        std::uint64_t m_LastStatisticsFrame = 0;
        std::uint32_t m_CompletedSamples = 0;
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
