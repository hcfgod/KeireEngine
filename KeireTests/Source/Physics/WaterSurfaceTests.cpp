#include "Keire/Water/WaterSurface.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

TEST_CASE("water rectangles preserve local depth and flow at distant world origins")
{
    for (const double origin : {0.0, 1.0e12})
    {
        Keire::WaterSurfaceDefinition definition;
        definition.Origin = {origin, -origin, origin};
        definition.HalfExtentMetres = {20.0, 10.0};
        definition.DepthMetres = 5.0;
        definition.FlowVelocityLocal = {2.0, 0.0, -1.0};
        const Keire::WaterSurface surface(definition);
        const auto sample = surface.Sample({origin + 10.0, -origin - 2.0, origin + 5.0});
        REQUIRE(sample);
        CHECK(sample->WithinFluid);
        CHECK(sample->SignedDistanceMetres == -2.0);
        CHECK(sample->SurfaceOffset == Keire::Vector3d{0.0, 2.0, 0.0});
        CHECK(sample->Normal == Keire::Vector3d{0.0, 1.0, 0.0});
        CHECK(sample->Velocity == definition.FlowVelocityLocal);
        CHECK(sample->DensityKilogramsPerCubicMetre == 1000.0);
        CHECK_FALSE(surface.Sample({origin + 21.0, -origin, origin}));
        CHECK_FALSE(surface.Sample({origin, -origin, origin + 11.0}));
        CHECK(surface.Sample({origin + 20.0, -origin, origin + 10.0}));
        CHECK_FALSE(surface.Sample({origin, -origin - 6.0, origin})->WithinFluid);
        CHECK_FALSE(surface.Sample({origin, -origin + 1.0, origin})->WithinFluid);
    }
}

TEST_CASE("water ellipse footprint differs from its bounding rectangle and infinite plane")
{
    Keire::WaterSurfaceDefinition definition;
    definition.Shape = Keire::WaterSurfaceShape::Ellipse;
    definition.HalfExtentMetres = {10.0, 5.0};
    const Keire::WaterSurface ellipse(definition);
    CHECK(ellipse.Sample({10.0, 0.0, 0.0}));
    CHECK(ellipse.Sample({0.0, 0.0, 5.0}));
    CHECK(ellipse.Sample({5.0, -1.0, 2.0}));
    CHECK_FALSE(ellipse.Sample({9.0, -1.0, 4.0}));
    definition.Shape = Keire::WaterSurfaceShape::Rectangle;
    CHECK(Keire::WaterSurface(definition).Sample({9.0, -1.0, 4.0}));
    definition.Shape = Keire::WaterSurfaceShape::InfinitePlane;
    const auto far = Keire::WaterSurface(definition).Sample({1.0e15, -2.0, -1.0e15});
    REQUIRE(far);
    CHECK(far->SignedDistanceMetres == -2.0);
}

TEST_CASE("water spherical oceans use radial distance at every pole")
{
    Keire::WaterSurfaceDefinition definition;
    definition.Shape = Keire::WaterSurfaceShape::Sphere;
    definition.Origin = {1.0e12, -1.0e12, 1.0e12};
    definition.RadiusMetres = 6'371'000.0;
    const Keire::WaterSurface ocean(definition);
    for (const Keire::Vector3d direction : {Keire::Vector3d{1.0, 0.0, 0.0},
                                            {-1.0, 0.0, 0.0},
                                            {0.0, 1.0, 0.0},
                                            {0.0, -1.0, 0.0},
                                            {0.0, 0.0, 1.0},
                                            {0.0, 0.0, -1.0}})
    {
        for (const double altitude : {-5.0, 0.0, 100'000.0})
        {
            const double radius = definition.RadiusMetres + altitude;
            const Keire::Vector3d query{definition.Origin.X + direction.X * radius,
                                        definition.Origin.Y + direction.Y * radius,
                                        definition.Origin.Z + direction.Z * radius};
            const auto sample = ocean.Sample(query);
            REQUIRE(sample);
            CHECK(sample->SignedDistanceMetres == doctest::Approx(altitude));
            CHECK(sample->Normal == direction);
            CHECK(sample->WithinFluid == (altitude <= 0.0));
            CHECK(sample->SurfaceOffset.X == doctest::Approx(-direction.X * altitude));
            CHECK(sample->SurfaceOffset.Y == doctest::Approx(-direction.Y * altitude));
            CHECK(sample->SurfaceOffset.Z == doctest::Approx(-direction.Z * altitude));
        }
    }
    CHECK_FALSE(ocean.Sample(definition.Origin));
    definition.Rotation = {1.0, 2.0, 3.0, 4.0};
    const Keire::WaterSurface rotatedOcean(definition);
    const Keire::Vector3d onSurface{definition.Origin.X + definition.RadiusMetres, definition.Origin.Y,
                                    definition.Origin.Z};
    const auto rotatedSample = rotatedOcean.Sample(onSurface);
    REQUIRE(rotatedSample);
    CHECK(rotatedSample->SignedDistanceMetres == 0.0);
    CHECK(rotatedSample->WithinFluid);
    CHECK(rotatedSample->Normal == Keire::Vector3d{1.0, 0.0, 0.0});
}

TEST_CASE("water orientation rotates bounds normals and flow while frame motion adds surface velocity")
{
    Keire::WaterSurfaceDefinition definition;
    definition.Rotation = {0.0, 0.0, 3.0, 3.0};
    definition.HalfExtentMetres = {4.0, 2.0};
    definition.FlowVelocityLocal = {2.0, 0.0, 0.0};
    definition.FrameLinearVelocity = {1.0, 0.0, 0.0};
    definition.FrameAngularVelocity = {0.0, 0.0, 1.0};
    const Keire::WaterSurface surface(definition);
    const auto sample = surface.Sample({1.0, 3.0, 0.0});
    REQUIRE(sample);
    CHECK(sample->SignedDistanceMetres == doctest::Approx(-1.0));
    CHECK(sample->Normal.X == doctest::Approx(-1.0));
    CHECK(sample->Normal.Y == doctest::Approx(0.0));
    CHECK(sample->SurfaceOffset.X == doctest::Approx(-1.0));
    CHECK(sample->Velocity.X == doctest::Approx(-2.0));
    CHECK(sample->Velocity.Y == doctest::Approx(2.0));
    CHECK_FALSE(surface.Sample({1.0, 5.0, 0.0}));
    CHECK(surface.Definition().Rotation[2] == doctest::Approx(std::sqrt(0.5)));
}

TEST_CASE("water samples reject invalid configuration coordinates and overflow")
{
    Keire::WaterSurfaceDefinition definition;
    SUBCASE("zero dimension")
    {
        definition.HalfExtentMetres[0] = 0.0;
        CHECK_THROWS_AS(Keire::WaterSurface{definition}, std::invalid_argument);
    }
    SUBCASE("invalid density")
    {
        definition.DensityKilogramsPerCubicMetre = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS_AS(Keire::WaterSurface{definition}, std::invalid_argument);
    }
    SUBCASE("invalid orientation")
    {
        definition.Rotation = {};
        CHECK_THROWS_AS(Keire::WaterSurface{definition}, std::invalid_argument);
    }
    SUBCASE("unknown shape")
    {
        definition.Shape = static_cast<Keire::WaterSurfaceShape>(255);
        CHECK_THROWS_AS(Keire::WaterSurface{definition}, std::invalid_argument);
    }
    SUBCASE("nonfinite query")
    {
        const Keire::WaterSurface surface(definition);
        CHECK_THROWS_AS((void)surface.Sample({std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                        std::invalid_argument);
    }
    SUBCASE("subtraction overflow")
    {
        definition.Origin.X = -std::numeric_limits<double>::max();
        const Keire::WaterSurface surface(definition);
        CHECK_THROWS_AS((void)surface.Sample({std::numeric_limits<double>::max(), 0.0, 0.0}), std::out_of_range);
    }
    SUBCASE("surface velocity overflow")
    {
        definition.FrameAngularVelocity.Z = std::numeric_limits<double>::max();
        const Keire::WaterSurface surface(definition);
        CHECK_THROWS_AS((void)surface.Sample({2.0, 0.0, 0.0}), std::out_of_range);
    }
}
