#pragma once

#include "KeireClientInternal/Editor/CloudControls.h"

#include <array>
#include <cstdint>
#include <limits>

namespace KeireEditor::Detail
{
    inline bool DrawVolumetricCloudControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                            Keire::VolumetricCloudSettings& settings, bool& commit)
    {
        auto section = ui.BeginTreeNode("Volumetric Clouds (In Development)");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Authoring and persistence only: the volume renderer is not connected yet. "
                              "These values currently have no viewport effect. Layered clouds remain separate.");
        bool changed = ui.Checkbox("Enable Volume", settings.Enabled);
        commit |= changed;
        if (ui.Button("Reset Volume Settings"))
        {
            const Keire::VolumetricCloudSettings defaults;
            if (settings != defaults)
            {
                settings = defaults;
                changed = true;
                commit = true;
            }
        }
        if (auto geometry = ui.BeginTreeNode("Shape and Density"); geometry)
        {
            changed |=
                DrawCloudScalar(ui, "Base Height (m)", settings.BaseHeightMeters, 10.0, -100000.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Thickness (m)", settings.ThicknessMeters, 10.0, 1.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Pattern Size (m)", settings.PatternSizeMeters, 10.0, 1.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Volume Coverage", settings.Coverage, 0.01, 0.0, 1.0, commit);
            changed |= DrawCloudScalar(ui, "Extinction (1/m)", settings.ExtinctionPerMeter, 0.0001, 0.0, 10.0, commit);
            changed |= DrawCloudScalar(ui, "Detail Erosion", settings.DetailAmount, 0.01, 0.0, 1.0, commit);
            std::uint64_t seed = settings.Seed;
            if (ui.DragUnsignedInteger("Volume Seed", seed, 1.0, std::uint64_t{0},
                                       std::uint64_t{std::numeric_limits<std::uint32_t>::max()}))
            {
                settings.Seed = static_cast<std::uint32_t>(seed);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
        }
        if (auto lighting = ui.BeginTreeNode("Scattering and Wind"); lighting)
        {
            changed |= DrawCloudScalar(ui, "Scattering Anisotropy", settings.Anisotropy, 0.01, -0.95, 0.95, commit);
            constexpr std::array colors{"Scattering Red", "Scattering Green", "Scattering Blue"};
            constexpr std::array winds{"East Wind (m/s)", "Up Wind (m/s)", "North Wind (m/s)"};
            for (std::size_t axis = 0; axis < settings.ScatteringAlbedo.size(); ++axis)
            {
                changed |= DrawCloudScalar(ui, colors[axis], settings.ScatteringAlbedo[axis], 0.01, 0.0, 1.0, commit);
                changed |= DrawCloudScalar(ui, winds[axis], settings.WindMetersPerSecond[axis], 0.1, -10000.0, 10000.0,
                                           commit);
            }
        }
        if (auto quality = ui.BeginTreeNode("Quality Budget"); quality)
        {
            std::uint64_t viewSteps = settings.ViewSteps;
            if (ui.DragUnsignedInteger("View Samples", viewSteps, 1.0, std::uint64_t{1}, std::uint64_t{128}))
            {
                settings.ViewSteps = static_cast<std::uint32_t>(viewSteps);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            std::uint64_t shadowSteps = settings.ShadowSteps;
            if (ui.DragUnsignedInteger("Shadow Samples", shadowSteps, 1.0, std::uint64_t{1}, std::uint64_t{16}))
            {
                settings.ShadowSteps = static_cast<std::uint32_t>(shadowSteps);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            changed |= DrawCloudScalar(ui, "Maximum Distance (m)", settings.MaximumDistanceMeters, 100.0, 1.0,
                                       1000000.0, commit);
            ui.TextColoredWrapped(
                theme.MutedText,
                "View and shadow samples multiply rendering cost. Budgets are limits, not performance guarantees.");
        }
        return changed;
    }
} // namespace KeireEditor::Detail
