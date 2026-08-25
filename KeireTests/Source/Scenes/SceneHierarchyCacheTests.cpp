#include "KeireInternal/Scenes/SceneHierarchyCache.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

TEST_CASE("scene hierarchy cache reuses deterministic order and invalidates structural views")
{
    const auto rootOne = Keire::EntityId::Generate();
    const auto childOne = Keire::EntityId::Generate();
    const auto grandchild = Keire::EntityId::Generate();
    const auto rootTwo = Keire::EntityId::Generate();
    const auto childTwo = Keire::EntityId::Generate();
    const std::vector authoredOrder{childTwo, rootTwo, grandchild, rootOne, childOne};
    std::unordered_map<Keire::EntityId, Keire::EntityId> parents{
        {childOne, rootOne}, {grandchild, childOne}, {childTwo, rootTwo}};
    std::size_t parentLookups = 0;
    const auto parentOf = [&](const Keire::EntityId id)
    {
        ++parentLookups;
        const auto found = parents.find(id);
        return found == parents.end() ? Keire::EntityId{} : found->second;
    };

    Keire::Detail::SceneHierarchyCache cache;
    CHECK(cache.Ordered(authoredOrder, parentOf) == std::vector{rootTwo, childTwo, rootOne, childOne, grandchild});
    CHECK(parentLookups == authoredOrder.size());
    CHECK(cache.Ordered(authoredOrder, parentOf) == std::vector{rootTwo, childTwo, rootOne, childOne, grandchild});
    CHECK(parentLookups == authoredOrder.size());
    CHECK(std::ranges::equal(cache.Children(rootOne, authoredOrder, parentOf), std::vector{childOne}));

    std::vector<Keire::EntityId> subtree;
    cache.VisitSubtree(rootOne, authoredOrder, parentOf, [&](const Keire::EntityId id) { subtree.push_back(id); });
    CHECK(subtree == std::vector{rootOne, childOne, grandchild});
    CHECK(parentLookups == authoredOrder.size());

    parents[childTwo] = rootOne;
    cache.Invalidate();
    CHECK(cache.Ordered(authoredOrder, parentOf) == std::vector{rootTwo, rootOne, childTwo, childOne, grandchild});
    CHECK(parentLookups == authoredOrder.size() * 2U);
    CHECK(cache.Children(rootTwo, authoredOrder, parentOf).empty());
    CHECK(std::ranges::equal(cache.Children(rootOne, authoredOrder, parentOf), std::vector{childTwo, childOne}));
}
