#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentLightingEditing.h"
#include "KeireInternal/Environment/EnvironmentLightingSettingsStore.h"
#include "KeireInternal/ProjectFileTransaction.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>

namespace
{
    struct LightingDocumentFixture final
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("ProjectEnvironmentLighting");

        ~LightingDocumentFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };

    Keire::Detail::EnvironmentLightingProfile LightingProfile()
    {
        return Keire::Detail::EnvironmentLightingProfile(
            Keire::Detail::EnvironmentDirectionalOwnership::Environment,
            {{0.0, 0.0, -80.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 80.0, {1.0, 0.9, 0.8}, 12.0}},
            {Keire::ShadowQuality::Soft, Keire::ShadowResolutionHint::Medium, 0.7F, 0.02F});
    }
} // namespace

TEST_CASE("Ambient preset settings persist and undo restores the prior profile")
{
    LightingDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Ambient cycle"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto original = document.EnvironmentLighting();
    const auto preset = KeireEditor::Detail::DaylightPreset({});
    document.UpdateEnvironmentLighting(preset);
    document.Save();
    CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(fixture.Root) == preset);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentLighting() == original);
    REQUIRE(history->Redo());
    CHECK(document.EnvironmentLighting() == preset);
    const auto disabled = KeireEditor::Detail::SetAmbientAnimation(preset, false, {});
    CHECK_FALSE(disabled.Evaluate({})->Ambient);
    CHECK(disabled.Shadows() == preset.Shadows());
    document.UpdateEnvironmentLighting(disabled);
    document.Save();
    CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(fixture.Root) == disabled);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentLighting() == preset);
}

TEST_CASE("Lighting key edits and ownership toggles preserve independent shadow settings")
{
    using namespace KeireEditor::Detail;
    const auto original = ::LightingProfile();
    const auto added = AddLightingKey(original);
    CHECK(added.Shadows() == original.Shadows());
    CHECK(RemoveLightingKey(added, 1).Shadows() == original.Shadows());
    auto key = original.Keyframes().front();
    key.Intensity = 2.0;
    CHECK(ReplaceLightingKey(original, 0, key).Shadows() == original.Shadows());
    const auto disabled = SetLightingOwnership(original, false);
    CHECK(disabled.Shadows() == original.Shadows());
    CHECK(SetLightingOwnership(disabled, true) == original);
}

TEST_CASE("Enabling a retained single daily key preserves authored values across midnight")
{
    using namespace KeireEditor::Detail;
    for (const double phase : {0.0, 0.25, 0.5, 0.75, std::nextafter(1.0, 0.0)})
    {
        const Keire::Detail::EnvironmentLightingKeyframe key{phase, 123.0, 27.0, {0.2, 0.4, 0.6}, 7.0};
        const auto shadows = ::LightingProfile().Shadows();
        const KeireEditor::Detail::LightingProfile original(LightingOwnership::Authored, {key}, shadows);
        const auto enabled = SetLightingOwnership(original, true);
        CHECK(original.Keyframes().size() == 1);
        CHECK(enabled.Ownership() == LightingOwnership::Environment);
        REQUIRE(enabled.Keyframes().size() == 2);
        CHECK(enabled.Shadows() == shadows);
        CHECK(std::ranges::find(enabled.Keyframes(), key) != enabled.Keyframes().end());
        CHECK(enabled.Keyframes()[0].DayFraction < enabled.Keyframes()[1].DayFraction);
        for (const double samplePhase : {0.0, 0.25, 0.5, 0.75})
        {
            const auto sample = enabled.Evaluate({.DayFraction = samplePhase});
            REQUIRE(sample.has_value());
            CHECK(sample->Intensity == doctest::Approx(key.Intensity));
            CHECK(sample->LinearColor == key.LinearColor);
        }
        CHECK(SetLightingOwnership(SetLightingOwnership(enabled, false), true) == enabled);
    }
}

TEST_CASE("Rendering import history preserves pending daily lighting edits")
{
    LightingDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Imported settings"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto originalRendering = document.Settings();
    const auto originalLighting = document.EnvironmentLighting();
    document.UpdateEnvironmentLighting(LightingProfile());

    // Mirror the workspace import callback's separate pre-import and imported edit boundaries.
    document.CommitEdit();
    auto importedRendering = originalRendering;
    importedRendering.Exposure = 2.0F;
    document.Update(importedRendering);
    document.CommitEdit("Import rendering settings");

    CHECK(history->UndoCount() == 2);
    CHECK(document.EnvironmentLighting() == LightingProfile());
    REQUIRE(history->Undo());
    CHECK(document.Settings() == originalRendering);
    CHECK(document.EnvironmentLighting() == LightingProfile());
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentLighting() == originalLighting);
    REQUIRE(history->Redo());
    REQUIRE(history->Redo());
    document.Save();
    CHECK_FALSE(document.Dirty());

    KeireEditor::ProjectSettingsDocument reopened;
    reopened.Open(fixture.Root, Keire::LoadRenderEnvironmentSettings(fixture.Root));
    CHECK(reopened.Settings() == importedRendering);
    CHECK(reopened.EnvironmentLighting() == LightingProfile());
}

TEST_CASE("Project lighting history cancels compound edits and round trips save undo redo reset")
{
    LightingDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto originalAuthoring = document.AuthoringSettings();
    const auto originalRendering = document.Settings();
    const auto originalTime = document.EnvironmentTime();
    auto authoring = originalAuthoring;
    authoring.PhysicsLayerNames[3] = "Custom Gameplay Layer";
    document.UpdateAuthoring(authoring);
    auto rendering = originalRendering;
    rendering.Exposure = 2.0F;
    document.Update(rendering);
    document.UpdateEnvironmentTime({9, 0.75});
    document.UpdateEnvironmentLighting(LightingProfile());
    CHECK(document.Dirty());
    document.CancelEdit();
    CHECK_FALSE(document.Dirty());
    CHECK(document.AuthoringSettings() == originalAuthoring);
    CHECK(document.Settings() == originalRendering);
    CHECK(document.EnvironmentTime() == originalTime);
    CHECK(document.EnvironmentLighting().Ownership() == Keire::Detail::EnvironmentDirectionalOwnership::Authored);
    CHECK(history->UndoCount() == 0);

    document.UpdateEnvironmentLighting(LightingProfile());
    document.CommitEdit("Change Environment Lighting");
    CHECK(history->UndoCount() == 1);
    document.Save();
    CHECK_FALSE(document.Dirty());
    REQUIRE(history->Undo());
    CHECK(document.Dirty());
    REQUIRE(history->Redo());
    CHECK_FALSE(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    KeireEditor::ProjectSettingsDocument reopened;
    reopened.Open(fixture.Root, {});
    CHECK(reopened.EnvironmentLighting() == LightingProfile());
    document.ResetEnvironmentLighting();
    CHECK(document.Dirty());
    REQUIRE(history->Undo());
    CHECK_FALSE(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    document.Close();
    CHECK_THROWS_AS(document.UpdateEnvironmentLighting(LightingProfile()), std::logic_error);
}

TEST_CASE("Corrupt lighting reopen preserves the current project values and history")
{
    LightingDocumentFixture original;
    LightingDocumentFixture corrupt;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(original.Root, {}, history);
    document.UpdateEnvironmentLighting(LightingProfile());
    document.CommitEdit();
    std::filesystem::create_directories(corrupt.Root / "ProjectSettings");
    Keire::Detail::WriteTextFileAtomically(corrupt.Root / "ProjectSettings" / "EnvironmentLighting.keiresettings",
                                           R"({"schemaVersion":2,"lighting":{}})");
    CHECK_THROWS_AS(document.Open(corrupt.Root, {}), std::invalid_argument);
    CHECK(document.Opened());
    CHECK(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    CHECK(document.UndoContext() == history);
    CHECK(history->IsOpen());
    CHECK(history->Undo());
}

TEST_CASE("Lighting save failure retains dirty state and can be repaired without losing edits")
{
    LightingDocumentFixture fixture;
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {});
    document.UpdateEnvironmentLighting(LightingProfile());
    const auto blockedFile = fixture.Root / "ProjectSettings" / "EnvironmentLighting.keiresettings";
    std::filesystem::create_directories(blockedFile);
    auto rendering = document.Settings();
    rendering.Exposure = 2.0F;
    document.Update(rendering);
    CHECK_THROWS_WITH_AS(document.Save(),
                         doctest::Contains("Could not save ProjectSettings/EnvironmentLighting.keiresettings"),
                         std::runtime_error);
    CHECK(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    CHECK(document.Settings() == rendering);
    // Every destination is checked before publishing any member of the settings group.
    CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == Keire::RenderEnvironmentSettings{});
    CHECK_FALSE(std::filesystem::exists(fixture.Root / "ProjectSettings" / "Rendering.keiresettings"));
    CHECK_FALSE(std::filesystem::exists(fixture.Root / "ProjectSettings" / "Authoring.keiresettings"));
    CHECK_FALSE(std::filesystem::exists(fixture.Root / "ProjectSettings" / "EnvironmentTime.keiresettings"));
    REQUIRE(std::filesystem::is_directory(blockedFile));
    REQUIRE(std::filesystem::remove(blockedFile));
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == rendering);
    CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(fixture.Root) == LightingProfile());
}

TEST_CASE("Pending settings recovery rejects document replacement without mixing supplied values")
{
    LightingDocumentFixture original;
    LightingDocumentFixture interrupted;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Pending recovery"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(original.Root, {}, history);
    document.UpdateEnvironmentLighting(LightingProfile());
    document.CommitEdit();
    const auto journal = interrupted.Root / "Library/ProjectSettingsSave/active/journal.json";
    std::filesystem::create_directories(journal.parent_path());
    Keire::Detail::WriteTextFileAtomically(journal, R"({"schemaVersion":1,"state":"prepared","files":[]})");

    CHECK_THROWS_WITH_AS(document.Open(interrupted.Root, {}), doctest::Contains("recover"), std::runtime_error);
    CHECK_THROWS_WITH_AS(document.Open(interrupted.Root, {}, Keire::DefaultProjectAuthoringSettings()),
                         doctest::Contains("recover"), std::runtime_error);
    CHECK(document.Opened());
    CHECK(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    CHECK(document.UndoContext() == history);
    CHECK(history->IsOpen());
    CHECK(history->Undo());
    CHECK(std::filesystem::is_regular_file(journal));
}

TEST_CASE("Settings save retry recovers prepared publication without discarding pending edits")
{
    LightingDocumentFixture fixture;
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {});
    document.Save();
    document.UpdateEnvironmentLighting(LightingProfile());
    const auto journal = fixture.Root / "Library/ProjectSettingsSave/active/journal.json";
    std::filesystem::create_directories(journal.parent_path());
    Keire::Detail::WriteTextFileAtomically(journal, R"({"schemaVersion":1,"state":"prepared","files":[]})");
    REQUIRE(Keire::Detail::HasPendingProjectSettingsSave(fixture.Root));

    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(Keire::Detail::HasPendingProjectSettingsSave(fixture.Root));
    CHECK(document.EnvironmentLighting() == LightingProfile());
    CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(fixture.Root) == LightingProfile());
}

TEST_CASE("Settings save failures identify each file and retain the underlying error")
{
    for (const auto* filename : {"Rendering.keiresettings", "Authoring.keiresettings", "EnvironmentTime.keiresettings",
                                 "EnvironmentLighting.keiresettings"})
    {
        LightingDocumentFixture fixture;
        KeireEditor::ProjectSettingsDocument document;
        document.Open(fixture.Root, {});
        document.UpdateEnvironmentLighting(LightingProfile());
        const auto blockedFile = fixture.Root / "ProjectSettings" / filename;
        std::filesystem::create_directories(blockedFile);
        try
        {
            document.Save();
            FAIL("A directory at the settings destination must reject saving.");
        }
        catch (const std::runtime_error& error)
        {
            const std::string message = error.what();
            CHECK(message.starts_with("Could not save ProjectSettings/" + std::string(filename) + ": "));
            CHECK(message.find("retry Save Settings before closing the project") != std::string::npos);
            CHECK_THROWS(std::rethrow_if_nested(error));
        }
        CHECK(document.Dirty());
        CHECK(document.EnvironmentLighting() == LightingProfile());
        REQUIRE(std::filesystem::remove(blockedFile));
        document.Save();
        CHECK_FALSE(document.Dirty());
    }
}

TEST_CASE("Settings saves preserve externally changed added or removed files and pending edits")
{
    for (const bool originallyPresent : {false, true})
    {
        for (const bool externallyRemoved : {false, true})
        {
            if (!originallyPresent && externallyRemoved)
            {
                continue;
            }
            LightingDocumentFixture fixture;
            const auto authoringPath = fixture.Root / "ProjectSettings/Authoring.keiresettings";
            auto authoring = Keire::DefaultProjectAuthoringSettings();
            if (originallyPresent)
            {
                Keire::SaveProjectAuthoringSettings(fixture.Root, authoring);
            }
            KeireEditor::ProjectSettingsDocument document;
            document.Open(fixture.Root, {});
            auto rendering = document.Settings();
            rendering.Exposure = 2.0F;
            document.Update(rendering);
            authoring.PhysicsLayerNames[3] = "External change";
            if (externallyRemoved)
            {
                REQUIRE(std::filesystem::remove(authoringPath));
            }
            else
            {
                Keire::SaveProjectAuthoringSettings(fixture.Root, authoring);
            }
            const Keire::Detail::AnchoredFileSystem files(fixture.Root);
            const auto externalBytes = externallyRemoved
                                           ? std::vector<std::byte>{}
                                           : files.Read("ProjectSettings/Authoring.keiresettings", 1024U * 1024U);
            CHECK_THROWS_WITH_AS(document.Save(), doctest::Contains("externally"), std::runtime_error);
            CHECK(document.Dirty());
            CHECK(document.Settings() == rendering);
            CHECK_FALSE(std::filesystem::exists(fixture.Root / "ProjectSettings/Rendering.keiresettings"));
            CHECK(std::filesystem::exists(authoringPath) == !externallyRemoved);
            if (!externallyRemoved)
            {
                CHECK(files.Read("ProjectSettings/Authoring.keiresettings", 1024U * 1024U) == externalBytes);
            }
            CHECK_FALSE(Keire::Detail::HasPendingProjectSettingsSave(fixture.Root));
        }
    }
}

TEST_CASE("Settings baseline advances after successful saves and failed opens retain it")
{
    LightingDocumentFixture fixture;
    LightingDocumentFixture invalid;
    KeireEditor::ProjectSettingsDocument document;
    const auto nonexistentRoot = fixture.Root / "NewProject";
    document.Open(nonexistentRoot, {}, Keire::DefaultProjectAuthoringSettings());
    document.Save();
    auto rendering = document.Settings();
    rendering.Exposure = 2.0F;
    document.Update(rendering);
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(Keire::LoadRenderEnvironmentSettings(nonexistentRoot) == rendering);

    std::filesystem::create_directories(invalid.Root / "ProjectSettings/Authoring.keiresettings");
    CHECK_THROWS(document.Open(invalid.Root, {}, Keire::DefaultProjectAuthoringSettings()));
    CHECK(document.Settings() == rendering);
    rendering.Exposure = 3.0F;
    document.Update(rendering);
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(Keire::LoadRenderEnvironmentSettings(nonexistentRoot) == rendering);

    auto external = document.AuthoringSettings();
    external.PhysicsLayerNames[4] = "Changed after save";
    Keire::SaveProjectAuthoringSettings(nonexistentRoot, external);
    document.UpdateEnvironmentLighting(LightingProfile());
    CHECK_THROWS_WITH_AS(document.Save(), doctest::Contains("externally"), std::runtime_error);
    CHECK(document.Dirty());
    CHECK(document.EnvironmentLighting() == LightingProfile());
    CHECK(Keire::LoadProjectAuthoringSettings(nonexistentRoot) == external);
}

TEST_CASE("Lighting authoring operations preserve keys and reject invalid structural edits")
{
    using namespace KeireEditor::Detail;
    const KeireEditor::Detail::LightingProfile original;
    const auto enabled = SetLightingOwnership(original, true);
    CHECK(original.Keyframes().empty());
    REQUIRE(enabled.Keyframes().size() == 2);
    CHECK(enabled.Ownership() == LightingOwnership::Environment);
    const auto disabled = SetLightingOwnership(enabled, false);
    CHECK(disabled.Keyframes() == enabled.Keyframes());
    CHECK_FALSE(disabled.Evaluate({}).has_value());
    CHECK(SetLightingOwnership(disabled, true) == enabled);
    const auto added = AddLightingKey(enabled);
    REQUIRE(added.Keyframes().size() == 3);
    CHECK(added.Keyframes()[1].DayFraction == doctest::Approx(0.25));
    CHECK(added.Keyframes()[1].LinearColor == enabled.Keyframes()[0].LinearColor);
    CHECK(RemoveLightingKey(added, 1) == enabled);
    CHECK_THROWS_AS((void)RemoveLightingKey(enabled, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)RemoveLightingKey(enabled, 8), std::out_of_range);
    CHECK_THROWS_AS((void)AddLightingKey(original), std::invalid_argument);
    auto full = enabled;
    while (full.Keyframes().size() < KeireEditor::Detail::LightingProfile::MaximumKeyframes)
    {
        full = AddLightingKey(full);
    }
    CHECK_THROWS_AS((void)AddLightingKey(full), std::invalid_argument);
    CHECK(full.Keyframes().size() == KeireEditor::Detail::LightingProfile::MaximumKeyframes);
}

TEST_CASE("Resetting daily shadows preserves keys and ownership and supports saved undo redo")
{
    LightingDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Shadow reset"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto original = LightingProfile();
    document.UpdateEnvironmentLighting(original);
    document.Save();
    const auto reset = KeireEditor::Detail::ResetLightingShadows(original);
    CHECK(reset.Keyframes() == original.Keyframes());
    CHECK(reset.Ownership() == original.Ownership());
    CHECK(reset.Shadows() == Keire::EnvironmentShadowSettings{});
    CHECK(KeireEditor::Detail::ResetLightingShadows(reset) == reset);
    document.UpdateEnvironmentLighting(reset);
    document.Save();
    CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(fixture.Root) == reset);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentLighting() == original);
    REQUIRE(history->Redo());
    CHECK(document.EnvironmentLighting() == reset);
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("Rejected lighting key edits leave document values and history unchanged")
{
    LightingDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Lighting edits"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto original = LightingProfile();
    document.UpdateEnvironmentLighting(original);
    document.Save();
    const auto undoCount = history->UndoCount();
    auto key = original.Keyframes()[0];
    key.DayFraction = 0.5;
    CHECK_THROWS_WITH(document.UpdateEnvironmentLighting(
                          KeireEditor::Detail::ReplaceLightingKey(document.EnvironmentLighting(), 0, key)),
                      "Keep this key's day fraction strictly between its neighboring keys.");
    CHECK(document.EnvironmentLighting() == original);
    CHECK_FALSE(document.Dirty());
    CHECK(history->UndoCount() == undoCount);
    key.DayFraction = 0.25;
    key.LinearColor[0] = -1.0;
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReplaceLightingKey(original, 0, key), std::invalid_argument);
    key.LinearColor[0] = 0.5;
    key.Intensity = 3.0;
    document.UpdateEnvironmentLighting(KeireEditor::Detail::ReplaceLightingKey(original, 0, key));
    document.Save();
    CHECK(document.EnvironmentLighting().Keyframes()[0] == key);
    CHECK(document.EnvironmentLighting().Keyframes()[1] == original.Keyframes()[1]);
    CHECK(history->UndoCount() == undoCount + 1);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentLighting() == original);
    REQUIRE(history->Redo());
    CHECK(document.EnvironmentLighting().Keyframes()[0] == key);
    CHECK_FALSE(document.Dirty());
}
