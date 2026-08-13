#include "KeireInternal/Scenes/FootGroundingSpace.h"

#include <doctest/doctest.h>

#include <cmath>

namespace
{
    [[nodiscard]] float Distance(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
    {
        const auto x = right.X - left.X;
        const auto y = right.Y - left.Y;
        const auto z = right.Z - left.Z;
        return std::sqrt(x * x + y * y + z * z);
    }
} // namespace

TEST_CASE("Foot grounding converts world-space authoring distances for scaled animated models")
{
    const auto modelToWorld = Keire::Math::ComposeTransform(
        {2.0F, 0.5F, -1.0F}, Keire::Math::EulerDegreesToQuaternion({0.0F, 35.0F, 0.0F}), {0.01F, 0.01F, 0.01F});
    const auto worldToModel = Keire::Math::Inverse(modelToWorld);
    const Keire::Vector3 hitPosition{2.25F, 0.6F, -0.75F};
    const Keire::Vector3 hitNormal{0.0F, 1.0F, 0.0F};

    const auto contact = Keire::Detail::ToModelFootGroundContact(worldToModel, hitPosition, hitNormal, 0.02F);
    REQUIRE(contact);
    const auto reconstructedWorldPosition = Keire::Math::TransformPoint(modelToWorld, contact->Position);
    CHECK(Distance(reconstructedWorldPosition, {hitPosition.X, hitPosition.Y + 0.02F, hitPosition.Z}) < 0.00001F);

    const auto modelPelvisLimit = Keire::Detail::WorldVerticalDistanceToModel(worldToModel, 0.5F);
    CHECK(modelPelvisLimit == doctest::Approx(50.0F));
    const auto reconstructedWorldAdjustment =
        Keire::Math::TransformDirection(modelToWorld, {0.0F, modelPelvisLimit, 0.0F});
    CHECK(Distance(reconstructedWorldAdjustment, {0.0F, 0.5F, 0.0F}) < 0.00001F);

    CHECK_FALSE(Keire::Detail::ToModelFootGroundContact(worldToModel, hitPosition, {}, 0.02F));
}
