#include "KeireInternal/Environment/EnvironmentLightingEvaluation.h"
#include "KeireInternal/Environment/EnvironmentLightingSettingsStore.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace Keire::Detail;

    EnvironmentSkyProfile Sky()
    {
        EnvironmentSkyProfile result;
        result.Enabled = true;
        result.Horizon = EnvironmentColorTrack({{0.0, {2.0, 0.0, 0.0}}, {0.5, {0.0, 4.0, 0.0}}});
        result.Zenith = EnvironmentColorTrack({{0.0, {0.0, 0.0, 8.0}}});
        return result;
    }
} // namespace

TEST_CASE("Sky tracks evaluate independently of authored directional ownership and survive profile moves")
{
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Authored, {}, {}, Sky());
    const auto snapshot = EvaluateEnvironmentLighting(profile, {.DayFraction = 0.25}, Keire::AssetId(1, 2), 1);
    CHECK_FALSE(snapshot.Directional);
    REQUIRE(snapshot.SkyColors);
    CHECK(snapshot.SkyColors->Horizon == std::array<float, 3>{1.0F, 2.0F, 0.0F});
    CHECK(snapshot.SkyColors->Zenith == std::array<float, 3>{0.0F, 0.0F, 8.0F});
    auto copy = profile;
    const auto moved = std::move(copy);
    CHECK(moved == profile);
    CHECK_FALSE(copy.Sky());
    CHECK_FALSE(EvaluateEnvironmentLighting(copy, {}, Keire::AssetId(1, 2), 1).SkyColors);
    static_assert(std::is_nothrow_move_assignable_v<EnvironmentLightingProfile>);
}

TEST_CASE("Sky profile record preserves disabled tracks and legacy lighting preserves physical sky")
{
    auto sky = Sky();
    sky.Enabled = false;
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Authored, {}, {}, sky);
    const auto record = EnvironmentLightingRecord::Encode(profile);
    CHECK(record.at("version") == 4);
    const auto loaded = EnvironmentLightingRecord::Decode(record);
    CHECK(loaded == profile);
    CHECK_FALSE(EvaluateEnvironmentLighting(loaded, {}, Keire::AssetId(1, 2), 1).SkyColors);
    const auto legacy = EnvironmentLightingRecord::Encode(EnvironmentLightingProfile{});
    CHECK(legacy.at("version") == 2);
    CHECK_FALSE(EnvironmentLightingRecord::Decode(legacy).Sky());
    auto invalid = record;
    invalid["sky"]["weight"] = 2.0;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = record;
    invalid["sky"]["enabled"] = 1;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
}

TEST_CASE("Sky snapshot validation checks colors even when no directional light is submitted")
{
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Authored, {}, {}, Sky());
    auto snapshot = EvaluateEnvironmentLighting(profile, {}, Keire::AssetId(1, 2), 1);
    snapshot.SkyColors->Horizon[0] = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    snapshot.SkyColors->Horizon[0] = 1.0F;
    snapshot.SkyColors->GradientExponent = 0.0F;
    CHECK_THROWS_AS(ValidateEnvironmentLighting(snapshot), std::invalid_argument);
}

TEST_CASE("Maximum sky and lighting profiles fit the expanded bounded settings payload")
{
    auto sky = Sky();
    std::vector<EnvironmentColorKey> colorKeys;
    std::vector<EnvironmentLightingKeyframe> lightingKeys;
    for (std::size_t index = 0; index < EnvironmentColorTrack::MaximumKeys; ++index)
    {
        const double phase = static_cast<double>(index) / EnvironmentColorTrack::MaximumKeys;
        colorKeys.push_back({phase, {65504.0, 1234.1234567890123, 0.12345678901234}});
        lightingKeys.push_back({phase,
                                359.1234567890123,
                                -89.1234567890123,
                                {0.1234567890123, 0.2345678901234, 1.0},
                                std::numeric_limits<float>::max(),
                                Keire::EnvironmentAmbientLight{}});
    }
    sky.Horizon = EnvironmentColorTrack(colorKeys);
    sky.Zenith = EnvironmentColorTrack(std::move(colorKeys));
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, std::move(lightingKeys), {},
                                             sky);
    const auto text = EnvironmentLightingSettingsStore::Serialize(profile);
    CHECK(text.size() <= 256U * 1024U);
    CHECK(EnvironmentLightingRecord::Decode(nlohmann::json::parse(text).at("lighting")) == profile);
}
