#include "Keire/Rendering/MaterialEcosystem.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("material parameter collections publish atomic ordered numeric uniform snapshots")
{
    const auto scalar = Keire::AssetId::Parse("7b7cf1f5-3454-4ca8-b297-e04973c47443");
    const auto tint = Keire::AssetId::Parse("75c6f0db-aa6c-4cfa-8654-c8c255f386fb");
    Keire::MaterialParameterCollectionDefinition definition;
    definition.Parameters = {{.Id = scalar, .Name = "Wind", .DefaultValue = 0.25F},
                             {.Id = tint,
                              .Name = "Tint",
                              .Type = Keire::ShaderPropertyType::Color,
                              .DefaultValue = Keire::Color{0.1F, 0.2F, 0.3F, 1.0F}}};
    auto state = Keire::CreateRef<Keire::MaterialParameterCollectionState>(definition);

    const auto initial = state->NumericUniformSnapshot();
    CHECK(initial.Revision == state->Revision());
    const std::vector<Keire::Vector4> expectedInitial{{0.25F, 0.0F, 0.0F, 0.0F}, {0.1F, 0.2F, 0.3F, 1.0F}};
    CHECK(initial.Values == expectedInitial);

    state->Set(tint, Keire::Color{0.8F, 0.7F, 0.6F, 1.0F});
    const auto changed = state->NumericUniformSnapshot();
    CHECK(changed.Revision == initial.Revision + 1U);
    CHECK(changed.Values[0] == initial.Values[0]);
    CHECK((changed.Values[1] == Keire::Vector4{0.8F, 0.7F, 0.6F, 1.0F}));

    state->Close();
    CHECK_THROWS_AS((void)state->NumericUniformSnapshot(), std::logic_error);
}

TEST_CASE("material numeric uniform cache coalesces deterministic dirty ranges")
{
    Keire::MaterialNumericUniformCache cache(6);
    Keire::MaterialNumericUniformSnapshot snapshot;
    snapshot.Revision = 4;
    snapshot.Values = {{0.0F, 0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F, 0.0F},
                       {2.0F, 0.0F, 0.0F, 0.0F},
                       {3.0F, 0.0F, 0.0F, 0.0F},
                       {4.0F, 0.0F, 0.0F, 0.0F}};

    const auto first = cache.Update(snapshot);
    CHECK(first.FullUpload);
    const std::vector firstRanges{Keire::MaterialNumericUniformDirtyRange{0, 5}};
    CHECK(first.DirtyRanges == firstRanges);

    snapshot.Revision = 5;
    snapshot.Values[1].X = 10.0F;
    snapshot.Values[2].X = 20.0F;
    snapshot.Values[4].X = 40.0F;
    const auto changed = cache.Update(snapshot);
    CHECK_FALSE(changed.FullUpload);
    const std::vector changedRanges{Keire::MaterialNumericUniformDirtyRange{1, 2},
                                    Keire::MaterialNumericUniformDirtyRange{4, 1}};
    CHECK(changed.DirtyRanges == changedRanges);
    CHECK(changed.Values == snapshot.Values);
    CHECK(cache.Revision() == 5);

    snapshot.Revision = 6;
    const auto unchanged = cache.Update(snapshot);
    CHECK_FALSE(unchanged.FullUpload);
    CHECK(unchanged.DirtyRanges.empty());
    CHECK(cache.Revision() == 6);

    snapshot.Values.pop_back();
    const auto resized = cache.Update(snapshot);
    CHECK(resized.FullUpload);
    const std::vector resizedRanges{Keire::MaterialNumericUniformDirtyRange{0, 4}};
    CHECK(resized.DirtyRanges == resizedRanges);

    cache.Reset();
    CHECK(cache.Revision() == 0);
    CHECK(cache.Values().empty());
    CHECK(cache.Update(snapshot).FullUpload);
}

TEST_CASE("material numeric uniform cache bounds reject transactionally")
{
    CHECK_THROWS_AS((void)Keire::MaterialNumericUniformCache(0), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::MaterialNumericUniformCache(Keire::MaximumMaterialNumericUniforms + 1U),
                    std::invalid_argument);

    Keire::MaterialNumericUniformCache cache(2);
    Keire::MaterialNumericUniformSnapshot valid{1, {{1.0F, 0.0F, 0.0F, 0.0F}}};
    (void)cache.Update(valid);
    const Keire::MaterialNumericUniformSnapshot oversized{
        2, {{1.0F, 0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F, 0.0F}, {3.0F, 0.0F, 0.0F, 0.0F}}};
    CHECK_THROWS_AS((void)cache.Update(oversized), std::invalid_argument);
    CHECK(cache.Revision() == 1);
    REQUIRE(cache.Values().size() == 1);
    CHECK(cache.Values().front() == valid.Values.front());

    auto nonFinite = valid;
    nonFinite.Revision = 3;
    nonFinite.Values.front().Y = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS((void)cache.Update(nonFinite), std::invalid_argument);
    CHECK(cache.Revision() == 1);
    CHECK(cache.Values().front() == valid.Values.front());
}
