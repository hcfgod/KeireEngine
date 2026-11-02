#include "Keire/Assets/PostProcessProfileAsset.h"
#include "Keire/Assets/RenderingAssets.h"

#include <doctest/doctest.h>

#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    std::vector<std::byte> ProfileBytes(const std::string_view text)
    {
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }
} // namespace

TEST_CASE("post process profiles preserve every parameter and override independently")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    const auto descriptions = PostProcessParameterDescriptions();
    REQUIRE(descriptions.size() == profile.Parameters.size());
    for (std::size_t index = 0; index < profile.Parameters.size(); ++index)
    {
        CHECK(profile.Parameters[index].Value == descriptions[index].Default);
        CHECK_FALSE(profile.Parameters[index].Override);
        profile.Parameters[index] = {index % 2 == 0, descriptions[index].Maximum};
    }
    for (std::size_t index = 0; index < profile.Effects.size(); ++index)
    {
        profile.Effects[index] = {index % 2 != 0, index % 3 == 0};
    }
    const auto bytes = PostProcessProfileAsset::Encode(profile);
    CHECK(PostProcessProfileAsset::Decode(bytes)->Definition() == profile);
    CHECK(PostProcessProfileAsset::Encode(PostProcessProfileAsset::Decode(bytes)->Definition()) == bytes);
}

TEST_CASE("post process profiles reject invalid values even without active overrides")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    for (std::size_t index = 0; index < profile.Parameters.size(); ++index)
    {
        auto invalid = profile;
        invalid.Parameters[index].Value = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS_AS(PostProcessProfileAsset::Validate(invalid), std::invalid_argument);
        invalid.Parameters[index].Value = PostProcessParameterDescriptions()[index].Maximum + 1.0;
        CHECK_THROWS_AS(PostProcessProfileAsset::Validate(invalid), std::invalid_argument);
    }
    profile.SchemaVersion = 4;
    CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
}

TEST_CASE("post process schema two preserves independent lookup overrides and legacy profiles")
{
    using namespace Keire;
    auto legacy = PostProcessProfileAsset::Decode(ProfileBytes(R"({"schemaVersion":1,"parameters":{},"effects":{}})"));
    CHECK(legacy->Definition().SchemaVersion == 1);
    CHECK_FALSE(legacy->Definition().ColorLookup.Override);
    CHECK_FALSE(legacy->Definition().ColorLookup.Value);
    CHECK(legacy->Definition().ColorLookupWeight.Value == 1.0);
    CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(legacy->Definition()))->Definition() ==
          legacy->Definition());
    for (const bool overrideReference : {false, true})
    {
        for (const bool overrideWeight : {false, true})
        {
            PostProcessProfileDefinition profile;
            profile.ColorLookup = {overrideReference, AssetId(5, 9)};
            profile.ColorLookupWeight = {overrideWeight, 0.75};
            CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(profile))->Definition() == profile);
            profile.ColorLookup.Value = {};
            CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(profile))->Definition() == profile);
            profile.SchemaVersion = 1;
            CHECK_THROWS_AS((void)PostProcessProfileAsset::Encode(profile), std::invalid_argument);
        }
    }
}

TEST_CASE("post process lookup rejects malformed records and out of range weights")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    for (const auto weight :
         {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        profile.ColorLookupWeight.Value = weight;
        CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
    }
    for (
        const std::string_view record :
        {R"({"schemaVersion":2,"parameters":{},"effects":{},"colorLookup":{}})",
         R"({"schemaVersion":1,"parameters":{},"effects":{},"colorLookup":{}})",
         R"({"schemaVersion":2,"parameters":{},"effects":{},"colorLookup":{"override":true,"texture":9,"weightOverride":true,"weight":1}})",
         R"({"schemaVersion":2,"parameters":{},"effects":{},"colorLookup":{"override":true,"texture":"00000000-0000-0000-0000-000000000000","weightOverride":1,"weight":1}})"})
    {
        CHECK_THROWS_AS((void)PostProcessProfileAsset::Decode(ProfileBytes(record)), std::runtime_error);
    }
}

TEST_CASE("post process custom stacks preserve ordering disabled entries and explicit clearing")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    profile.CustomEffects = {true,
                             {{AssetId(1, 2), PostProcessCustomStage::AfterToneMap, true},
                              {AssetId(3, 4), PostProcessCustomStage::BeforeToneMap, false},
                              {AssetId(1, 2), PostProcessCustomStage::AfterUi, true}}};
    const auto encoded = PostProcessProfileAsset::Encode(profile);
    CHECK(PostProcessProfileAsset::Decode(encoded)->Definition() == profile);
    const auto importer = CreatePostProcessProfileAssetImporter();
    const auto output = importer.ContextualImport({}, encoded);
    CHECK(output.AssetDependencies == std::vector<AssetId>{AssetId(1, 2), AssetId(3, 4)});
    CHECK(importer.RestoreCachedOutput(encoded).AssetDependencies == output.AssetDependencies);
    profile.CustomEffects.Value.clear();
    CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(profile))->Definition() == profile);
    profile.SchemaVersion = 2;
    CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
    profile.CustomEffects.Override = false;
    CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(profile))->Definition() == profile);
}

TEST_CASE("post process custom stacks reject invalid materials stages and excessive workloads")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    profile.CustomEffects.Value.push_back({});
    CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
    profile.CustomEffects.Value[0].Material = AssetId(1, 2);
    profile.CustomEffects.Value[0].Stage = static_cast<PostProcessCustomStage>(255);
    CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
    profile.CustomEffects.Value[0].Stage = PostProcessCustomStage::AfterToneMap;
    profile.CustomEffects.Value.resize(MaximumPostProcessCustomEffects + 1, profile.CustomEffects.Value[0]);
    CHECK_THROWS_AS(PostProcessProfileAsset::Validate(profile), std::invalid_argument);
    profile.CustomEffects.Value.resize(1);
    const auto importer = CreatePostProcessProfileAssetImporter();
    AssetImportContext context;
    context.Asset = AssetId(1, 2);
    CHECK_THROWS_AS((void)importer.ContextualImport(context, PostProcessProfileAsset::Encode(profile)),
                    std::invalid_argument);
    context.Asset = AssetId(5, 6);
    context.ResolveAssetSource = [](AssetId id) -> std::optional<AssetImportSource>
    { return AssetImportSource{id, Texture2DAsset::StaticType(), "Wrong.png"}; };
    CHECK_THROWS_AS((void)importer.ContextualImport(context, PostProcessProfileAsset::Encode(profile)),
                    std::invalid_argument);
    context.ResolveAssetSource = [](AssetId id) -> std::optional<AssetImportSource>
    { return AssetImportSource{id, MaterialAsset::StaticType(), "Effect.keiremateriallegacy"}; };
    CHECK_NOTHROW((void)importer.ContextualImport(context, PostProcessProfileAsset::Encode(profile)));
}

TEST_CASE("post process importer retains inactive lookup dependencies and rejects invalid reference types")
{
    using namespace Keire;
    PostProcessProfileDefinition profile;
    profile.ColorLookup = {false, AssetId(5, 9)};
    const auto bytes = PostProcessProfileAsset::Encode(profile);
    const auto importer = CreatePostProcessProfileAssetImporter();
    REQUIRE(importer.ContextualImport);
    REQUIRE(importer.RestoreCachedOutput);
    AssetImportContext context;
    context.Asset = AssetId(3, 7);
    auto output = importer.ContextualImport(context, bytes);
    CHECK(output.AssetDependencies == std::vector<AssetId>{profile.ColorLookup.Value});
    CHECK(importer.RestoreCachedOutput(bytes).AssetDependencies == output.AssetDependencies);
    context.ResolveAssetSource = [&](const AssetId id) -> std::optional<AssetImportSource>
    { return AssetImportSource{id, Texture2DAsset::StaticType(), "Assets/Grade.png"}; };
    CHECK_NOTHROW((void)importer.ContextualImport(context, bytes));
    context.ResolveAssetSource = [&](const AssetId id) -> std::optional<AssetImportSource>
    { return AssetImportSource{id, PostProcessProfileAsset::StaticType(), "Assets/Other.keirepostprocess"}; };
    CHECK_THROWS_AS((void)importer.ContextualImport(context, bytes), std::invalid_argument);
    context.Asset = profile.ColorLookup.Value;
    context.ResolveAssetSource = {};
    CHECK_THROWS_AS((void)importer.ContextualImport(context, bytes), std::invalid_argument);
    profile.ColorLookup.Value = {};
    CHECK(importer.ContextualImport(context, PostProcessProfileAsset::Encode(profile)).AssetDependencies.empty());
}

TEST_CASE("post process lookup texture contract accepts bounded linear strips and rejects unsafe sampling")
{
    using namespace Keire;
    TextureImportSettings settings;
    settings.Semantic = TextureSemantic::Data;
    settings.ColorSpace = TextureColorSpace::Linear;
    settings.Mips = TextureMipPolicy::None;
    settings.Sampler.AddressU = TextureAddressMode::Clamp;
    settings.Sampler.AddressV = TextureAddressMode::Clamp;
    settings.Sampler.AddressW = TextureAddressMode::Clamp;
    const auto makeTexture =
        [](const TextureImportSettings& importSettings, const std::uint32_t width, const std::uint32_t height)
    {
        TextureMipLevel mip{width, height, std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4U)};
        return CreateRef<Texture2DAsset>(importSettings, std::vector<TextureMipLevel>{std::move(mip)});
    };
    for (const std::uint32_t edge : {16U, 32U, 64U})
    {
        CHECK(ValidatePostProcessColorLookup(*makeTexture(settings, edge * edge, edge)) == edge);
    }
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*makeTexture(settings, 16, 16)), std::invalid_argument);
    auto invalid = settings;
    invalid.Semantic = TextureSemantic::Color;
    invalid.ColorSpace = TextureColorSpace::Srgb;
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*makeTexture(invalid, 256, 16)), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.AddressU = TextureAddressMode::Repeat;
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*makeTexture(invalid, 256, 16)), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.Minimum = TextureFilter::Nearest;
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*makeTexture(invalid, 256, 16)), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.Magnification = TextureFilter::Nearest;
    const auto nearestMagnification = makeTexture(invalid, 256, 16);
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*nearestMagnification), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.AddressV = TextureAddressMode::Repeat;
    const auto repeatingV = makeTexture(invalid, 256, 16);
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*repeatingV), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.AddressW = TextureAddressMode::Repeat;
    const auto repeatingW = makeTexture(invalid, 256, 16);
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*repeatingW), std::invalid_argument);
    invalid = settings;
    invalid.Sampler.Anisotropy = 2;
    const auto anisotropic = makeTexture(invalid, 256, 16);
    CHECK_THROWS_AS((void)ValidatePostProcessColorLookup(*anisotropic), std::invalid_argument);
}

TEST_CASE("post process profile documents reject malformed fields and oversized inputs")
{
    using namespace Keire;
    for (const std::string_view record :
         {R"({"schemaVersion":2,"parameters":{},"effects":{}})", R"({"schemaVersion":1,"parameters":[],"effects":{}})",
          R"({"schemaVersion":1,"parameters":{"contrast":{"override":false,"value":true}},"effects":{}})",
          R"({"schemaVersion":1,"parameters":{},"effects":{"bloom":{"override":true,"value":1}}})"})
    {
        CHECK_THROWS_AS((void)PostProcessProfileAsset::Decode(ProfileBytes(record)), std::runtime_error);
    }
    CHECK_THROWS_AS((void)PostProcessProfileAsset::Decode({}), std::runtime_error);
    const std::vector<std::byte> oversized(64U * 1024U + 1U);
    CHECK_THROWS_AS((void)PostProcessProfileAsset::Decode(oversized), std::runtime_error);
}
