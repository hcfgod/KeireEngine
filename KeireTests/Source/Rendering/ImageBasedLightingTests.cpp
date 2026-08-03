#include "KeireInternal/Rendering/ImageBasedLightingInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] Keire::Ref<Keire::Texture2DAsset>
    ConstantEnvironment(const Keire::TextureEnvironmentLayout layout = Keire::TextureEnvironmentLayout::Equirectangular)
    {
        const std::uint32_t width = layout == Keire::TextureEnvironmentLayout::Equirectangular ? 64U : 24U;
        const std::uint32_t height = layout == Keire::TextureEnvironmentLayout::Equirectangular ? 32U : 4U;
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
        for (std::size_t index = 0; index < pixels.size(); index += 4U)
        {
            pixels[index] = std::byte{64};
            pixels[index + 1U] = std::byte{128};
            pixels[index + 2U] = std::byte{255};
            pixels[index + 3U] = std::byte{255};
        }
        Keire::TextureImportSettings settings;
        settings.Semantic = Keire::TextureSemantic::Environment;
        settings.ColorSpace = Keire::TextureColorSpace::Linear;
        settings.Mips = Keire::TextureMipPolicy::None;
        settings.EnvironmentLayout = layout;
        return Keire::CreateRef<Keire::Texture2DAsset>(
            settings, std::vector<Keire::TextureMipLevel>{{width, height, std::move(pixels)}});
    }
} // namespace

TEST_CASE("diffuse irradiance baking preserves constant environment radiance")
{
    constexpr float pi = 3.14159265358979323846F;
    for (const auto layout :
         {Keire::TextureEnvironmentLayout::Equirectangular, Keire::TextureEnvironmentLayout::HorizontalStrip})
    {
        const auto environment = ConstantEnvironment(layout);
        const auto irradiance = Keire::RenderBackend::BakeDiffuseIrradiance(*environment);
        for (const auto direction : std::array{Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{-1.0F, 0.0F, 0.0F},
                                               Keire::Vector3{0.0F, 1.0F, 0.0F}, Keire::Vector3{0.0F, 0.0F, -1.0F}})
        {
            const auto evaluated = Keire::RenderBackend::EvaluateDiffuseIrradiance(irradiance, direction);
            CHECK(evaluated.X == doctest::Approx(pi * 64.0F / 255.0F).epsilon(0.025));
            CHECK(evaluated.Y == doctest::Approx(pi * 128.0F / 255.0F).epsilon(0.025));
            CHECK(evaluated.Z == doctest::Approx(pi).epsilon(0.025));
        }
    }

    Keire::TextureImportSettings settings;
    settings.Semantic = Keire::TextureSemantic::Data;
    settings.Mips = Keire::TextureMipPolicy::None;
    const auto data = Keire::CreateRef<Keire::Texture2DAsset>(
        settings,
        std::vector<Keire::TextureMipLevel>{{1, 1, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}}});
    CHECK_THROWS_AS((void)Keire::RenderBackend::BakeDiffuseIrradiance(*data), std::invalid_argument);
}

TEST_CASE("BRDF integration LUT baking is deterministic and bounded")
{
    const auto first = Keire::RenderBackend::CreateBrdfIntegrationLut(16U, 64U);
    const auto second = Keire::RenderBackend::CreateBrdfIntegrationLut(16U, 64U);
    REQUIRE(first->Mips().size() == 1U);
    CHECK(first->Width() == 16U);
    CHECK(first->Height() == 16U);
    CHECK(first->Mips().front().Pixels == second->Mips().front().Pixels);
    bool hasIntegratedEnergy = false;
    const auto& pixels = first->Mips().front().Pixels;
    for (std::size_t index = 0; index < pixels.size(); index += 4U)
    {
        hasIntegratedEnergy |= std::to_integer<std::uint8_t>(pixels[index]) != 0U ||
                               std::to_integer<std::uint8_t>(pixels[index + 1U]) != 0U;
        CHECK(pixels[index + 2U] == std::byte{0});
        CHECK(pixels[index + 3U] == std::byte{255});
    }
    CHECK(hasIntegratedEnergy);
    CHECK_THROWS_AS((void)Keire::RenderBackend::CreateBrdfIntegrationLut(1U, 64U), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::RenderBackend::CreateBrdfIntegrationLut(16U, 8U), std::invalid_argument);
}

TEST_CASE("HDR environment imports include a radiance-preserving specular mip chain")
{
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    std::vector<std::byte> source;
    std::ranges::transform(header, std::back_inserter(source),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    source.insert(source.end(), {std::byte{128}, std::byte{64}, std::byte{32}, std::byte{129}, std::byte{64},
                                 std::byte{128}, std::byte{32}, std::byte{129}});
    const auto importer = Keire::CreateTexture2DAssetImporter();
    const auto environment = Keire::Texture2DAsset::Decode(importer.Import(source));
    REQUIRE(environment->Mips().size() == 2U);
    CHECK(environment->Mips().front().Width == 2U);
    CHECK(environment->Mips().back().Width == 1U);
    CHECK(environment->Mips().back().Height == 1U);
    CHECK(environment->Settings().Mips == Keire::TextureMipPolicy::Generate);
    CHECK(environment->Settings().HighDynamicRange);
}

TEST_CASE("shader manifests opt into image-based and spatial-lighting ABI v2")
{
    const std::string manifest = R"({
        "schemaVersion": 1,
        "source": "Assets/Test.hlsl",
        "stages": {"vertex": "VSMain", "fragment": "PSMain"},
        "vertexLayoutVersion": 3,
        "usesImageBasedLighting": true,
        "spatialLightingAbiVersion": 2,
        "properties": []
    })";
    std::vector<std::byte> bytes(manifest.size());
    std::ranges::transform(manifest, bytes.begin(),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    auto definition = Keire::ShaderAsset::DecodeManifest(bytes);
    CHECK(definition.UsesImageBasedLighting);
    CHECK(definition.VertexLayoutVersion == 3U);
    CHECK(definition.SpatialLightingAbiVersion == 2U);
    for (const auto format :
         {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
        definition.Variants.push_back({format, {std::byte{1}, std::byte{2}}, {std::byte{3}, std::byte{4}}});
    const auto decoded = Keire::ShaderAsset::Decode(Keire::ShaderAsset::Encode(definition));
    CHECK(decoded->Definition().UsesImageBasedLighting);
    CHECK(decoded->Definition().SpatialLightingAbiVersion == 2U);

    const std::string invalidManifest = R"({
        "schemaVersion": 1,
        "source": "Assets/Test.hlsl",
        "stages": {"vertex": "VSMain", "fragment": "PSMain"},
        "vertexLayoutVersion": 3,
        "spatialLightingAbiVersion": 2,
        "properties": []
    })";
    std::vector<std::byte> invalidBytes(invalidManifest.size());
    std::ranges::transform(invalidManifest, invalidBytes.begin(),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    CHECK_THROWS_AS((void)Keire::ShaderAsset::DecodeManifest(invalidBytes), std::invalid_argument);

    std::string textureProperties;
    for (std::uint32_t index = 0; index < 8U; ++index)
    {
        if (!textureProperties.empty())
            textureProperties += ',';
        textureProperties +=
            "{\"name\":\"Texture" + std::to_string(index) + "\",\"type\":\"Texture2D\",\"default\":null}";
    }
    const std::string overBudgetManifest =
        "{\"schemaVersion\":1,\"source\":\"Assets/Test.hlsl\","
        "\"stages\":{\"vertex\":\"VSMain\",\"fragment\":\"PSMain\"},"
        "\"vertexLayoutVersion\":3,\"receivesShadows\":true,\"usesImageBasedLighting\":true,"
        "\"spatialLightingAbiVersion\":2,\"properties\":[" +
        textureProperties + "]}";
    std::vector<std::byte> overBudgetBytes(overBudgetManifest.size());
    std::ranges::transform(overBudgetManifest, overBudgetBytes.begin(),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    CHECK_THROWS_WITH_AS((void)Keire::ShaderAsset::DecodeManifest(overBudgetBytes),
                         "Shader material textures and fixed lighting resources exceed the portable 16-sampler limit.",
                         std::invalid_argument);
}
