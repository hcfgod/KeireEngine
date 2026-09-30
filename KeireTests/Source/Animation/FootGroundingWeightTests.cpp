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

TEST_CASE("Pelvis support release is independent of the blended foot endpoint")
{
    for (const float blend : {0.0F, 0.01F, 0.25F, 0.5F, 0.9F, 1.0F})
    {
        GroundingFixture fixture;
        fixture.Request.Contacts.resize(1);
        auto& contact = fixture.Request.Contacts.front();
        contact.SupportPosition = Keire::Vector3{-0.25F, -0.2F, 0.0F};
        contact.Position.Y = -0.2F * blend;
        contact.SupportWeight = blend;
        fixture.Request.PelvisWeight = 1.0F;
        const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(result);
        CAPTURE(blend);
        CHECK(result->PelvisAdjustment == doctest::Approx(-0.2F * blend).epsilon(0.00001F));
        CHECK(result->MaximumPositionError <= fixture.Request.PositionTolerance);
    }
}

TEST_CASE("Standing balance centers over active support within its limit and weight")
{
    for (const float weight : {0.0F, 0.25F, 1.0F})
    {
        GroundingFixture fixture;
        fixture.Request.BalanceOverSupport = true;
        fixture.Request.MaximumHorizontalPelvisAdjustment = 0.15F;
        fixture.Request.PelvisWeight = weight;
        const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(result);
        CHECK(result->HorizontalPelvisAdjustment.X == doctest::Approx(-0.15F * weight));
        CHECK(result->HorizontalPelvisAdjustment.Z == doctest::Approx(0.0F));
        if (weight == 1.0F)
            CHECK(result->UnreachableFeet == 0);
    }

    GroundingFixture fixture;
    fixture.Request.BalanceOverSupport = true;
    fixture.Request.Contacts.front().Weight = 0.0F;
    const auto original = fixture.Pose;
    const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(result);
    CHECK(fixture.Pose == original);
}

TEST_CASE("Invalid support anchors reject grounding without changing the pose")
{
    GroundingFixture fixture;
    const auto original = fixture.Pose;
    fixture.Request.Contacts.front().SupportPosition =
        Keire::Vector3{0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F};
    CHECK_FALSE(Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request));
    CHECK(fixture.Pose == original);
}

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

TEST_CASE("Acquiring support fades pelvis influence independently of full foot IK")
{
    GroundingFixture fixture;
    fixture.Request.Contacts[0].Position = {-0.25F, 0.0F, 0.3F};
    fixture.Request.Contacts[1].Position = {0.25F, 0.0F, -0.3F};
    fixture.Request.Contacts[1].Normal = {0.0F, 1.0F, 0.0F};
    fixture.Request.Contacts[1].Weight = 1.0F;
    fixture.Request.Contacts[1].SupportWeight = 0.0F;
    auto previous = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(previous);
    CHECK(previous->SolvedFeet == 2);
    CHECK(previous->HorizontalPelvisAdjustment.Z == doctest::Approx(0.3F));
    const auto bindPose = [&]
    {
        std::vector<Keire::BoneTransform> pose;
        for (const auto& bone : fixture.Skeleton.Bones())
            pose.push_back(bone.BindPose);
        return pose;
    };
    for (int frame = 1; frame <= 100; ++frame)
    {
        fixture.Pose = bindPose();
        fixture.Request.Contacts[1].SupportWeight = static_cast<float>(frame) / 100.0F;
        const auto current = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
        REQUIRE(current);
        CHECK(current->SolvedFeet == 2);
        CHECK(std::abs(current->HorizontalPelvisAdjustment.Z - previous->HorizontalPelvisAdjustment.Z) < 0.006F);
        previous = current;
    }
    CHECK(previous->HorizontalPelvisAdjustment.Z == doctest::Approx(0.0F));
    for (auto& contact : fixture.Request.Contacts)
        contact.SupportWeight = 0.0F;
    fixture.Pose = bindPose();
    const auto unsupported = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(unsupported);
    CHECK(unsupported->SolvedFeet == 2);
    CHECK(unsupported->HorizontalPelvisAdjustment == Keire::Vector3{});
    CHECK(unsupported->PelvisAdjustment == 0.0F);
    for (const float invalid : {-0.1F, 1.1F, std::numeric_limits<float>::quiet_NaN()})
    {
        fixture.Request.Contacts[0].SupportWeight = invalid;
        const auto original = fixture.Pose;
        CHECK_FALSE(Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request));
        CHECK(fixture.Pose == original);
    }
}

TEST_CASE("Foot grounding preserves sampled foot orientation when terrain rotation is disabled")
{
    GroundingFixture fixture;
    fixture.Request.Contacts.resize(1);
    fixture.Request.Contacts[0].Position = {-0.25F, 0.3F, 0.4F};
    fixture.Request.Contacts[0].RotationWeight = 0.0F;
    fixture.Request.Pelvis.reset();
    fixture.Request.Torso.reset();
    const auto sampledRotation = Keire::Math::EulerDegreesToQuaternion({25.0F, 10.0F, 0.0F});
    fixture.Pose[3].Rotation = sampledRotation;
    REQUIRE(Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request));
    Keire::Matrix4 model;
    for (std::size_t index = 0; index <= 3; ++index)
    {
        const auto& bone = fixture.Pose[index];
        const auto local = Keire::Math::ComposeTransform(bone.Translation, bone.Rotation, bone.Scale);
        model = index == 0 ? local : Keire::Math::Multiply(model, local);
    }
    Keire::Vector3 translation;
    Keire::Vector3 scale;
    Keire::Quaternion rotation;
    REQUIRE(Keire::Math::DecomposeTransform(model, translation, rotation, scale));
    const auto dot = rotation.X * sampledRotation.X + rotation.Y * sampledRotation.Y + rotation.Z * sampledRotation.Z +
                     rotation.W * sampledRotation.W;
    CHECK(std::abs(dot) == doctest::Approx(1.0F).epsilon(0.00001F));
}

TEST_CASE("Foot grounding lowers the pelvis only when a support exceeds leg reach")
{
    GroundingFixture fixture;
    fixture.Request.Contacts.resize(1);
    fixture.Request.Torso.reset();
    fixture.Request.MaximumHorizontalPelvisAdjustment = 0.0F;
    fixture.Pose[2].Rotation = Keire::Math::EulerDegreesToQuaternion({60.0F, 0.0F, 0.0F});
    fixture.Request.Contacts[0].Position = {-0.25F, 0.1F, 0.1F};
    const auto sampled = fixture.Pose;
    const auto reachable = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(reachable);
    CHECK(reachable->PelvisAdjustment == doctest::Approx(0.0F));
    CHECK(reachable->UnreachableFeet == 0);
    fixture.Pose = sampled;
    fixture.Request.MaximumPelvisAdjustment = 0.5F;
    fixture.Request.Contacts[0].Position = {-0.25F, -0.2F, 0.8F};
    const auto lowered = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(lowered);
    CHECK(lowered->PelvisAdjustment == doctest::Approx(-0.2F + std::sqrt(4.0F - 0.64F) - 2.0F));
    CHECK(lowered->UnreachableFeet == 0);
}

TEST_CASE("Flat support preserves authored torso lean throughout contact release")
{
    for (const float lean : {-25.0F, -10.0F, 10.0F, 25.0F})
    {
        for (const float support : {0.0F, 0.01F, 0.25F, 0.5F, 1.0F})
        {
            CAPTURE(lean);
            CAPTURE(support);
            GroundingFixture fixture;
            fixture.Pose[0].Rotation = Keire::Math::EulerDegreesToQuaternion({lean, 0.0F, 0.0F});
            fixture.Request.MaximumHorizontalPelvisAdjustment = 0.0F;
            fixture.Request.MaximumPelvisAdjustment = 0.0F;
            fixture.Request.Contacts.resize(1);
            fixture.Request.Contacts.front().SupportWeight = support;
            const auto sampledRotation = fixture.Pose[0].Rotation;
            const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
            REQUIRE(result);
            CHECK(result->PelvisRotationAdjustmentDegrees == doctest::Approx(0.0F).epsilon(0.00001F));
            CHECK(fixture.Pose[0].Rotation == sampledRotation);
        }
    }
}

TEST_CASE("Slope correction adds terrain tilt to authored torso lean")
{
    GroundingFixture fixture;
    fixture.Pose[0].Rotation = Keire::Math::EulerDegreesToQuaternion({10.0F, 0.0F, 0.0F});
    fixture.Request.MaximumHorizontalPelvisAdjustment = 0.0F;
    fixture.Request.MaximumPelvisAdjustment = 0.0F;
    fixture.Request.Contacts.resize(1);
    constexpr float slopeRadians = 10.0F * 0.01745329251994329577F;
    fixture.Request.Contacts.front().Normal = {0.0F, std::cos(slopeRadians), std::sin(slopeRadians)};
    const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
    REQUIRE(result);
    CHECK(result->PelvisRotationAdjustmentDegrees == doctest::Approx(10.0F).epsilon(0.001F));
    const auto expected = Keire::Math::EulerDegreesToQuaternion({20.0F, 0.0F, 0.0F});
    const auto actual = fixture.Pose[0].Rotation;
    CHECK(std::abs(actual.X * expected.X + actual.Y * expected.Y + actual.Z * expected.Z + actual.W * expected.W) >
          0.99999F);
}

TEST_CASE("Authored strides do not shift the pelvis as matching ground supports change")
{
    for (const float phase : {-25.0F, -10.0F, 10.0F, 25.0F})
    {
        for (const float support : {0.0F, 0.01F, 0.25F, 0.5F, 1.0F})
        {
            CAPTURE(phase);
            CAPTURE(support);
            GroundingFixture fixture;
            fixture.Request.Torso.reset();
            fixture.Pose[1].Rotation = Keire::Math::EulerDegreesToQuaternion({phase, 0.0F, 0.0F});
            fixture.Pose[4].Rotation = Keire::Math::EulerDegreesToQuaternion({-phase, 0.0F, 0.0F});
            std::vector<Keire::Matrix4> models;
            for (std::size_t index = 0; index < fixture.Pose.size(); ++index)
            {
                const auto& bone = fixture.Pose[index];
                const auto local = Keire::Math::ComposeTransform(bone.Translation, bone.Rotation, bone.Scale);
                const auto parent = fixture.Skeleton.Bones()[index].Parent;
                models.push_back(parent < 0 ? local : Keire::Math::Multiply(models[parent], local));
            }
            for (auto& contact : fixture.Request.Contacts)
            {
                contact.Position = Keire::Math::TransformPoint(models[contact.Foot], {});
                contact.Normal = {0.0F, 1.0F, 0.0F};
                contact.Weight = 1.0F;
                contact.RotationWeight = 0.0F;
            }
            fixture.Request.Contacts[1].SupportWeight = support;
            const auto pelvis = fixture.Pose[0].Translation;
            const auto result = Keire::SolveFootGrounding(fixture.Skeleton, fixture.Pose, fixture.Request);
            REQUIRE(result);
            CHECK(result->HorizontalPelvisAdjustment.X == doctest::Approx(0.0F).epsilon(0.00001F));
            CHECK(result->HorizontalPelvisAdjustment.Z == doctest::Approx(0.0F).epsilon(0.00001F));
            CHECK(fixture.Pose[0].Translation.X == doctest::Approx(pelvis.X));
            CHECK(fixture.Pose[0].Translation.Z == doctest::Approx(pelvis.Z));
        }
    }
}
