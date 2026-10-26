#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentLightingEditing.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/EnvironmentTimeline.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

TEST_CASE("Preview key lookup distinguishes existing keys from nearby insertion times")
{
    using namespace KeireEditor::Detail;
    const auto profile = DaylightPreset({});
    for (std::size_t index = 0; index < profile.Keyframes().size(); ++index)
    {
        const double phase = profile.Keyframes()[index].DayFraction;
        CHECK(LightingKeyAtPhase(profile, phase) == index);
        CHECK_FALSE(LightingKeyAtPhase(profile, std::nextafter(phase, 1.0)));
    }
    CHECK_FALSE(LightingKeyAtPhase(LightingProfile{}, 0.0));
    CHECK_FALSE(LightingKeyAtPhase(profile, -1.0));
    CHECK_FALSE(LightingKeyAtPhase(profile, 1.0));
    CHECK_FALSE(LightingKeyAtPhase(profile, std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("Preview insertion handles polar lights and capacity without mutating rejected profiles")
{
    using namespace KeireEditor::Detail;
    for (const double elevation : {-90.0, -89.999999, 0.0, 89.999999, 90.0})
    {
        const LightingProfile original(LightingOwnership::Environment, {{0.0, 350.0, elevation, {1.0, 0.5, 0.2}, 2.0},
                                                                        {0.5, 170.0, elevation, {0.2, 0.5, 1.0}, 4.0}});
        for (const double phase : {0.125, 0.75})
        {
            const auto inserted = InsertLightingKeyAtPhase(original, phase);
            for (int step = 0; step < 64; ++step)
            {
                const Keire::Detail::EnvironmentClockState clock{0, static_cast<double>(step) / 64.0, 86400.0};
                const auto before = original.Evaluate(clock);
                const auto after = inserted.Evaluate(clock);
                REQUIRE(before.has_value());
                REQUIRE(after.has_value());
                CHECK_FALSE(after->Ambient.has_value());
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    CHECK(std::abs(after->Direction[channel] - before->Direction[channel]) < 1e-7);
                }
            }
        }
    }
    std::vector<Keire::Detail::EnvironmentLightingKeyframe> keys;
    for (std::size_t index = 0; index < LightingProfile::MaximumKeyframes; ++index)
    {
        auto key = Keire::Detail::EnvironmentLightingKeyframe{};
        key.DayFraction = static_cast<double>(index) / LightingProfile::MaximumKeyframes;
        keys.push_back(key);
    }
    const LightingProfile full(LightingOwnership::Environment, keys);
    CHECK_THROWS_AS((void)InsertLightingKeyAtPhase(full, 0.001), std::invalid_argument);
    CHECK(full.Keyframes() == keys);
    CHECK_THROWS_AS((void)InsertLightingKeyAtPhase(full, std::numeric_limits<double>::quiet_NaN()),
                    std::invalid_argument);
}

TEST_CASE("Insert preview lighting key preserves the evaluated daily cycle")
{
    using namespace KeireEditor::Detail;
    const auto original = DaylightPreset({});
    for (const double insertionPhase : {0.125, 0.375, 0.9})
    {
        const auto inserted = InsertLightingKeyAtPhase(original, insertionPhase);
        CHECK(inserted.Keyframes().size() == original.Keyframes().size() + 1);
        CHECK(inserted.Shadows() == original.Shadows());
        for (int minute = 0; minute < 1440; minute += 7)
        {
            const Keire::Detail::EnvironmentClockState clock{0, static_cast<double>(minute) / 1440.0, 86400.0};
            const auto before = original.Evaluate(clock);
            const auto after = inserted.Evaluate(clock);
            REQUIRE(before.has_value());
            REQUIRE(after.has_value());
            CHECK(after->Intensity == doctest::Approx(before->Intensity));
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                CHECK(after->Direction[channel] == doctest::Approx(before->Direction[channel]).epsilon(1e-6));
                CHECK(after->LinearColor[channel] == doctest::Approx(before->LinearColor[channel]));
                CHECK(after->Ambient->LinearColor[channel] ==
                      doctest::Approx(before->Ambient->LinearColor[channel]).epsilon(1e-6));
            }
            CHECK(after->Ambient->Intensity == doctest::Approx(before->Ambient->Intensity).epsilon(1e-6));
        }
    }
    CHECK_THROWS_AS((void)InsertLightingKeyAtPhase(original, 0.5), std::invalid_argument);
    CHECK_THROWS_AS((void)InsertLightingKeyAtPhase(original, 1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)InsertLightingKeyAtPhase(LightingProfile{}, 0.3), std::invalid_argument);
    CHECK(original.Keyframes().size() == 8);
}

TEST_CASE("Timeline intensity knots preserve narrow peaks and the cyclic midnight seam")
{
    using namespace KeireEditor::Detail;
    const LightingProfile profile(LightingOwnership::Environment, {{0.4, 0.0, 0.0, {1.0, 1.0, 1.0}, 0.0},
                                                                   {0.40001, 0.0, 0.0, {1.0, 1.0, 1.0}, 10.0},
                                                                   {0.40002, 0.0, 0.0, {1.0, 1.0, 1.0}, 0.0}});
    const auto knots = TimelineIntensityKnots(profile.Keyframes());
    REQUIRE(knots.size() == 5);
    CHECK(knots.front() == 0.0);
    CHECK(knots.back() == 1.0);
    CHECK(knots[2] == 0.40001);
    REQUIRE(profile.Evaluate({0, knots[2], 86400.0}).has_value());
    CHECK(profile.Evaluate({0, knots[2], 86400.0})->Intensity == 10.0);
    const auto preset = DaylightPreset({});
    const auto presetKnots = TimelineIntensityKnots(preset.Keyframes());
    CHECK(presetKnots.size() == preset.Keyframes().size() + 1);
    CHECK(std::adjacent_find(presetKnots.begin(), presetKnots.end()) == presetKnots.end());
    CHECK(TimelineIntensityKnots({}).size() == 2);
}

TEST_CASE("Lighting key hour edits preserve strict neighbor ordering and fractional minutes")
{
    using namespace KeireEditor::Detail;
    const auto profile = DaylightPreset({});
    const auto& keys = profile.Keyframes();
    CHECK(LightingKeyPhaseFromHours(profile, 4, 12.5) == doctest::Approx(12.5 / 24.0));
    CHECK(LightingKeyPhaseFromHours(profile, 0, -24.0) == 0.0);
    CHECK(LightingKeyPhaseFromHours(profile, keys.size() - 1, 48.0) == std::nextafter(1.0, 0.0));
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        for (const double hours : {-100.0, 0.0, 6.0, 12.5, 24.0, 100.0})
        {
            auto replacement = keys[index];
            replacement.DayFraction = LightingKeyPhaseFromHours(profile, index, hours);
            CHECK_NOTHROW((void)ReplaceLightingKey(profile, index, replacement));
        }
    }
    CHECK_THROWS_AS((void)LightingKeyPhaseFromHours(profile, keys.size(), 12.0), std::out_of_range);
    CHECK_THROWS_AS((void)LightingKeyPhaseFromHours(profile, 0, std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
}

TEST_CASE("Environment timeline scrubbing clamps endpoints and snaps to exact key phases")
{
    using KeireEditor::Detail::TimelinePhase;
    const auto profile = KeireEditor::Detail::DaylightPreset({});
    const auto& keys = profile.Keyframes();
    CHECK(TimelinePhase(-100.0, 10.0, 1000.0, keys, false) == 0.0);
    CHECK(TimelinePhase(2000.0, 10.0, 1000.0, keys, false) == std::nextafter(1.0, 0.0));
    for (const auto& key : keys)
    {
        const double x = 10.0 + key.DayFraction * 1000.0 + 2.0;
        CHECK(TimelinePhase(x, 10.0, 1000.0, keys, true) == key.DayFraction);
        CHECK(TimelinePhase(x, 10.0, 1000.0, keys, false) == doctest::Approx(key.DayFraction + 0.002));
    }
    CHECK(TimelinePhase(500.0, 10.0, 0.0, keys, true) == 0.0);
    CHECK(TimelinePhase(std::numeric_limits<double>::quiet_NaN(), 10.0, 1000.0, keys, true) == 0.0);
    CHECK(TimelinePhase(510.0, 10.0, 1000.0, {}, true) == 0.5);
}

TEST_CASE("Environment timeline key selection uses nearest pixel hit and rejects empty space")
{
    using KeireEditor::Detail::TimelineKeyAtPhase;
    const auto profile = KeireEditor::Detail::DaylightPreset({});
    const auto& keys = profile.Keyframes();
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        CHECK(TimelineKeyAtPhase(keys[index].DayFraction + 0.001, 1000.0, keys) == index);
    }
    CHECK_FALSE(TimelineKeyAtPhase(0.1, 1000.0, keys));
    CHECK_FALSE(TimelineKeyAtPhase(0.0, 0.0, keys));
    CHECK_FALSE(TimelineKeyAtPhase(0.0, 1000.0, {}));
    CHECK_FALSE(TimelineKeyAtPhase(std::numeric_limits<double>::quiet_NaN(), 1000.0, keys));
}

TEST_CASE("Daylight authoring preset produces editable cyclic lighting with retained shadows")
{
    const Keire::EnvironmentShadowSettings shadows{Keire::ShadowQuality::Soft, Keire::ShadowResolutionHint::High, 0.7F,
                                                   0.01F};
    const auto profile = KeireEditor::Detail::DaylightPreset(shadows);
    CHECK(profile.Shadows() == shadows);
    REQUIRE(profile.Keyframes().size() == 8);
    CHECK(profile.Evaluate({.DayFraction = 0.0})->Intensity == 0.0);
    CHECK(profile.Evaluate({.DayFraction = 0.5})->Intensity == 5.0);
    for (int minute = 0; minute < 1440; ++minute)
    {
        const auto sample = profile.Evaluate({.DayFraction = minute / 1440.0});
        REQUIRE(sample);
        CHECK(std::isfinite(sample->Intensity));
        CHECK(sample->Intensity >= 0.0);
        CHECK(sample->Intensity <= 5.0);
    }
}

TEST_CASE("Environment preview clock labels use bounded 24 hour time")
{
    using KeireEditor::Detail::EnvironmentClockLabel;
    CHECK(EnvironmentClockLabel(0.0) == "00:00");
    CHECK(EnvironmentClockLabel(0.25) == "06:00");
    CHECK(EnvironmentClockLabel(0.5) == "12:00");
    CHECK(EnvironmentClockLabel(0.75) == "18:00");
    CHECK(EnvironmentClockLabel(std::nextafter(1.0, 0.0)) == "23:59");
    for (int minute = 0; minute < 1440; ++minute)
    {
        const int hour = minute / 60;
        const int remainder = minute % 60;
        const std::string expected = std::string(hour < 10 ? "0" : "") + std::to_string(hour) + ":" +
                                     (remainder < 10 ? "0" : "") + std::to_string(remainder);
        CHECK(EnvironmentClockLabel((static_cast<double>(minute) / 60.0) / 24.0) == expected);
    }
    CHECK(EnvironmentClockLabel(59.99 / 1440.0) == "00:59");
    CHECK_THROWS_AS((void)EnvironmentClockLabel(1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)EnvironmentClockLabel(-0.01), std::invalid_argument);
    CHECK_THROWS_AS((void)EnvironmentClockLabel(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

TEST_CASE("Key-time preview pauses on the current day without changing authored playback")
{
    const Keire::Detail::EnvironmentClockState authored{42, 0.25, 2400.0, -0.5, true};
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind(authored);
    preview.Seek(1234567890123456789LL, 0.125);
    preview.SetSpeed(4.0);
    preview.SetPlaying(true);

    const auto keyTime = 0.67;
    preview.Seek(preview.State().Day, keyTime);
    preview.SetPlaying(false);
    const auto selected = preview.State();
    CHECK(selected.Day == 1234567890123456789LL);
    CHECK(selected.DayFraction == keyTime);
    CHECK_FALSE(preview.Playing());
    preview.Tick(30.0, true, true);
    CHECK(preview.State() == selected);

    const auto candidate = preview.ApplyTimeCandidate();
    CHECK(candidate.Rate == authored.Rate);
    CHECK(candidate.Paused == authored.Paused);
    CHECK(candidate.SecondsPerDay == authored.SecondsPerDay);
    preview.Reset();
    CHECK(preview.ApplyTimeCandidate() == authored);
    CHECK_FALSE(preview.Playing());
}

TEST_CASE("Shadow-only preview edits invalidate lighting history without resetting playback")
{
    KeireEditor::EnvironmentPreviewClock preview;
    const Keire::AssetId source(11, 22);
    const Keire::Detail::EnvironmentLightingProfile baseline(
        Keire::Detail::EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 60.0, {1.0, 0.9, 0.8}, 12.0}});
    preview.Bind({0, 0.25, 100.0});
    preview.SetPlaying(true);
    auto previous = preview.CaptureLighting(baseline, source);
    auto shadows = baseline.Shadows();
    for (int edit = 0; edit < 4; ++edit)
    {
        switch (edit)
        {
        case 0:
            shadows.Quality = Keire::ShadowQuality::Soft;
            break;
        case 1:
            shadows.Resolution = Keire::ShadowResolutionHint::Low;
            break;
        case 2:
            shadows.Strength = 0.5F;
            break;
        default:
            shadows.Bias = 0.02F;
            break;
        }
        const Keire::Detail::EnvironmentLightingProfile changed(baseline.Ownership(), baseline.Keyframes(), shadows);
        const auto before = preview.State();
        const auto snapshot = preview.CaptureLighting(changed, source);
        REQUIRE(snapshot.Directional);
        CHECK(snapshot.Directional->Shadows == shadows);
        CHECK(snapshot.Revision == previous.Revision + 1);
        CHECK(snapshot.Source == source);
        CHECK(preview.State() == before);
        CHECK(preview.Playing());
        CHECK(preview.CaptureLighting(changed, source).Revision == snapshot.Revision);
        preview.Tick(1.0, true, true);
        CHECK(preview.CaptureLighting(changed, source).Revision == snapshot.Revision);
        previous = snapshot;
    }
    const auto restored = preview.CaptureLighting(baseline, source);
    CHECK(restored.Revision == previous.Revision + 1);
    CHECK(restored.Directional->Shadows == baseline.Shadows());
    CHECK(preview.Playing());
}

TEST_CASE("Preview lighting preserves playback while invalidating profile seek and lifetime changes")
{
    KeireEditor::EnvironmentPreviewClock preview;
    const Keire::AssetId source(1, 2);
    const Keire::Detail::EnvironmentLightingProfile authored;
    const Keire::Detail::EnvironmentLightingProfile automatic(
        Keire::Detail::EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -80.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 80.0, {1.0, 0.9, 0.8}, 12.0}});
    preview.Bind({0, 0.0, 100.0});
    const auto first = preview.CaptureLighting(automatic, source);
    REQUIRE(first.Directional);
    preview.SetPlaying(true);
    preview.Tick(10.0, true, true);
    const auto advanced = preview.CaptureLighting(automatic, source);
    CHECK(advanced.Source == first.Source);
    CHECK(advanced.Revision == first.Revision);
    CHECK(advanced.Directional->Intensity != first.Directional->Intensity);
    CHECK(preview.Playing());
    const auto phase = preview.State();
    const auto changed = preview.CaptureLighting(authored, source);
    CHECK_FALSE(changed.Directional);
    CHECK(changed.Revision == first.Revision + 1);
    CHECK(preview.State() == phase);
    CHECK(preview.Playing());
    CHECK(preview.CaptureLighting(authored, source).Revision == changed.Revision);
    CHECK_THROWS_AS((void)preview.CaptureLighting(automatic, {}), std::invalid_argument);
    CHECK(preview.CaptureLighting(authored, source).Revision == changed.Revision);
    preview.Seek(10, 0.75);
    CHECK(preview.CaptureLighting(authored, source).Revision == changed.Revision + 1);
    preview.Unbind();
    CHECK_FALSE(preview.LightingSource());
    CHECK_THROWS_AS((void)preview.CaptureLighting(automatic, source), std::logic_error);
    preview.Bind({});
    const auto reopened = preview.CaptureLighting(automatic, Keire::AssetId(3, 4));
    CHECK(reopened.Source != first.Source);
    CHECK(reopened.Revision == 1);
}

TEST_CASE("Environment preview synchronizes changed authoring without restarting unchanged playback")
{
    KeireEditor::EnvironmentPreviewClock preview;
    const Keire::Detail::EnvironmentClockState authored{42, 0.25, 100.0, 3.0, true};
    preview.SynchronizeAuthored(authored);
    preview.SetSpeed(2.0);
    preview.SetPlaying(true);
    preview.Tick(10.0, true, true);
    const auto playingState = preview.State();
    const auto revision = preview.Revision();
    preview.SynchronizeAuthored(authored);
    CHECK(preview.State() == playingState);
    CHECK(preview.Revision() == revision);
    CHECK(preview.Playing());

    auto changed = authored;
    changed.Day = -7;
    changed.DayFraction = 0.75;
    preview.SynchronizeAuthored(changed);
    CHECK_FALSE(preview.Playing());
    CHECK(preview.ApplyTimeCandidate() == changed);
    CHECK(preview.Revision() == revision + 1);
    preview.SynchronizeAuthored(authored);
    CHECK(preview.ApplyTimeCandidate() == authored);
    const auto restored = preview.State();
    changed.SecondsPerDay = 0.0;
    CHECK_THROWS_AS(preview.SynchronizeAuthored(changed), std::invalid_argument);
    CHECK(preview.State() == restored);
    CHECK(preview.ApplyTimeCandidate() == authored);
    preview.Unbind();
    preview.SynchronizeAuthored(authored);
    CHECK(preview.Bound());
    CHECK_FALSE(preview.Playing());
}

TEST_CASE("Environment edit preview isolates authored and runtime clocks")
{
    const Keire::Detail::EnvironmentClockState authored{42, 0.25, 100.0, 3.0, true};
    Keire::Detail::EnvironmentClock runtime(authored);
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind(authored);
    CHECK_FALSE(preview.Playing());
    CHECK(preview.State().DayFraction == 0.25);
    preview.Tick(10.0, true, true);
    CHECK(preview.State().DayFraction == 0.25);
    preview.SetSpeed(2.0);
    preview.SetPlaying(true);
    preview.Tick(10.0, true, true);
    CHECK(preview.State().DayFraction == doctest::Approx(0.45));
    CHECK(runtime.State() == authored);
    const auto candidate = preview.ApplyTimeCandidate();
    CHECK(candidate.Day == 42);
    CHECK(candidate.DayFraction == doctest::Approx(0.45));
    CHECK(candidate.Rate == authored.Rate);
    CHECK(candidate.Paused == authored.Paused);
    CHECK(candidate.SecondsPerDay == authored.SecondsPerDay);
    preview.Reset();
    CHECK_FALSE(preview.Playing());
    CHECK(preview.ApplyTimeCandidate() == authored);
}

TEST_CASE("Environment edit preview freezes hidden and play-mode frames without catchup")
{
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind({0, 0.5, 100.0});
    preview.SetPlaying(true);
    preview.Tick(1000.0, false, true);
    preview.Tick(1000.0, true, false);
    CHECK(preview.State().DayFraction == 0.5);
    preview.Tick(10.0, true, true);
    CHECK(preview.State().DayFraction == doctest::Approx(0.6));
    preview.SetPlaying(false);
    preview.Tick(1000.0, true, true);
    CHECK(preview.State().DayFraction == doctest::Approx(0.6));
    const auto revision = preview.Revision();
    preview.Seek(-7, 0.0);
    CHECK(preview.Revision() == revision + 1);
    preview.SetSpeed(-1.0);
    preview.SetPlaying(true);
    preview.Tick(25.0, true, true);
    CHECK(preview.State().Day == -8);
    CHECK(preview.State().DayFraction == 0.75);
    CHECK(preview.Revision() == revision + 1);
}

TEST_CASE("Environment edit preview validates lifecycle and preserves rejected state")
{
    KeireEditor::EnvironmentPreviewClock preview;
    CHECK_FALSE(preview.Bound());
    CHECK_THROWS_AS(preview.SetPlaying(true), std::logic_error);
    preview.Bind({std::numeric_limits<std::int64_t>::max(), 0.75, 1.0});
    preview.SetPlaying(true);
    const auto before = preview.State();
    const auto revision = preview.Revision();
    CHECK_THROWS_AS(preview.Tick(1.0, true, true), std::overflow_error);
    CHECK_THROWS_AS(preview.SetSpeed(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    CHECK_THROWS_AS(preview.Seek(0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(preview.Bind({0, 0.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(preview.Tick(-1.0, false, false), std::invalid_argument);
    CHECK(preview.State() == before);
    CHECK(preview.Revision() == revision);
    CHECK(preview.Playing());
    std::atomic<int> rejected = 0;
    std::thread worker(
        [&]
        {
            try
            {
                preview.Unbind();
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
            try
            {
                (void)preview.State();
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
        });
    worker.join();
    CHECK(rejected == 2);
    CHECK(preview.State() == before);
    preview.Unbind();
    CHECK_FALSE(preview.Bound());
    CHECK_THROWS_AS((void)preview.State(), std::logic_error);
    CHECK_THROWS_AS((void)preview.ApplyTimeCandidate(), std::logic_error);
    preview.Bind({1, 0.125, 10.0});
    CHECK(preview.State().Day == 1);
    CHECK(preview.State().DayFraction == 0.125);
    CHECK(preview.Revision() > revision);
    CHECK_FALSE(preview.Playing());
}
