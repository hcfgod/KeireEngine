#include "KeireInternal/Environment/FogDensity.h"
#include "KeireInternal/Environment/FogDensityRecord.h"
#include "KeireInternal/Rendering/FogViewUniforms.h"

#include <doctest/doctest.h>

#include <limits>

TEST_CASE("Fog GPU preparation subtracts origin before narrowing and culls unreachable volumes")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.LocalVolumes.emplace_back();
    settings.LocalVolumes.front().Center = {1.0e12 + 0.125, 3.0, 0.0};
    auto distantVolume = settings.LocalVolumes.front();
    distantVolume.Center[0] += 1.0e9;
    settings.LocalVolumes.push_back(distantVolume);
    const auto prepared = Keire::RenderBackend::MakeFogViewUniforms(settings, {1.0e12, 2.0, 0.0});
    CHECK(prepared.Controls[0] == 1);
    REQUIRE(prepared.Controls[2] == 1);
    CHECK(prepared.Local[0].CenterExtinction[0] == 0.125F);
    CHECK(prepared.Local[0].CenterExtinction[1] == 1.0F);
    CHECK(prepared.HeightDensity[0] == -2.0F);
    CHECK(prepared.Local[1].CenterExtinction[3] == 0.0F);
    CHECK_THROWS_AS(
        (void)Keire::RenderBackend::MakeFogViewUniforms(settings, {std::numeric_limits<double>::infinity(), 0, 0}),
        std::invalid_argument);
}

TEST_CASE("Fog view packing retains nearby offsets at the supported coordinate limit")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.LocalVolumes.resize(Keire::MaximumLocalFogVolumes);
    const std::array<double, 3> observer{1.0e15 - 16.0, 0.0, -1.0e15 + 16.0};
    for (std::size_t index = 0; index < settings.LocalVolumes.size(); ++index)
    {
        settings.LocalVolumes[index].Center = {observer[0] + 0.125 * static_cast<double>(index), 0.0,
                                               observer[2] - 0.125};
    }
    const auto prepared = Keire::RenderBackend::MakeFogViewUniforms(settings, observer);
    REQUIRE(prepared.Controls[2] == Keire::MaximumLocalFogVolumes);
    for (std::size_t index = 0; index < settings.LocalVolumes.size(); ++index)
    {
        CHECK(prepared.Local[index].CenterExtinction[0] == static_cast<float>(index) * 0.125F);
        CHECK(prepared.Local[index].CenterExtinction[2] == -0.125F);
    }
    settings.Enabled = false;
    CHECK(Keire::RenderBackend::MakeFogViewUniforms(settings, observer) == Keire::RenderBackend::FogViewUniforms{});
}

TEST_CASE("Fog records round trip authored density and reject malformed data")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.LocalVolumes.emplace_back();
    settings.LocalVolumes.front().Center = {1.0e12, -30, 20};
    const auto record = Keire::Detail::EncodeFogDensity(settings);
    CHECK(Keire::Detail::EncodeFogDensity(Keire::Detail::DecodeFogDensity(record)) == record);
    for (const auto& invalid :
         {nlohmann::json(true), nlohmann::json("64"), nlohmann::json(2.5), nlohmann::json(-1), nlohmann::json(129)})
    {
        auto changed = record;
        changed["viewSteps"] = invalid;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(changed), std::invalid_argument);
    }
    auto changed = record;
    changed["extinction"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(changed), std::invalid_argument);
    changed = record;
    changed["localVolumes"][0]["halfExtent"] = {1, 2};
    CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(changed), std::invalid_argument);
    changed = record;
    changed.erase("albedo");
    CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(changed), std::invalid_argument);
    changed = record;
    changed["unexpected"] = 0;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(changed), std::invalid_argument);
}

TEST_CASE("Local fog enable state preserves authored values and migrates legacy records")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.ExtinctionPerMeter = 0.0;
    settings.LocalVolumes.emplace_back();
    settings.LocalVolumes.front().Enabled = false;
    CHECK(Keire::Detail::SampleFogExtinction(settings, {}) == 0.0);
    CHECK(Keire::RenderBackend::MakeFogViewUniforms(settings, {}).Controls[2] == 0);
    auto record = Keire::Detail::EncodeFogDensity(settings);
    CHECK(Keire::Detail::DecodeFogDensity(record) == settings);
    record["localVolumes"][0].erase("enabled");
    const auto migrated = Keire::Detail::DecodeFogDensity(record);
    CHECK(migrated.LocalVolumes.front().Enabled);
    CHECK(Keire::Detail::SampleFogExtinction(migrated, {}) == 0.02);
    CHECK(Keire::RenderBackend::MakeFogViewUniforms(migrated, {}).Controls[2] == 1);
    for (const auto& invalid : {nlohmann::json(0), nlohmann::json("false"), nlohmann::json(nullptr)})
    {
        record["localVolumes"][0]["enabled"] = invalid;
        CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(record), std::invalid_argument);
    }
    settings.LocalVolumes.front().HalfExtent[0] = 0.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
}

TEST_CASE("Fog density is disabled by default and height falloff remains bounded")
{
    Keire::FogSettings settings;
    CHECK_NOTHROW(Keire::Detail::ValidateFogDensity(settings));
    CHECK(Keire::Detail::SampleFogExtinction(settings, {}) == 0.0);
    settings.Enabled = true;
    CHECK(Keire::Detail::SampleFogExtinction(settings, {}) == settings.ExtinctionPerMeter);
    CHECK(Keire::Detail::SampleFogExtinction(settings, {0, -1000, 0}) == settings.ExtinctionPerMeter);
    CHECK(Keire::Detail::SampleFogExtinction(settings, {0, 100, 0}) ==
          doctest::Approx(settings.ExtinctionPerMeter / std::exp(1.0)));
    CHECK(Keire::Detail::SampleFogExtinction(settings, {0, 1.0e9, 0}) == 0.0);
}

TEST_CASE("Local fog volumes blend smoothly and add overlapping extinction")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.ExtinctionPerMeter = 0.0;
    settings.LocalVolumes.emplace_back();
    const auto density = [&](double x) { return Keire::Detail::SampleFogExtinction(settings, {x, 0, 0}); };
    CHECK(density(0) == 0.02);
    CHECK(density(8) == doctest::Approx(0.02));
    CHECK(density(9) == doctest::Approx(0.01));
    CHECK(density(10) == 0.0);
    CHECK(density(-10) == 0.0);
    CHECK(density(11) == 0.0);
    settings.LocalVolumes.push_back(settings.LocalVolumes.front());
    CHECK(density(9) == doctest::Approx(0.02));
    settings.LocalVolumes.front().EdgeFraction = 0;
    CHECK(density(9) == doctest::Approx(0.03));
}

TEST_CASE("Fog validates disabled records and rejects nonfinite coordinates and invalid budgets")
{
    Keire::FogSettings settings;
    settings.ViewSteps = 0;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
    settings.ViewSteps = 129;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
    settings.ViewSteps = 64;
    settings.LocalVolumes.resize(Keire::MaximumLocalFogVolumes + 1);
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
    settings.LocalVolumes.resize(1);
    settings.LocalVolumes.front().HalfExtent[0] = 0;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
    settings.LocalVolumes.clear();
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        settings.ExtinctionPerMeter = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
    }
    CHECK_THROWS_AS((void)Keire::Detail::SampleFogExtinction({}, {std::numeric_limits<double>::quiet_NaN(), 0, 0}),
                    std::invalid_argument);
}
