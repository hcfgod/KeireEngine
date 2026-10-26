#include "KeireInternal/Environment/BloomRecord.h"

#include <doctest/doctest.h>

TEST_CASE("Bloom records preserve enabled and disabled authored values")
{
    Keire::BloomSettings settings;
    settings.Intensity = 2.0;
    settings.Threshold = 4.0;
    settings.SoftKnee = 0.25;
    settings.Radius = 3.5;
    for (const bool enabled : {false, true})
    {
        settings.Enabled = enabled;
        CHECK(Keire::Detail::DecodeBloomSettings(Keire::Detail::EncodeBloomSettings(settings)) == settings);
    }
}

TEST_CASE("Bloom records reject future versions wrong types and disabled invalid parameters")
{
    const auto baseline = Keire::Detail::EncodeBloomSettings({});
    for (const auto* name : {"version", "enabled", "intensity", "threshold", "softKnee", "radius"})
    {
        auto missing = baseline;
        missing.erase(name);
        CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(missing), std::invalid_argument);
        auto wrongType = baseline;
        wrongType[name] = "invalid";
        CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(wrongType), std::invalid_argument);
    }
    auto changed = baseline;
    changed["version"] = 2;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(changed), std::invalid_argument);
    changed = baseline;
    changed["unknown"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(changed), std::invalid_argument);
    changed = baseline;
    changed["radius"] = 0.0;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(changed), std::invalid_argument);
    changed = baseline;
    changed["intensity"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeBloomSettings(changed), std::invalid_argument);
}
