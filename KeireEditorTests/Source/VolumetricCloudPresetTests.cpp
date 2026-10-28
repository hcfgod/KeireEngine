#include "KeireClientInternal/Editor/VolumetricCloudPresets.h"
#include "KeireInternal/Environment/VolumetricCloudValidation.h"

#include <doctest/doctest.h>

#include <initializer_list>

TEST_CASE("Cloud shape starting points preserve scene placement lighting and quality")
{
    Keire::VolumetricCloudSettings authored;
    authored.Enabled = true;
    authored.BaseHeightMeters = 2300.0;
    authored.ThicknessMeters = 1900.0;
    authored.PatternSizeMeters = 4200.0;
    authored.Seed = 97;
    authored.WindMetersPerSecond = {12.0, 1.0, -7.0};
    authored.ScatteringAlbedo = {0.6, 0.7, 0.8};
    authored.Anisotropy = 0.4;
    authored.AmbientOcclusionStrength = 0.65;
    authored.MultipleScatteringContribution = 0.25;
    authored.MultipleScatteringOcclusion = 0.75;
    authored.ViewSteps = 96;
    authored.ShadowSteps = 12;
    authored.MaximumDistanceMeters = 80000.0;

    for (const auto style : KeireEditor::Detail::CloudShapeStyles)
    {
        const auto result = KeireEditor::Detail::CloudShapePreset(authored, style);
        CHECK_NOTHROW(Keire::Detail::ValidateVolumetricCloudSettings(result));
        CHECK(result.Enabled == authored.Enabled);
        CHECK(result.BaseHeightMeters == authored.BaseHeightMeters);
        CHECK(result.ThicknessMeters == authored.ThicknessMeters);
        CHECK(result.PatternSizeMeters == authored.PatternSizeMeters);
        CHECK(result.Seed == authored.Seed);
        CHECK(result.WindMetersPerSecond == authored.WindMetersPerSecond);
        CHECK(result.ScatteringAlbedo == authored.ScatteringAlbedo);
        CHECK(result.Anisotropy == authored.Anisotropy);
        CHECK(result.AmbientOcclusionStrength == authored.AmbientOcclusionStrength);
        CHECK(result.MultipleScatteringContribution == authored.MultipleScatteringContribution);
        CHECK(result.MultipleScatteringOcclusion == authored.MultipleScatteringOcclusion);
        CHECK(result.ViewSteps == authored.ViewSteps);
        CHECK(result.ShadowSteps == authored.ShadowSteps);
        CHECK(result.MaximumDistanceMeters == authored.MaximumDistanceMeters);
        CHECK(KeireEditor::Detail::CloudShapePreset(result, style) == result);
    }
}

TEST_CASE("Cloud shape presets can restore soft defaults after defined billows")
{
    const Keire::VolumetricCloudSettings defaults;
    const auto defined =
        KeireEditor::Detail::CloudShapePreset(defaults, KeireEditor::Detail::CloudShapeStyle::DefinedBillows);
    CHECK(defined.ExtinctionPerMeter > defaults.ExtinctionPerMeter);
    CHECK(defined.ErosionAmount > defaults.ErosionAmount);
    CHECK(defined.RegionalCoverageStrength > defaults.RegionalCoverageStrength);
    CHECK(KeireEditor::Detail::CloudShapePreset(defined, KeireEditor::Detail::CloudShapeStyle::SoftBodies) == defaults);
    CHECK_FALSE(defined.Enabled);
}

TEST_CASE("Dense billows increases optical thickness without changing the defined shape field")
{
    using namespace KeireEditor::Detail;
    const auto defined = CloudShapePreset({}, CloudShapeStyle::DefinedBillows);
    auto dense = CloudShapePreset({}, CloudShapeStyle::DenseBillows);
    CHECK(dense.ExtinctionPerMeter == doctest::Approx(0.04));
    CHECK(CloudShapePresetLabel(dense) == "Dense billows");
    dense.ExtinctionPerMeter = defined.ExtinctionPerMeter;
    CHECK(dense == defined);
    CHECK(CloudShapePresetLabel(dense) == "Defined billows");
}

TEST_CASE("Cloud preset labels reflect edited shape without treating lighting or placement as customization")
{
    using namespace KeireEditor::Detail;
    Keire::VolumetricCloudSettings settings;
    CHECK(CloudShapePresetLabel(settings) == "Soft bodies");
    settings = CloudShapePreset(settings, CloudShapeStyle::DefinedBillows);
    CHECK(CloudShapePresetLabel(settings) == "Defined billows");
    settings.BaseHeightMeters = 4000.0;
    settings.Seed = 42;
    settings.WindMetersPerSecond = {20.0, 0.0, 10.0};
    settings.Anisotropy = 0.1;
    settings.AmbientOcclusionStrength = 0.8;
    settings.ViewSteps = 96;
    CHECK(CloudShapePresetLabel(settings) == "Defined billows");
    settings.ExtinctionPerMeter *= 1.1;
    CHECK(CloudShapePresetLabel(settings) == "Custom");
    settings = CloudShapePreset(settings, CloudShapeStyle::SoftBodies);
    CHECK(CloudShapePresetLabel(settings) == "Soft bodies");
    settings.ErosionAmount = 0.9;
    CHECK(CloudShapePresetLabel(settings) == "Custom");
}
