#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Environment/BloomEvaluation.h"
#include "KeireInternal/Rendering/PostProcessEvaluation.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline EvaluatedPostProcessing MakePostProcessBaseline(const RenderEnvironmentSettings& environment)
    {
        ValidateBloomSettings(environment.Bloom);
        EvaluatedPostProcessing result;
        const auto assign = [&](const PostProcessParameter parameter, const double value)
        { result.Parameters[static_cast<std::size_t>(parameter)] = value; };
        assign(PostProcessParameter::BloomIntensity, environment.Bloom.Intensity);
        assign(PostProcessParameter::BloomThreshold, environment.Bloom.Threshold);
        assign(PostProcessParameter::BloomSoftKnee, environment.Bloom.SoftKnee);
        assign(PostProcessParameter::BloomRadius, environment.Bloom.Radius);
        result.EffectWeights[static_cast<std::size_t>(PostProcessEffect::Bloom)] =
            environment.Bloom.Enabled ? 1.0 : 0.0;
        return result;
    }

    /// Apply once to a captured camera-local environment, before lighting/sky uniforms are produced.
    /// Never write the derived exposure back into project settings or apply it again during tone mapping.
    inline void ApplyPostProcessEnvironment(RenderEnvironmentSettings& environment,
                                            const EvaluatedPostProcessing& evaluated)
    {
        // Also validate caller-supplied evaluation data before making any changes.
        (void)BlendPostProcessProfiles({}, evaluated);
        const auto value = [&](const PostProcessParameter parameter)
        { return evaluated.Parameters[static_cast<std::size_t>(parameter)]; };
        const double exposure =
            static_cast<double>(environment.Exposure) * std::exp2(value(PostProcessParameter::ExposureStops));
        if (!std::isfinite(exposure) || exposure <= 0.0 || exposure > std::numeric_limits<float>::max())
        {
            throw std::invalid_argument("Post-processing exposure exceeds the finite rendering range.");
        }
        BloomSettings bloom;
        const double weight = evaluated.EffectWeights[static_cast<std::size_t>(PostProcessEffect::Bloom)];
        bloom.Enabled = weight > 0.0;
        bloom.Intensity = value(PostProcessParameter::BloomIntensity) * weight;
        bloom.Threshold = value(PostProcessParameter::BloomThreshold);
        bloom.SoftKnee = value(PostProcessParameter::BloomSoftKnee);
        bloom.Radius = value(PostProcessParameter::BloomRadius);
        ValidateBloomSettings(bloom);
        environment.Exposure = static_cast<float>(exposure);
        environment.Bloom = bloom;
    }
} // namespace Keire::Detail
