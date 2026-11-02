#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"
#include "KeireClientInternal/Editor/PostProcessSearch.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace KeireEditor::Detail
{
    struct PostProcessAuthoringGroup
    {
        std::string_view Name;
        std::string_view Description;
        Keire::PostProcessEffect Effect;
        Keire::PostProcessParameter First;
        Keire::PostProcessParameter Last;
    };

    inline constexpr std::array PostProcessAuthoringGroups{
        PostProcessAuthoringGroup{"Exposure", "Balance the brightness of the image in exposure stops.",
                                  Keire::PostProcessEffect::Count, Keire::PostProcessParameter::ExposureStops,
                                  Keire::PostProcessParameter::ExposureStops},
        PostProcessAuthoringGroup{"Bloom", "Add a soft glow around bright highlights.", Keire::PostProcessEffect::Bloom,
                                  Keire::PostProcessParameter::BloomIntensity,
                                  Keire::PostProcessParameter::BloomRadius},
        PostProcessAuthoringGroup{"Color", "Shape the overall palette, contrast and color balance.",
                                  Keire::PostProcessEffect::ColorGrading, Keire::PostProcessParameter::Temperature,
                                  Keire::PostProcessParameter::BlueGain},
        PostProcessAuthoringGroup{"Vignette", "Draw attention toward the center of the image.",
                                  Keire::PostProcessEffect::Vignette, Keire::PostProcessParameter::VignetteIntensity,
                                  Keire::PostProcessParameter::VignetteRoundness},
        PostProcessAuthoringGroup{"Grain", "Add film texture while protecting bright areas.",
                                  Keire::PostProcessEffect::FilmGrain, Keire::PostProcessParameter::GrainIntensity,
                                  Keire::PostProcessParameter::GrainResponse},
        PostProcessAuthoringGroup{"Chromatic", "Separate color channels near the edge of the image.",
                                  Keire::PostProcessEffect::ChromaticAberration,
                                  Keire::PostProcessParameter::ChromaticAberration,
                                  Keire::PostProcessParameter::ChromaticAberration},
        PostProcessAuthoringGroup{"Depth of Field", "Control focus using distance and photographic lens settings.",
                                  Keire::PostProcessEffect::DepthOfField, Keire::PostProcessParameter::FocusDistance,
                                  Keire::PostProcessParameter::DepthOfFieldStrength},
        PostProcessAuthoringGroup{"Motion Blur", "Soften motion using camera and object velocity.",
                                  Keire::PostProcessEffect::MotionBlur, Keire::PostProcessParameter::MotionBlurStrength,
                                  Keire::PostProcessParameter::MotionBlurStrength}};

    [[nodiscard]] inline const PostProcessAuthoringGroup&
    PostProcessGroupForEffect(const Keire::PostProcessEffect effect)
    {
        for (const auto& group : PostProcessAuthoringGroups)
        {
            if (group.Effect == effect && effect != Keire::PostProcessEffect::Count)
            {
                return group;
            }
        }
        throw std::invalid_argument("Unknown post-processing effect.");
    }

    [[nodiscard]] inline Keire::PostProcessProfileDefinition
    SetPostProcessEffectEnabled(const Keire::PostProcessProfileDefinition& source,
                                const Keire::PostProcessEffect effect, const bool enabled)
    {
        const auto& group = PostProcessGroupForEffect(effect);
        auto result = source;
        result.Effects[static_cast<std::size_t>(effect)] = {true, enabled};
        if (enabled)
        {
            for (auto index = static_cast<std::size_t>(group.First); index <= static_cast<std::size_t>(group.Last);
                 ++index)
            {
                result.Parameters[index].Override = true;
            }
        }
        Keire::PostProcessProfileAsset::Validate(result);
        return result;
    }

    [[nodiscard]] inline Keire::PostProcessProfileDefinition
    InheritPostProcessEffect(const Keire::PostProcessProfileDefinition& source, const Keire::PostProcessEffect effect)
    {
        const auto& group = PostProcessGroupForEffect(effect);
        auto result = source;
        result.Effects[static_cast<std::size_t>(effect)].Override = false;
        for (auto index = static_cast<std::size_t>(group.First); index <= static_cast<std::size_t>(group.Last); ++index)
        {
            result.Parameters[index].Override = false;
        }
        if (effect == Keire::PostProcessEffect::ColorGrading)
        {
            result.ColorLookup.Override = false;
            result.ColorLookupWeight.Override = false;
        }
        Keire::PostProcessProfileAsset::Validate(result);
        return result;
    }

    [[nodiscard]] inline Keire::PostProcessProfileDefinition
    EditPostProcessParameter(const Keire::PostProcessProfileDefinition& source,
                             const Keire::PostProcessParameter parameter, const double value)
    {
        const auto index = static_cast<std::size_t>(parameter);
        if (index >= source.Parameters.size())
        {
            throw std::invalid_argument("Unknown post-processing parameter.");
        }
        auto result = source;
        for (const auto& group : PostProcessAuthoringGroups)
        {
            if (index < static_cast<std::size_t>(group.First) || index > static_cast<std::size_t>(group.Last) ||
                group.Effect == Keire::PostProcessEffect::Count)
            {
                continue;
            }
            const auto state = source.Effects[static_cast<std::size_t>(group.Effect)];
            if (!state.Override || !state.Value)
            {
                result = SetPostProcessEffectEnabled(source, group.Effect, true);
            }
            break;
        }
        result.Parameters[index] = {true, value};
        Keire::PostProcessProfileAsset::Validate(result);
        return result;
    }

    [[nodiscard]] inline Keire::PostProcessProfileDefinition
    ApplyPostProcessLookup(const Keire::PostProcessProfileDefinition& source,
                           Keire::PostProcessOverride<Keire::AssetId> reference,
                           Keire::PostProcessOverride<double> weight)
    {
        auto result = source;
        const auto grading = source.Effects[static_cast<std::size_t>(Keire::PostProcessEffect::ColorGrading)];
        if (reference.Value && (!grading.Override || !grading.Value))
        {
            result = SetPostProcessEffectEnabled(source, Keire::PostProcessEffect::ColorGrading, true);
        }
        result.SchemaVersion = 3;
        reference.Override = true;
        weight.Override = true;
        result.ColorLookup = reference;
        result.ColorLookupWeight = weight;
        Keire::PostProcessProfileAsset::Validate(result);
        return result;
    }

    [[nodiscard]] inline bool PostProcessGroupMatches(const std::string_view search,
                                                      const PostProcessAuthoringGroup& group)
    {
        if (group.Effect == Keire::PostProcessEffect::ColorGrading &&
            PostProcessSearchMatches(search, "Color Lookup LUT Texture Weight"))
        {
            return true;
        }
        const auto descriptions = Keire::PostProcessParameterDescriptions();
        for (auto index = static_cast<std::size_t>(group.First); index <= static_cast<std::size_t>(group.Last); ++index)
        {
            if (PostProcessParameterMatches(search, group.Name, descriptions[index].Label, descriptions[index].Unit))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline std::string_view PostProcessGroupState(const Keire::PostProcessProfileDefinition& profile,
                                                                const PostProcessAuthoringGroup& group)
    {
        if (group.Effect == Keire::PostProcessEffect::Count)
        {
            return profile.Parameters[static_cast<std::size_t>(group.First)].Override ? "Adjusted" : "Inherited";
        }
        const auto state = profile.Effects[static_cast<std::size_t>(group.Effect)];
        return !state.Override ? "Inherited" : state.Value ? "Enabled" : "Disabled";
    }
} // namespace KeireEditor::Detail
