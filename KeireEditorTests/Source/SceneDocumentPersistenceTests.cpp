#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/DocumentSourcePersistence.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    struct PersistenceFixture final
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-ScenePersistence-" + Keire::AssetId::Generate().ToString());
        std::filesystem::path Source = Root / "Scene.keirescene";
        std::filesystem::path Recovery = Root / "Scene.recovery";
        Keire::AssetId Asset = Keire::AssetId::Generate();
        KeireEditor::SceneDocument Document;

        PersistenceFixture()
        {
            const auto bytes = Encode("Initial");
            Keire::Detail::WriteFileAtomically(Source, bytes);
            auto scene = Keire::CreateRef<Keire::Scene>(Asset, Keire::SceneAsset::Decode(bytes)->Definition());
            Document.Open(std::move(scene), Asset, Source, {}, bytes);
            Document.SetRecoveryPath(Recovery);
            (void)Document.CreateEntity("Local edit");
            REQUIRE(Document.WriteRecovery());
        }

        ~PersistenceFixture()
        {
            Document.Close();
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        static std::vector<std::byte> Encode(const std::string& name)
        {
            return Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition(name));
        }

        void CheckPreserved()
        {
            CHECK(Document.Dirty());
            CHECK(std::filesystem::is_regular_file(Recovery));
            CHECK(Document.EditingScene()->Entities().size() == 1);
        }
    };
} // namespace

TEST_CASE("Transform restoration preserves unit rotations and rejects invalid state atomically")
{
    const auto registration = Keire::CreateTransformComponentRegistration();
    Keire::TransformComponent transform;
    for (int angle = -180; angle <= 180; angle += 15)
    {
        transform.SetLocalEulerAngles({static_cast<float>(angle), 37.0F, -71.0F});
        const auto rotation = transform.LocalRotation();
        auto values = registration.Serialize(transform);
        for (int cycle = 0; cycle < 32; ++cycle)
        {
            registration.Deserialize(transform, values, 1);
            CHECK(transform.LocalRotation() == rotation);
            values = registration.Serialize(transform);
        }
        const Keire::Quaternion opposite{-rotation.X, -rotation.Y, -rotation.Z, -rotation.W};
        values["rotation"] = opposite;
        registration.Deserialize(transform, values, 1);
        CHECK(transform.LocalRotation() == opposite);
    }
    auto values = registration.Serialize(transform);
    values["rotation"] = Keire::Quaternion{0.0F, 0.0F, 0.0F, 2.0F};
    registration.Deserialize(transform, values, 1);
    CHECK(transform.LocalRotation() == Keire::Quaternion{});
    const auto before = registration.Serialize(transform);
    for (const auto invalid : std::array{Keire::Quaternion{0.0F, 0.0F, 0.0F, 0.0F},
                                         Keire::Quaternion{std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 1.0F},
                                         Keire::Quaternion{0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F}})
    {
        values["position"] = Keire::Vector3{12.0F, 13.0F, 14.0F};
        values["rotation"] = invalid;
        CHECK_THROWS_AS(registration.Deserialize(transform, values, 1), std::invalid_argument);
        CHECK(registration.Serialize(transform) == before);
    }
}

TEST_CASE("Scene history round trips normalized rotations without manufacturing unsaved edits")
{
    PersistenceFixture fixture;
    const auto scene = fixture.Document.EditingScene();
    const auto entity = scene->Entities().front();
    const auto transform = entity.GetComponent<Keire::TransformComponent>();
    REQUIRE(transform);
    // This rounded authored light orientation alternates between adjacent unit float quaternions if normalized again.
    transform->SetLocalRotation({0.880651F, 0.113152F, -0.376104F, 0.264946F});
    const auto light = fixture.Document.AddComponent(entity.Id(), Keire::DirectionalLightComponent::StaticType());
    REQUIRE(light);
    fixture.Document.Save();
    const auto saved = scene->Snapshot();
    const auto savedBytes = Keire::SceneAsset::Encode(saved);
    const auto savedRotation = transform->LocalRotation();
    const auto registry = scene->Components();
    const auto restore = [&](const Keire::SceneDefinition& definition)
    {
        auto replacement = Keire::CreateRef<Keire::Scene>(fixture.Asset, definition, registry);
        replacement->MarkDirty();
        fixture.Document.ReplaceEditingScene(std::move(replacement), true);
        fixture.Document.RefreshDirtyState();
    };
    for (int cycle = 0; cycle < 32; ++cycle)
    {
        INFO("History round trip: " << cycle);
        const auto before = fixture.Document.EditingScene()->Snapshot();
        const std::array selected{entity.Id().Value()};
        fixture.Document.SetComponentsEnabled(selected, light->Type(), false);
        const auto after = fixture.Document.EditingScene()->Snapshot();
        fixture.Document.RefreshDirtyState();
        CHECK(fixture.Document.Dirty());
        restore(before);
        const auto current = fixture.Document.EditingScene();
        CHECK_FALSE(fixture.Document.Dirty());
        CHECK(Keire::SceneAsset::Encode(current->Snapshot()) == savedBytes);
        CHECK(current->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalRotation() ==
              savedRotation);
        CHECK(current->FindEntity(entity.Id()).GetComponent(light->Type())->Enabled());
        restore(after);
        CHECK(fixture.Document.Dirty());
        CHECK_FALSE(fixture.Document.EditingScene()->FindEntity(entity.Id()).GetComponent(light->Type())->Enabled());
        restore(before);
    }
}

TEST_CASE("Scene persistence rejects changed source and preserves dirty state and recovery")
{
    PersistenceFixture fixture;
    const auto external = PersistenceFixture::Encode("External");
    Keire::Detail::WriteFileAtomically(fixture.Source, external);
    CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
    CHECK(fixture.Document.SourceConflict());
    fixture.CheckPreserved();
    CHECK(KeireEditor::Detail::ReadSceneBytes(fixture.Source) == external);

    fixture.Document.DismissSourceConflict();
    CHECK_FALSE(fixture.Document.SourceConflict());
    CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
    fixture.Document.Save(true);
    CHECK_FALSE(fixture.Document.Dirty());
    CHECK_FALSE(fixture.Document.SourceConflict());
    CHECK_FALSE(std::filesystem::exists(fixture.Recovery));
    (void)fixture.Document.CreateEntity("Next edit");
    CHECK_NOTHROW(fixture.Document.Save());
}

TEST_CASE("Scene persistence detects external deletion and allows explicit recreation")
{
    PersistenceFixture fixture;
    REQUIRE(std::filesystem::remove(fixture.Source));
    CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
    fixture.CheckPreserved();
    CHECK_FALSE(std::filesystem::exists(fixture.Source));
    CHECK_THROWS(fixture.Document.ReloadSource());
    fixture.CheckPreserved();
    fixture.Document.Save(true);
    CHECK(std::filesystem::is_regular_file(fixture.Source));
    CHECK_FALSE(fixture.Document.Dirty());
}

TEST_CASE("Scene history dirty state follows saved content across replacement and later saves")
{
    PersistenceFixture fixture;
    fixture.Document.Save();
    const auto initial = fixture.Document.EditingScene()->Snapshot();
    const auto restore = [&](const Keire::SceneDefinition& definition)
    {
        auto scene = Keire::CreateRef<Keire::Scene>(fixture.Asset, definition);
        scene->MarkDirty();
        fixture.Document.ReplaceEditingScene(std::move(scene));
        fixture.Document.RefreshDirtyState();
    };
    (void)fixture.Document.CreateEntity("Added component target");
    const auto changed = fixture.Document.EditingScene()->Snapshot();
    fixture.Document.RefreshDirtyState();
    CHECK(fixture.Document.Dirty());
    REQUIRE(fixture.Document.WriteRecovery());
    restore(initial);
    CHECK_FALSE(fixture.Document.Dirty());
    CHECK_FALSE(std::filesystem::exists(fixture.Recovery));
    restore(changed);
    CHECK(fixture.Document.Dirty());
    fixture.Document.Save();
    restore(initial);
    CHECK(fixture.Document.Dirty());
    restore(changed);
    CHECK_FALSE(fixture.Document.Dirty());

    SUBCASE("failed save does not replace the saved content")
    {
        restore(initial);
        Keire::Detail::WriteFileAtomically(fixture.Source, PersistenceFixture::Encode("External"));
        CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
        fixture.Document.RefreshDirtyState();
        CHECK(fixture.Document.Dirty());
        restore(changed);
        CHECK_FALSE(fixture.Document.Dirty());
        CHECK(fixture.Document.SourceConflict());
    }
    SUBCASE("prefab overrides remain authored changes even when objects match")
    {
        auto prefabChanged = changed;
        prefabChanged.PrefabOverrides.push_back({.Kind = Keire::PrefabOverrideKind::RenameObject,
                                                 .Object = changed.Objects.front().Id,
                                                 .Name = "Override"});
        restore(prefabChanged);
        CHECK(fixture.Document.Dirty());
        restore(changed);
        CHECK_FALSE(fixture.Document.Dirty());
    }
    SUBCASE("baked lighting identity remains an authored change")
    {
        auto lightingChanged = changed;
        lightingChanged.BakedLighting = Keire::AssetId::Generate();
        restore(lightingChanged);
        CHECK(fixture.Document.Dirty());
    }
    SUBCASE("unresolved recovery is never discarded by history reconciliation")
    {
        Keire::Detail::WriteFileAtomically(fixture.Recovery, PersistenceFixture::Encode("Unresolved"));
        fixture.Document.SetRecoveryAvailable(true);
        restore(changed);
        CHECK_FALSE(fixture.Document.Dirty());
        CHECK(fixture.Document.RecoveryAvailable());
        CHECK(std::filesystem::exists(fixture.Recovery));
    }
}

TEST_CASE("Scene history preserves unsaved documents and isolates Play from the edit savepoint")
{
    PersistenceFixture fixture;
    fixture.Document.RefreshDirtyState();
    CHECK(fixture.Document.Dirty());
    fixture.Document.Save();
    fixture.Document.BeginPlay();
    (void)fixture.Document.CreateEntity("Runtime only");
    fixture.Document.RefreshDirtyState();
    CHECK_FALSE(fixture.Document.Dirty());
    fixture.Document.EndPlay();
    fixture.Document.RefreshDirtyState();
    CHECK_FALSE(fixture.Document.Dirty());
    fixture.Document.Close();
    CHECK_NOTHROW(fixture.Document.RefreshDirtyState());

    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Unsaved"));
    scene->MarkDirty();
    fixture.Document.Open(std::move(scene));
    fixture.Document.RefreshDirtyState();
    CHECK(fixture.Document.Dirty());
}

TEST_CASE("Scene persistence retains the exact revision decoded before open")
{
    PersistenceFixture fixture;
    const auto loaded = KeireEditor::Detail::ReadSceneBytes(fixture.Source);
    auto scene = Keire::CreateRef<Keire::Scene>(fixture.Asset, Keire::SceneAsset::Decode(loaded)->Definition());
    Keire::Detail::WriteFileAtomically(fixture.Source, PersistenceFixture::Encode("Changed during open"));
    fixture.Document.Open(std::move(scene), fixture.Asset, fixture.Source, {}, loaded);
    (void)fixture.Document.CreateEntity("Edit after open");
    CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
    CHECK(fixture.Document.Dirty());
}

TEST_CASE("Scene persistence reload prepares a complete replacement before discarding local state")
{
    PersistenceFixture fixture;
    const auto before = fixture.Document.EditingScene();
    const auto generation = fixture.Document.EditGeneration();
    Keire::Detail::WriteTextFileAtomically(fixture.Source, "invalid scene");
    CHECK_THROWS(fixture.Document.ReloadSource());
    CHECK(fixture.Document.EditingScene() == before);
    CHECK(fixture.Document.EditGeneration() == generation);
    fixture.CheckPreserved();

    Keire::Detail::WriteFileAtomically(fixture.Source, PersistenceFixture::Encode("Reloaded"));
    fixture.Document.ReloadSource();
    CHECK(fixture.Document.EditingScene() != before);
    CHECK(fixture.Document.EditGeneration() != generation);
    CHECK(fixture.Document.EditingScene()->Name() == "Reloaded");
    CHECK_FALSE(fixture.Document.Dirty());
    CHECK_FALSE(std::filesystem::exists(fixture.Recovery));
    (void)fixture.Document.CreateEntity("After reload");
    CHECK_NOTHROW(fixture.Document.Save());
}

TEST_CASE("Scene persistence failed publication does not accept a new baseline")
{
    PersistenceFixture fixture;
    const auto original = KeireEditor::Detail::ReadSceneBytes(fixture.Source);
    REQUIRE(std::filesystem::remove(fixture.Source));
    REQUIRE(std::filesystem::create_directory(fixture.Source));
    CHECK_THROWS(fixture.Document.Save(true));
    fixture.CheckPreserved();
    REQUIRE(std::filesystem::remove(fixture.Source));
    Keire::Detail::WriteFileAtomically(fixture.Source, original);
    CHECK_NOTHROW(fixture.Document.Save());
    fixture.Document.Close();
    CHECK_THROWS_AS(fixture.Document.Save(), std::logic_error);
    CHECK_THROWS_AS(fixture.Document.ReloadSource(), std::logic_error);
}

TEST_CASE("Scene persistence rejects collisions for a source that was initially absent")
{
    PersistenceFixture fixture;
    KeireEditor::DocumentSourcePersistence persistence;
    const auto destination = fixture.Root / "New.keirescene";
    persistence.Bind(destination);
    const auto external = PersistenceFixture::Encode("Created externally");
    Keire::Detail::WriteFileAtomically(destination, external);
    CHECK_THROWS_AS(persistence.Publish(PersistenceFixture::Encode("Local")), KeireEditor::DocumentSourceConflict);
    CHECK(KeireEditor::Detail::ReadSceneBytes(destination) == external);
}

TEST_CASE("Scene persistence relocation preserves the loaded revision")
{
    PersistenceFixture fixture;
    const auto destination = fixture.Root / "Renamed.keirescene";
    std::filesystem::rename(fixture.Source, destination);
    Keire::Detail::WriteFileAtomically(destination, PersistenceFixture::Encode("External rename edit"));
    fixture.Document.SetIdentity(fixture.Asset, destination);
    CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
    fixture.CheckPreserved();
}

TEST_CASE("Scene persistence source rename advances the baseline without losing local edits")
{
    PersistenceFixture fixture;
    fixture.Document.Save();
    const auto destination = fixture.Root / "Renamed.keirescene";
    std::filesystem::rename(fixture.Source, destination);
    fixture.Document.SetIdentity(fixture.Asset, destination);

    SUBCASE("clean rename and subsequent edit save")
    {
        fixture.Document.SynchronizeSourceName("Renamed");
        CHECK_FALSE(fixture.Document.Dirty());
        CHECK(Keire::SceneAsset::Decode(KeireEditor::Detail::ReadSceneBytes(destination))->Definition().Name ==
              "Renamed");
        (void)fixture.Document.CreateEntity("After rename");
        CHECK_NOTHROW(fixture.Document.Save());
        CHECK_FALSE(fixture.Document.SourceConflict());
        CHECK(fixture.Document.EditingScene()->Entities().size() == 2);
        CHECK_FALSE(std::filesystem::exists(fixture.Source));
    }
    SUBCASE("dirty rename does not silently save edits")
    {
        const auto original = KeireEditor::Detail::ReadSceneBytes(destination);
        (void)fixture.Document.CreateEntity("Unsaved");
        fixture.Document.SynchronizeSourceName("Renamed");
        CHECK(fixture.Document.Dirty());
        CHECK(KeireEditor::Detail::ReadSceneBytes(destination) == original);
        CHECK_NOTHROW(fixture.Document.Save());
    }
    SUBCASE("external edits still conflict during automatic rename save")
    {
        const auto external = PersistenceFixture::Encode("External");
        Keire::Detail::WriteFileAtomically(destination, external);
        CHECK_THROWS_AS(fixture.Document.SynchronizeSourceName("Renamed"), KeireEditor::DocumentSourceConflict);
        CHECK(fixture.Document.Dirty());
        CHECK(fixture.Document.SourceConflict());
        CHECK(KeireEditor::Detail::ReadSceneBytes(destination) == external);
    }
}

TEST_CASE("Scene persistence rejected history reset leaves the current scene and recovery intact")
{
    PersistenceFixture fixture;
    const auto undo = Keire::CreateRef<Keire::UndoService>();
    const auto history = undo->CreateContext({.Name = "Scene persistence"});
    fixture.Document.SetUndoContext(history);
    const auto scene = fixture.Document.EditingScene();
    Keire::Detail::WriteFileAtomically(fixture.Source, PersistenceFixture::Encode("External"));
    {
        auto transaction = history->BeginTransaction("Pending edit");
        CHECK_THROWS_AS(fixture.Document.ReloadSource(), std::logic_error);
        CHECK(fixture.Document.EditingScene() == scene);
        CHECK_THROWS_AS(fixture.Document.Save(), KeireEditor::DocumentSourceConflict);
        fixture.CheckPreserved();
    }
    CHECK_NOTHROW(fixture.Document.ReloadSource());
    fixture.Document.Close();
    history->Close();
    undo->Close();
}

TEST_CASE("Document persistence protects input actions from changed and deleted sources")
{
    PersistenceFixture fixture;
    const auto path = fixture.Root / "Actions.keireinput";
    const auto original = Keire::InputActionAsset::DefaultDefinition();
    const auto bytes = Keire::InputActionAsset::Encode(original);
    Keire::Detail::WriteFileAtomically(path, bytes);
    KeireEditor::InputActionsDocument document;
    document.Open(Keire::AssetId::Generate(), original, {}, path, bytes);
    auto local = original;
    local.Name = "Local actions";
    document.ReplaceDefinition(local);
    SUBCASE("External edit")
    {
        auto external = original;
        external.Name = "External actions";
        const auto changed = Keire::InputActionAsset::Encode(external);
        Keire::Detail::WriteFileAtomically(path, changed);
        CHECK_THROWS_AS(document.Save(), KeireEditor::DocumentSourceConflict);
        CHECK(KeireEditor::Detail::ReadBytes(path) == changed);
    }
    SUBCASE("External deletion")
    {
        REQUIRE(std::filesystem::remove(path));
        CHECK_THROWS_AS(document.Save(), KeireEditor::DocumentSourceConflict);
        CHECK_FALSE(std::filesystem::exists(path));
    }
    CHECK(document.Dirty());
    CHECK(document.Definition().Name == "Local actions");
    Keire::Detail::WriteFileAtomically(path, bytes);
    CHECK_NOTHROW(document.Save());
    CHECK_FALSE(document.Dirty());
    document.Close();
}

TEST_CASE("Document persistence protects animator controller sources")
{
    PersistenceFixture fixture;
    const auto path = fixture.Root / "Controller.keireanimgraph";
    Keire::AnimationGraphDefinition original;
    Keire::AnimationLayerDefinition layer;
    layer.Id = "base";
    layer.Name = "Base";
    layer.EntryStateId = "idle";
    Keire::AnimationStateDefinition state;
    state.Id = "idle";
    state.Name = "Idle";
    state.Clip = Keire::AssetId::Generate();
    state.Motion.Clip = state.Clip;
    layer.States.push_back(state);
    original.Layers.push_back(layer);
    const auto bytes = Keire::AnimationGraphAsset::Encode(original);
    Keire::Detail::WriteFileAtomically(path, bytes);
    KeireEditor::AnimatorControllerDocument document;
    document.Open(Keire::AssetId::Generate(), original, {}, path, bytes);
    auto local = original;
    local.Layers.front().Name = "Local layer";
    document.ReplaceDefinition(local);
    REQUIRE(std::filesystem::remove(path));
    CHECK_THROWS_AS(document.Save(), KeireEditor::DocumentSourceConflict);
    CHECK(document.Dirty());
    CHECK(document.Definition().Layers.front().Name == "Local layer");
    CHECK_FALSE(std::filesystem::exists(path));
    Keire::Detail::WriteFileAtomically(path, bytes);
    CHECK_NOTHROW(document.Save());
    CHECK_FALSE(document.Dirty());
    document.Close();
}

TEST_CASE("Document persistence protects UI trees and accepts an explicit reload baseline")
{
    PersistenceFixture fixture;
    const auto path = fixture.Root / "Hud.keireui";
    Keire::UiVisualTreeDefinition original;
    original.Name = "Hud";
    original.Root.StableId = Keire::AssetId::Generate();
    original.Root.Name = "root";
    Keire::Detail::WriteFileAtomically(path, Keire::UiVisualTreeAsset::EncodeSource(original));
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Generate(), original, 1, path);
    auto local = original;
    local.Name = "Local Hud";
    REQUIRE(document.Edit("Rename", local));
    auto external = original;
    external.Name = "External Hud";
    const auto changed = Keire::UiVisualTreeAsset::EncodeSource(external);
    Keire::Detail::WriteFileAtomically(path, changed);
    CHECK_THROWS_AS(document.Save(), KeireEditor::DocumentSourceConflict);
    CHECK(document.Dirty());
    CHECK(KeireEditor::Detail::ReadBytes(path) == changed);
    CHECK_THROWS(document.ReloadFromSource());
    CHECK(document.Dirty());
    document.ReloadFromSource(true);
    CHECK_FALSE(document.Dirty());
    CHECK(document.Definition().Name == "External Hud");
    REQUIRE(document.Edit("Rename again", local));
    CHECK_NOTHROW(document.Save());
    document.Close();
}

TEST_CASE("Document persistence protects deleted UI style sources and leaves Save As available")
{
    PersistenceFixture fixture;
    const auto path = fixture.Root / "Hud.keirestyle";
    const std::string original = "@keire-style 1;\nLabel { color: #ffffffff; }\n";
    Keire::Detail::WriteTextFileAtomically(path, original);
    const auto definition = Keire::UiStyleSheetAsset::ParseSource(std::as_bytes(std::span(original)));
    KeireEditor::UiBuilderStyleSheetDocument document;
    document.Open(Keire::AssetId::Generate(), definition, 1, path);
    REQUIRE(document.ApplySourceDraft("@keire-style 1;\nLabel { color: #ff0000ff; }\n"));
    REQUIRE(std::filesystem::remove(path));
    CHECK(document.ExternalConflict());
    CHECK_THROWS(document.Save());
    CHECK(document.Dirty());
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK_NOTHROW(document.SaveAs(fixture.Root / "Copy.keirestyle"));
    CHECK(document.Dirty());
    CHECK(std::filesystem::is_regular_file(fixture.Root / "Copy.keirestyle"));
    document.Close();
}
