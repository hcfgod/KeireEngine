#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"

#include <string_view>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline std::string_view
    PostProcessLensDiagnostic(const Keire::PostProcessProfileDefinition& profile) noexcept
    {
        const auto& focus = profile.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::FocusDistance)];
        const auto& focal = profile.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::FocalLength)];

        // Inherited values are camera-dependent; only diagnose a fully authored pair here.
        if (focus.Override && focal.Override && focus.Value <= focal.Value * 0.001)
        {
            return "Focus distance must exceed focal length (in metres). Depth of field is bypassed for an invalid "
                   "lens.";
        }

        return {};
    }
} // namespace KeireEditor::Detail
