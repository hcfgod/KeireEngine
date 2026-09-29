#include "Keire/Animation/RiggingSystem.h"
#include "KeireInternal/Animation/RiggingMath.h"

#include <doctest/doctest.h>

#include <vector>

TEST_CASE("FABRIK reaches closer collinear targets from a straight chain")
{
    for (const float scale : {0.001F, 1.0F, 10.0F})
        for (const auto rotation : {Keire::Quaternion{}, Keire::Math::EulerDegreesToQuaternion({37.0F, 61.0F, 19.0F})})
        {
            CAPTURE(scale);
            const Keire::SkeletonAsset skeleton(
                {{"Parent", -1, {{2.0F, 3.0F, 4.0F}, rotation, {scale, scale, scale}}, {}},
                 {"Root", 0, {{}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                 {"Middle", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                 {"End", 2, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                 {"Other", 0, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}});
            std::vector<Keire::BoneTransform> original;
            for (const auto& bone : skeleton.Bones())
                original.push_back(bone.BindPose);
            const auto before = Keire::RiggingDetail::WorldMatrices(skeleton, original);
            for (const float distance : {0.0F, 0.5F, 1.0F, 1.5F})
            {
                CAPTURE(distance);
                auto pose = original;
                const auto target = Keire::Math::TransformPoint(before[1], {0.0F, distance, 0.0F});
                const Keire::FabrikIkRequest request{{1, 2, 3}, target, 128, scale * 0.001F, 1.0F};
                REQUIRE(Keire::SolveFabrikIk(skeleton, pose, request));
                const auto after = Keire::RiggingDetail::WorldMatrices(skeleton, pose);
                const auto root = Keire::Math::TransformPoint(after[1], {});
                const auto middle = Keire::Math::TransformPoint(after[2], {});
                const auto end = Keire::Math::TransformPoint(after[3], {});
                CHECK(Keire::RiggingDetail::Length(Keire::RiggingDetail::Subtract(end, target)) < scale * 0.003F);
                CHECK(root == Keire::Math::TransformPoint(before[1], {}));
                CHECK(Keire::RiggingDetail::Length(Keire::RiggingDetail::Subtract(middle, root)) ==
                      doctest::Approx(scale).epsilon(0.002));
                CHECK(Keire::RiggingDetail::Length(Keire::RiggingDetail::Subtract(end, middle)) ==
                      doctest::Approx(scale).epsilon(0.002));
                CHECK(pose[0] == original[0]);
                CHECK(pose[4] == original[4]);
                auto repeated = original;
                REQUIRE(Keire::SolveFabrikIk(skeleton, repeated, request));
                CHECK(repeated == pose);
                auto disabled = request;
                disabled.Weight = 0.0F;
                repeated = original;
                REQUIRE(Keire::SolveFabrikIk(skeleton, repeated, disabled));
                CHECK(repeated == original);
            }
        }
}
