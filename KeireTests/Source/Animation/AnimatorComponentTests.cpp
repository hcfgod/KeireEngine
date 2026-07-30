#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <stdexcept>

TEST_CASE("Animator edit preview state can be cleared without changing authored assignments")
{
    Keire::AnimatorComponent animator;
    const auto graph = Keire::AssetId::Generate();
    const auto skeleton = Keire::AssetId::Generate();
    const auto skin = Keire::AssetId::Generate();
    animator.SetGraph(graph);
    animator.SetSkeleton(skeleton);
    animator.SetSkinnedMesh(skin);

    const std::array<Keire::Matrix4, 1> palette{};
    animator.SetRuntimePose("Idle", 0.25F, true, palette);
    animator.SetRuntimeDebugSnapshot(std::make_shared<Keire::AnimatorDebugSnapshot>());
    animator.SetRuntimeDiagnostic("preview");
    animator.ClearRuntimePose();

    CHECK(animator.Graph() == graph);
    CHECK(animator.Skeleton() == skeleton);
    CHECK(animator.SkinnedMesh() == skin);
    CHECK(animator.CurrentState().empty());
    CHECK(animator.SkinPalette().empty());
    CHECK_FALSE(animator.RuntimeDebugSnapshot());
    CHECK(animator.RuntimeDiagnostic().empty());
}

TEST_CASE("Animator component queues ordered managed playback controls and preserves speed while paused")
{
    Keire::AnimatorComponent animator;
    animator.SetSpeed(1.75F);
    animator.SetPaused(true);
    animator.Play("Run", "Base", 0.25F);
    animator.CrossFade("Jump", 0.4F, "Upper Body", 0.1F);
    animator.Stop();

    CHECK(animator.Speed() == doctest::Approx(1.75F));
    CHECK(animator.Paused());
    const auto commands = animator.ConsumeRuntimeCommands();
    REQUIRE(commands.size() == 3);
    CHECK(commands[0].Type == Keire::AnimatorCommandType::Play);
    CHECK(commands[0].Name == "Run");
    CHECK(commands[0].Layer == "Base");
    CHECK(commands[0].FloatValue == doctest::Approx(0.25F));
    CHECK(commands[1].Type == Keire::AnimatorCommandType::CrossFade);
    CHECK(commands[1].Name == "Jump");
    CHECK(commands[1].Layer == "Upper Body");
    CHECK(commands[1].FloatValue == doctest::Approx(0.1F));
    CHECK(commands[1].SecondaryFloatValue == doctest::Approx(0.4F));
    CHECK(commands[2].Type == Keire::AnimatorCommandType::Stop);
    CHECK(commands[0].Sequence < commands[1].Sequence);
    CHECK(commands[1].Sequence < commands[2].Sequence);

    CHECK_THROWS_AS(animator.Play("Run", {}, -0.1F), std::invalid_argument);
    CHECK_THROWS_AS(animator.CrossFade("Run", -0.1F), std::invalid_argument);
}
