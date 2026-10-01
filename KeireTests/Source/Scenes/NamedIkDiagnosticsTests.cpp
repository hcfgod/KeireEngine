#include "KeireInternal/Scenes/NamedIkDiagnostics.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace
{
    const std::map<std::string, std::uint32_t, std::less<>> Names{{"root", 0}, {"tip", 1}};
    Keire::AnimatorIkGoal Goal(const Keire::AnimatorIkSpace space, const Keire::Vector3 target)
    {
        Keire::AnimatorIkGoal goal;
        goal.Name = "leg lower";
        goal.Solver = Keire::AnimatorIkSolver::Fabrik;
        goal.Bones = {"root", "tip"};
        goal.Target = target;
        goal.Space = space;
        return goal;
    }
    std::shared_ptr<Keire::AnimatorDebugSnapshot> Pose(const Keire::Vector3 endpoint)
    {
        auto pose = std::make_shared<Keire::AnimatorDebugSnapshot>();
        pose->Pose = {{"root", -1, {}, {}}, {"tip", 0, {}, endpoint}};
        return pose;
    }
} // namespace

TEST_CASE("Named IK endpoint diagnostics preserve solver spaces and inherited presentation scale")
{
    using namespace Keire;
    const auto world = Math::ComposeTransform({10, 2, 3}, Math::EulerDegreesToQuaternion({0, 20, 0}), {2, 3, 4});
    const auto presentation =
        Math::Multiply(Math::ComposeTransform({-3, 5, 7}, Math::EulerDegreesToQuaternion({0, 65, 0}), {3, 2, 5}),
                       Math::ComposeTransform({1, 0, 2}, Math::EulerDegreesToQuaternion({10, 15, 0}), {1, 2, 1}));
    const Vector3 model{.2F, .5F, -.3F};
    const auto pose = Pose({model.X + .01F, model.Y, model.Z});
    for (const auto space : {AnimatorIkSpace::Model, AnimatorIkSpace::World, AnimatorIkSpace::PresentationWorld})
    {
        const auto target = space == AnimatorIkSpace::Model
                                ? model
                                : Math::TransformPoint(space == AnimatorIkSpace::World ? world : presentation, model);
        const auto capture = Detail::CaptureNamedIkEndpoint(Goal(space, target), Names, Math::Inverse(world),
                                                            Math::Inverse(presentation));
        REQUIRE(capture.Index == 1);
        const auto residual = Detail::MeasureNamedIkEndpoint(capture, pose->Pose, presentation);
        REQUIRE(residual);
        CHECK(residual->ModelDistance == doctest::Approx(.01F).epsilon(.0001));
        const auto expected = Math::TransformDirection(presentation, {.01F, 0, 0});
        CHECK(residual->PresentedDistance ==
              doctest::Approx(std::hypot(expected.X, expected.Y, expected.Z)).epsilon(.0001));
        CHECK(capture.OriginalTarget == target);
    }
}

TEST_CASE("Named IK endpoint diagnostics reject unavailable bones transforms and nonfinite samples")
{
    using namespace Keire;
    auto goal = Goal(AnimatorIkSpace::World, {1, 2, 3});
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, {}, Matrix4{}).Index);
    goal.Space = AnimatorIkSpace::PresentationWorld;
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, Matrix4{}, {}).Index);
    goal.Space = AnimatorIkSpace::Model;
    goal.Bones = {"missing parent", "tip"};
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, {}, {}).Index);
    goal.Bones.clear();
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, {}, {}).Index);
    goal = Goal(AnimatorIkSpace::Model, {1, 2, 3});
    goal.Target.X = std::numeric_limits<float>::infinity();
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, {}, {}).Index);
    goal = Goal(AnimatorIkSpace::Model, {1, 2, 3});
    auto capture = Detail::CaptureNamedIkEndpoint(goal, Names, {}, {});
    auto pose = Pose({1, 2, 3});
    pose->Pose[1].Name = "different tip";
    CHECK_FALSE(Detail::MeasureNamedIkEndpoint(capture, pose->Pose, {}));
    pose->Pose.clear();
    CHECK_FALSE(Detail::MeasureNamedIkEndpoint(capture, pose->Pose, {}));
    pose = Pose({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    CHECK_FALSE(Detail::MeasureNamedIkEndpoint(capture, pose->Pose, {}));
    for (const float weight : {0.0F, .5F, 1.0F})
    {
        goal.Weight = weight;
        const auto weighted = Detail::CaptureNamedIkEndpoint(goal, Names, {}, {});
        CHECK(weighted.Weight == weight);
        CHECK(weighted.Index == 1);
    }
    goal.Weight = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(Detail::CaptureNamedIkEndpoint(goal, Names, {}, {}).Index);
}

TEST_CASE("Named IK endpoint diagnostics pair exact publications and measure final overlapping goals")
{
    using namespace Keire;
    const auto first = Pose({1, 0, 0}), later = Pose({2, 0, 0});
    CHECK(Detail::IsNamedIkPublicationCurrent(first, 3, first, 3));
    CHECK_FALSE(Detail::IsNamedIkPublicationCurrent(first, 3, first, 4));
    CHECK_FALSE(Detail::IsNamedIkPublicationCurrent(first, 3, later, 3));
    CHECK_FALSE(Detail::IsNamedIkPublicationCurrent({}, 3, {}, 3));
    const auto captured = Detail::CaptureNamedIkEndpoint(Goal(AnimatorIkSpace::Model, {1, 0, 0}), Names, {}, {});
    REQUIRE(Detail::MeasureNamedIkEndpoint(captured, later->Pose, {}));
    CHECK(Detail::MeasureNamedIkEndpoint(captured, later->Pose, {})->ModelDistance == 1);
}

TEST_CASE("Named IK endpoint diagnostics report stale destroyed and inactive goals and reset idempotently")
{
    using namespace Keire;
    KeireTests::LogFixture logs("named-ik-endpoints");
    Log::Initialize(logs.Config);
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Spider QA");
    const auto animator = entity.AddComponent<AnimatorComponent>();
    animator->SetFabrikIk("leg", {"root", "tip"}, {1, 0, 0}, 0, 12, .001F, AnimatorIkSpace::Model);
    Detail::NamedIkDiagnostics inspection("Spider QA");
    const auto publish = [&]
    {
        inspection.Capture(entity, *animator, Names, Matrix4{}, Matrix4{});
        const std::array<Matrix4, 2> palette{};
        animator->SetRuntimePose("Rest", 0, true, palette);
        animator->SetRuntimeDebugSnapshot(Pose({1, 0, 0}));
        inspection.Published(entity, *animator);
    };
    publish();
    inspection.Observe(.2F);
    publish();
    animator->SetRuntimeDebugSnapshot(Pose({2, 0, 0}));
    inspection.Observe(.2F);
    publish();
    animator->SetEnabled(false);
    inspection.Observe(.2F);
    animator->SetEnabled(true);
    publish();
    entity.SetActive(false);
    inspection.Observe(.2F);
    entity.SetActive(true);
    publish();
    REQUIRE(entity.Destroy());
    inspection.Observe(.2F);
    inspection.Reset();
    inspection.Reset();
    scene->Close();
    std::string contents;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        Log::Flush();
        contents = KeireTests::ReadFile(logs.Directory / logs.Config.CoreLogFile);
        if (contents.find("kind=final") != std::string::npos)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(contents.find("captured=5 observed=5 invalid=1 inactive=1") != std::string::npos);
    CHECK(contents.find("unavailable=3") != std::string::npos);
    const auto final = contents.find("kind=final");
    REQUIRE(final != std::string::npos);
    CHECK(contents.find("kind=final", final + 1) == std::string::npos);
}

TEST_CASE("Named IK endpoint diagnostics distinguish solve residual from original world target drift")
{
    using namespace Keire;
    const auto finalPresentation = Math::ComposeTransform({10, 0, 0}, {}, {1, 1, 1});
    const auto pose = Pose({0, 0, 0});
    for (const auto space : {AnimatorIkSpace::Model, AnimatorIkSpace::World, AnimatorIkSpace::PresentationWorld})
    {
        const auto capture = Detail::CaptureNamedIkEndpoint(Goal(space, {0, 0, 0}), Names, Matrix4{}, Matrix4{});
        const auto residual = Detail::MeasureNamedIkEndpoint(capture, pose->Pose, finalPresentation);
        REQUIRE(residual);
        CHECK(residual->ModelDistance == 0);
        CHECK(residual->PresentedDistance == 0);
        if (space == AnimatorIkSpace::Model)
            CHECK_FALSE(residual->OriginalWorldDistance);
        else
        {
            REQUIRE(residual->OriginalWorldDistance);
            CHECK(*residual->OriginalWorldDistance == 10);
        }
    }
}
