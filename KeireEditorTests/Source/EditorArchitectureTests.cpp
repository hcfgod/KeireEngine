#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <doctest/doctest.h>

#include <stdexcept>

TEST_CASE("editor command router centralizes availability and execution")
{
    KeireEditor::EditorCommandRouter router;
    bool available = false;
    unsigned int executions = 0;
    router.Bind(
        KeireEditor::EditorCommand::SaveScene, [&executions] { ++executions; }, [&available] { return available; });

    CHECK_FALSE(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 0);
    available = true;
    CHECK(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 1);
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::Exit));
    CHECK_THROWS_AS(router.Bind(KeireEditor::EditorCommand::Exit, {}), std::invalid_argument);
}

TEST_CASE("scene document owns selection and deterministic close state")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000001"),
                                                Keire::SceneAsset::EmptyDefinition("Document test"));
    auto entity = scene->CreateEntity("Selected");
    document.SceneStorage() = scene;
    document.AssetStorage() = scene->Asset();
    document.Select(entity.Id().Value());
    CHECK(document.Selection() == entity.Id().Value());
    document.Select(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000099"));
    CHECK_FALSE(document.Selection());
    document.RecoveryAvailableStorage() = true;

    document.Close();
    CHECK_FALSE(document.Scene());
    CHECK_FALSE(document.Asset());
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK_FALSE(scene->IsOpen());
}

TEST_CASE("input actions document owns authoring state and dirty lifecycle")
{
    KeireEditor::InputActionsDocument document;
    document.AssetStorage() = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000002");
    document.DefinitionStorage() = Keire::InputActionAsset::DefaultDefinition();
    document.MarkDirty();
    CHECK(document.Dirty());
    CHECK(document.Definition().ActionMaps.size() > 0);
    document.MarkSaved();
    CHECK_FALSE(document.Dirty());
    document.Close();
    CHECK_FALSE(document.Asset());
    CHECK(document.Definition().ActionMaps.empty());
}
