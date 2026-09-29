#include "Keire/Animation/RiggingSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    struct GroundingFixture
    {
        Keire::SkeletonAsset Skeleton{{{"Pelvis", -1, {{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"LeftHip", 0, {{-0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"LeftKnee", 1, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"LeftFoot", 2, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"RightHip", 0, {{0.25F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"RightKnee", 4, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"RightFoot", 5, {{0.0F, -1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                       {"Torso", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}}};
        std::vector<Keire::BoneTransform> Pose;
        Keire::FootGroundingRequest Request;

        GroundingFixture()
        {
            for (const auto& bone : Skeleton.Bones())
                Pose.push_back(bone.BindPose);
            Request.Pelvis = 0;
            Request.Torso = 7;
            Request.FootHeight = 0.0F;
            Request.MaximumHorizontalPelvisAdjustment = 0.5F;
            Request.PelvisRotationWeight = 1.0F;
            Request.MaximumPelvisRotationDegrees = 30.0F;
            Request.Contacts.push_back({1, 2, 3, {-0.25F, -0.2F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.25F, 1.0F, 1.0F}});
            Request.Contacts.push_back({4, 5, 6, {4.0F, -5.0F, 0.0F}, {0.6F, 0.8F, 0.0F}, {0.25F, 1.0F, 1.0F}, 0.0F});
        }
    };
} // namespace

TEST_CASE("Zero-weight ground contacts do not pull the pelvis or change active support diagnostics")
{
    GroundingFixture fixture;
    auto singlePose = fixture.Pose;
    auto singleRequest = fixture.Request;
    singleRequest.Contacts.pop_back();
    const auto single = Keire::SolveFootGrounding(fixture.Skeleton, singlePose, singleRequest);
    REQUIRE(single);
    const auto mixed = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(mixed);
    CHECK(fixture.Pose == singlePose);
    CHECK(mixed->SolvedFeet == single->SolvedFeet);
    CHECK(mixed->UnreachableFeet == single->UnreachableFeet);
    CHECK(mixed->MaximumPositionError == single->MaximumPositionError);
    CHECK(mixed->PelvisAdjustment == single->PelvisAdjustment);
    CHECK(mixed->HorizontalPelvisAdjustment == single->HorizontalPelvisAdjustment);
    CHECK(mixed->PelvisRotationAdjustmentDegrees == single->PelvisRotationAdjustmentDegrees);

    GroundingFixture reordered;
    std::ranges::reverse(reordered.Request.Contacts);
    const auto reversed = Keire::SolveFootGrounding(reordered.Skeleton, reordered.Pose, reordered.Request);
    REQUIRE(reversed);
    CHECK(reordered.Pose == singlePose);
    CHECK(reversed->MaximumPositionError == single->MaximumPositionError);
}

TEST_CASE("Disabled ground contacts preserve the exact pose but still reject invalid input")
{
    GroundingFixture fixture;
    fixture.Request.Contacts.front().Weight = 0.0F;
    fixture.Pose[3].Rotation.W = 0.9999F;
    const auto original = fixture.Pose;
    const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(result);
    CHECK(fixture.Pose == original);
    CHECK(result->SolvedFeet == 0);
    CHECK(result->UnreachableFeet == 0);
    CHECK(result->MaximumPositionError == 0.0F);
    CHECK(result->PelvisAdjustment == 0.0F);
    CHECK(result->HorizontalPelvisAdjustment == Keire::Vector3{});
    CHECK(result->PelvisRotationAdjustmentDegrees == 0.0F);

    fixture.Request.Contacts.back().Foot = 99;
    CHECK_FALSE(Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request));
    CHECK(fixture.Pose == original);
    fixture.Request.Contacts.back().Foot = 6;
    fixture.Pose[7].Translation.X = std::numeric_limits<float>::infinity();
    const auto invalid = fixture.Pose;
    CHECK_FALSE(Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request));
    CHECK(fixture.Pose == invalid);
    fixture.Pose = original;
    fixture.Request.Contacts.front().Weight = 1.0F;
    const auto recovered = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(recovered);
    CHECK(recovered->SolvedFeet == 1);
}

TEST_CASE("Ground contact fades bound pelvis correction continuously near zero weight")
{
    for (const float weight : {0.0F, std::numeric_limits<float>::min(), 0.00001F, 0.1F, 0.5F, 1.0F})
    {
        CAPTURE(weight);
        GroundingFixture fixture;
        fixture.Request.Contacts.erase(fixture.Request.Contacts.begin());
        fixture.Request.Contacts.front().Weight = weight;
        fixture.Request.PelvisWeight = 0.5F;
        const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(result);
        CHECK(std::abs(result->PelvisAdjustment) <= 0.5F * weight * 0.5F + 0.000001F);
        const auto horizontal = result->HorizontalPelvisAdjustment;
        CHECK(std::sqrt(horizontal.X * horizontal.X + horizontal.Z * horizontal.Z) <= 0.5F * weight * 0.5F + 0.000001F);
        CHECK(result->PelvisRotationAdjustmentDegrees <= 30.0F * weight * 0.5F + 0.000001F);
    }
}

TEST_CASE("Fading in a second support does not jump the pelvis away from the planted foot")
{
    GroundingFixture baseline;
    const auto base = Keire::SolveFootGrounding(baseline.Skeleton, baseline.Pose, baseline.Request);
    REQUIRE(base);
    GroundingFixture faded;
    faded.Request.Contacts.back().Weight = 0.00001F;
    const auto result = Keire::SolveFootGrounding(faded.Skeleton, faded.Pose, faded.Request);
    REQUIRE(result);
    CHECK(std::abs(result->PelvisAdjustment - base->PelvisAdjustment) < 0.001F);
    CHECK(std::abs(result->HorizontalPelvisAdjustment.X - base->HorizontalPelvisAdjustment.X) < 0.001F);
    CHECK(std::abs(result->PelvisRotationAdjustmentDegrees - base->PelvisRotationAdjustmentDegrees) < 0.001F);
}

TEST_CASE("Reachable foot targets do not report leg limits during contact blending")
{
    for (const float weight : {0.1F, 0.5F, 1.0F})
    {
        CAPTURE(weight);
        GroundingFixture fixture;
        fixture.Request.Pelvis.reset();
        fixture.Request.Torso.reset();
        fixture.Request.Contacts.resize(1);
        fixture.Request.Contacts.front().Position = {-0.25F, 0.4F, 0.4F};
        fixture.Request.Contacts.front().Weight = weight;
        const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(result);
        CHECK(result->SolvedFeet == 1);
        CHECK(result->UnreachableFeet == 0);
        if (weight < 1.0F)
            CHECK(result->MaximumPositionError > fixture.Request.PositionTolerance);
    }
}

TEST_CASE("Unreachable foot targets retain limit diagnostics at partial blend weights")
{
    for (const float weight : {0.1F, 0.5F, 1.0F})
    {
        CAPTURE(weight);
        GroundingFixture fixture;
        fixture.Request.Pelvis.reset();
        fixture.Request.Torso.reset();
        fixture.Request.Contacts.resize(1);
        fixture.Request.Contacts.front().Position = {-0.25F, -2.0F, 0.4F};
        fixture.Request.Contacts.front().Weight = weight;
        const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(result);
        CHECK(result->SolvedFeet == 1);
        CHECK(result->UnreachableFeet == 1);
        CHECK(result->MaximumPositionError > fixture.Request.PositionTolerance);
    }
}
