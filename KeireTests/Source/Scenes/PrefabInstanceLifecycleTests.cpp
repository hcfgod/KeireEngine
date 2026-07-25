#include <Keire/Scenes/Scene.h>
#include <Keire/Scenes/SceneAsset.h>

#include <doctest/doctest.h>

namespace
{
    Keire::SceneDefinition MakePrefabInstanceScene(const Keire::AssetId root, const Keire::AssetId child)
    {
        const auto sourceRoot = Keire::AssetId::Generate();
        const auto sourceChild = Keire::AssetId::Generate();
        Keire::SceneDefinition definition = Keire::SceneAsset::EmptyDefinition("Prefab Lifecycle");
        definition.Objects.push_back({.Id = root, .Name = "Root"});
        definition.Objects.push_back({.Id = child, .Parent = root, .Name = "Child"});
        definition.PrefabInstances.push_back(
            {.Prefab = Keire::AssetId::Generate(),
             .Root = root,
             .Objects = {{.Source = sourceRoot, .Instance = root}, {.Source = sourceChild, .Instance = child}},
             .Overrides = {
                 {.Kind = Keire::PrefabOverrideKind::RenameObject, .Object = sourceChild, .Name = "Child Override"}}});
        return definition;
    }
} // namespace

TEST_CASE("Destroying a prefab instance root removes its authored instance metadata")
{
    const auto root = Keire::AssetId::Generate();
    const auto child = Keire::AssetId::Generate();
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), MakePrefabInstanceScene(root, child));

    REQUIRE(scene->DestroyEntity(Keire::EntityId(root)));
    const auto snapshot = scene->Snapshot();
    CHECK(snapshot.Objects.empty());
    CHECK(snapshot.PrefabInstances.empty());
    CHECK_NOTHROW(Keire::SceneAsset::Validate(snapshot));
}

TEST_CASE("Destroying a mapped prefab descendant removes stale mappings and overrides")
{
    const auto root = Keire::AssetId::Generate();
    const auto child = Keire::AssetId::Generate();
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), MakePrefabInstanceScene(root, child));

    REQUIRE(scene->DestroyEntity(Keire::EntityId(child)));
    const auto snapshot = scene->Snapshot();
    REQUIRE(snapshot.PrefabInstances.size() == 1);
    CHECK(snapshot.PrefabInstances.front().Root == root);
    CHECK(snapshot.PrefabInstances.front().Objects.size() == 1);
    CHECK(snapshot.PrefabInstances.front().Overrides.empty());
    CHECK_NOTHROW(Keire::SceneAsset::Validate(snapshot));
}
