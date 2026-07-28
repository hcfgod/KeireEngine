#include "Keire/Animation/Skinning.h"

#include <doctest/doctest.h>

#include <array>
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
