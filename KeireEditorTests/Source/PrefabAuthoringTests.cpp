#include "KeireClient/Editor/PrefabAuthoring.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <stdexcept>

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

TEST_CASE("prefab placement includes origin persists as an override and preserves root spacing")
{
    const auto root = Keire::AssetId::Generate();
    const auto other = Keire::AssetId::Generate();
    const auto child = Keire::AssetId::Generate();
    auto composed = Keire::SceneAsset::EmptyDefinition("Prefab");
    composed.Objects = {Object(root, "Root"), Object(other, "Other"), Object(child, "Child", root)};
    composed.Objects[0].Transform.Position = {12.0F, 4.0F, 8.0F};
    composed.Objects[1].Transform.Position = {15.0F, 4.0F, 8.0F};
    composed.Objects[2].Transform.Position = {1.0F, 2.0F, 3.0F};
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    const auto instance =
        KeireEditor::InstantiatePrefab(scene, Keire::AssetId::Generate(), composed, {}, Keire::Vector3{});
    CHECK(scene.Objects[0].Transform.Position == Keire::Vector3{});
    CHECK(scene.Objects[1].Transform.Position == Keire::Vector3{3.0F, 0.0F, 0.0F});
    CHECK(scene.Objects[2].Transform.Position == Keire::Vector3{1.0F, 2.0F, 3.0F});
    REQUIRE(instance.Overrides.size() == 2);
    CHECK(instance.Overrides.front().Kind == Keire::PrefabOverrideKind::SetObjectTransform);
    CHECK(instance.Overrides.front().Transform.Position == Keire::Vector3{});
    const auto decoded = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(scene));
    CHECK(decoded->Definition().PrefabInstances.front().Overrides.size() == 2);
}

TEST_CASE("Prefab review derives live appearance changes instead of stale override metadata")
{
    Keire::PrefabDefinition source;
    source.Template = Keire::SceneAsset::EmptyDefinition("Dash Block");
    const auto sourceRoot = Keire::AssetId::Generate();
    source.Template.Objects.push_back(Object(sourceRoot, "Jump Block"));
    const auto mesh = Keire::MeshRendererComponent::StaticType();
    source.Template.Objects.front().Components.push_back({mesh, 1, true, R"({"tint":[1,1,1,1],"visible":true})"});
    auto scene = Keire::SceneAsset::EmptyDefinition("Course");
    const auto instance = KeireEditor::InstantiatePrefab(scene, Keire::AssetId::Generate(), source.Template);
    scene.Objects.front().Transform.Position.X = -4.0F;
    scene.Objects.front().Components.front().Data = R"({"tint":[0.25,1,1,1],"visible":true})";
    REQUIRE(scene.PrefabInstances.front().Overrides.empty());
    const auto beforeReview = Keire::SceneAsset::Encode(scene);

    const auto review = KeireEditor::ReviewPrefabInstance(scene, instance.Root, source.Template);
    REQUIRE(review.size() == 1);
    CHECK(review.front().ObjectName == "Jump Block");
    CHECK(review.front().Component == mesh);
    CHECK(review.front().Property == "tint");
    CHECK(review.front().Before == "[1,1,1,1]");
    CHECK(review.front().After == "[0.25,1,1,1]");
    CHECK(Keire::SceneAsset::Encode(scene) == beforeReview);

    const auto applied = KeireEditor::ApplyPrefabInstanceToSource(scene, instance.Root, source, source.Template);
    CHECK(applied.Prefab.Template.Objects.front().Components.front().Data ==
          scene.Objects.front().Components.front().Data);
    CHECK(KeireEditor::ReviewPrefabInstance(applied.Scene, instance.Root, applied.Prefab.Template).empty());
    CHECK(applied.Scene.Objects.front().Transform.Position.X == -4.0F);
}

TEST_CASE("Prefab review describes structural and metadata edits and rejects incompatible sources without mutation")
{
    auto source = Keire::SceneAsset::EmptyDefinition("Prefab");
    const auto root = Keire::AssetId::Generate();
    const auto child = Keire::AssetId::Generate();
    source.Objects = {Object(root, "Root"), Object(child, "Child", root)};
    const auto mesh = Keire::MeshRendererComponent::StaticType();
    source.Objects.front().Components.push_back({mesh, 1, true, R"({"tint":[1,1,1,1]})"});
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    const auto instance = KeireEditor::InstantiatePrefab(scene, Keire::AssetId::Generate(), source);
    scene.Objects.front().Name = "Renamed";
    scene.Objects.front().Components.front().Enabled = false;
    scene.Objects.front().Components.front().SchemaVersion = 2;
    scene.Objects[1].Transform.Position.X = 3.0F;
    auto review = KeireEditor::ReviewPrefabInstance(scene, instance.Root, source);
    CHECK(review.size() == 4);
    CHECK(std::ranges::any_of(
        review, [](const auto& entry)
        { return entry.Property == "Name" && entry.Before == "\"Root\"" && entry.After == "\"Renamed\""; }));
    CHECK(std::ranges::any_of(review, [](const auto& entry)
                              { return entry.Property == "Position" && entry.ObjectName == "Child"; }));
    scene.Objects.front().Components.clear();
    scene.Objects.push_back(Object(Keire::AssetId::Generate(), "New child", instance.Root));
    review = KeireEditor::ReviewPrefabInstance(scene, instance.Root, source);
    CHECK(std::ranges::any_of(review, [](const auto& entry)
                              { return entry.Property == "Component" && entry.After == "Removed"; }));
    CHECK(std::ranges::any_of(review, [](const auto& entry)
                              { return entry.ObjectName == "New child" && entry.After == "Added"; }));

    // Runtime deletion removes the mapping as well as the object. Without a baseline, Apply must reject this
    // ambiguous state rather than mistake a newly added source object for an intentional scene deletion.
    auto missingMapping = scene;
    missingMapping.Objects.erase(missingMapping.Objects.begin() + 1);
    missingMapping.PrefabInstances.front().Objects.erase(missingMapping.PrefabInstances.front().Objects.begin() + 1);
    const auto beforeMissingMapping = Keire::SceneAsset::Encode(missingMapping);
    CHECK_THROWS_AS((void)KeireEditor::ReviewPrefabInstance(missingMapping, instance.Root, source),
                    std::invalid_argument);
    CHECK(Keire::SceneAsset::Encode(missingMapping) == beforeMissingMapping);

    const auto beforeFailure = Keire::SceneAsset::Encode(scene);
    source.Objects.push_back(Object(Keire::AssetId::Generate(), "New source child", root));
    CHECK_THROWS_AS((void)KeireEditor::ReviewPrefabInstance(scene, instance.Root, source), std::invalid_argument);
    CHECK(Keire::SceneAsset::Encode(scene) == beforeFailure);
}

TEST_CASE("Prefab revert retains every root placement and parent while restoring inherited child and appearance")
{
    auto source = Keire::SceneAsset::EmptyDefinition("Prefab");
    const auto root = Keire::AssetId::Generate();
    const auto other = Keire::AssetId::Generate();
    const auto child = Keire::AssetId::Generate();
    source.Objects = {Object(root, "Root"), Object(other, "Other root"), Object(child, "Child", root)};
    source.Objects.front().Transform.Position = {4.0F, 1.0F, 0.0F};
    const auto mesh = Keire::MeshRendererComponent::StaticType();
    source.Objects.front().Components.push_back({mesh, 1, true, R"({"tint":[1,1,1,1]})"});
    source = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(source))->Definition();
    auto scene = Keire::SceneAsset::EmptyDefinition("Scene");
    const auto parent = Keire::AssetId::Generate();
    scene.Objects.push_back(Object(parent, "Course"));
    const auto instance = KeireEditor::InstantiatePrefab(scene, Keire::AssetId::Generate(), source, parent);
    scene.Objects[1].Transform.Position = {-4.0F, 2.0F, 3.0F};
    scene.Objects[1].Transform.Scale = {2.0F, 3.0F, 4.0F};
    scene.Objects[1].Transform.Rotation = {0.0F, 1.0F, 0.0F, 0.0F};
    const auto instanceMesh =
        std::ranges::find(scene.Objects[1].Components, mesh, &Keire::SceneComponentDefinition::Type);
    REQUIRE(instanceMesh != scene.Objects[1].Components.end());
    instanceMesh->Data = R"({"tint":[0.25,1,1,1]})";
    scene.Objects[2].Transform.Position.X = 12.0F;
    scene.Objects[3].Transform.Position.X = 8.0F;
    // Recreate serialized Transform data from the typed fixture edits. Encode correctly treats an existing
    // component payload as authoritative, so leaving the original payload here would undo the test edits.
    for (auto& object : scene.Objects)
    {
        std::erase_if(object.Components,
                      [](const auto& component) { return component.Type == Keire::TransformComponent::StaticType(); });
    }
    scene = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(scene))->Definition();
    const auto placement = scene.Objects[1].Transform;
    REQUIRE(placement.Position == Keire::Vector3{-4.0F, 2.0F, 3.0F});
    REQUIRE(placement.Scale == Keire::Vector3{2.0F, 3.0F, 4.0F});
    REQUIRE(placement.Rotation == Keire::Quaternion{0.0F, 1.0F, 0.0F, 0.0F});
    REQUIRE(scene.Objects[2].Transform.Position.X == 12.0F);
    REQUIRE(scene.Objects[3].Transform.Position.X == 8.0F);
    REQUIRE(KeireEditor::RevertPrefabInstance(scene, instance.Root, source));
    const auto restored = Keire::SceneAsset::Decode(Keire::SceneAsset::Encode(scene))->Definition();
    REQUIRE(restored.Objects.size() == 4);
    CHECK(restored.Objects[1].Transform == placement);
    CHECK(restored.Objects[1].Parent == parent);
    CHECK(restored.Objects[2].Transform.Position.X == 12.0F);
    CHECK(restored.Objects[2].Parent == parent);
    CHECK(restored.Objects[3].Transform == source.Objects[2].Transform);
    const auto restoredMesh =
        std::ranges::find(restored.Objects[1].Components, mesh, &Keire::SceneComponentDefinition::Type);
    REQUIRE(restoredMesh != restored.Objects[1].Components.end());
    const auto sourceMesh =
        std::ranges::find(source.Objects[0].Components, mesh, &Keire::SceneComponentDefinition::Type);
    REQUIRE(sourceMesh != source.Objects[0].Components.end());
    CHECK(restoredMesh->Data == sourceMesh->Data);
}
