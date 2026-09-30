#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/DocumentSourcePersistence.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
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
