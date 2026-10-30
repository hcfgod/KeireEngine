#pragma once

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline Keire::Detail::PlanetDefinition
    PlanetFromAtmosphere(const Keire::AtmosphereSettings& atmosphere,
                         const Keire::Detail::PlanetProjectSettings& settings)
    {
        Keire::Detail::PlanetDefinition planet;
        planet.Planet = Keire::AssetId::Generate();
        planet.Frame = 2;
        while (
            std::ranges::any_of(settings.Planets, [&](const auto& existing) { return existing.Frame == planet.Frame; }))
        {
            ++planet.Frame;
        }
        planet.ParentCenter = Keire::Detail::SectorPosition({1, 1}, {}, atmosphere.PlanetCenter);
        planet.RadiusMetres = atmosphere.PlanetRadius;
        return planet;
    }

    inline bool DrawPlanetControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                   ProjectSettingsDocument& document)
    {
        auto section = ui.BeginSection("Planet Placement and Ownership");
        if (!section)
        {
            return false;
        }
        using namespace Keire::Detail;
        auto settings = document.Planets();
        bool changed = false;
        bool commit = false;
        ui.TextColoredWrapped(
            theme.MutedText,
            "Choose a shared planet for environment geometry and gravity. Flat mode preserves existing scenes. "
            "Planet placement stays fixed while the camera travels; scene objects keep their authored positions.");
        {
            auto disabled = ui.BeginDisabled(settings.Planets.size() >= 64);
            if (ui.Button("Add Planet From Current Atmosphere"))
            {
                auto planet = PlanetFromAtmosphere(document.Settings().Atmosphere, settings);
                settings.SelectedPlanet = planet.Planet;
                settings.Planets.push_back(std::move(planet));
                changed = true;
                commit = true;
            }
        }
        ui.TextColoredWrapped(theme.MutedText, "Adding copies the atmosphere center and radius. Geometry and gravity "
                                               "modes change only when you select them.");
        const auto selectedLabel = settings.SelectedPlanet ? settings.SelectedPlanet.ToString() : std::string("None");
        if (auto combo = ui.BeginCombo("Selected Planet", selectedLabel); combo)
        {
            for (const auto& planet : settings.Planets)
            {
                if (ui.Selectable(planet.Planet.ToString(), settings.SelectedPlanet == planet.Planet))
                {
                    settings.SelectedPlanet = planet.Planet;
                    changed = true;
                    commit = true;
                }
            }
        }
        constexpr std::array geometryLabels{"Flat", "Planetary"};
        if (auto combo =
                ui.BeginCombo("Environment Geometry", geometryLabels[static_cast<std::size_t>(settings.Geometry)]);
            combo)
        {
            for (std::size_t index = 0; index < geometryLabels.size(); ++index)
            {
                auto disabled = ui.BeginDisabled(index != 0 && !settings.SelectedPlanet);
                if (ui.Selectable(geometryLabels[index], static_cast<std::size_t>(settings.Geometry) == index))
                {
                    settings.Geometry = static_cast<PlanetGeometryMode>(index);
                    changed = true;
                    commit = true;
                }
            }
        }
        constexpr std::array gravityLabels{"Uniform", "Radial", "Combined"};
        if (settings.Geometry == PlanetGeometryMode::Planetary)
        {
            constexpr std::array selectionLabels{"Selected planet", "Nearest surface per camera"};
            if (auto combo =
                    ui.BeginCombo("Environment Planet", selectionLabels[static_cast<std::size_t>(settings.Selection)]);
                combo)
            {
                for (std::size_t index = 0; index < selectionLabels.size(); ++index)
                {
                    if (ui.Selectable(selectionLabels[index], static_cast<std::size_t>(settings.Selection) == index))
                    {
                        settings.Selection = static_cast<PlanetSelectionMode>(index);
                        changed = true;
                        commit = true;
                    }
                }
            }
            ui.TextColoredWrapped(
                theme.MutedText,
                "Each camera uses the selected planet or the nearest planet surface. Atmosphere appearance is shared; "
                "this does not blend multiple atmospheres. The atmosphere center and "
                "radius below remain saved for Flat mode.");
        }
        if (auto combo = ui.BeginCombo("Gravity Mode", gravityLabels[static_cast<std::size_t>(settings.Gravity)]);
            combo)
        {
            for (std::size_t index = 0; index < gravityLabels.size(); ++index)
            {
                auto disabled = ui.BeginDisabled(index != 0 && settings.Planets.empty());
                if (ui.Selectable(gravityLabels[index], static_cast<std::size_t>(settings.Gravity) == index))
                {
                    settings.Gravity = static_cast<PlanetGravityMode>(index);
                    changed = true;
                    commit = true;
                }
            }
        }
        ui.TextColoredWrapped(
            theme.MutedText,
            "Uniform uses Audio / Physics acceleration. Radial uses all authored planet sources. Combined adds both. "
            "Gravity mode changes take effect on the next Play session. More sources increase physics evaluation "
            "cost.");
        auto selected = std::ranges::find(settings.Planets, settings.SelectedPlanet, &PlanetDefinition::Planet);
        if (selected != settings.Planets.end())
        {
            auto& planet = *selected;
            ui.Text("Stable planet ID: " + planet.Planet.ToString());
            ui.Text("Frame: " + std::to_string(planet.Frame));
            std::array<std::int64_t, 3> sectors{};
            std::array<double, 3> offsets{};
            constexpr std::array axes{"X", "Y", "Z"};
            bool placementChanged = false;
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                sectors[axis] = planet.ParentCenter.Axes()[axis].Sector();
                offsets[axis] = planet.ParentCenter.Axes()[axis].Offset();
                placementChanged |= ui.DragInteger(std::string("Sector ") + axes[axis], sectors[axis]);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                placementChanged |=
                    DrawAtmosphereScalar(ui, std::string("Offset ") + axes[axis] + " (m)", offsets[axis], 1.0,
                                         -SectorCoordinate::HalfSector, SectorCoordinate::HalfSector, commit);
            }
            if (placementChanged)
            {
                planet.ParentCenter = SectorPosition(planet.ParentCenter.Identity(), sectors, offsets);
                changed = true;
            }
            ui.TextColoredWrapped(theme.MutedText, "Each sector is 65,536 metres. Offsets retain local detail at "
                                                   "distant placements. Ctrl-click to enter exact values.");
            changed |= DrawAtmosphereScalar(ui, "Surface Radius (m)", planet.RadiusMetres, 100.0, 1.0, 1.0e12, commit);
            changed |= DrawAtmosphereScalar(ui, "Gravity Parameter GM (m^3/s^2)", planet.GravitationalParameter, 1.0,
                                            0.0, 1.0e30, commit);
            ui.TextColoredWrapped(theme.MutedText, "GM defaults to zero. Surface acceleration is GM / radius squared.");
            auto orientation = planet.Orientation.Components();
            bool rotationChanged = false;
            constexpr std::array quaternionLabels{"Orientation X", "Orientation Y", "Orientation Z", "Orientation W"};
            for (std::size_t axis = 0; axis < orientation.size(); ++axis)
            {
                rotationChanged |=
                    DrawAtmosphereScalar(ui, quaternionLabels[axis], orientation[axis], 0.01, -1.0, 1.0, commit);
            }
            if (rotationChanged)
            {
                planet.Orientation =
                    FrameRotation::Normalize(orientation[0], orientation[1], orientation[2], orientation[3]);
                changed = true;
            }
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                changed |= DrawAtmosphereScalar(ui, std::string("Angular Velocity ") + axes[axis] + " (rad/s)",
                                                planet.AngularVelocity[axis], 0.00001, -100.0, 100.0, commit);
            }
            changed |= DrawAtmosphereScalar(ui, "Rotation Epoch (simulation seconds)", planet.EpochSeconds, 1.0,
                                            -1.0e12, 1.0e12, commit);
            if (ui.Button("Reset Planet Rotation"))
            {
                planet.Orientation = {};
                planet.AngularVelocity = {};
                planet.EpochSeconds = 0.0;
                changed = true;
                commit = true;
            }
            ui.TextColoredWrapped(theme.MutedText, "Rotation reset preserves identity, placement, radius and gravity.");
            const bool hasChildren =
                std::ranges::any_of(settings.Planets, [&](const auto& other)
                                    { return other.ParentCenter.Identity().Frame == planet.Frame; });
            auto disabled = ui.BeginDisabled(hasChildren);
            if (ui.Button("Remove Selected Planet"))
            {
                settings.Planets.erase(selected);
                settings.SelectedPlanet = settings.Planets.empty() ? Keire::AssetId{} : settings.Planets.front().Planet;
                if (settings.Planets.empty())
                {
                    settings.Geometry = PlanetGeometryMode::Flat;
                    settings.Gravity = PlanetGravityMode::Uniform;
                }
                changed = true;
                commit = true;
            }
        }
        if (changed)
        {
            document.UpdatePlanets(std::move(settings));
        }
        return commit;
    }
} // namespace KeireEditor::Detail
