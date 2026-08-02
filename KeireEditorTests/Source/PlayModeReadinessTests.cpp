#include "KeireClient/Editor/PlayModeReadiness.h"

#include "doctest/doctest.h"

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
    CHECK(EvaluatePlayModeReadiness(true, true, Keire::ManagedBuildState::Generating,
                                    Keire::ManagedReloadState::Active) == PlayModeReadiness::Ready);
}
