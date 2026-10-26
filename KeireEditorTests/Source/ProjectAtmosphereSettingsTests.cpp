#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    struct AtmosphereDocumentFixture final
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("ProjectAtmosphereSettings");

        ~AtmosphereDocumentFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };

    Keire::RenderEnvironmentSettings AuthoredSky()
    {
        Keire::RenderEnvironmentSettings settings;
        settings.Atmosphere.Enabled = true;
        settings.Atmosphere.PlanetCenter = {1.0e12, -6'360'000.0, 123.25};
        settings.Atmosphere.MieAnisotropy = 0.65;
        settings.Atmosphere.SolarIrradiance = {18.0, 19.0, 20.0};
        settings.Atmosphere.ViewSamples = 48;
        settings.Stars.Enabled = true;
        settings.Stars.Seed = 9876;
        settings.Stars.Tint = {0.3, 0.6, 0.9};
        settings.Stars.Intensity = 2.5;
        settings.Clouds.Layers[0].Enabled = true;
        settings.Clouds.Layers[0].Coverage = 0.75;
        settings.Clouds.Layers[0].WindMetersPerSecond = {-12.0, 3.0};
        settings.Clouds.Layers[1].Enabled = true;
        settings.Clouds.Layers[1].AltitudeMeters = 6000.0;
        settings.Clouds.Layers[1].Opacity = 0.35;
        return settings;
    }
} // namespace

TEST_CASE("Project atmosphere clouds and stars save reload and participate in undo redo")
{
    AtmosphereDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Atmosphere authoring"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    const auto original = document.Settings();
    const auto authored = AuthoredSky();
    document.Update(authored);
    CHECK(document.Dirty());
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(history->UndoCount() == 1);
    CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == authored);

    KeireEditor::ProjectSettingsDocument reopened;
    reopened.Open(fixture.Root, Keire::LoadRenderEnvironmentSettings(fixture.Root));
    CHECK(reopened.Settings() == authored);
    CHECK_FALSE(reopened.Dirty());
    REQUIRE(history->Undo());
    CHECK(document.Settings() == original);
    CHECK(document.Dirty());
    REQUIRE(history->Redo());
    CHECK(document.Settings() == authored);
    CHECK_FALSE(document.Dirty());

    auto disabled = authored;
    disabled.Atmosphere.Enabled = false;
    disabled.Clouds.Layers[0].Enabled = false;
    disabled.Clouds.Layers[1].Enabled = false;
    disabled.Stars.Enabled = false;
    document.Update(disabled);
    document.Save();
    CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == disabled);
    REQUIRE(history->Undo());
    CHECK(document.Settings() == authored);
    REQUIRE(history->Redo());
    CHECK(document.Settings() == disabled);
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("Cancelling compound atmosphere cloud star and time edits restores the saved document")
{
    AtmosphereDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Cancel sky edit"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    document.Update(AuthoredSky());
    document.Save();
    const auto savedRendering = document.Settings();
    const auto savedTime = document.EnvironmentTime();
    const auto undoCount = history->UndoCount();
    auto changed = savedRendering;
    changed.Atmosphere.Height = 120'000.0;
    changed.Clouds.Layers[0].Coverage = 0.1;
    changed.Stars.Seed = 54321;
    changed.Stars.TwinkleAmplitude = 0.5;
    document.Update(changed);
    document.UpdateEnvironmentTime({17, 0.75});
    CHECK(document.Dirty());
    document.CancelEdit();
    CHECK(document.Settings() == savedRendering);
    CHECK(document.EnvironmentTime() == savedTime);
    CHECK_FALSE(document.Dirty());
    CHECK(history->UndoCount() == undoCount);
    CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == savedRendering);
}

TEST_CASE("Rejected atmosphere cloud and star edits preserve pending values and undo history")
{
    AtmosphereDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Invalid sky edit"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    document.Update(AuthoredSky());
    document.Save();
    const auto saved = document.Settings();
    auto pending = saved;
    pending.Atmosphere.SunAngularRadius = 0.006;
    document.Update(pending);
    const auto undoCount = history->UndoCount();
    for (const auto invalidField : {"atmosphere", "clouds", "stars"})
    {
        auto invalid = pending;
        if (std::string_view(invalidField) == "atmosphere")
        {
            invalid.Atmosphere.PlanetRadius = std::numeric_limits<double>::quiet_NaN();
        }
        else if (std::string_view(invalidField) == "clouds")
        {
            invalid.Clouds.Layers[1].Coverage = 1.5;
        }
        else
        {
            invalid.Stars.Density = -0.1;
        }
        CHECK_THROWS(document.Update(invalid));
        CHECK(document.Settings() == pending);
        CHECK(document.Dirty());
        CHECK(history->UndoCount() == undoCount);
        CHECK(Keire::LoadRenderEnvironmentSettings(fixture.Root) == saved);
    }
    document.CancelEdit();
    CHECK(document.Settings() == saved);
    CHECK_FALSE(document.Dirty());
    CHECK(history->UndoCount() == undoCount);
    document.Close();
    CHECK_THROWS_AS(document.Update(pending), std::logic_error);
}

TEST_CASE("Resetting atmosphere clouds and stars is undoable without resetting environment time")
{
    AtmosphereDocumentFixture fixture;
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Reset sky edit"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    document.Update(AuthoredSky());
    document.UpdateEnvironmentTime({123, 0.25});
    document.Save();
    const auto saved = document.Settings();
    const auto savedTime = document.EnvironmentTime();
    document.Reset();
    CHECK(document.Settings().Atmosphere == Keire::AtmosphereSettings{});
    CHECK(document.Settings().Clouds == Keire::CloudSettings{});
    CHECK(document.Settings().Stars == Keire::StarFieldSettings{});
    CHECK(document.EnvironmentTime() == savedTime);
    CHECK(document.Dirty());
    REQUIRE(history->Undo());
    CHECK(document.Settings() == saved);
    CHECK(document.EnvironmentTime() == savedTime);
    CHECK_FALSE(document.Dirty());
    REQUIRE(history->Redo());
    CHECK(document.Settings().Atmosphere == Keire::AtmosphereSettings{});
    CHECK(document.Settings().Clouds == Keire::CloudSettings{});
    CHECK(document.Settings().Stars == Keire::StarFieldSettings{});
    CHECK(document.EnvironmentTime() == savedTime);
}
