#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

TEST_CASE("lighting texture arrays preserve deterministic mip and layer payloads")
{
    Keire::LightingTextureArrayDefinition definition;
    definition.Encoding = Keire::LightingTextureEncoding::Rgba16Float;
    definition.Mips.push_back({2, 2, 2, std::vector<std::byte>(64, std::byte{0x2a})});
    const auto encoded = Keire::LightingTextureArrayAsset::Encode(definition);
    const auto decoded = Keire::LightingTextureArrayAsset::Decode(encoded);
    REQUIRE(decoded->Definition().Mips.size() == 1);
    CHECK(decoded->Definition().Mips.front().Layers == 2);
    CHECK(decoded->Definition().Mips.front().Pixels == definition.Mips.front().Pixels);
}

TEST_CASE("lighting cube arrays require complete six-face groups")
{
    Keire::LightingTextureArrayDefinition definition;
    definition.Target = Keire::LightingTextureTarget::CubeArray;
    definition.Encoding = Keire::LightingTextureEncoding::Rgbe8;
    definition.Mips.push_back({1, 1, 5, std::vector<std::byte>(20)});
    CHECK_THROWS_AS((void)Keire::LightingTextureArrayAsset::Encode(definition), std::invalid_argument);
    definition.Mips.front().Layers = 6;
    definition.Mips.front().Pixels.resize(24);
    CHECK_NOTHROW((void)Keire::LightingTextureArrayAsset::Encode(definition));
}

TEST_CASE("light probe volumes round-trip SH visibility and validity")
{
    Keire::LightProbeVolumeDefinition definition;
    definition.Origin = {-1.0F, 2.0F, 3.0F};
    definition.Spacing = {0.5F, 1.0F, 2.0F};
    definition.CountX = 1;
    definition.CountY = 1;
    definition.CountZ = 1;
    Keire::BakedLightProbe probe;
    probe.Irradiance[0] = {1.0F, 0.5F, 0.25F};
    probe.Visibility[2] = 0.25F;
    probe.Validity = 0.75F;
    definition.Probes.push_back(probe);
    const auto decoded = Keire::LightProbeVolumeAsset::Decode(Keire::LightProbeVolumeAsset::Encode(definition));
    REQUIRE(decoded->Definition().Probes.size() == 1);
    CHECK(decoded->Definition().Probes.front().Irradiance[0] == probe.Irradiance[0]);
    CHECK(decoded->Definition().Probes.front().Visibility[2] == doctest::Approx(0.25F));
    CHECK(decoded->Definition().Probes.front().Validity == doctest::Approx(0.75F));
}

TEST_CASE("lighting sets preserve eight-channel mixed-light assignments")
{
    Keire::LightingSetDefinition definition;
    definition.Scene = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    definition.InputFingerprint = std::string(64, 'a');
    for (std::uint8_t channel = 0; channel < 8; ++channel)
        definition.MixedLights.push_back(
            {Keire::AssetId(0x2000000000000000ULL, static_cast<std::uint64_t>(channel) + 1U), channel});
    const auto decoded = Keire::LightingSetAsset::Decode(Keire::LightingSetAsset::Encode(definition));
    REQUIRE(decoded->Definition().MixedLights.size() == 8);
    for (std::uint8_t channel = 0; channel < 8; ++channel)
        CHECK(decoded->Definition().MixedLights[channel].ShadowMaskChannel == channel);
    definition.MixedLights.back().ShadowMaskChannel = 8;
    CHECK_THROWS_AS((void)Keire::LightingSetAsset::Encode(definition), std::invalid_argument);
}

TEST_CASE("scene schema v6 persists bake settings and generated lighting reference")
{
    auto definition = Keire::SceneAsset::EmptyDefinition("LightingScene");
    definition.Lighting.Backend = Keire::LightingBakeBackend::CPU;
    definition.Lighting.Quality = Keire::LightingBakeQuality::Production;
    definition.Lighting.SamplesPerTexel = 1024;
    definition.BakedLighting = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001");
    const auto decoded = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(definition));
    CHECK(decoded->Definition().SchemaVersion == 6);
    CHECK(decoded->Definition().Lighting.Backend == Keire::LightingBakeBackend::CPU);
    CHECK(decoded->Definition().Lighting.Quality == Keire::LightingBakeQuality::Production);
    CHECK(decoded->Definition().Lighting.SamplesPerTexel == 1024);
    CHECK(decoded->Definition().BakedLighting == definition.BakedLighting);
}

TEST_CASE("mesh asset v4 preserves a dedicated lightmap UV channel")
{
    std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{1.0F, 0.0F, 0.0F}},
                        Keire::MeshVertex{{0.0F, 1.0F, 0.0F}}};
    vertices[0].UV1 = {0.1F, 0.2F};
    vertices[1].UV1 = {0.8F, 0.2F};
    vertices[2].UV1 = {0.1F, 0.9F};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const auto decoded = Keire::MeshAsset::Decode(Keire::MeshAsset::Encode(vertices, indices));
    REQUIRE(decoded->Vertices().size() == vertices.size());
    CHECK(decoded->Vertices()[0].UV1 == vertices[0].UV1);
    CHECK(decoded->Vertices()[1].UV1 == vertices[1].UV1);
    CHECK(decoded->Vertices()[2].UV1 == vertices[2].UV1);
}
