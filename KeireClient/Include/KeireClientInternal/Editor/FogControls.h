#pragma once

#include "Keire/Rendering/Fog.h"
#include "KeireClientInternal/Editor/CloudControls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>

namespace KeireEditor::Detail
{
    inline bool DrawFogControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme, Keire::FogSettings& settings,
                                bool& commit)
    {
        auto id = ui.PushId("FogControls");
        ui.TextWrapped("Height and Local Fog (In Development)");
        ui.TextColoredWrapped(
            theme.MutedText,
            "Fog fills the air between the camera and visible surfaces. Global height fog and local boxes add "
            "together. Exclusion regions suppress fog without affecting clouds. Terrain shadows and final "
            "performance tuning are still in development.");
        bool changed = ui.Checkbox("Enable Fog", settings.Enabled);
        commit |= changed;
        if (ui.Button("Reset Fog"))
        {
            settings = {};
            changed = true;
            commit = true;
        }
        if (auto section = ui.BeginSection("Height and Visibility"); section)
        {
            changed |=
                DrawCloudScalar(ui, "Base Height (m)", settings.BaseHeightMeters, 1.0, -100000.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Density (per m)", settings.ExtinctionPerMeter, 0.0001, 0.0, 10.0, commit);
            changed |= DrawCloudScalar(ui, "Height Falloff (per m)", settings.HeightFalloffPerMeter, 0.0001, 0.0, 10.0,
                                       commit);
            changed |= DrawCloudScalar(ui, "Maximum Distance (m)", settings.MaximumDistanceMeters, 10.0, 1.0, 1000000.0,
                                       commit);
            ui.TextColoredWrapped(theme.MutedText, "Density stays constant below Base Height and fades above it. Set "
                                                   "global Density to zero for local boxes only.");
        }
        if (auto section = ui.BeginSection("Scattering and Quality"); section)
        {
            constexpr std::array channels{"Scattering Red", "Scattering Green", "Scattering Blue"};
            for (std::size_t channel = 0; channel < channels.size(); ++channel)
            {
                changed |=
                    DrawCloudScalar(ui, channels[channel], settings.ScatteringAlbedo[channel], 0.01, 0.0, 1.0, commit);
            }
            changed |= DrawCloudScalar(ui, "Directional Scattering", settings.Anisotropy, 0.01, -0.95, 0.95, commit);
            std::uint64_t steps = settings.ViewSteps;
            if (ui.DragUnsignedInteger("Ray Samples", steps, 1.0, std::uint64_t{1}, std::uint64_t{128}))
            {
                settings.ViewSteps =
                    static_cast<std::uint32_t>(std::clamp(steps, std::uint64_t{1}, std::uint64_t{128}));
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            ui.TextColoredWrapped(theme.MutedText, "More ray samples improve steep density changes at greater GPU "
                                                   "cost. Local box boundaries add samples automatically.");
        }
        if (auto section = ui.BeginSection("Local Volumes"); section)
        {
            ui.TextWrapped(std::format("{} / {} boxes", settings.LocalVolumes.size(), Keire::MaximumLocalFogVolumes));
            {
                auto disabled = ui.BeginDisabled(settings.LocalVolumes.size() >= Keire::MaximumLocalFogVolumes);
                if (ui.Button("Add Fog Box"))
                {
                    settings.LocalVolumes.emplace_back();
                    changed = true;
                    commit = true;
                }
            }
            for (std::size_t index = 0; index < settings.LocalVolumes.size(); ++index)
            {
                const auto label = std::format("Fog Box {}", index + 1);
                auto volumeId = ui.PushId(label);
                auto volumeSection = ui.BeginSection(label);
                if (!volumeSection)
                {
                    continue;
                }
                if (ui.Button("Remove Box"))
                {
                    settings.LocalVolumes.erase(settings.LocalVolumes.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    commit = true;
                    break;
                }
                auto& volume = settings.LocalVolumes[index];
                if (ui.Checkbox("Enable Box", volume.Enabled))
                {
                    changed = true;
                    commit = true;
                }
                constexpr std::array centers{"Center X (m)", "Center Y (m)", "Center Z (m)"};
                constexpr std::array extents{"Half Width (m)", "Half Height (m)", "Half Depth (m)"};
                for (std::size_t axis = 0; axis < centers.size(); ++axis)
                {
                    changed |= DrawCloudScalar(ui, centers[axis], volume.Center[axis], 1.0, -1.0e15, 1.0e15, commit);
                    changed |=
                        DrawCloudScalar(ui, extents[axis], volume.HalfExtent[axis], 0.1, 0.01, 1000000.0, commit);
                }
                changed |=
                    DrawCloudScalar(ui, "Box Density (per m)", volume.ExtinctionPerMeter, 0.0001, 0.0, 10.0, commit);
                changed |= DrawCloudScalar(ui, "Edge Softness", volume.EdgeFraction, 0.01, 0.0, 1.0, commit);
            }
        }
        if (auto section = ui.BeginSection("Exclusion Regions"); section)
        {
            ui.TextWrapped(
                std::format("{} / {} regions", settings.ExclusionRegions.size(), Keire::MaximumFogExclusionRegions));
            ui.TextColoredWrapped(theme.MutedText, "Suppress height and local fog inside a region. Overlaps use the "
                                                   "strongest exclusion; duplicating a region does not deepen it.");
            {
                auto disabled = ui.BeginDisabled(settings.ExclusionRegions.size() >= Keire::MaximumFogExclusionRegions);
                if (ui.Button("Add Exclusion Region"))
                {
                    settings.ExclusionRegions.emplace_back();
                    changed = true;
                    commit = true;
                }
            }
            for (std::size_t index = 0; index < settings.ExclusionRegions.size(); ++index)
            {
                const auto label = std::format("Exclusion {}", index + 1);
                auto regionId = ui.PushId(label);
                auto regionSection = ui.BeginSection(label);
                if (!regionSection)
                {
                    continue;
                }
                if (ui.Button("Remove Region"))
                {
                    settings.ExclusionRegions.erase(settings.ExclusionRegions.begin() +
                                                    static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    commit = true;
                    break;
                }
                auto& region = settings.ExclusionRegions[index];
                if (ui.Checkbox("Enable Region", region.Enabled))
                {
                    changed = true;
                    commit = true;
                }
                constexpr std::array centers{"Center X (m)", "Center Y (m)", "Center Z (m)"};
                constexpr std::array extents{"Half Width (m)", "Half Height (m)", "Half Depth (m)"};
                for (std::size_t axis = 0; axis < centers.size(); ++axis)
                {
                    changed |= DrawCloudScalar(ui, centers[axis], region.Center[axis], 1.0, -1.0e15, 1.0e15, commit);
                    changed |=
                        DrawCloudScalar(ui, extents[axis], region.HalfExtent[axis], 0.1, 0.01, 1000000.0, commit);
                }
                changed |= DrawCloudScalar(ui, "Strength", region.Strength, 0.01, 0.0, 1.0, commit);
                changed |= DrawCloudScalar(ui, "Edge Softness", region.EdgeFraction, 0.01, 0.0, 1.0, commit);
            }
        }
        return changed;
    }
} // namespace KeireEditor::Detail
