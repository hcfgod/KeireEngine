#include "Keire/Animation/Skinning.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("CPU skinning applies deterministic linear blend and dual quaternion transforms")
{
    const std::array source{
        Keire::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {}, {}, {1.0F, 0.0F, 0.0F, 1.0F}}};
    Keire::SkinVertexInfluence influence;
    influence.Bones[0] = 0;
    influence.Weights[0] = 1.0F;
    const std::array influences{influence};
    const std::array palette{Keire::Math::ComposeTransform({3.0F, 2.0F, -1.0F}, {}, {1.0F, 1.0F, 1.0F})};

    const auto linear = Keire::SkinMeshCpu(source, influences, palette, Keire::SkinningMethod::LinearBlend);
    REQUIRE(linear.size() == 1);
    CHECK(linear.front().Position.X == doctest::Approx(4.0F));
    CHECK(linear.front().Position.Y == doctest::Approx(2.0F));
    CHECK(linear.front().Position.Z == doctest::Approx(-1.0F));
    CHECK(linear.front().Normal.Y == doctest::Approx(1.0F));

    const auto dualQuaternion = Keire::SkinMeshCpu(source, influences, palette, Keire::SkinningMethod::DualQuaternion);
    REQUIRE(dualQuaternion.size() == 1);
    CHECK(dualQuaternion.front().Position.X == doctest::Approx(4.0F));
    CHECK(dualQuaternion.front().Position.Y == doctest::Approx(2.0F));
    CHECK(dualQuaternion.front().Position.Z == doctest::Approx(-1.0F));
}

TEST_CASE("CPU skinning validates cardinality and preserves vertices without valid weights")
{
    const std::array source{Keire::MeshVertex{{2.0F, 3.0F, 4.0F}}};
    const std::array emptyInfluences{Keire::SkinVertexInfluence{}};
    const std::array palette{Keire::Matrix4{}};

    const auto result = Keire::SkinMeshCpu(source, emptyInfluences, palette);
    REQUIRE(result.size() == 1);
    CHECK(result.front().Position == source.front().Position);

    const auto expanded = Keire::ExpandSkinInfluences(emptyInfluences);
    REQUIRE(expanded.size() == 1);
    CHECK(expanded.front().Count == 4);
    std::vector<Keire::MeshVertex> invalidDestination;
    CHECK_THROWS_AS(
        Keire::SkinMeshCpu(source, expanded, palette, Keire::SkinningMethod::LinearBlend, invalidDestination),
        std::invalid_argument);
}

TEST_CASE("Bind-space influence bounds produce conservative linear-blend current-pose bounds")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{2.0F, 1.0F, 0.0F}},
                              Keire::MeshVertex{{4.0F, 2.0F, 0.0F}}};
    constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
    const std::array submeshes{Keire::MeshSubmesh{0, 3, 0, {{0.0F, 0.0F, 0.0F}, {4.0F, 2.0F, 0.0F}}}};
    std::array<Keire::SkinVertexInfluence8, 3> influences;
    influences[0].Bones[0] = 0;
    influences[0].Weights[0] = 1.0F;
    influences[0].Count = 1;
    influences[1].Bones[0] = 0;
    influences[1].Weights[0] = 0.25F;
    influences[1].Bones[1] = 1;
    influences[1].Weights[1] = 0.75F;
    influences[1].Count = 2;
    influences[2].Bones[0] = 1;
    influences[2].Weights[0] = 1.0F;
    influences[2].Count = 1;

    const auto bindBounds = Keire::CalculateBindSpaceSkinInfluenceBounds(vertices, indices, submeshes, influences);
    REQUIRE(bindBounds.size() == 2);
    CHECK(bindBounds[0].Submesh == 0);
    CHECK(bindBounds[0].Bone == 0);
    CHECK(bindBounds[0].Minimum == (Keire::Vector3{0.0F, 0.0F, 0.0F}));
    CHECK(bindBounds[0].Maximum == (Keire::Vector3{2.0F, 1.0F, 0.0F}));
    CHECK(bindBounds[1].Bone == 1);
    CHECK(bindBounds[1].Minimum == (Keire::Vector3{2.0F, 1.0F, 0.0F}));
    CHECK(bindBounds[1].Maximum == (Keire::Vector3{4.0F, 2.0F, 0.0F}));

    const std::array palette{Keire::Math::ComposeTransform({10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}),
                             Keire::Math::ComposeTransform({-10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F})};
    const auto poseBounds = Keire::CalculateLinearBlendPoseBounds(bindBounds, 1, palette);
    REQUIRE(poseBounds.size() == 1);
    CHECK(poseBounds.front().Minimum == (Keire::Vector3{-8.0F, 0.0F, 0.0F}));
    CHECK(poseBounds.front().Maximum == (Keire::Vector3{12.0F, 2.0F, 0.0F}));
}

TEST_CASE("Skin influence bounds reject incomplete and non-finite inputs")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}};
    constexpr std::array<std::uint32_t, 1> indices{0};
    const std::array submeshes{Keire::MeshSubmesh{0, 1, 0, {}}};
    std::array<Keire::SkinVertexInfluence8, 1> influences;
    influences.front().Count = 1;
    influences.front().Weights[0] = 1.0F;
    CHECK_NOTHROW((void)Keire::CalculateBindSpaceSkinInfluenceBounds(vertices, indices, submeshes, influences));

    influences.front().Weights[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS((void)Keire::CalculateBindSpaceSkinInfluenceBounds(vertices, indices, submeshes, influences),
                    std::invalid_argument);
    const std::array bounds{Keire::SkinInfluenceBounds{0, 0, {}, {1.0F, 1.0F, 1.0F}}};
    const std::array palette{Keire::Matrix4{}};
    CHECK_THROWS_AS((void)Keire::CalculateLinearBlendPoseBounds(bounds, 2, palette), std::invalid_argument);
}
