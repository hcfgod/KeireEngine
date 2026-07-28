#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] Keire::AssetId Id(const std::uint64_t value) { return Keire::AssetId(0x415544494f434f4dULL, value); }
} // namespace

TEST_CASE("Audio Source schema two preserves stable mixer routing and legacy string APIs")
{
    const auto registration = Keire::CreateAudioSourceComponentRegistration();
    CHECK(registration.SchemaVersion == 2);
    REQUIRE(registration.Migrate);

    const auto source = registration.Factory();
    auto& authored = dynamic_cast<Keire::AudioSourceComponent&>(*source);
    authored.SetClip(Id(1));
    authored.SetMixer(Id(2));
    authored.SetBusId(Id(3));
    authored.SetBus("Gameplay SFX");
    authored.SetGain(0.75F);
    authored.SetPitch(1.25F);
    authored.SetPriority(42);
    authored.SetMinimumDistance(2.0F);
    authored.SetMaximumDistance(80.0F);
    authored.SetAttenuation(Keire::Curve1D::Linear(1.0F, 0.25F));
    authored.SetLoop(true);
    authored.SetSpatial(true);
    authored.SetPlayOnAwake(false);

    const auto restoredComponent = registration.Factory();
    registration.Deserialize(*restoredComponent, registration.Serialize(*source), registration.SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::AudioSourceComponent&>(*restoredComponent);
    CHECK(restored.Clip() == Id(1));
    CHECK(restored.Mixer() == Id(2));
    CHECK(restored.BusId() == Id(3));
    CHECK(restored.Bus() == "Gameplay SFX");
    CHECK(restored.MinimumDistance() == doctest::Approx(2.0F));
    CHECK(restored.MaximumDistance() == doctest::Approx(80.0F));
    CHECK(restored.Attenuation() == Keire::Curve1D::Linear(1.0F, 0.25F));

    const auto request = restored.PlaybackRequest({}, {1.0F, 2.0F, 3.0F});
    CHECK(request.Mixer == Id(2));
    CHECK(request.BusId == Id(3));
    CHECK(request.Bus == "Gameplay SFX");
    CHECK(request.Position == Keire::Vector3{1.0F, 2.0F, 3.0F});
    CHECK(request.Attenuation == restored.Attenuation());

    authored.SetBus("Renamed fallback");
    CHECK(authored.BusId() == Id(3));
}

TEST_CASE("Audio Source schema one migration preserves legacy bus and distance behavior")
{
    const auto registration = Keire::CreateAudioSourceComponentRegistration();
    REQUIRE(registration.Migrate);
    const Keire::ComponentPropertyBag legacy{
        {"clip", Id(10)},
        {"bus", std::string("Legacy SFX")},
        {"gain", 0.5},
        {"pitch", 1.0},
        {"priority", std::int64_t{64}},
        {"minimumDistance", 3.0},
        {"maximumDistance", 60.0},
        {"loop", false},
        {"spatial", true},
        {"playOnAwake", true},
    };
    const auto migrated = registration.Migrate(legacy, 1);
    CHECK_FALSE(std::get<Keire::AssetId>(migrated.at("mixer")));
    CHECK(std::get<std::string>(migrated.at("busId")).empty());
    CHECK(std::get<Keire::Curve1D>(migrated.at("attenuation")) == Keire::Curve1D::Constant(1.0F));

    const auto component = registration.Factory();
    registration.Deserialize(*component, migrated, registration.SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::AudioSourceComponent&>(*component);
    CHECK(restored.Bus() == "Legacy SFX");
    CHECK(restored.MinimumDistance() == doctest::Approx(3.0F));
    CHECK(restored.MaximumDistance() == doctest::Approx(60.0F));
    CHECK(restored.Attenuation().Evaluate(0.75F) == doctest::Approx(1.0F));
    CHECK_THROWS_AS(registration.Migrate(legacy, 0), std::invalid_argument);
}

TEST_CASE("Audio Source state commits transactionally after complete validation")
{
    Keire::AudioSourceComponent source;
    auto state = source.CaptureState();
    state.Clip = Id(11);
    state.Mixer = Id(12);
    state.BusId = Id(13);
    state.Bus = "Managed SFX";
    state.Gain = 0.6F;
    state.Pitch = 1.2F;
    state.Priority = 24;
    state.MinimumDistance = 4.0F;
    state.MaximumDistance = 90.0F;
    state.Attenuation = Keire::Curve1D::Linear(1.0F, 0.1F);
    state.Loop = true;
    state.Spatial = false;
    state.PlayOnAwake = true;
    source.ApplyState(state);
    CHECK(source.CaptureState() == state);

    const auto committed = source.CaptureState();
    auto rejected = committed;
    rejected.Bus = "Rejected";
    rejected.Gain = 0.25F;
    rejected.MinimumDistance = 100.0F;
    rejected.MaximumDistance = 10.0F;
    CHECK_THROWS_AS(source.ApplyState(std::move(rejected)), std::invalid_argument);
    CHECK(source.CaptureState() == committed);
}

TEST_CASE("Audio Reverb Zone round trips bounded box and sphere authoring")
{
    const auto registration = Keire::CreateAudioReverbZoneComponentRegistration();
    CHECK(registration.SchemaVersion == 1);
    const auto source = registration.Factory();
    auto& zone = dynamic_cast<Keire::AudioReverbZoneComponent&>(*source);
    zone.SetMixer(Id(20));
    zone.SetSnapshotId(Id(21));
    zone.SetShape(Keire::AudioReverbZoneShape::Sphere);
    zone.SetBoxHalfExtent({2.0F, 3.0F, 4.0F});
    zone.SetSphereRadius(12.0F);
    zone.SetPriority(128);
    zone.SetBlendDistance(2.5F);
    zone.SetReverbSend(0.65F);

    const auto restoredComponent = registration.Factory();
    registration.Deserialize(*restoredComponent, registration.Serialize(*source), registration.SchemaVersion);
    const auto& restored = dynamic_cast<const Keire::AudioReverbZoneComponent&>(*restoredComponent);
    CHECK(restored.Mixer() == Id(20));
    CHECK(restored.SnapshotId() == Id(21));
    CHECK(restored.Shape() == Keire::AudioReverbZoneShape::Sphere);
    CHECK(restored.BoxHalfExtent() == Keire::Vector3{2.0F, 3.0F, 4.0F});
    CHECK(restored.SphereRadius() == doctest::Approx(12.0F));
    CHECK(restored.Priority() == 128);
    CHECK(restored.BlendDistance() == doctest::Approx(2.5F));
    CHECK(restored.ReverbSend() == doctest::Approx(0.65F));

    CHECK_THROWS_AS(zone.SetShape(static_cast<Keire::AudioReverbZoneShape>(100)), std::invalid_argument);
    CHECK_THROWS_AS(zone.SetBoxHalfExtent({1.0F, 0.0F, 1.0F}), std::invalid_argument);
    CHECK_THROWS_AS(zone.SetSphereRadius(0.0F), std::invalid_argument);
    CHECK_THROWS_AS(zone.SetBlendDistance(-1.0F), std::invalid_argument);
    CHECK_THROWS_AS(zone.SetReverbSend(1.1F), std::invalid_argument);

    const auto registry = Keire::ComponentRegistry::CreateDefault();
    CHECK(registry->Contains(Keire::AudioReverbZoneComponent::StaticType()));
}

TEST_CASE("Scene import treats mixer assets but not local audio IDs as cook dependencies")
{
    const auto clip = Id(30);
    const auto sourceMixer = Id(31);
    const auto zoneMixer = Id(32);
    const auto localBus = Id(33);
    const auto localSnapshot = Id(34);

    auto definition = Keire::SceneAsset::EmptyDefinition("Audio dependencies");
    definition.Objects.push_back(
        {.Id = Id(40),
         .Name = "Audio",
         .Components = {
             {Keire::AudioSourceComponent::StaticType(), 2, true,
              "{\"clip\":\"" + clip.ToString() + "\",\"mixer\":\"" + sourceMixer.ToString() + "\",\"busId\":\"" +
                  localBus.ToString() + "\"}"},
             {Keire::AudioReverbZoneComponent::StaticType(), 1, true,
              "{\"mixer\":\"" + zoneMixer.ToString() + "\",\"snapshotId\":\"" + localSnapshot.ToString() + "\"}"},
         }});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto output = importer.ContextualImport({}, Keire::SceneAsset::Encode(definition));
    auto expected = std::vector{clip, sourceMixer, zoneMixer};
    std::ranges::sort(expected);
    CHECK(output.AssetDependencies == expected);
    CHECK(std::ranges::find(output.AssetDependencies, localBus) == output.AssetDependencies.end());
    CHECK(std::ranges::find(output.AssetDependencies, localSnapshot) == output.AssetDependencies.end());
}
