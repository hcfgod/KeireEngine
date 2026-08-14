#include "KeireInternal/Scenes/AnimationIkPasses.h"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

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
