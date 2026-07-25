#include "KeireClient/Editor/PrefabAuthoring.h"

#include <doctest/doctest.h>

#include <array>

namespace
{
    [[nodiscard]] Keire::SceneObjectDefinition Object(const Keire::AssetId id, const char* name,
                                                      const Keire::AssetId parent = {})
    {
        return {id, parent, name, true, {}};
    }
} // namespace

TEST_CASE("Prefab authoring extracts selected roots and their descendants")
{
    const auto root = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000001");
    const auto child = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000002");
    const auto other = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000003");
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    scene.Objects = {Object(root, "Root"), Object(child, "Child", root), Object(other, "Other")};
    const std::array selection{root, child};

    const auto prefab = KeireEditor::CreatePrefabFromSelection(scene, selection, "Selection");
    REQUIRE(prefab.Template.Objects.size() == 2);
    CHECK(prefab.Template.Objects[0].Id == root);
    CHECK_FALSE(prefab.Template.Objects[0].Parent);
    CHECK(prefab.Template.Objects[1].Parent == root);
}

TEST_CASE("Prefab instantiation is transactional and records stable mappings")
{
    const auto sourceRoot = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000001");
    const auto sourceChild = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000002");
    const auto prefab = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000003");
    auto composed = Keire::SceneAsset::EmptyDefinition("Prefab");
    composed.Objects = {Object(sourceRoot, "Root"), Object(sourceChild, "Child", sourceRoot)};
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");

    const auto instance = KeireEditor::InstantiatePrefab(scene, prefab, composed);
    REQUIRE(scene.Objects.size() == 2);
    REQUIRE(scene.PrefabInstances.size() == 1);
    CHECK(instance.Root == scene.Objects.front().Id);
    CHECK(scene.Objects[1].Parent == scene.Objects[0].Id);
    CHECK(instance.Objects[0].Source == sourceRoot);
    CHECK(instance.Objects[1].Source == sourceChild);

    const auto beforeFailure = Keire::SceneAsset::Encode(scene);
    auto invalid = composed;
    invalid.Objects[1].Parent = Keire::AssetId::Generate();
    CHECK_THROWS_AS((void)KeireEditor::InstantiatePrefab(scene, prefab, invalid), std::invalid_argument);
    CHECK(Keire::SceneAsset::Encode(scene) == beforeFailure);

    CHECK(KeireEditor::UnpackPrefab(scene, instance.Root));
    CHECK(scene.PrefabInstances.empty());
    CHECK(scene.Objects.size() == 2);
    CHECK_FALSE(KeireEditor::UnpackPrefab(scene, instance.Root));
}

TEST_CASE("Prefab revert reconstructs inherited state and removes structural overrides transactionally")
{
    const auto sourceRoot = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000001");
    const auto sourceChild = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000002");
    const auto prefab = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000003");
    auto composed = Keire::SceneAsset::EmptyDefinition("Prefab");
    composed.Objects = {Object(sourceRoot, "Root"), Object(sourceChild, "Child", sourceRoot)};
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    const auto instance = KeireEditor::InstantiatePrefab(scene, prefab, composed);
    const auto instanceChild = instance.Objects[1].Instance;
    const auto added = Keire::AssetId::Parse("51000000-0000-4000-8000-000000000004");

    scene.Objects.front().Name = "Overridden";
    scene.Objects[1].Name = "Removed inherited placeholder";
    scene.Objects.push_back(Object(added, "Added", instance.Root));
    auto& changed = scene.PrefabInstances.front();
    changed.Overrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::RenameObject, .Object = sourceRoot, .Name = "Overridden"});
    changed.Overrides.push_back({.Kind = Keire::PrefabOverrideKind::RemoveObject, .Object = sourceChild});
    changed.Overrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::AddObject, .AddedObject = Object(added, "Added", instance.Root)});

    REQUIRE(KeireEditor::RevertPrefabInstance(scene, instance.Root, composed));
    REQUIRE(scene.PrefabInstances.size() == 1);
    CHECK(scene.PrefabInstances.front().Overrides.empty());
    REQUIRE(scene.Objects.size() == 2);
    CHECK(scene.Objects[0].Name == "Root");
    CHECK(scene.Objects[1].Name == "Child");
    CHECK(scene.Objects[1].Id == instanceChild);
    CHECK(scene.Objects[1].Parent == instance.Root);

    const auto before = Keire::SceneAsset::Encode(scene);
    auto invalid = composed;
    invalid.Objects[1].Parent = Keire::AssetId::Generate();
    CHECK_THROWS_AS((void)KeireEditor::RevertPrefabInstance(scene, instance.Root, invalid), std::invalid_argument);
    CHECK(Keire::SceneAsset::Encode(scene) == before);
}

TEST_CASE("Prefab variant editing regenerates overrides against its composed base")
{
    const auto baseId = Keire::AssetId::Parse("52000000-0000-4000-8000-000000000001");
    const auto root = Keire::AssetId::Parse("52000000-0000-4000-8000-000000000002");
    auto composedBase = Keire::SceneAsset::EmptyDefinition("Base");
    composedBase.Objects.push_back(Object(root, "Base Root"));

    Keire::PrefabDefinition variant;
    variant.BasePrefab = baseId;
    variant.Template = Keire::SceneAsset::EmptyDefinition("Variant");
    auto edited = composedBase;
    edited.Objects.front().Name = "Edited Variant Root";

    const auto updated = KeireEditor::UpdatePrefabFromEditingScene(variant, edited, &composedBase);
    REQUIRE(updated.Template.PrefabOverrides.size() == 1);
    CHECK(updated.Template.PrefabOverrides.front().Kind == Keire::PrefabOverrideKind::RenameObject);
    CHECK(updated.Template.PrefabOverrides.front().Object == root);
    CHECK(updated.Template.PrefabOverrides.front().Name == "Edited Variant Root");
}

TEST_CASE("Prefab instance apply preserves scene-owned root placement")
{
    const auto prefab = Keire::AssetId::Parse("53000000-0000-4000-8000-000000000001");
    const auto sourceRoot = Keire::AssetId::Parse("53000000-0000-4000-8000-000000000002");
    const auto instanceRoot = Keire::AssetId::Parse("53000000-0000-4000-8000-000000000003");
    Keire::PrefabDefinition source;
    source.Template = Keire::SceneAsset::EmptyDefinition("Source");
    source.Template.Objects.push_back(Object(sourceRoot, "Source Root"));

    auto instanceObject = Object(instanceRoot, "Applied Name");
    instanceObject.Transform.Position.X = 42.0F;
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    scene.Objects.push_back(instanceObject);
    scene.PrefabInstances.push_back({prefab, instanceRoot, {{sourceRoot, instanceRoot}}, {}});

    const auto updated = KeireEditor::ApplyPrefabInstanceToSource(scene, instanceRoot, source, source.Template);
    REQUIRE(updated.Prefab.Template.Objects.size() == 1);
    CHECK(updated.Prefab.Template.Objects.front().Name == "Applied Name");
    CHECK(updated.Prefab.Template.Objects.front().Transform == source.Template.Objects.front().Transform);
    REQUIRE(updated.Scene.PrefabInstances.size() == 1);
    CHECK(updated.Scene.PrefabInstances.front().Overrides.empty());
}

TEST_CASE("Direct nested prefab source editing rejects ownership flattening")
{
    const auto nested = Keire::AssetId::Parse("54000000-0000-4000-8000-000000000001");
    const auto root = Keire::AssetId::Parse("54000000-0000-4000-8000-000000000002");
    Keire::PrefabDefinition source;
    source.Template = Keire::SceneAsset::EmptyDefinition("Nested Owner");
    source.Template.Objects.push_back(Object(root, "Nested Root"));
    source.Template.PrefabInstances.push_back({nested, root, {{root, root}}, {}});
    CHECK_THROWS_AS((void)KeireEditor::UpdatePrefabFromEditingScene(source, source.Template), std::invalid_argument);
}
