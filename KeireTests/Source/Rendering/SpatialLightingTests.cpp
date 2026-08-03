#include "KeireInternal/Rendering/SpatialLightingInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    [[nodiscard]] Keire::Detail::SpatialReflectionProbe Probe(const char* id, const Keire::Vector3 position,
                                                              const std::int32_t importance = 0)
    {
        Keire::Detail::SpatialReflectionProbe result;
        result.Entity = Keire::AssetId::Parse(id);
        result.LocalToWorld = Keire::Math::ComposeTransform(position, {}, {1.0F, 1.0F, 1.0F});
        result.WorldToLocal = Keire::Math::Inverse(result.LocalToWorld);
        result.BoxExtents = {2.0F, 2.0F, 2.0F};
        result.BlendDistance = 1.0F;
        result.Importance = importance;
        return result;
    }
} // namespace

TEST_CASE("spatial reflection probes blend two stable equal-priority candidates")
{
    const std::array probes{Probe("00000000-0000-4000-8000-000000000001", {-0.5F, 0.0F, 0.0F}),
                            Probe("00000000-0000-4000-8000-000000000002", {0.5F, 0.0F, 0.0F})};
    const auto selected = Keire::Detail::SelectReflectionProbes({}, probes);
    REQUIRE(selected.size() == 2);
    CHECK(selected[0].Weight == doctest::Approx(0.5F));
    CHECK(selected[1].Weight == doctest::Approx(0.5F));
    CHECK(selected[0].Probe->Entity < selected[1].Probe->Entity);
}

TEST_CASE("reflection probe importance overrides lower-priority overlaps")
{
    const std::array probes{Probe("00000000-0000-4000-8000-000000000001", {}, 1),
                            Probe("00000000-0000-4000-8000-000000000002", {}, 5)};
    const auto selected = Keire::Detail::SelectReflectionProbes({}, probes);
    REQUIRE(selected.size() == 1);
    CHECK(selected.front().Probe->Importance == 5);
    CHECK(selected.front().Weight == doctest::Approx(1.0F));
}

TEST_CASE("box-projected reflections intersect the oriented local influence box")
{
    auto probe = Probe("00000000-0000-4000-8000-000000000001", {5.0F, 0.0F, 0.0F});
    probe.LocalToWorld = Keire::Math::ComposeTransform(
        {5.0F, 0.0F, 0.0F}, Keire::Math::EulerDegreesToQuaternion({0.0F, 45.0F, 0.0F}), {1.0F, 1.0F, 1.0F});
    probe.WorldToLocal = Keire::Math::Inverse(probe.LocalToWorld);
    const auto direction = Keire::Detail::BoxProjectedReflection({5.5F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, probe);
    const auto length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z);
    CHECK(length == doctest::Approx(1.0F));
    CHECK(direction.X > 0.0F);
}

TEST_CASE("shadow atlas allocation is priority ordered, bounded, and temporally stable")
{
    Keire::Detail::ShadowAtlasAllocator atlas(1024, 256);
    const std::array requests{
        Keire::Detail::ShadowAtlasRequest{{Keire::AssetId::Parse("00000000-0000-4000-8000-000000000001"), 0}, 512, 10},
        Keire::Detail::ShadowAtlasRequest{{Keire::AssetId::Parse("00000000-0000-4000-8000-000000000002"), 0}, 512, 5},
        Keire::Detail::ShadowAtlasRequest{{Keire::AssetId::Parse("00000000-0000-4000-8000-000000000003"), 0}, 512, 1}};
    const auto firstSpan = atlas.Allocate(requests);
    const std::vector first(firstSpan.begin(), firstSpan.end());
    REQUIRE(first.size() == 3);
    const auto secondSpan = atlas.Allocate(requests);
    const std::vector second(secondSpan.begin(), secondSpan.end());
    REQUIRE(second.size() == first.size());
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        CHECK(second[index].Key == first[index].Key);
        CHECK(second[index].X == first[index].X);
        CHECK(second[index].Y == first[index].Y);
        CHECK(second[index].Size == first[index].Size);
        CHECK(second[index].X + second[index].Size <= atlas.AtlasSize());
        CHECK(second[index].Y + second[index].Size <= atlas.AtlasSize());
    }
}

TEST_CASE("shadow atlas rejects duplicate stable request keys")
{
    Keire::Detail::ShadowAtlasAllocator atlas(1024, 256);
    const auto id = Keire::AssetId::Parse("00000000-0000-4000-8000-000000000001");
    const std::array requests{Keire::Detail::ShadowAtlasRequest{{id, 0}, 256, 0},
                              Keire::Detail::ShadowAtlasRequest{{id, 0}, 512, 1}};
    CHECK_THROWS_AS((void)atlas.Allocate(requests), std::invalid_argument);
}

TEST_CASE("light probe volume sampling trilinearly interpolates valid SH records")
{
    Keire::LightProbeVolumeDefinition volume;
    volume.CountX = 2;
    volume.CountY = 1;
    volume.CountZ = 1;
    volume.Spacing = {1.0F, 1.0F, 1.0F};
    volume.Probes.resize(2);
    volume.Probes[0].Irradiance[0] = {1.0F, 0.0F, 0.0F};
    volume.Probes[1].Irradiance[0] = {3.0F, 0.0F, 0.0F};
    const auto sampled = Keire::Detail::SampleLightProbeIrradiance(volume, {0.5F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    REQUIRE(sampled);
    CHECK(sampled->X == doctest::Approx(2.0F * 0.28209479177387814F));
    CHECK(sampled->Y == doctest::Approx(0.0F));
    CHECK_FALSE(Keire::Detail::SampleLightProbeIrradiance(volume, {2.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}));
}
