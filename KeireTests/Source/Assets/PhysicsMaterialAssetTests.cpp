#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <limits>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        std::vector<std::byte> result(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
            result[index] = static_cast<std::byte>(text[index]);
        return result;
    }
} // namespace

TEST_CASE("physics material assets round trip deterministically")
{
    Keire::PhysicsMaterialDefinition definition;
    definition.Friction = 0.75F;
    definition.Restitution = 0.4F;
    definition.FrictionCombine = Keire::PhysicsMaterialCombineMode::Maximum;
    definition.RestitutionCombine = Keire::PhysicsMaterialCombineMode::Multiply;

    const auto encoded = Keire::PhysicsMaterialAsset::Encode(definition);
    const auto decoded = Keire::PhysicsMaterialAsset::Decode(encoded);
    CHECK(decoded->Definition().SchemaVersion == 1);
    CHECK(decoded->Definition().Friction == doctest::Approx(0.75F));
    CHECK(decoded->Definition().Restitution == doctest::Approx(0.4F));
    CHECK(decoded->Definition().FrictionCombine == Keire::PhysicsMaterialCombineMode::Maximum);
    CHECK(decoded->Definition().RestitutionCombine == Keire::PhysicsMaterialCombineMode::Multiply);
    CHECK(Keire::PhysicsMaterialAsset::Encode(decoded->Definition()) == encoded);

    const auto importer = Keire::CreatePhysicsMaterialAssetImporter();
    CHECK(importer.Name == "Keire.PhysicsMaterial");
    CHECK(importer.Type == Keire::PhysicsMaterialAsset::StaticType());
    REQUIRE(importer.Extensions.size() == 1);
    CHECK(importer.Extensions.front() == ".keirephysicsmaterial");
    REQUIRE(importer.Import);
    CHECK(importer.Import(encoded) == encoded);

    const auto decoder = Keire::CreatePhysicsMaterialAssetDecoder();
    CHECK(decoder.Type == Keire::PhysicsMaterialAsset::StaticType());
    REQUIRE(decoder.Fallback);
    REQUIRE(decoder.Decode);
    CHECK(decoder.Decode(encoded)->Type() == Keire::PhysicsMaterialAsset::StaticType());
}

TEST_CASE("physics material assets reject malformed and unsafe values")
{
    CHECK_THROWS_AS((void)Keire::PhysicsMaterialAsset::Decode(Bytes("{}")), std::runtime_error);
    CHECK_THROWS_AS((void)Keire::PhysicsMaterialAsset::Decode(
                        Bytes(R"({"schemaVersion":1,"friction":0.5,"restitution":0,"frictionCombine":"Unknown"})")),
                    std::runtime_error);

    Keire::PhysicsMaterialDefinition invalid;
    invalid.Restitution = 2.0F;
    CHECK_THROWS_AS((void)Keire::PhysicsMaterialAsset::Encode(invalid), std::invalid_argument);
    invalid.Restitution = 0.0F;
    invalid.Friction = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS((void)Keire::PhysicsMaterialAsset::Encode(invalid), std::invalid_argument);
}
