#include "KeireInternal/Scenes/AnimationIkPasses.h"
#include "KeireInternal/Scenes/AnimationNamedIkGoals.h"

#include "Keire/Scenes/Scene.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

TEST_CASE("Automatic foot grounding recognizes character hierarchy colliders")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto movingPlatform = scene->CreateEntity("Moving platform");
    auto player = scene->CreateEntity("Player", movingPlatform);
    REQUIRE(player.AddComponent<Keire::CharacterControllerComponent>());
    const auto visual = scene->CreateEntity("Visual", player);
    const auto nestedCollider = scene->CreateEntity("Nested collider", visual);
    const auto ground = scene->CreateEntity("Ground");

    CHECK(Keire::Detail::IsSameOrDescendantOf(player, player));
    CHECK(Keire::Detail::IsSameOrDescendantOf(visual, player));
    CHECK(Keire::Detail::IsSameOrDescendantOf(nestedCollider, player));
    CHECK_FALSE(Keire::Detail::IsSameOrDescendantOf(player, visual));
    CHECK_FALSE(Keire::Detail::IsSameOrDescendantOf(ground, player));
    CHECK_FALSE(Keire::Detail::IsSameOrDescendantOf({}, player));
    CHECK(Keire::Detail::FindCharacterControllerRoot(visual) == player);
    CHECK(Keire::Detail::FindCharacterControllerRoot(nestedCollider) == player);
    CHECK_FALSE(Keire::Detail::FindCharacterControllerRoot(movingPlatform));
}

TEST_CASE("Animation IK passes remain independent when an earlier pass reports a diagnostic")
{
    std::vector<std::string> evaluated;
    const auto diagnostics = Keire::Detail::EvaluateIndependentAnimationIkPasses(
        [&]
        {
            evaluated.emplace_back("managed");
            return std::string("Managed IK failed.");
        },
        [&]
        {
            evaluated.emplace_back("left-arm");
            return std::string("Left arm IK has no target.");
        },
        [&]
        {
            evaluated.emplace_back("right-arm");
            return std::string{};
        },
        [&]
        {
            evaluated.emplace_back("feet");
            return std::string{};
        });

    CHECK(evaluated == std::vector<std::string>{"managed", "left-arm", "right-arm", "feet"});
    CHECK(diagnostics == "Managed IK failed.\nLeft arm IK has no target.");
}

TEST_CASE("Automatic arm IK preserves its elbow side through sampled-pose singularities")
{
    Keire::Detail::AutomaticLimbIkState state;
    const Keire::Vector3 root{};
    const Keire::Vector3 target{1.25F, 0.0F, 0.0F};
    const auto first =
        Keire::Detail::StableAutomaticLimbPole(root, {0.75F, 0.0F, 0.1F}, {1.5F, 0.0F, 0.0F}, target, state);
    const auto crossed =
        Keire::Detail::StableAutomaticLimbPole(root, {0.75F, 0.0F, -0.1F}, {1.5F, 0.0F, 0.0F}, target, state);
    const auto straight = Keire::Detail::StableAutomaticLimbPole(root, {0.75F, 0.0F, 0.0F}, {1.5F, 0.0F, 0.0F},
                                                                 {0.00001F, 0.0F, 0.0F}, state);

    CHECK(first.Z > 0.0F);
    CHECK(crossed.Z > 0.0F);
    CHECK(straight.Z > 0.0F);
    CHECK(std::isfinite(straight.X));
    CHECK(std::isfinite(straight.Y));
    CHECK(std::isfinite(straight.Z));
}

TEST_CASE("Automatic leg IK preserves the sampled knee bend instead of forcing a model axis")
{
    Keire::Detail::AutomaticLimbIkState state;
    const Keire::Vector3 hip{0.0F, 2.0F, 0.0F};
    const Keire::Vector3 ankle{0.0F, 0.0F, 0.0F};
    const Keire::Vector3 target{0.0F, -0.2F, 0.0F};
    const auto sampled = Keire::Detail::StableAutomaticLimbPole(hip, {0.0F, 1.0F, -0.2F}, ankle, target, state);
    const auto straight = Keire::Detail::StableAutomaticLimbPole(hip, {0.0F, 1.0F, 0.0F}, ankle, target, state);

    CHECK(sampled.Z < 0.0F);
    CHECK(straight.Z < 0.0F);
    CHECK(std::isfinite(straight.X));
    CHECK(std::isfinite(straight.Y));
    CHECK(std::isfinite(straight.Z));
}

TEST_CASE("Automatic leg IK derives a shared knee plane from rig geometry and resists slope sway")
{
    const Keire::Vector3 leftHip{-0.25F, 2.0F, 0.0F};
    const Keire::Vector3 rightHip{0.25F, 2.0F, 0.0F};
    const auto reference = Keire::Detail::AutomaticBipedKneeReference(leftHip, rightHip, {0.0F, 1.0F, 0.0F});
    REQUIRE(reference.Z > 0.99F);
    const auto oriented =
        Keire::Detail::OrientBipedKneeReference(reference, leftHip, {-0.25F, 1.0F, -0.25F}, {-0.25F, 0.0F, 0.0F},
                                                rightHip, {0.25F, 1.0F, -0.25F}, {0.25F, 0.0F, 0.0F});
    CHECK(oriented.Z < -0.99F);

    Keire::Detail::AutomaticLimbIkState leftState;
    Keire::Detail::AutomaticLimbIkState rightState;
    const auto solve = [&](const Keire::Vector3 target, const Keire::Vector3 leftKnee, const Keire::Vector3 rightKnee)
    {
        const auto left = Keire::Detail::StableAutomaticLimbPole(leftHip, leftKnee, {-0.25F, 0.0F, 0.0F}, target,
                                                                 reference, 1.0F / 60.0F, 0.12F, 0.9F, leftState);
        const auto right = Keire::Detail::StableAutomaticLimbPole(rightHip, rightKnee, {0.25F, 0.0F, 0.0F}, target,
                                                                  reference, 1.0F / 60.0F, 0.12F, 0.9F, rightState);
        return std::array{left, right};
    };

    const auto initial = solve({0.0F, 0.1F, 0.2F}, {-0.5F, 1.0F, 0.25F}, {0.5F, 1.0F, 0.25F});
    const auto sloped = solve({0.4F, 0.65F, -0.15F}, {0.2F, 1.0F, -0.25F}, {-0.2F, 1.0F, -0.25F});
    const auto leftInitialDirection = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(initial[0], leftHip));
    const auto rightInitialDirection = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(initial[1], rightHip));
    const auto leftSlopeDirection = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(sloped[0], leftHip));
    const auto rightSlopeDirection = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(sloped[1], rightHip));

    CHECK(Keire::Detail::IkDot(leftInitialDirection, rightInitialDirection) > 0.95F);
    CHECK(Keire::Detail::IkDot(leftSlopeDirection, rightSlopeDirection) > 0.9F);
    CHECK(Keire::Detail::IkDot(leftInitialDirection, leftSlopeDirection) > 0.8F);
    CHECK(Keire::Detail::IkDot(rightInitialDirection, rightSlopeDirection) > 0.8F);
}

TEST_CASE("Automatic leg IK keeps its knee side when a near-straight sampled reference changes sign")
{
    Keire::Detail::AutomaticLimbIkState state;
    const Keire::Vector3 hip{0.0F, 2.0F, 0.0F};
    const Keire::Vector3 ankle{};
    for (int frame = 0; frame < 120; ++frame)
    {
        const float sign = frame % 2 == 0 ? 1.0F : -1.0F;
        const auto pole =
            Keire::Detail::StableAutomaticLimbPole(hip, {0.0F, 1.0F, sign * 0.0001F}, ankle, {0.0F, 0.1F, 0.0F},
                                                   {0.0F, 0.0F, sign}, 1.0F / 60.0F, 0.12F, 0.9F, state);
        CAPTURE(frame);
        CHECK(pole.Z > 1.9F);
    }
}

TEST_CASE("Automatic leg IK follows a continuously turning reference without reversing its knee")
{
    Keire::Detail::AutomaticLimbIkState state;
    const Keire::Vector3 hip{0.0F, 2.0F, 0.0F};
    Keire::Vector3 previous{0.0F, 0.0F, 1.0F};
    for (int frame = 0; frame <= 360; ++frame)
    {
        const float angle = static_cast<float>(frame) * 0.01745329252F;
        const Keire::Vector3 reference{std::sin(angle), 0.0F, std::cos(angle)};
        const auto pole =
            Keire::Detail::StableAutomaticLimbPole(hip, {reference.X * 0.2F, 1.0F, reference.Z * 0.2F}, {},
                                                   {0.0F, 0.1F, 0.0F}, reference, 1.0F / 60.0F, 0.12F, 0.9F, state);
        const auto direction = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(pole, hip));
        CHECK(Keire::Detail::IkDot(previous, direction) > 0.99F);
        CHECK(Keire::Detail::IkDot(reference, direction) > 0.95F);
        previous = direction;
    }
}

TEST_CASE("Automatic foot planting locks animation drift and releases on a deliberate lift")
{
    Keire::Detail::AutomaticFootPlantState state;
    const Keire::Vector3 normal{0.0F, 1.0F, 0.0F};

    const auto planted = Keire::Detail::UpdateAutomaticFootPlant({0.0F, 0.04F, 0.0F}, {0.0F, 0.0F, 0.0F}, normal, 1.0F,
                                                                 0.08F, 0.18F, state);
    REQUIRE(state.Locked);
    CHECK(planted == Keire::Vector3{});

    const auto animationDrift = Keire::Detail::UpdateAutomaticFootPlant({0.12F, 0.06F, 0.0F}, {0.12F, 0.0F, 0.0F},
                                                                        normal, 1.0F, 0.08F, 0.18F, state);
    CHECK(state.Locked);
    CHECK(animationDrift == Keire::Vector3{});

    const auto overextended = Keire::Detail::UpdateAutomaticFootPlant({0.22F, 0.06F, 0.0F}, {0.22F, 0.0F, 0.0F}, normal,
                                                                      1.0F, 0.08F, 0.18F, state);
    CHECK(state.Locked);
    CHECK((state.Position == Keire::Vector3{0.22F, 0.0F, 0.0F}));
    CHECK((overextended == Keire::Vector3{0.22F, 0.0F, 0.0F}));

    const auto lifted = Keire::Detail::UpdateAutomaticFootPlant({0.22F, 0.3F, 0.0F}, {0.22F, 0.0F, 0.0F}, normal, 1.0F,
                                                                0.08F, 0.18F, state);
    CHECK_FALSE(state.Locked);
    CHECK((lifted == Keire::Vector3{0.22F, 0.0F, 0.0F}));
}

TEST_CASE("Moving foot supports re-anchor before dragging a planted leg into full extension")
{
    const Keire::Vector3 planted{0.0F, 0.0F, 0.0F};
    const Keire::Vector3 normal{0.0F, 1.0F, 0.0F};

    CHECK_FALSE(Keire::Detail::ShouldReanchorMovingFootSupport({0.17F, 0.0F, 0.0F}, planted, normal, 1.0F, 0.18F));
    CHECK(Keire::Detail::ShouldReanchorMovingFootSupport({0.19F, 0.0F, 0.0F}, planted, normal, 1.0F, 0.18F));
    CHECK_FALSE(Keire::Detail::ShouldReanchorMovingFootSupport({0.0F, 0.5F, 0.0F}, planted, normal, 1.0F, 0.18F));
    CHECK(Keire::Detail::ShouldReanchorMovingFootSupport(planted, planted, {}, 1.0F, 0.18F));
}

TEST_CASE("Automatic foot planting switches to a newly occluding support and recovers from penetration")
{
    const Keire::Vector3 surface{0.0F, 0.0F, 0.0F};
    const Keire::Vector3 normal{0.0F, 1.0F, 0.0F};

    CHECK(Keire::Detail::ShouldReplaceAutomaticFootSupport(surface, normal, {0.0F, 0.01F, 0.0F}));
    CHECK_FALSE(Keire::Detail::ShouldReplaceAutomaticFootSupport(surface, normal, {0.5F, 0.0F, 0.0F}));
    CHECK_FALSE(Keire::Detail::ShouldReplaceAutomaticFootSupport(surface, normal, {0.0F, -0.01F, 0.0F}));

    Keire::Detail::AutomaticFootPlantState state;
    REQUIRE(Keire::Detail::ForceAutomaticFootPlant({0.0F, 0.25F, 0.0F}, normal, state));
    CHECK(state.Locked);
    CHECK((state.Position == Keire::Vector3{0.0F, 0.25F, 0.0F}));
    CHECK_FALSE(Keire::Detail::ForceAutomaticFootPlant({}, {}, state));
    CHECK_FALSE(state.Locked);
}

TEST_CASE("Automatic foot grounding smooths support handoffs independently of frame rate")
{
    constexpr Keire::Vector3 normal{0.0F, 1.0F, 0.0F};
    Keire::Detail::AutomaticFootGroundingSmoothingState sixtyHertz;
    Keire::Detail::AutomaticFootGroundingSmoothingState thirtyHertz;
    REQUIRE(Keire::Detail::UpdateAutomaticFootGroundingSmoothing({}, {}, {}, 1.0F / 60.0F, 0.1F, sixtyHertz) ==
            std::nullopt);
    REQUIRE(
        Keire::Detail::UpdateAutomaticFootGroundingSmoothing({Keire::Vector3{}}, {normal}, {}, 0.0F, 0.0F, sixtyHertz));
    REQUIRE(Keire::Detail::UpdateAutomaticFootGroundingSmoothing({Keire::Vector3{}}, {normal}, {}, 0.0F, 0.0F,
                                                                 thirtyHertz));

    std::optional<Keire::Detail::AutomaticFootGroundingTarget> sixtyHertzTarget;
    std::optional<Keire::Detail::AutomaticFootGroundingTarget> thirtyHertzTarget;
    for (std::size_t frame = 0; frame < 60; ++frame)
    {
        sixtyHertzTarget = Keire::Detail::UpdateAutomaticFootGroundingSmoothing(
            {Keire::Vector3{1.0F, -1.0F, 0.0F}}, {normal}, {}, 1.0F / 60.0F, 0.1F, sixtyHertz);
    }
    for (std::size_t frame = 0; frame < 30; ++frame)
    {
        thirtyHertzTarget = Keire::Detail::UpdateAutomaticFootGroundingSmoothing(
            {Keire::Vector3{1.0F, -1.0F, 0.0F}}, {normal}, {}, 1.0F / 30.0F, 0.1F, thirtyHertz);
    }

    REQUIRE(sixtyHertzTarget);
    REQUIRE(thirtyHertzTarget);
    CHECK(sixtyHertzTarget->Position.X == doctest::Approx(thirtyHertzTarget->Position.X).epsilon(0.0001));
    CHECK(sixtyHertzTarget->Position.Y == doctest::Approx(thirtyHertzTarget->Position.Y).epsilon(0.0001));
    CHECK(sixtyHertzTarget->Blend == doctest::Approx(thirtyHertzTarget->Blend).epsilon(0.0001));
    CHECK(sixtyHertzTarget->Position.X < 1.0F);
    CHECK(sixtyHertzTarget->Position.X > 0.99F);
}

TEST_CASE("Automatic foot grounding blends endpoints above flat and sloped contact planes")
{
    for (const auto normal : {Keire::Vector3{0.0F, 1.0F, 0.0F}, Keire::Vector3{0.6F, 0.8F, 0.0F}})
    {
        for (const float rate : {30.0F, 60.0F, 144.0F})
        {
            Keire::Detail::AutomaticFootGroundingSmoothingState state;
            const Keire::Vector3 surface{0.0F, 0.15F, 0.0F};
            for (int frame = 0; frame < 60; ++frame)
            {
                const auto target = Keire::Detail::UpdateAutomaticFootGroundingSmoothing(surface, normal, {},
                                                                                         1.0F / rate, 0.12F, state);
                REQUIRE(target);
                CHECK(Keire::Detail::IkDot(Keire::Detail::IkSubtract(target->Position, surface), normal) >= -0.000001F);
            }
            const auto released =
                Keire::Detail::UpdateAutomaticFootGroundingSmoothing({}, {}, {}, 1.0F / rate, 0.12F, state);
            REQUIRE(released);
            CHECK(Keire::Detail::IkDot(Keire::Detail::IkSubtract(released->Position, surface), normal) < 0.0F);
        }
    }
}

TEST_CASE("Automatic foot grounding keeps rising surfaces collision safe and fades released contacts")
{
    constexpr Keire::Vector3 normal{0.0F, 1.0F, 0.0F};
    Keire::Detail::AutomaticFootGroundingSmoothingState state;
    REQUIRE(Keire::Detail::UpdateAutomaticFootGroundingSmoothing({Keire::Vector3{}}, {normal}, {}, 0.0F, 0.0F, state));

    const auto rising = Keire::Detail::UpdateAutomaticFootGroundingSmoothing({Keire::Vector3{1.0F, 0.5F, 0.0F}},
                                                                             {normal}, {}, 1.0F / 60.0F, 0.2F, state);
    REQUIRE(rising);
    CHECK(rising->Position.Y == doctest::Approx(0.5F));
    CHECK(rising->Position.X > 0.0F);
    CHECK(rising->Position.X < 1.0F);

    const auto released =
        Keire::Detail::UpdateAutomaticFootGroundingSmoothing({}, {}, {2.0F, 1.0F, 0.0F}, 1.0F / 60.0F, 0.2F, state);
    REQUIRE(released);
    CHECK(released->Blend > 0.0F);
    CHECK(released->Blend < 1.0F);
    CHECK(released->Position.X > rising->Position.X);

    CHECK_FALSE(
        Keire::Detail::UpdateAutomaticFootGroundingSmoothing({}, {}, {2.0F, 1.0F, 0.0F}, 1.0F / 60.0F, 0.0F, state));
    CHECK_FALSE(state.Initialized);
}

TEST_CASE("Named animation IK reports each failed goal and still solves independent limbs")
{
    const Keire::SkeletonAsset skeleton({{"A0", -1, {{-2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"A1", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"A2", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"B0", -1, {{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"B1", 3, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"B2", 4, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}});
    std::map<std::string, std::uint32_t, std::less<>> indices;
    std::vector<Keire::BoneTransform> pose;
    for (std::uint32_t index = 0; index < skeleton.Bones().size(); ++index)
    {
        indices.emplace(skeleton.Bones()[index].Name, index);
        pose.push_back(skeleton.Bones()[index].BindPose);
    }
    const auto before = pose;
    auto expected = pose;
    REQUIRE(Keire::SolveTwoBoneIk(skeleton, expected, {0, 1, 2, {-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}));
    REQUIRE(Keire::SolveFabrikIk(skeleton, expected, {{3, 4, 5}, {3.0F, 1.0F, 0.0F}}));
    const std::vector<Keire::AnimatorIkGoal> goals{
        {.Name = "missing", .Space = Keire::AnimatorIkSpace::Model, .Bones = {"absent", "A1", "A2"}},
        {.Name = "left",
         .Space = Keire::AnimatorIkSpace::Model,
         .Bones = {"A0", "A1", "A2"},
         .Target = {-1.0F, 1.0F, 0.0F}},
        {.Name = "reversed",
         .Solver = Keire::AnimatorIkSolver::Fabrik,
         .Space = Keire::AnimatorIkSpace::Model,
         .Bones = {"B2", "B1", "B0"}},
        {.Name = "no-world", .Bones = {"B0", "B1", "B2"}},
        {.Name = "right",
         .Solver = Keire::AnimatorIkSolver::Fabrik,
         .Space = Keire::AnimatorIkSpace::Model,
         .Bones = {"B0", "B1", "B2"},
         .Target = {3.0F, 1.0F, 0.0F}}};
    const auto diagnostics = Keire::Detail::ApplyNamedAnimationIkGoals(skeleton, goals, pose, indices, std::nullopt);
    CHECK(diagnostics.find("'missing' references missing bone 'absent'") != std::string::npos);
    CHECK(diagnostics.find("'reversed' could not solve its chain") != std::string::npos);
    CHECK(diagnostics.find("'no-world' could not resolve") != std::string::npos);
    CHECK(diagnostics.find("'left'") == std::string::npos);
    CHECK(diagnostics.find("'right'") == std::string::npos);
    CHECK(pose == expected);
    CHECK(pose != before);

    auto worldGoal = goals.back();
    worldGoal.Space = Keire::AnimatorIkSpace::World;
    worldGoal.Target.X += 10.0F;
    pose = before;
    expected = before;
    REQUIRE(Keire::SolveFabrikIk(skeleton, expected, {{3, 4, 5}, {3.0F, 1.0F, 0.0F}}));
    const auto worldToModel = Keire::Math::ComposeTransform({-10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F});
    CHECK(Keire::Detail::ApplyNamedAnimationIkGoals(skeleton, {&worldGoal, 1}, pose, indices, worldToModel).empty());
    CHECK(pose == expected);

    pose = before;
    CHECK_FALSE(
        Keire::Detail::ApplyNamedAnimationIkGoals(skeleton, {goals.data(), 1}, pose, indices, std::nullopt).empty());
    CHECK(pose == before);
}

TEST_CASE("Automatic foot grounding release uses one response blend at every frame rate")
{
    for (const int rate : {30, 60, 144})
    {
        CAPTURE(rate);
        Keire::Detail::AutomaticFootGroundingSmoothingState state;
        const Keire::Vector3 normal{0.0F, 1.0F, 0.0F};
        REQUIRE(
            Keire::Detail::UpdateAutomaticFootGroundingSmoothing({Keire::Vector3{}}, {normal}, {}, 0.0F, 0.0F, state));
        for (int frame = 0; frame < rate; ++frame)
        {
            const auto target = Keire::Detail::UpdateAutomaticFootGroundingSmoothing(
                {}, {}, {0.0F, 1.0F, 0.0F}, 1.0F / static_cast<float>(rate), 1.0F, state);
            REQUIRE(target);
            const auto elapsed = static_cast<float>(frame + 1) / static_cast<float>(rate);
            CHECK(target->Position.Y == doctest::Approx(1.0F - std::exp(-elapsed)).epsilon(0.0001F));
        }
    }
}
