#include "KeireInternal/Scenes/SceneSerialization.h"
#include "KeireInternal/Scripting/ManagedReflection.h"

#include <doctest/doctest.h>

#include <limits>
#include <type_traits>
#include <variant>

static_assert(
    std::is_same_v<std::variant_alternative_t<14, Keire::ComponentPropertyValue>, Keire::ComponentReferenceValue>);
static_assert(std::is_same_v<std::variant_alternative_t<15, Keire::ComponentPropertyValue>, Keire::Vector3d>);

TEST_CASE("Precise component positions round trip without float narrowing")
{
    Keire::ComponentRegistration registration;
    Keire::ComponentProperty property;
    property.Key = "position";
    property.Kind = Keire::ComponentPropertyKind::Vector3d;
    registration.Properties.push_back(property);
    const Keire::Vector3d expected{1.0e15 + 0.125, -1.0e15 - 0.25, 6371000.001};
    const auto encoded = Keire::Detail::EncodeComponentPropertyBag({{"position", expected}});
    const auto decoded = Keire::Detail::DecodeComponentPropertyBag(encoded, registration);
    CHECK(std::get<Keire::Vector3d>(decoded.at("position")) == expected);
    CHECK_THROWS_AS((void)Keire::Detail::DecodeComponentPropertyBag(R"({"position":[1,2]})", registration),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::EncodeComponentPropertyBag(
                        {{"position", Keire::Vector3d{std::numeric_limits<double>::infinity(), 0, 0}}}),
                    std::invalid_argument);
}

TEST_CASE("Precise managed Inspector properties preserve double coordinates")
{
    Keire::ComponentProperty property;
    property.Key = "Position";
    property.Kind = Keire::ComponentPropertyKind::Vector3d;
    const std::vector<Keire::ComponentProperty> properties{property};
    const std::string state =
        R"({"Version":4,"Fields":[{"Name":"Position","Value":{"X":1000000000000000.125,"Y":-1000000000000000.25,"Z":6371000.001}}]})";
    const Keire::Vector3d expected{1e15 + 0.125, -1e15 - 0.25, 6371000.001};
    const auto projected = Keire::Detail::ProjectManagedState(state, properties);
    CHECK(std::get<Keire::Vector3d>(projected.at("Position")) == expected);

    const Keire::Vector3d moved{expected.X + 0.25, expected.Y - 0.5, expected.Z + 100000.0};
    const auto updated = Keire::Detail::ApplyManagedState(state, {{"Position", moved}}, properties);
    const auto restored = Keire::Detail::ProjectManagedState(updated, properties);
    CHECK(std::get<Keire::Vector3d>(restored.at("Position")) == moved);
    CHECK_THROWS_AS(
        (void)Keire::Detail::ApplyManagedState(
            state, {{"Position", Keire::Vector3d{std::numeric_limits<double>::infinity(), 0, 0}}}, properties),
        std::invalid_argument);
}
