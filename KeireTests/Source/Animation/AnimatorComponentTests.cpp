#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <memory>

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
    animator.SetRuntimePose("Idle", palette);
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
