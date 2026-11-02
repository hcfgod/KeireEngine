#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>

TEST_CASE("Planet character clearance inclusion and exclusion preserve "
          "collision filtering")
{
    using namespace Keire;
    const auto system = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = system->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.HalfExtent = {1.0F, 1.0F, 1.0F};
    const auto first = world->CreateBody(definition);
    const auto second = world->CreateBody(definition);
    PhysicsCapsuleOverlapQuery query;
    const auto contact = [&](const std::optional<PhysicsBodyId> selected, const std::span<const PhysicsBodyId> excluded)
    { return Detail::PhysicsFrameAccess::HasCapsuleContact(*world, query, selected, excluded); };
    CHECK(contact(std::nullopt, {}));
    CHECK(contact(first, {}));
    const std::array excludeFirst{first};
    CHECK_FALSE(contact(first, excludeFirst));
    CHECK(contact(second, excludeFirst));
    const std::array excludeBoth{first, second};
    CHECK_FALSE(contact(std::nullopt, excludeBoth));
    query.IgnoreBody = first;
    CHECK_FALSE(contact(first, {}));
    CHECK(contact(second, {}));
    query.Mask = 0;
    CHECK_FALSE(contact(second, {}));
    query.Mask = ~0U;
    query.Layer = 3;
    CHECK_THROWS_AS((void)contact(second, {}), std::invalid_argument);
    query.Layer = 1;
    query.Center.X = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS((void)contact(second, {}), std::invalid_argument);
    query.Center.X = 0.0F;
    world->DestroyBody(second);
    CHECK_THROWS_AS((void)contact(second, {}), std::invalid_argument);
    CHECK_THROWS_AS((void)contact(std::nullopt, excludeBoth), std::invalid_argument);
    CHECK_THROWS_AS((void)contact(PhysicsBodyId{}, {}), std::invalid_argument);
    world->Close();
    CHECK_THROWS_AS((void)contact(std::nullopt, {}), std::logic_error);
    system->Close();
}

TEST_CASE("Planet character selected body retains reciprocal layers and trigger policy")
{
    using namespace Keire;
    const auto system = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto world = system->CreateWorld();
    PhysicsBodyDefinition definition;
    definition.Trigger = true;
    definition.Layer = 2;
    definition.Mask = 4;
    const auto body = world->CreateBody(definition);
    PhysicsCapsuleOverlapQuery query;
    query.Mask = 2;
    query.Layer = 4;
    const auto contact = [&] { return Detail::PhysicsFrameAccess::HasCapsuleContact(*world, query, body, {}); };
    CHECK_FALSE(contact());
    query.IncludeTriggers = true;
    CHECK(contact());
    query.Mask = 1;
    CHECK_FALSE(contact());
    query.Mask = 2;
    query.Layer = 1;
    CHECK_FALSE(contact());
    world->Close();
    system->Close();
}
