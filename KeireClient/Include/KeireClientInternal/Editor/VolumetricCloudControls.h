#pragma once

#include "KeireClientInternal/Editor/CloudControls.h"
#include "KeireClientInternal/Editor/VolumetricCloudPresets.h"

#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <string>

namespace KeireEditor::Detail
{
    inline bool DrawVolumetricCloudControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                            Keire::VolumetricCloudSettings& settings, bool& commit)
    {
        auto section = ui.BeginSection("Volumetric Clouds (In Development)");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(
            theme.MutedText,
            "Experimental local cloud volume. Legacy custom shaders and multiplicative materials still need "
            "volume integration. Particle overlap, planet interaction and MSAA edge quality are unverified. "
            "Layered clouds remain separate.");
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
        if (auto geometry = ui.BeginSection("Shape and Density"); geometry)
        {
            ui.TextWrapped("Shape Starting Point");
            ui.SetNextItemWidth(-1.0F);
            if (auto preset = ui.BeginCombo("##ShapeStartingPoint", CloudShapePresetLabel(settings)); preset)
            {
                for (const auto style : CloudShapeStyles)
                {
                    const auto candidate = CloudShapePreset(settings, style);
                    if (ui.Selectable(CloudShapeStyleLabel(style), candidate == settings))
                    {
                        if (candidate != settings)
                        {
                            settings = candidate;
                            changed = true;
                            commit = true;
                        }
                    }
                }
            }
            ui.TextColoredWrapped(theme.MutedText,
                                  "Starting points replace shape and density controls. Placement, seed, wind, "
                                  "lighting and quality stay as authored. Dense billows increases light absorption "
                                  "for firmer cloud bodies. All values remain editable.");
            changed |=
                DrawCloudScalar(ui, "Base Height (m)", settings.BaseHeightMeters, 10.0, -100000.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Thickness (m)", settings.ThicknessMeters, 10.0, 1.0, 100000.0, commit);
            changed |= DrawCloudScalar(ui, "Pattern Size (m)", settings.PatternSizeMeters, 10.0, 1.0, 100000.0, commit);
            if (auto profile = ui.BeginSection("Vertical Shape"); profile)
            {
                constexpr std::array labels{"Base Start", "Base Fullness", "Crown Falloff", "Crown End"};
                for (std::size_t index = 0; index < settings.HeightProfile.size(); ++index)
                {
                    const double minimum = index == 0 ? 0.0 : settings.HeightProfile[index - 1];
                    const double maximum = index == 3 ? 1.0 : settings.HeightProfile[index + 1];
                    double candidate = settings.HeightProfile[index];
                    if (DrawCloudScalar(ui, labels[index], candidate, 0.005, minimum, maximum, commit))
                    {
                        // A collapsed whole layer is invalid, while sharp base/crown ramps are supported.
                        if ((index != 0 || candidate < settings.HeightProfile[3]) &&
                            (index != 3 || candidate > settings.HeightProfile[0]))
                        {
                            settings.HeightProfile[index] = candidate;
                            changed = true;
                        }
                    }
                }
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Heights are fractions of Thickness above Base Height. The base ramp fills the cloud; "
                    "the crown ramp tapers it. Density changes opacity independently.");
            }
            changed |= DrawCloudScalar(ui, "Volume Coverage", settings.Coverage, 0.01, 0.0, 1.0, commit);
            constexpr double referenceExtinction = Keire::VolumetricCloudSettings{}.ExtinctionPerMeter;
            double densityMultiplier = settings.ExtinctionPerMeter / referenceExtinction;
            if (DrawCloudScalar(ui, "Cloud Density", densityMultiplier, 0.025, 0.0, 10.0 / referenceExtinction, commit))
            {
                settings.ExtinctionPerMeter = densityMultiplier * referenceExtinction;
                changed = true;
            }
            ui.TextColoredWrapped(theme.MutedText,
                                  "Density controls opacity and light absorption without changing the shape field. "
                                  "One is the default; higher values make clouds optically thicker.");
            changed |= DrawCloudScalar(ui, "Shape Detail", settings.DetailAmount, 0.01, 0.0, 1.0, commit);
            changed |= DrawCloudScalar(ui, "Cloud Erosion", settings.ErosionAmount, 0.01, 0.0, 1.0, commit);
            ui.TextColoredWrapped(theme.MutedText,
                                  "Shape Detail adds smaller variations. Cloud Erosion removes thin regions; "
                                  "increase it independently for broken cloud edges.");
            changed |= DrawCloudScalar(ui, "Core Concentration", settings.DensityContrast, 0.01, 0.25, 4.0, commit);
            ui.TextColoredWrapped(theme.MutedText,
                                  "Higher Core Concentration removes thin edges and concentrates the cloud into "
                                  "smaller cores. Use Cloud Density to make the existing shape more opaque.");
            std::uint64_t detailFrequency = settings.DetailFrequency;
            ui.TextWrapped("Detail Frequency");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragUnsignedInteger("##DetailFrequency", detailFrequency, 1.0, std::uint64_t{1}, std::uint64_t{16}))
            {
                settings.DetailFrequency = static_cast<std::uint32_t>(detailFrequency);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            changed |= DrawCloudScalar(ui, "Variation Strength", settings.VariationStrength, 0.01, 0.0, 1.0, commit);
            const auto variationLabel = [&](const std::uint32_t period)
            {
                const double meters = settings.PatternSizeMeters * 4.0 * period;
                return meters < 1000.0 ? std::format("{:.3g} m", meters) : std::format("{:.3g} km", meters / 1000.0);
            };
            const auto periodLabel = variationLabel(settings.VariationPeriod);
            ui.TextWrapped("Variation Scale");
            ui.SetNextItemWidth(-1.0F);
            if (auto period = ui.BeginCombo("##VariationScale", periodLabel); period)
            {
                for (const std::uint32_t value : {1U, 2U, 4U, 8U, 16U, 32U, 64U})
                {
                    const auto label = variationLabel(value);
                    if (ui.Selectable(label, settings.VariationPeriod == value))
                    {
                        settings.VariationPeriod = value;
                        changed = true;
                        commit = true;
                    }
                }
            }
            ui.TextColoredWrapped(
                theme.MutedText,
                "Detail Frequency multiplies body frequency. Variation Scale sets the world-space repeat distance "
                "of the displacement field and follows Pattern Size. Smaller scales distort shapes more rapidly. "
                "Zero Variation Strength disables displacement.");
            const bool weatherChanged = ui.Checkbox("Independent Weather", settings.IndependentWeather);
            changed |= weatherChanged;
            commit |= weatherChanged;
            if (settings.IndependentWeather)
            {
                changed |= DrawCloudScalar(ui, "Cloud Type", settings.CloudType, 0.01, 0.0, 1.0, commit);
                changed |= DrawCloudScalar(ui, "Type Variation", settings.TypeVariation, 0.01, 0.0, 1.0, commit);
                changed |= DrawCloudScalar(ui, "Base Height Variation (m)", settings.BaseHeightVariationMeters, 10.0,
                                           0.0, 100000.0, commit);
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Separate seeded weather groups coverage and cloud types. Cloud Type ranges from shallow "
                    "layers (0) to the full vertical profile (1). Type Variation mixes regional heights; "
                    "zero keeps one type. Base Height Variation shifts regional cloud bases above and below "
                    "Base Height without changing their thickness. Density still controls light absorption.");
            }
            changed |=
                DrawCloudScalar(ui, "Regional Coverage", settings.RegionalCoverageStrength, 0.01, 0.0, 1.0, commit);
            const auto regionLabel = [&settings](const std::uint32_t period)
            {
                const double scale = settings.IndependentWeather ? 1.0 : 4.0;
                const double meters = settings.PatternSizeMeters * scale * period;
                return meters < 1000.0 ? std::format("{:.3g} m", meters) : std::format("{:.3g} km", meters / 1000.0);
            };
            ui.TextWrapped("Region Scale");
            ui.SetNextItemWidth(-1.0F);
            if (auto region = ui.BeginCombo("##RegionScale", regionLabel(settings.RegionalCoveragePeriod)); region)
            {
                for (const std::uint32_t value : {1U, 2U, 4U, 8U, 16U, 32U, 64U})
                {
                    if (ui.Selectable(regionLabel(value), settings.RegionalCoveragePeriod == value))
                    {
                        settings.RegionalCoveragePeriod = value;
                        changed = true;
                        commit = true;
                    }
                }
            }
            ui.TextColoredWrapped(theme.MutedText,
                                  "Regional Coverage groups clouds and clear areas without stretching their shapes. "
                                  "Zero keeps uniform coverage. Empty and full coverage remain unchanged.");
            ui.TextColoredWrapped(
                theme.MutedText,
                settings.IndependentWeather
                    ? "Region Scale is the largest weather-cell size; it follows Pattern Size and also affects "
                      "Type Variation. Weather repeats after 256 Pattern Sizes."
                    : "Region Scale is the horizontal repeat distance of the legacy coverage field; "
                      "it follows Pattern Size.");
            std::uint64_t seed = settings.Seed;
            ui.TextWrapped("Volume Seed");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragUnsignedInteger("##VolumeSeed", seed, 1.0, std::uint64_t{0},
                                       std::uint64_t{std::numeric_limits<std::uint32_t>::max()}))
            {
                settings.Seed = static_cast<std::uint32_t>(seed);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
        }
        if (auto lighting = ui.BeginSection("Scattering and Wind"); lighting)
        {
            changed |=
                DrawCloudScalar(ui, "Ambient Sky Occlusion", settings.AmbientOcclusionStrength, 0.01, 0.0, 1.0, commit);
            ui.TextColoredWrapped(theme.MutedText,
                                  "Darkens cloud interiors using four sky directions. Zero preserves unoccluded "
                                  "ambient light. Enabling this adds lighting work, especially while moving or "
                                  "outside the lighting cache. It does not change cloud shape or scene ambient light.");
            changed |= DrawCloudScalar(ui, "Scattering Anisotropy", settings.Anisotropy, 0.01, -0.95, 0.95, commit);
            changed |= DrawCloudScalar(ui, "Multiple Scattering", settings.MultipleScatteringContribution, 0.01, 0.0,
                                       1.0, commit);
            changed |= DrawCloudScalar(ui, "Scattering Occlusion", settings.MultipleScatteringOcclusion, 0.01, 0.0, 1.0,
                                       commit);
            ui.TextColoredWrapped(theme.MutedText,
                                  "Multiple Scattering adds two approximate light bounces. Zero retains single "
                                  "scattering; lower Occlusion lets bounced light penetrate further. This is an "
                                  "artistic approximation and adds shader work without extra shadow samples.");
            constexpr std::array colors{"Scattering Red", "Scattering Green", "Scattering Blue"};
            constexpr std::array winds{"East Wind (m/s)", "Up Wind (m/s)", "North Wind (m/s)"};
            for (std::size_t axis = 0; axis < settings.ScatteringAlbedo.size(); ++axis)
            {
                changed |= DrawCloudScalar(ui, colors[axis], settings.ScatteringAlbedo[axis], 0.01, 0.0, 1.0, commit);
                changed |= DrawCloudScalar(ui, winds[axis], settings.WindMetersPerSecond[axis], 0.1, -10000.0, 10000.0,
                                           commit);
            }
        }
        if (auto quality = ui.BeginSection("Quality Budget"); quality)
        {
            std::uint64_t viewSteps = settings.ViewSteps;
            ui.TextWrapped("View Samples");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragUnsignedInteger("##ViewSamples", viewSteps, 1.0, std::uint64_t{1}, std::uint64_t{128}))
            {
                settings.ViewSteps = static_cast<std::uint32_t>(viewSteps);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            std::uint64_t shadowSteps = settings.ShadowSteps;
            ui.TextWrapped("Shadow Samples");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragUnsignedInteger("##ShadowSamples", shadowSteps, 1.0, std::uint64_t{1}, std::uint64_t{16}))
            {
                settings.ShadowSteps = static_cast<std::uint32_t>(shadowSteps);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            changed |= DrawCloudScalar(ui, "Maximum Distance (m)", settings.MaximumDistanceMeters, 100.0, 1.0,
                                       1000000.0, commit);
            ui.TextColoredWrapped(
                theme.MutedText,
                "View and shadow samples multiply rendering cost. Each interval averages three density samples. "
                "Budgets are limits, not performance guarantees.");
        }
        return changed;
    }
} // namespace KeireEditor::Detail
