#include "KeireClient/Editor/SceneDocument.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <system_error>

namespace
{
    struct LightingTestDirectory
    {
        std::filesystem::path Path =
            std::filesystem::temp_directory_path() / ("Keire-LightingUndo-" + Keire::AssetId::Generate().ToString());

        ~LightingTestDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    };
} // namespace

TEST_CASE("lighting settings undo retains earlier history and supports redo")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting"});
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Lighting"));
    KeireEditor::SceneDocument document;
    document.Open(scene, {}, {}, history);
    int earlierValue = 0;
    history->Execute(Keire::CreateUndoCommand("Earlier edit", [&] { earlierValue = 1; }, [&] { earlierValue = 0; }));
    const auto original = scene->LightingBakeConfiguration();
    auto changed = original;
    changed.Quality = Keire::LightingBakeQuality::Production;
    document.SetLightingBakeConfiguration(changed);
    CHECK(scene->LightingBakeConfiguration() == changed);
    CHECK(history->UndoCount() == 2);
    document.SetLightingBakeConfiguration(changed);
    CHECK(history->UndoCount() == 2);
    REQUIRE(history->Undo());
    CHECK(scene->LightingBakeConfiguration() == original);
    CHECK(earlierValue == 1);
    REQUIRE(history->Redo());
    CHECK(scene->LightingBakeConfiguration() == changed);
    REQUIRE(history->Undo());
    REQUIRE(history->Undo());
    CHECK(earlierValue == 0);
    document.Close();
    service->Close();
}

TEST_CASE("lighting settings history follows snapshot replacement without changing the obsolete scene")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting"});
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Original"));
    KeireEditor::SceneDocument document;
    document.Open(scene, {}, {}, history);
    const auto original = scene->LightingBakeConfiguration();
    auto changed = original;
    changed.Quality = Keire::LightingBakeQuality::Production;
    document.SetLightingBakeConfiguration(changed);
    auto replacement = Keire::CreateRef<Keire::Scene>(scene->Asset(), scene->Snapshot());
    document.ReplaceEditingScene(replacement);
    REQUIRE(history->Undo());
    CHECK(replacement->LightingBakeConfiguration() == original);
    CHECK(scene->LightingBakeConfiguration() == changed);
    scene.Reset();
    REQUIRE(history->Redo());
    CHECK(replacement->LightingBakeConfiguration() == changed);
    document.Close();
    service->Close();
}

TEST_CASE("lighting commands expire when the document closes reopens or is destroyed")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting lifetime"});
    auto first =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("First"));
    auto second =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Second"));
    {
        KeireEditor::SceneDocument document;
        document.Open(first, {}, {}, history);
        auto changed = first->LightingBakeConfiguration();
        changed.Quality = Keire::LightingBakeQuality::Production;
        document.SetLightingBakeConfiguration(changed);
        const auto secondOriginal = second->LightingBakeConfiguration();
        document.Open(second, {}, {}, history);
        CHECK_FALSE(history->Undo());
        CHECK(second->LightingBakeConfiguration() == secondOriginal);
        document.SetLightingBakeConfiguration(changed);
    }
    const auto beforeUndo = second->LightingBakeConfiguration();
    CHECK_FALSE(history->Undo());
    CHECK(second->LightingBakeConfiguration() == beforeUndo);
    second->Close();
    service->Close();
}

TEST_CASE("lighting settings reject Play edits and retain Edit history after Play")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting Play"});
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Play"));
    KeireEditor::SceneDocument document;
    document.Open(scene, {}, {}, history);
    const auto original = scene->LightingBakeConfiguration();
    auto changed = original;
    changed.Quality = Keire::LightingBakeQuality::Production;
    document.SetLightingBakeConfiguration(changed);
    document.BeginPlay();
    CHECK_THROWS(document.SetLightingBakeConfiguration(original));
    CHECK_FALSE(history->Undo());
    CHECK(scene->LightingBakeConfiguration() == changed);
    CHECK(history->UndoCount() == 1);
    document.EndPlay();
    REQUIRE(history->Undo());
    CHECK(scene->LightingBakeConfiguration() == original);
    document.Close();
    service->Close();
}

TEST_CASE("lighting source reload clears old commands and restores saved settings")
{
    LightingTestDirectory temporary;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting reload"});
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Reload"));
    KeireEditor::SceneDocument document;
    document.Open(scene, {}, temporary.Path / "Reload.keirescene", history);
    document.Save();
    const auto original = scene->LightingBakeConfiguration();
    auto changed = original;
    changed.Quality = Keire::LightingBakeQuality::Production;
    document.SetLightingBakeConfiguration(changed);
    document.ReloadSource();
    CHECK(document.EditingScene()->LightingBakeConfiguration() == original);
    CHECK(scene->LightingBakeConfiguration() == changed);
    CHECK_FALSE(history->Undo());
    CHECK_FALSE(history->Redo());
    document.SetLightingBakeConfiguration(changed);
    REQUIRE(history->Undo());
    CHECK(document.EditingScene()->LightingBakeConfiguration() == original);
    document.Close();
    service->Close();
}

TEST_CASE("invalid lighting settings preserve the scene and undo history")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting validation"});
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Validation"));
    KeireEditor::SceneDocument document;
    document.Open(scene, {}, {}, history);
    const auto original = scene->LightingBakeConfiguration();
    auto invalid = original;
    invalid.LightmapResolution = 0;
    CHECK_THROWS(document.SetLightingBakeConfiguration(invalid));
    CHECK(scene->LightingBakeConfiguration() == original);
    CHECK(history->UndoCount() == 0);
    document.Close();
    CHECK_THROWS(document.SetLightingBakeConfiguration(original));
    service->Close();
}
