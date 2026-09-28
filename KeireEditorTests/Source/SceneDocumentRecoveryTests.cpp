#include "KeireClient/Editor/SceneDocument.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace
{
    struct RecoveryDirectory
    {
        std::filesystem::path Path =
            std::filesystem::temp_directory_path() / ("Keire-RecoveryTarget-" + Keire::AssetId::Generate().ToString());

        ~RecoveryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }
    };
} // namespace

TEST_CASE("Scene Save As recovery state follows the saved copy and preserves the original snapshot")
{
    RecoveryDirectory temporary;
    const auto originalRecovery = temporary.Path / "original.recovery";
    const auto copyRecovery = temporary.Path / "copy.recovery";
    const auto original = Keire::AssetId::Generate();
    const auto copy = Keire::AssetId::Generate();
    auto scene = Keire::CreateRef<Keire::Scene>(original, Keire::SceneAsset::EmptyDefinition("Original"));
    scene->MarkDirty();
    KeireEditor::SceneDocument document;
    document.Open(scene, original, temporary.Path / "Original.keirescene");
    document.SetRecoveryPath(originalRecovery);
    REQUIRE(document.WriteRecovery());
    REQUIRE_FALSE(document.RecoveryAvailable());
    document.AdvanceRecovery(12.0);

    auto saved = Keire::CreateRef<Keire::Scene>(copy, scene->Snapshot());
    saved->MarkSaved();
    document.ReplaceEditingScene(saved, false);
    document.SetIdentity(copy, temporary.Path / "Copy.keirescene");
    document.SetRecoveryPath(copyRecovery);

    CHECK_FALSE(document.RecoveryAvailable());
    CHECK(document.RecoverySeconds() == 0.0);
    CHECK_FALSE(document.Dirty());
    CHECK(document.Asset() == copy);
    CHECK(std::filesystem::is_regular_file(originalRecovery));
    CHECK_FALSE(std::filesystem::exists(copyRecovery));
    CHECK_THROWS_AS(document.RestoreRecovery(), std::logic_error);

    document.SetRecoveryPath(originalRecovery);
    CHECK(document.RecoveryAvailable());
    document.RestoreRecovery();
    CHECK_FALSE(document.RecoveryAvailable());
    document.AdvanceRecovery(2.0);
    document.SetRecoveryPath(originalRecovery);
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK(document.RecoverySeconds() == 2.0);

    document.SetRecoveryPath({});
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK(document.RecoverySeconds() == 0.0);
    CHECK(std::filesystem::is_regular_file(originalRecovery));
    document.SetRecoveryPath(temporary.Path);
    CHECK_FALSE(document.RecoveryAvailable());
    document.Close();
}

TEST_CASE("Scene autosave stays quiet and preserves unresolved recovery from an earlier document")
{
    RecoveryDirectory temporary;
    const auto recovery = temporary.Path / "scene.recovery";
    const auto asset = Keire::AssetId::Generate();
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Autosave"));
    const auto entity = scene->CreateEntity("First edit").Id();
    KeireEditor::SceneDocument document;
    document.Open(scene, asset);
    document.SetRecoveryPath(recovery);
    document.AdvanceRecovery(30.0);
    REQUIRE(document.WriteRecovery());
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK(document.RecoverySeconds() == 0.0);
    scene->FindEntity(entity).SetName("Latest autosave");
    REQUIRE(document.WriteRecovery());
    CHECK_FALSE(document.RecoveryAvailable());
    const auto snapshot = scene->Snapshot();
    document.Close();

    auto reopened = Keire::CreateRef<Keire::Scene>(asset, snapshot);
    reopened->FindEntity(entity).SetName("New unsaved edit");
    document.Open(reopened, asset);
    document.SetRecoveryPath(recovery);
    REQUIRE(document.RecoveryAvailable());
    CHECK_FALSE(document.WriteRecovery());
    CHECK(document.RecoveryAvailable());
    document.RestoreRecovery();
    CHECK(document.EditingScene()->FindEntity(entity).Name() == "Latest autosave");
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK(document.Dirty());
    document.EditingScene()->FindEntity(entity).SetName("After recovery");
    CHECK(document.WriteRecovery());
    CHECK_FALSE(document.RecoveryAvailable());
    document.Close();
}
