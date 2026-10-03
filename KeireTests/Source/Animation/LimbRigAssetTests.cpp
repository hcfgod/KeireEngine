#include "Keire/Animation/RiggingSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <vector>

namespace
{
    Keire::Ref<Keire::SkeletonAsset> LimbSkeleton()
    {
        return Keire::CreateRef<Keire::SkeletonAsset>(std::vector<Keire::SkeletonBone>{
            {"base", -1, {}, {}}, {"knee", 0, {{1, 0, 0}}, {}}, {"tip", 1, {{1, 0, 0}}, {}}});
    }

    Keire::Ref<Keire::RigDefinitionAsset> DecodeJson(const nlohmann::json& json)
    {
        const auto text = json.dump();
        return Keire::RigDefinitionAsset::Decode(std::as_bytes(std::span(text.data(), text.size())));
    }
} // namespace

TEST_CASE("Explicit limb rig assets roundtrip constraints and bind to the generic solver")
{
    const auto skeleton = LimbSkeleton();
    auto rig = Keire::InferRigDefinition(*skeleton, Keire::RigProfileType::Custom);
    rig.SchemaVersion = 2;
    Keire::LimbDefinition limb{{7}, "front", {"base", "knee", "tip"}};
    limb.ContactRadius = 0.04F;
    limb.MaximumIterations = 17;
    limb.Tolerance = 0.002F;
    limb.BendLimits = Keire::LimbBendLimits{5.0F, 160.0F};
    limb.PreferredBendDirection = Keire::Vector3{0, 0, 1};
    rig.Limbs.push_back(limb);
    const auto bytes = Keire::RigDefinitionAsset::Encode(rig);
    const auto decoded = Keire::RigDefinitionAsset::Decode(bytes);
    CHECK(decoded->Definition().Profile == Keire::RigProfileType::Custom);
    REQUIRE(decoded->Definition().Limbs.size() == 1);
    const auto& actual = decoded->Definition().Limbs.front();
    CHECK(actual.Id.Value == 7);
    CHECK(actual.Bones == limb.Bones);
    CHECK(actual.ContactRadius == 0.04F);
    CHECK(actual.MaximumIterations == 17);
    CHECK(actual.Tolerance == 0.002F);
    REQUIRE(actual.BendLimits);
    CHECK(actual.BendLimits->MaximumDegrees == 160.0F);
    CHECK(actual.PreferredBendDirection == limb.PreferredBendDirection);
    CHECK(Keire::RigDefinitionAsset::Encode(decoded->Definition()) == bytes);
    const Keire::BoundLimbRig bound(skeleton, decoded->Definition().Limbs);
    REQUIRE(bound.Limbs().size() == 1);
    CHECK(bound.Limbs().front().MaximumReach == doctest::Approx(2.0F));
}

TEST_CASE("Explicit limb rig assets retain legacy schema and reject invalid serialized chains")
{
    auto rig = Keire::InferRigDefinition(*LimbSkeleton(), Keire::RigProfileType::Custom);
    const auto legacy = Keire::RigDefinitionAsset::Encode(rig);
    const auto decoded = Keire::RigDefinitionAsset::Decode(legacy);
    CHECK(decoded->Definition().SchemaVersion == 1);
    CHECK(decoded->Definition().Limbs.empty());
    CHECK(Keire::RigDefinitionAsset::Encode(decoded->Definition()) == legacy);
    rig.Limbs.push_back({{1}, "front", {"base", "knee", "tip"}});
    CHECK_THROWS_AS((void)Keire::RigDefinitionAsset::Encode(rig), std::invalid_argument);
    rig.SchemaVersion = 2;
    const auto bytes = Keire::RigDefinitionAsset::Encode(rig);
    const auto json = nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()),
                                            reinterpret_cast<const char*>(bytes.data() + bytes.size()));
    for (const auto schema : {0, 1, 3})
    {
        auto invalid = json;
        invalid["schemaVersion"] = schema;
        CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    }
    auto invalid = json;
    invalid["schemaVersion"] = 4294967298ULL;
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid = json;
    invalid["limbs"][0]["solver"] = "guess";
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid = json;
    invalid["limbs"][0]["bones"] = {"base", "tip", "knee"};
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid = json;
    invalid["limbs"][0]["id"] = -1;
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid["limbs"][0]["id"] = 4294967296ULL;
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid = json;
    invalid["limbs"].push_back(invalid["limbs"][0]);
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
    invalid = json;
    invalid["limbs"][0]["solver"] = "fabrik";
    invalid["limbs"][0]["bendLimits"] = {{"minimumDegrees", 0}, {"maximumDegrees", 170}};
    CHECK_THROWS_AS(DecodeJson(invalid), std::invalid_argument);
}
