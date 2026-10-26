#pragma once

#include "Keire/Rendering/Bloom.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"

namespace KeireEditor::Detail
{
    inline bool DrawBloomControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                  Keire::BloomSettings& settings, bool& commit)
    {
        auto section = ui.BeginTreeNode("HDR Bloom");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Soft glow from bright scene pixels, including the sun. Bloom is applied before tone "
                              "mapping; it does not change the sun's angular size or illuminate other objects.");
        bool changed = ui.Checkbox("Enable Bloom", settings.Enabled);
        commit |= changed;
        if (ui.Button("Reset Bloom"))
        {
            const bool enabled = settings.Enabled;
            settings = {};
            settings.Enabled = enabled;
            changed = true;
            commit = true;
        }
        auto disabled = ui.BeginDisabled(!settings.Enabled);
        changed |= DrawAtmosphereScalar(ui, "Glow Intensity", settings.Intensity, 0.01, 0.0, 16.0, commit);
        changed |= DrawAtmosphereScalar(ui, "HDR Threshold", settings.Threshold, 0.05, 0.0, 65504.0, commit);
        changed |= DrawAtmosphereScalar(ui, "Soft Knee", settings.SoftKnee, 0.01, 0.0, 1.0, commit);
        changed |= DrawAtmosphereScalar(ui, "Blur Radius", settings.Radius, 0.05, 0.5, 4.0, commit);
        ui.TextColoredWrapped(theme.MutedText,
                              "Threshold uses exposed linear HDR, so exposure affects which pixels glow. Soft Knee "
                              "smooths that threshold. Blur Radius is Gaussian sigma in half-resolution pixels, "
                              "not a world-space distance. "
                              "Reset preserves enabled state. Ctrl-click a value for exact entry.");
        return changed;
    }
} // namespace KeireEditor::Detail
