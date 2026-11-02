#pragma once

#include "Keire/ECS/Components/WaterBodyComponent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor::Detail
{
    enum class WaterWavePreset
    {
        Still,
        Gentle,
        Ocean
    };

    enum class WaterSetupPreset
    {
        Sea,
        Lake,
        Pond
    };

    enum class WaterLookPreset
    {
        Natural,
        Stylized
    };

    [[nodiscard]] inline std::vector<Keire::WaterWaveDefinition> WaterPresetWaves(const WaterWavePreset preset)
    {
        switch (preset)
        {
        case WaterWavePreset::Still:
            return {};
        case WaterWavePreset::Gentle:
            return {{8.0, 0.06, 1.8, 20.0, 0.0, 0.25}, {3.0, 0.025, 1.0, 85.0, 70.0, 0.2}};
        case WaterWavePreset::Ocean:
            return {{48.0, 0.7, 8.0, 20.0, 0.0, 0.45},
                    {23.0, 0.3, 5.0, 35.0, 80.0, 0.35},
                    {11.0, 0.12, 3.0, 75.0, 160.0, 0.25},
                    {4.0, 0.04, 1.5, 120.0, 245.0, 0.2}};
        }
        throw std::invalid_argument("Unknown water wave preset.");
    }

    /// Explicit setup action replaces fluid, geometry and surface settings together; Transform is independent.
    inline void ApplyWaterSetup(Keire::WaterBodySettings& settings, const WaterSetupPreset preset)
    {
        Keire::WaterBodySettings candidate;
        switch (preset)
        {
        case WaterSetupPreset::Sea:
            candidate.Shape = Keire::WaterSurfaceShape::InfinitePlane;
            candidate.Waves = WaterPresetWaves(WaterWavePreset::Ocean);
            candidate.SurfaceColor = {0.008F, 0.055F, 0.085F, 0.9F};
            candidate.Roughness = 0.12F;
            candidate.CrestFoamStrength = 0.65F;
            candidate.DensityKilogramsPerCubicMetre = 1'025.0;
            candidate.GridSpacingMetres = 0.75;
            break;
        case WaterSetupPreset::Lake:
            candidate.Shape = Keire::WaterSurfaceShape::Ellipse;
            candidate.HalfExtentXMetres = 100.0;
            candidate.HalfExtentZMetres = 70.0;
            candidate.DepthMetres = 20.0;
            candidate.Waves = WaterPresetWaves(WaterWavePreset::Gentle);
            candidate.SurfaceColor = {0.015F, 0.08F, 0.07F, 0.8F};
            candidate.Roughness = 0.1F;
            candidate.CrestFoamStrength = 0.1F;
            candidate.GridSpacingMetres = 0.5;
            break;
        case WaterSetupPreset::Pond:
            candidate.Shape = Keire::WaterSurfaceShape::Ellipse;
            candidate.HalfExtentXMetres = 12.0;
            candidate.HalfExtentZMetres = 9.0;
            candidate.DepthMetres = 3.0;
            candidate.Waves = {{3.7, 0.012, 0.7, 25.0, 0.0, 0.15}, {1.6, 0.004, 0.4, 78.0, 117.0, 0.1}};
            candidate.SurfaceColor = {0.025F, 0.075F, 0.035F, 0.75F};
            candidate.Roughness = 0.08F;
            candidate.CrestFoamStrength = 0.0F;
            candidate.GridSpacingMetres = 0.25;
            break;
        default:
            throw std::invalid_argument("Unknown water setup preset.");
        }
        Keire::WaterBodyComponent::Validate(candidate);
        settings = std::move(candidate);
    }

    /// Looks affect appearance only, never geometry, simulation, wave composition or custom material assignment.
    inline void ApplyWaterLook(Keire::WaterBodySettings& settings, const WaterLookPreset preset)
    {
        switch (preset)
        {
        case WaterLookPreset::Natural:
            settings.SurfaceColor = {0.012F, 0.07F, 0.09F, 0.85F};
            settings.Roughness = 0.12F;
            settings.CrestFoamStrength = 0.3F;
            return;
        case WaterLookPreset::Stylized:
            settings.SurfaceColor = {0.025F, 0.3F, 0.45F, 0.95F};
            settings.Roughness = 0.35F;
            settings.CrestFoamStrength = 0.75F;
            return;
        }
        throw std::invalid_argument("Unknown water look preset.");
    }

    [[nodiscard]] inline double WaterWaveHeight(const Keire::WaterBodySettings& settings)
    {
        double amplitude = 0.0;
        for (const auto& wave : settings.Waves)
        {
            amplitude += wave.AmplitudeMetres;
        }
        return 2.0 * amplitude;
    }

    /// Scales the whole spectrum, retaining relative wave sizes, phases, directions and speeds.
    inline void SetWaterWaveHeight(Keire::WaterBodySettings& settings, const double heightMetres)
    {
        if (!std::isfinite(heightMetres) || heightMetres < 0.0 || heightMetres > 100.0 ||
            settings.Shape == Keire::WaterSurfaceShape::Sphere)
        {
            throw std::invalid_argument("Wave height requires planar water and a finite value from 0 to 100 metres.");
        }
        auto candidate = settings;
        if (WaterWaveHeight(candidate) == 0.0 && heightMetres > 0.0)
        {
            candidate.Waves = WaterPresetWaves(WaterWavePreset::Gentle);
        }
        const auto previousHeight = WaterWaveHeight(candidate);
        if (previousHeight > 0.0)
        {
            const auto scale = heightMetres / previousHeight;
            for (auto& wave : candidate.Waves)
            {
                wave.AmplitudeMetres *= scale;
            }
        }
        Keire::WaterBodyComponent::Validate(candidate);
        settings = std::move(candidate);
    }

    inline void SetWaterWaveDirection(Keire::WaterBodySettings& settings, const double directionDegrees)
    {
        if (!std::isfinite(directionDegrees))
        {
            throw std::invalid_argument("Water direction must be finite.");
        }
        if (settings.Waves.empty())
        {
            return;
        }
        const auto delta =
            std::remainder(directionDegrees, 360.0) - std::remainder(settings.Waves.front().DirectionDegrees, 360.0);
        for (auto& wave : settings.Waves)
        {
            wave.DirectionDegrees = std::remainder(std::remainder(wave.DirectionDegrees, 360.0) + delta, 360.0);
        }
    }

    inline void AddWaterWave(Keire::WaterBodySettings& settings)
    {
        if (settings.Shape == Keire::WaterSurfaceShape::Sphere ||
            settings.Waves.size() >= Keire::WaterWaveField::MaximumWaves)
        {
            throw std::invalid_argument("A wave cannot be added to this water body.");
        }
        settings.Waves.emplace_back();
    }

    inline void RemoveWaterWave(Keire::WaterBodySettings& settings, const std::size_t index)
    {
        if (index >= settings.Waves.size())
        {
            throw std::out_of_range("Water wave index is outside the authored list.");
        }
        settings.Waves.erase(settings.Waves.begin() + static_cast<std::ptrdiff_t>(index));
    }

    inline void MoveWaterWave(Keire::WaterBodySettings& settings, const std::size_t from, const std::size_t to)
    {
        if (from >= settings.Waves.size() || to >= settings.Waves.size())
        {
            throw std::out_of_range("Water wave move is outside the authored list.");
        }
        if (from < to)
        {
            std::rotate(settings.Waves.begin() + static_cast<std::ptrdiff_t>(from),
                        settings.Waves.begin() + static_cast<std::ptrdiff_t>(from + 1),
                        settings.Waves.begin() + static_cast<std::ptrdiff_t>(to + 1));
        }
        else if (from > to)
        {
            std::rotate(settings.Waves.begin() + static_cast<std::ptrdiff_t>(to),
                        settings.Waves.begin() + static_cast<std::ptrdiff_t>(from),
                        settings.Waves.begin() + static_cast<std::ptrdiff_t>(from + 1));
        }
    }

    /// Produces only changed properties; unrelated per-selection authoring remains untouched.
    [[nodiscard]] inline Keire::ComponentPropertyBag WaterBodyPropertyChanges(const Keire::WaterBodySettings& before,
                                                                              const Keire::WaterBodySettings& after)
    {
        const auto registration = Keire::CreateWaterBodyComponentRegistration();
        const auto source = Keire::CreateRef<Keire::WaterBodyComponent>();
        const auto candidate = Keire::CreateRef<Keire::WaterBodyComponent>();
        source->Configure(before);
        candidate->Configure(after);
        const auto original = registration.Serialize(*source);
        auto changes = registration.Serialize(*candidate);
        std::erase_if(changes, [&](const auto& entry) { return original.at(entry.first) == entry.second; });
        return changes;
    }
} // namespace KeireEditor::Detail
