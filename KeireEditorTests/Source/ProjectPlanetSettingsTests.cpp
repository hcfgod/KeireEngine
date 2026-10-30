#include "Keire/Core.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/PlanetControls.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsStore.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace
{
    struct PlanetDocumentFixture
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("Keire-PlanetDocument-" + Keire::AssetId::Generate().ToString());

        ~PlanetDocumentFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
} // namespace

TEST_CASE("Planet document shares undo cancel persistence and external-change protection")
{
    using namespace Keire::Detail;
    PlanetDocumentFixture fixture;
    const auto service = Keire::CreateRef<Keire::UndoService>();
    const auto history = service->CreateContext({.Name = "Planet Settings"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(fixture.Root, {}, history);
    CHECK(document.Planets() == PlanetProjectSettings{});
    auto settings = document.Planets();
    const auto planet = KeireEditor::Detail::PlanetFromAtmosphere(document.Settings().Atmosphere, settings);
    settings.Planets.push_back(planet);
    settings.SelectedPlanet = planet.Planet;
    settings.Geometry = PlanetGeometryMode::Planetary;
    settings.Gravity = PlanetGravityMode::Radial;
    settings.Selection = PlanetSelectionMode::NearestSurface;
    document.UpdatePlanets(settings);
    document.CommitEdit("Configure Planet");
    CHECK(document.Dirty());
    REQUIRE(history->Undo());
    CHECK(document.Planets() == PlanetProjectSettings{});
    REQUIRE(history->Redo());
    CHECK(document.Planets() == settings);
    document.Save();
    CHECK_FALSE(document.Dirty());
    for (const char* name : {"Rendering.keiresettings", "Authoring.keiresettings", "EnvironmentTime.keiresettings",
                             "EnvironmentLighting.keiresettings", "Planets.keiresettings"})
    {
        CHECK(std::filesystem::is_regular_file(fixture.Root / "ProjectSettings" / name));
    }
    KeireEditor::ProjectSettingsDocument reopened;
    reopened.Open(fixture.Root, document.Settings());
    CHECK(reopened.Planets() == settings);
    reopened.Close();
    CHECK(PlanetProjectSettingsStore::Load(fixture.Root) == settings);
    auto changed = settings;
    changed.Planets.front().RadiusMetres += 100.0;
    document.UpdatePlanets(changed);
    document.CancelEdit();
    CHECK(document.Planets() == settings);
    CHECK_FALSE(document.Dirty());
    auto invalid = settings;
    invalid.SelectedPlanet = Keire::AssetId(9, 9);
    CHECK_THROWS_AS(document.UpdatePlanets(invalid), std::invalid_argument);
    CHECK(document.Planets() == settings);
    PlanetProjectSettingsStore::Save(fixture.Root, changed);
    auto rendering = document.Settings();
    rendering.Exposure = 2.0F;
    document.Update(rendering);
    CHECK_THROWS_AS(document.Save(), std::runtime_error);
    CHECK(document.Dirty());
    CHECK(PlanetProjectSettingsStore::Load(fixture.Root) == changed);
    document.Close();
    CHECK_THROWS_AS(document.UpdatePlanets({}), std::logic_error);
}

TEST_CASE("Explicit planet creation preserves distant atmosphere placement without changing legacy modes")
{
    using namespace Keire::Detail;
    Keire::AtmosphereSettings atmosphere;
    atmosphere.PlanetCenter = {1.0e12 + 0.125, -6'360'000.0, -1.0e12 + 0.25};
    PlanetProjectSettings settings;
    const auto first = KeireEditor::Detail::PlanetFromAtmosphere(atmosphere, settings);
    const auto center = first.ParentCenter.RelativeTo(SectorPosition({1, 1}, {}, {}));
    CHECK(center == atmosphere.PlanetCenter);
    CHECK(first.RadiusMetres == atmosphere.PlanetRadius);
    CHECK(first.GravitationalParameter == 0.0);
    settings.Planets.push_back(first);
    const auto second = KeireEditor::Detail::PlanetFromAtmosphere(atmosphere, settings);
    CHECK(first.Planet != second.Planet);
    CHECK(first.Frame != second.Frame);
    CHECK(settings.Geometry == PlanetGeometryMode::Flat);
    CHECK(settings.Gravity == PlanetGravityMode::Uniform);
}
