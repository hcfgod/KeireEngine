#include "KeireInternal/Spatial/PlanetProjectFrameResolver.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsStore.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace
{
    struct PlanetSettingsFixture
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("PlanetSettings");

        ~PlanetSettingsFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
} // namespace

TEST_CASE("Planet project settings preserve flat legacy defaults and explicit modes through atomic saves")
{
    using namespace Keire::Detail;
    PlanetSettingsFixture fixture;
    CHECK(PlanetProjectSettingsStore::Load(fixture.Root) == PlanetProjectSettings{});
    CHECK_FALSE(std::filesystem::exists(fixture.Root));
    PlanetDefinition planet;
    planet.Planet = Keire::AssetId(4, 5);
    planet.Frame = 2;
    planet.ParentCenter = SectorPosition({1, 1}, {1'000'000'000'000, -1'000'000'000'000, 0}, {0.125, 0.5, 0.25});
    planet.GravitationalParameter = 3.986e14;
    PlanetProjectSettings settings;
    settings.Planets = {planet};
    settings.SelectedPlanet = planet.Planet;
    settings.Geometry = PlanetGeometryMode::Planetary;
    for (const auto gravity : {PlanetGravityMode::Uniform, PlanetGravityMode::Radial, PlanetGravityMode::Combined})
    {
        settings.Gravity = gravity;
        PlanetProjectSettingsStore::Save(fixture.Root, settings);
        CHECK(PlanetProjectSettingsStore::Load(fixture.Root) == settings);
    }
    const auto path = fixture.Root / "ProjectSettings" / "Planets.keiresettings";
    const auto original = KeireTests::ReadFile(path);
    auto invalid = settings;
    invalid.SelectedPlanet = Keire::AssetId(6, 7);
    CHECK_THROWS_AS(PlanetProjectSettingsStore::Save(fixture.Root, invalid), std::invalid_argument);
    CHECK(KeireTests::ReadFile(path) == original);
    CHECK_THROWS_AS(PlanetProjectSettingsStore::Load({}), std::invalid_argument);
}

TEST_CASE("Planet project records reject malformed modes selection and topology")
{
    using namespace Keire::Detail;
    const auto baseline = PlanetProjectSettingsRecord::Encode({});
    auto record = baseline;
    record["geometry"] = "planetary";
    CHECK_THROWS_AS(PlanetProjectSettingsRecord::Decode(record), std::invalid_argument);
    record = baseline;
    record["gravity"] = "radial";
    CHECK_THROWS_AS(PlanetProjectSettingsRecord::Decode(record), std::invalid_argument);
    record = baseline;
    record["geometry"] = "camera-follow";
    CHECK_THROWS_AS(PlanetProjectSettingsRecord::Decode(record), std::invalid_argument);
    record = baseline;
    record["schemaVersion"] = 1.0;
    CHECK_THROWS_AS(PlanetProjectSettingsRecord::Decode(record), std::invalid_argument);
    PlanetDefinition planet;
    planet.Planet = Keire::AssetId(4, 5);
    planet.Frame = 2;
    planet.ParentCenter = SectorPosition({1, 3}, {}, {});
    PlanetProjectSettings settings;
    settings.Planets = {planet};
    CHECK_THROWS_AS(ValidatePlanetProjectSettings(settings), std::invalid_argument);
    auto other = planet;
    other.Planet = Keire::AssetId(6, 7);
    other.Frame = 3;
    other.ParentCenter = SectorPosition({1, 2}, {}, {});
    settings.Planets.push_back(other);
    CHECK_THROWS_AS(ValidatePlanetProjectSettings(settings), std::invalid_argument);
}

TEST_CASE(
    "Planet project preview caches topology while rotating and invalidates only authored or clock discontinuities")
{
    using namespace Keire::Detail;
    PlanetProjectFrameResolver resolver;
    const SectorPosition observer({1, 1}, {}, {0.0, 101.0, 0.0});
    CHECK_FALSE(resolver.Resolve(observer, 0.0, {}));
    PlanetDefinition planet;
    planet.Planet = Keire::AssetId(4, 5);
    planet.Frame = 2;
    planet.RadiusMetres = 100.0;
    planet.AngularVelocity = {0.0, 0.0, 0.25};
    PlanetProjectSettings settings;
    settings.Planets = {planet};
    settings.SelectedPlanet = planet.Planet;
    settings.Geometry = PlanetGeometryMode::Planetary;
    resolver.Configure(settings);
    const auto first = resolver.Resolve(observer, 0.0, {});
    REQUIRE(first);
    resolver.Configure(settings);
    const auto rotated = resolver.Resolve(observer, 1.0, {});
    REQUIRE(rotated);
    CHECK(first->TopologyRevision == rotated->TopologyRevision);
    CHECK(first->DiscontinuityRevision == rotated->DiscontinuityRevision);
    CHECK(first->ObserverFromCenterPlanetMetres != rotated->ObserverFromCenterPlanetMetres);
    CHECK(rotated->RootToObserver.Components() == FrameRotation{}.Components());
    const EnvironmentClockState remoteClock{std::numeric_limits<std::int64_t>::max(), 0.125};
    const auto scrubbed = resolver.Resolve(observer, 1.0, remoteClock, 2);
    REQUIRE(scrubbed);
    CHECK(scrubbed->Clock == remoteClock);
    CHECK(scrubbed->DiscontinuityRevision > rotated->DiscontinuityRevision);
    settings.Planets.front().RadiusMetres = 99.0;
    resolver.Configure(settings);
    const auto edited = resolver.Resolve(observer, 1.0, remoteClock, 2);
    REQUIRE(edited);
    CHECK(edited->DiscontinuityRevision > scrubbed->DiscontinuityRevision);
    auto invalid = settings;
    invalid.SelectedPlanet = Keire::AssetId(99, 99);
    CHECK_THROWS_AS(resolver.Configure(invalid), std::invalid_argument);
    CHECK(resolver.Resolve(observer, 1.0, remoteClock, 2)->RadiusMetres == 99.0);
}
