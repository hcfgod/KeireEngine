#include "KeireClient/Editor/PlayModeReadiness.h"

#include "doctest/doctest.h"

#include <initializer_list>

TEST_CASE("Play Mode waits for the first managed runtime generation")
{
    using KeireEditor::EvaluatePlayModeReadiness;
    using KeireEditor::PlayModeReadiness;

    CHECK(EvaluatePlayModeReadiness(false, false, Keire::ManagedBuildState::Idle, Keire::ManagedReloadState::Idle) ==
          PlayModeReadiness::Ready);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Generating,
                                    Keire::ManagedReloadState::Idle) == PlayModeReadiness::WaitingForManagedRuntime);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Succeeded,
                                    Keire::ManagedReloadState::Prepared) ==
          PlayModeReadiness::WaitingForManagedRuntime);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Succeeded,
                                    Keire::ManagedReloadState::Active) == PlayModeReadiness::Ready);
}

TEST_CASE("Play Mode rejects an unavailable first managed generation but accepts an active last-good generation")
{
    using KeireEditor::EvaluatePlayModeReadiness;
    using KeireEditor::PlayModeReadiness;

    CHECK(EvaluatePlayModeReadiness(true, false, Keire::ManagedBuildState::Idle, Keire::ManagedReloadState::Idle) ==
          PlayModeReadiness::ManagedRuntimeUnavailable);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Failed, Keire::ManagedReloadState::Idle) ==
          PlayModeReadiness::ManagedRuntimeUnavailable);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Succeeded,
                                    Keire::ManagedReloadState::Failed) == PlayModeReadiness::ManagedRuntimeUnavailable);
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Failed, Keire::ManagedReloadState::Active) ==
          PlayModeReadiness::Ready);
}

TEST_CASE("Play Mode waits for an in-flight replacement even with an active last-good generation")
{
    CHECK(KeireEditor::EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Succeeded,
                                                 Keire::ManagedReloadState::Active,
                                                 false) == KeireEditor::PlayModeReadiness::WaitingForManagedRuntime);
    for (const auto state : {Keire::ManagedBuildState::Generating, Keire::ManagedBuildState::Compiling,
                             Keire::ManagedBuildState::Publishing})
    {
        CHECK(KeireEditor::EvaluatePlayModeReadiness(true, true, state, Keire::ManagedReloadState::Active) ==
              KeireEditor::PlayModeReadiness::WaitingForManagedRuntime);
        CHECK(KeireEditor::EvaluatePlayModeReadiness(false, true, state, Keire::ManagedReloadState::Active) ==
              KeireEditor::PlayModeReadiness::Ready);
    }
}

TEST_CASE("Play Mode distinguishes pending available and failed default input loads")
{
    using KeireEditor::EvaluatePlayModeInputReadiness;
    using KeireEditor::PlayModeInputReadiness;

    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Queued) == PlayModeInputReadiness::Waiting);
    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Loading) == PlayModeInputReadiness::Waiting);
    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Ready) == PlayModeInputReadiness::Ready);
    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Reloading) == PlayModeInputReadiness::Ready);
    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Failed) == PlayModeInputReadiness::Unavailable);
    CHECK(EvaluatePlayModeInputReadiness(Keire::AssetState::Cancelled) == PlayModeInputReadiness::Unavailable);
}
