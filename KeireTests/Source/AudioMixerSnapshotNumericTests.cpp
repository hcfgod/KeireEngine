#include "Keire/Audio/AudioAssets.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

TEST_CASE("Audio mixer snapshot interpolation preserves finite extreme effect parameters")
{
    constexpr Keire::AssetId effect(1, 2);
    constexpr Keire::AssetId snapshot(1, 3);
    constexpr float maximum = std::numeric_limits<float>::max();
    auto definition = Keire::AudioMixerAsset::DefaultDefinition();
    definition.Buses.front().Effects.push_back(
        {.Id = effect, .Name = "Finite parameter", .Type = Keire::AudioGraphNodeType::Gain, .Parameters = {-maximum}});
    definition.Snapshots.push_back(
        {.Id = snapshot,
         .Name = "Finite target",
         .Parameters = {
             {.Type = Keire::AudioMixerSnapshotParameterType::EffectParameter, .Target = effect, .Value = maximum}}});
    REQUIRE_NOTHROW(Keire::ValidateAudioMixer(definition));

    for (const float direction : {-1.0F, 1.0F})
    {
        definition.Buses.front().Effects.front().Parameters.front() = -direction * maximum;
        definition.Snapshots.front().Parameters.front().Value = direction * maximum;
        for (const float weight : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F})
        {
            CAPTURE(direction);
            CAPTURE(weight);
            const auto result = Keire::BlendAudioMixerSnapshot(definition, snapshot, weight);
            const float value = result.Buses.front().Effects.front().Parameters.front();
            CHECK(std::isfinite(value));
            const float expected = direction * maximum * (2.0F * weight - 1.0F);
            CHECK(value == doctest::Approx(expected));
        }
    }

    CHECK(definition.Buses.front().Effects.front().Parameters.front() == -maximum);
    CHECK_THROWS_AS((void)Keire::BlendAudioMixerSnapshot(definition, snapshot, -0.1F), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::BlendAudioMixerSnapshot(definition, snapshot, 1.1F), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::BlendAudioMixerSnapshot(definition, snapshot, std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
}
