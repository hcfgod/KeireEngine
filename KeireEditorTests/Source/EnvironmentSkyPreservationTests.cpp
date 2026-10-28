#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentLightingEditing.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/EnvironmentSkyEditing.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

TEST_CASE("Sky preview finds exact keys without merging distinct nearby times")
{
    using namespace KeireEditor::Detail;
    const auto track = DaylightSkyPreset().Horizon;
    CHECK(FindSkyColorKeyAtPhase(track, 0.0) == 0);
    CHECK(FindSkyColorKeyAtPhase(track, 0.75) == 3);
    CHECK_FALSE(FindSkyColorKeyAtPhase(track, 0.125));
    CHECK_FALSE(FindSkyColorKeyAtPhase(track, std::nextafter(0.25, 1.0)));
    CHECK_FALSE(FindSkyColorKeyAtPhase(track, 1.0));
    CHECK_FALSE(FindSkyColorKeyAtPhase(track, -1.0));
    CHECK_FALSE(FindSkyColorKeyAtPhase(track, std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("Sky timeline selects the nearest crowded key and rejects misses")
{
    using namespace KeireEditor::Detail;
    const Keire::Detail::EnvironmentColorTrack track({{0.0, {}}, {0.25, {}}, {0.26, {}}, {0.75, {}}});
    CHECK(PickSkyColorKey(track, 0.26, 400.0) == 2);
    CHECK(PickSkyColorKey(track, 0.251, 400.0) == 1);
    CHECK(PickSkyColorKey(track, 0.75, 10.0) == 3);
    CHECK(PickSkyColorKey(track, 0.0, 400.0) == 0);
    CHECK_FALSE(PickSkyColorKey(track, 0.5, 400.0));
    CHECK_FALSE(PickSkyColorKey(track, 0.25, 0.0));
    CHECK_FALSE(PickSkyColorKey(track, 0.25, -1.0));
    CHECK_FALSE(PickSkyColorKey(track, std::numeric_limits<double>::quiet_NaN(), 400.0));
    CHECK_FALSE(PickSkyColorKey(track, 0.25, std::numeric_limits<double>::infinity()));
}

TEST_CASE("Daily-light editing preserves independent sky tracks")
{
    using namespace KeireEditor::Detail;
    Keire::Detail::EnvironmentSkyProfile sky;
    sky.Enabled = true;
    sky.BlendWeight = 0.25;
    sky.Horizon = Keire::Detail::EnvironmentColorTrack({{0.0, {2.0, 3.0, 4.0}}});
    const auto profile = DaylightPreset({}, sky);
    CHECK(profile.Sky() == sky);
    CHECK(SetLightingOwnership(profile, false).Sky() == sky);
    CHECK(SetLightingOwnership(SetLightingOwnership(profile, false), true).Sky() == sky);
    CHECK(ResetLightingShadows(profile).Sky() == sky);
    CHECK(SetAmbientAnimation(profile, false, {}).Sky() == sky);
    CHECK(AddLightingKey(profile).Sky() == sky);
    CHECK(RemoveLightingKey(profile, 0).Sky() == sky);
    CHECK(InsertLightingKeyAtPhase(profile, 0.1).Sky() == sky);
    auto replacement = profile.Keyframes().front();
    replacement.Intensity = 0.5;
    CHECK(ReplaceLightingKey(profile, 0, replacement).Sky() == sky);
    CHECK(DaylightPreset(profile.Shadows(), profile.Sky()).Sky() == sky);
}

TEST_CASE("Sky-only preset and reset preserve directional keys ambient and shadows")
{
    using namespace KeireEditor::Detail;
    const auto original = DaylightPreset({.Strength = 0.25F});
    const auto edited = ReplaceSkyProfile(original, DaylightSkyPreset());
    CHECK(edited.Keyframes() == original.Keyframes());
    CHECK(edited.Shadows() == original.Shadows());
    CHECK(edited.Ownership() == original.Ownership());
    REQUIRE(edited.Sky());
    CHECK(ReplaceSkyProfile(edited, std::nullopt) == original);
}

TEST_CASE("Sky key insertion samples the current curve and constrained edits cannot merge adjacent keys")
{
    using namespace KeireEditor::Detail;
    const auto sky = DaylightSkyPreset();
    const auto expanded = InsertSkyColorKey(sky.Horizon, 0.125);
    CHECK(expanded.Keys().size() == sky.Horizon.Keys().size() + 1);
    CHECK(expanded.Keys()[1].LinearColor == sky.Horizon.Evaluate({.DayFraction = 0.125}));
    CHECK_THROWS_AS((void)InsertSkyColorKey(expanded, 0.125), std::invalid_argument);
    CHECK_THROWS_AS((void)InsertSkyColorKey(expanded, 1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)InsertSkyColorKey(expanded, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    CHECK(SkyColorKeyPhase(expanded, 1, 0.0) > expanded.Keys()[0].DayFraction);
    CHECK(SkyColorKeyPhase(expanded, 1, 24.0) < expanded.Keys()[2].DayFraction);
    CHECK(SkyColorKeyPhase(expanded, expanded.Keys().size() - 1, 24.0) < 1.0);
    CHECK_THROWS_AS((void)SkyColorKeyPhase(expanded, expanded.Keys().size(), 12.0), std::invalid_argument);
}

TEST_CASE("Sky-only edits and restoring a previous profile invalidate preview history without saving time")
{
    using namespace KeireEditor::Detail;
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind({.Day = 1234567890123456789LL, .DayFraction = 0.25});
    const auto authoredTime = preview.State();
    const auto original = DaylightPreset({});
    const Keire::AssetId source(1, 2);
    const auto first = preview.CaptureLighting(original, source);
    const auto edited = ReplaceSkyProfile(original, DaylightSkyPreset());
    const auto second = preview.CaptureLighting(edited, source);
    REQUIRE(second.SkyColors);
    CHECK(second.Revision > first.Revision);
    CHECK(preview.CaptureLighting(edited, source).Revision == second.Revision);
    const auto restored = preview.CaptureLighting(original, source);
    CHECK(restored.Revision > second.Revision);
    CHECK_FALSE(restored.SkyColors);
    CHECK(preview.State() == authoredTime);
}

TEST_CASE("Night fill preserves daylight and independent sky while enabling consistent ambient keys")
{
    using namespace KeireEditor::Detail;
    const auto source = DaylightPreset({}, DaylightSkyPreset());
    const Keire::EnvironmentAmbientLight fallback{{0.3F, 0.4F, 0.5F}, 0.6F};
    const auto filled = NightFillPreset(source, fallback);
    REQUIRE(filled.Keyframes().size() == source.Keyframes().size());
    CHECK(filled.Sky() == source.Sky());
    CHECK(filled.Ownership() == source.Ownership());
    for (std::size_t index = 0; index < source.Keyframes().size(); ++index)
    {
        const auto& before = source.Keyframes()[index];
        const auto& after = filled.Keyframes()[index];
        CHECK(after.DayFraction == before.DayFraction);
        CHECK(after.Intensity == before.Intensity);
        CHECK(after.ElevationDegrees == before.ElevationDegrees);
        CHECK(after.AzimuthDegrees == before.AzimuthDegrees);
        CHECK(after.LinearColor == before.LinearColor);
        REQUIRE(after.Ambient);
        if (before.ElevationDegrees >= 0.0)
        {
            CHECK(after.Ambient == before.Ambient);
        }
    }
    CHECK(filled.Keyframes().front().Ambient->Intensity == doctest::Approx(0.18F));
    const auto withoutAmbient = SetAmbientAnimation(source, false, fallback);
    const auto enabled = NightFillPreset(withoutAmbient, fallback);
    for (const auto& key : enabled.Keyframes())
    {
        REQUIRE(key.Ambient);
        if (key.ElevationDegrees >= 0.0)
        {
            CHECK(*key.Ambient == fallback);
        }
    }
    CHECK(source.Keyframes().front().Ambient->Intensity == doctest::Approx(0.08F));
}

TEST_CASE("Night fill is repeatable at twilight and preserves the source profile")
{
    using namespace KeireEditor::Detail;
    const auto source = DaylightPreset({});
    auto twilightKey = source.Keyframes()[1];
    twilightKey.ElevationDegrees = -6.0;
    const auto twilight = ReplaceLightingKey(source, 1, twilightKey);
    const Keire::EnvironmentAmbientLight fallback{{0.3F, 0.4F, 0.5F}, 0.6F};
    const auto filled = NightFillPreset(twilight, fallback);
    CHECK(NightFillPreset(filled, fallback) == filled);
    CHECK(filled.Keyframes()[1].Ambient->Intensity == doctest::Approx(0.39F));
    CHECK(twilight.Keyframes()[1] == twilightKey);
    CHECK(filled.Shadows() == twilight.Shadows());
}
