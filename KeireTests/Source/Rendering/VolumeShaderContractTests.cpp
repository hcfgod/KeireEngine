#include "KeireInternal/Assets/RenderingAssetValidation.h"
#include "KeireInternal/Assets/ShaderAssetCodecInternal.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace
{
    Keire::ShaderAssetDefinition VolumeShader()
    {
        Keire::ShaderAssetDefinition definition;
        definition.Source = "Assets/Volume.hlsl";
        definition.VertexLayoutVersion = 3U;
        definition.VolumeLightingAbiVersion = 1U;
        return definition;
    }
} // namespace

TEST_CASE("volume shader contract accepts only supported mesh lighting layouts")
{
    auto definition = VolumeShader();
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));

    definition.VolumeLightingAbiVersion = 2U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 3U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 4U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 5U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 6U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 7U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 8U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 9U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 10U;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.VolumeLightingAbiVersion = 1U;
    definition.VertexLayoutVersion = Keire::UiShaderVertexLayoutVersion;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.VertexLayoutVersion = 3U;
    definition.ReceivesShadows = true;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.UsesImageBasedLighting = true;
    definition.SpatialLightingAbiVersion = 2U;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.UsesForwardPlus = true;
    definition.SpatialLightingAbiVersion = 3U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
}

TEST_CASE("canonical volume shader metadata round trips and fails closed on older schemas")
{
    auto definition = VolumeShader();
    definition.Variants.push_back({Keire::ShaderBinaryFormat::Dxil, {std::byte{1}}, {std::byte{2}}});
    const auto bytes = Keire::Detail::EncodeCanonicalShaderAsset(definition);
    const auto decoded = Keire::Detail::DecodeCanonicalShaderAsset(bytes);
    CHECK(decoded.VolumeLightingAbiVersion == 1U);
    auto document = nlohmann::json::from_cbor(bytes);
    const auto decode = [&document]()
    {
        const auto encoded = nlohmann::json::to_cbor(document);
        return Keire::Detail::DecodeCanonicalShaderAsset(std::as_bytes(std::span(encoded)));
    };

    for (const auto schema : {1U, 2U, 3U})
    {
        document["schemaVersion"] = schema;
        CHECK_THROWS_AS(decode(), std::invalid_argument);
        document.erase("volumeLightingAbiVersion");
        CHECK(decode().VolumeLightingAbiVersion == 0U);
        document["volumeLightingAbiVersion"] = 1U;
    }
    document["schemaVersion"] = Keire::ShaderAssetSchemaVersion;
    document["volumeLightingAbiVersion"] = 2U;
    CHECK(decode().VolumeLightingAbiVersion == 2U);
    document["volumeLightingAbiVersion"] = 3U;
    CHECK(decode().VolumeLightingAbiVersion == 3U);
    document["volumeLightingAbiVersion"] = 4U;
    CHECK(decode().VolumeLightingAbiVersion == 4U);
    document["volumeLightingAbiVersion"] = 5U;
    CHECK(decode().VolumeLightingAbiVersion == 5U);
    document["volumeLightingAbiVersion"] = 6U;
    CHECK(decode().VolumeLightingAbiVersion == 6U);
    document["volumeLightingAbiVersion"] = 7U;
    CHECK(decode().VolumeLightingAbiVersion == 7U);
    document["volumeLightingAbiVersion"] = 8U;
    CHECK(decode().VolumeLightingAbiVersion == 8U);
    document["volumeLightingAbiVersion"] = 9U;
    CHECK(decode().VolumeLightingAbiVersion == 9U);
    for (const auto invalidVersion : {-1LL, 10LL, 4294967296LL, 4294967297LL})
    {
        document["volumeLightingAbiVersion"] = invalidVersion;
        CHECK_THROWS_AS(decode(), std::invalid_argument);
    }
}

TEST_CASE("sampled volume ABI reserves a sampler while legacy volume layouts retain their capacity")
{
    auto definition = VolumeShader();
    definition.VolumeLightingAbiVersion = 3U;
    definition.UserResourceSlots = 16;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.UserResourceSlots = 15;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 1U;
    definition.UserResourceSlots = 16;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.VolumeLightingAbiVersion = 2U;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.UserResourceSlots = 15;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));

    definition.ReceivesShadows = true;
    definition.UsesForwardPlus = true;
    definition.UsesImageBasedLighting = true;
    definition.SpatialLightingAbiVersion = 3U;
    definition.UserResourceSlots = 6;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
    definition.UserResourceSlots = 7;
    CHECK_THROWS_AS(Keire::Detail::ValidateShaderDefinition(definition, false, true), std::invalid_argument);
    definition.VolumeLightingAbiVersion = 1U;
    CHECK_NOTHROW(Keire::Detail::ValidateShaderDefinition(definition, false, true));
}
