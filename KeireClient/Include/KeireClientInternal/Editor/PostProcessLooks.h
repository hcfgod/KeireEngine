#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace KeireEditor::Detail
{
    struct PostProcessColorLook
    {
        std::string_view Name;
        double Temperature;
        double Contrast;
        double Saturation;
    };

    inline constexpr std::array PostProcessColorLooks{
        PostProcessColorLook{"Neutral", 0.0, 1.0, 1.0}, PostProcessColorLook{"Warm", 12.0, 1.05, 1.0},
        PostProcessColorLook{"Cool", -12.0, 1.05, 0.95}, PostProcessColorLook{"Vivid", 0.0, 1.1, 1.25},
        PostProcessColorLook{"Monochrome", 0.0, 1.1, 0.0}};

    /// A look replaces color grading only; exposure, lens effects and their override flags are preserved.
    [[nodiscard]] inline Keire::PostProcessProfileDefinition
    ApplyPostProcessColorLook(const Keire::PostProcessProfileDefinition& source, const std::size_t lookIndex)
    {
        if (lookIndex >= PostProcessColorLooks.size())
        {
            throw std::invalid_argument("Unknown post-processing color look.");
        }
        Keire::PostProcessProfileAsset::Validate(source);
        auto result = source;
        const auto descriptions = Keire::PostProcessParameterDescriptions();
        const auto first = static_cast<std::size_t>(Keire::PostProcessParameter::Temperature);
        const auto last = static_cast<std::size_t>(Keire::PostProcessParameter::BlueGain);
        for (std::size_t index = first; index <= last; ++index)
        {
            result.Parameters[index] = {true, descriptions[index].Default};
        }
        const auto& look = PostProcessColorLooks[lookIndex];
        result.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::Temperature)].Value = look.Temperature;
        result.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::Contrast)].Value = look.Contrast;
        result.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::Saturation)].Value = look.Saturation;
        result.Effects[static_cast<std::size_t>(Keire::PostProcessEffect::ColorGrading)] = {true, true};
        Keire::PostProcessProfileAsset::Validate(result);
        return result;
    }
} // namespace KeireEditor::Detail
