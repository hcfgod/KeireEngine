#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/SettingsEditSurfaceTracker.h"
#include "KeireInternal/Environment/EnvironmentTimeSettingsStore.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

namespace
{
    struct ProjectEnvironmentFixture final
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("ProjectEnvironmentTime");

        ~ProjectEnvironmentFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
} // namespace

TEST_CASE("Project environment time supports preview apply undo redo and persistence")
{
    ProjectEnvironmentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Project environment"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto baseline = document.EnvironmentTime();
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind(baseline);
    preview.Seek(15, 0.75);
    CHECK_FALSE(document.Dirty());
    CHECK(document.EnvironmentTime() == baseline);
    CHECK(history->UndoCount() == 0);
    document.UpdateEnvironmentTime(preview.ApplyTimeCandidate());
    document.CommitEdit("Apply Preview Time");
    CHECK(document.Dirty());
    CHECK(document.EnvironmentTime().Day == 15);
    preview.SynchronizeAuthored(document.EnvironmentTime());
    CHECK(preview.ApplyTimeCandidate() == document.EnvironmentTime());
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentTime() == baseline);
    preview.SynchronizeAuthored(document.EnvironmentTime());
    CHECK(preview.ApplyTimeCandidate() == baseline);
    REQUIRE(history->Redo());
    CHECK(document.EnvironmentTime().DayFraction == 0.75);
    preview.SynchronizeAuthored(document.EnvironmentTime());
    CHECK(preview.ApplyTimeCandidate() == document.EnvironmentTime());
    document.Save();
    CHECK_FALSE(document.Dirty());
    KeireEditor::ProjectSettingsDocument reopened;
    reopened.Open(fixture.Root, {});
    CHECK(reopened.EnvironmentTime() == document.EnvironmentTime());
    document.ResetEnvironmentTime();
    CHECK(document.EnvironmentTime() == baseline);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentTime().Day == 15);
    document.Close();
    CHECK_FALSE(history->IsOpen());
    CHECK_THROWS_AS((void)history->Undo(), std::logic_error);
}

TEST_CASE("Environment and rendering gestures remain independent across explicit edit boundaries")
{
    ProjectEnvironmentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Shared environment settings"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto originalTime = document.EnvironmentTime();
    const auto originalRendering = document.Settings();

    document.UpdateEnvironmentTime({8, 0.25, 300.0});
    document.CommitEdit("Finish time gesture");
    auto rendering = document.Settings();
    rendering.Bloom.Enabled = true;
    document.Update(rendering);
    document.CommitEdit("Finish bloom gesture");
    document.CommitEdit("Empty focus transition");
    REQUIRE(history->UndoCount() == 2);

    REQUIRE(history->Undo());
    CHECK(document.Settings() == originalRendering);
    CHECK(document.EnvironmentTime().Day == 8);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentTime() == originalTime);
    CHECK_FALSE(document.Dirty());
    REQUIRE(history->Redo());
    REQUIRE(history->Redo());
    CHECK(document.Settings().Bloom.Enabled);
    CHECK(document.EnvironmentTime().Day == 8);
    document.Close();
}

TEST_CASE("Settings surface transitions finish gestures regardless of panel draw order")
{
    using Surface = KeireEditor::Detail::SettingsEditSurface;
    KeireEditor::Detail::SettingsEditSurfaceTracker tracker;
    CHECK_FALSE(tracker.Observe(Surface::Project, true, true));
    CHECK_FALSE(tracker.Observe(Surface::Project, true, true));
    CHECK_FALSE(tracker.Observe(Surface::Environment, true, false));
    // Incoming surface drawn first: finish the old gesture before accepting another edit.
    CHECK(tracker.Observe(Surface::Environment, true, true));
    CHECK_FALSE(tracker.Observe(Surface::Project, true, false));
    // Outgoing surface drawn first: the same boundary occurs exactly once.
    CHECK(tracker.Observe(Surface::Environment, true, false));
    CHECK_FALSE(tracker.Observe(Surface::Project, true, true));
    CHECK(tracker.Observe(Surface::Project, false, false));
    CHECK_FALSE(tracker.Observe(Surface::Project, false, false));
    CHECK_FALSE(tracker.Observe(Surface::Environment, true, true));
    CHECK(tracker.Observe(Surface::Environment, true, false));
    CHECK_FALSE(tracker.Observe(Surface::None, false, false));
}

TEST_CASE("Project environment edits cancel and reject invalid input without changing history")
{
    ProjectEnvironmentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Project environment"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto baseline = document.EnvironmentTime();
    document.UpdateEnvironmentTime(baseline);
    document.CommitEdit();
    CHECK_FALSE(document.Dirty());
    CHECK(history->UndoCount() == 0);
    CHECK_THROWS_AS(document.UpdateEnvironmentTime({0, 1.0}), std::invalid_argument);
    CHECK_FALSE(document.Dirty());
    CHECK(document.EnvironmentTime() == baseline);
    document.UpdateEnvironmentTime({8, 0.5, 300.0});
    document.CancelEdit();
    CHECK(document.EnvironmentTime() == baseline);
    CHECK(history->UndoCount() == 0);
    document.Close();
    CHECK_THROWS_AS(document.UpdateEnvironmentTime({}), std::logic_error);
}

TEST_CASE("Project settings cancel and undo restore the saved dirty state across setting groups")
{
    ProjectEnvironmentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Project settings"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    auto rendering = document.Settings();
    rendering.Exposure = 2.0F;
    document.Update(rendering);
    document.CancelEdit();
    CHECK_FALSE(document.Dirty());
    auto authoring = document.AuthoringSettings();
    authoring.ExternalEditorId = "custom";
    document.UpdateAuthoring(authoring);
    document.UpdateEnvironmentTime({7, 0.5, 100.0});
    document.CancelEdit();
    CHECK_FALSE(document.Dirty());
    document.UpdateEnvironmentTime({7, 0.5, 100.0});
    document.CommitEdit();
    REQUIRE(history->Undo());
    CHECK_FALSE(document.Dirty());
    REQUIRE(history->Redo());
    CHECK(document.Dirty());
    document.Save();
    REQUIRE(history->Undo());
    CHECK(document.Dirty());
    REQUIRE(history->Redo());
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("Project environment corrupted reopen preserves the current document")
{
    ProjectEnvironmentFixture fixture;
    ProjectEnvironmentFixture corrupt;
    Keire::Detail::EnvironmentTimeSettingsStore::Save(corrupt.Root, {});
    Keire::Detail::WriteTextFileAtomically(corrupt.Root / "ProjectSettings" / "EnvironmentTime.keiresettings", "{");
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Project environment"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    document.UpdateEnvironmentTime({2, 0.5, 100.0});
    document.CommitEdit();
    const auto before = document.EnvironmentTime();
    CHECK_THROWS(document.Open(corrupt.Root, {}));
    CHECK(document.Opened());
    CHECK(document.EnvironmentTime() == before);
    CHECK(document.Dirty());
    CHECK(history->IsOpen());
    CHECK(history->UndoCount() == 1);
    REQUIRE(history->Undo());
    CHECK(document.EnvironmentTime() == Keire::Detail::EnvironmentClockState{});
}
