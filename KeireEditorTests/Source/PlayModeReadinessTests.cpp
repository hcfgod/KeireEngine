#include "KeireClient/Editor/PlayModeReadiness.h"

#include "doctest/doctest.h"

#include <initializer_list>

TEST_CASE("Play Mode explains last-good scripts only after a failed or cancelled replacement")
{
    using KeireEditor::PlayModeLastGoodScriptNotice;
    for (const auto state : {Keire::ManagedBuildState::Failed, Keire::ManagedBuildState::Cancelled})
    {
        CHECK(KeireEditor::EvaluatePlayModeReadiness(true, true, state, Keire::ManagedReloadState::Active) ==
              KeireEditor::PlayModeReadiness::Ready);
        const auto notice = PlayModeLastGoodScriptNotice(true, state, Keire::ManagedReloadState::Active);
        CHECK(notice.find("last successful script build") != std::string_view::npos);
        CHECK(notice.find("changes are not running") != std::string_view::npos);
        CHECK(PlayModeLastGoodScriptNotice(false, state, Keire::ManagedReloadState::Active).empty());
        for (const auto reload : {Keire::ManagedReloadState::Idle, Keire::ManagedReloadState::Prepared,
                                  Keire::ManagedReloadState::Failed, Keire::ManagedReloadState::Cancelled})
            CHECK(PlayModeLastGoodScriptNotice(true, state, reload).empty());
    }
    for (const auto state :
         {Keire::ManagedBuildState::Idle, Keire::ManagedBuildState::Generating, Keire::ManagedBuildState::Compiling,
          Keire::ManagedBuildState::Publishing, Keire::ManagedBuildState::Succeeded})
        CHECK(PlayModeLastGoodScriptNotice(true, state, Keire::ManagedReloadState::Active).empty());
    CHECK(PlayModeLastGoodScriptNotice(true, Keire::ManagedBuildState::Failed, Keire::ManagedReloadState::Active)
              .find("Console") != std::string_view::npos);
    CHECK(PlayModeLastGoodScriptNotice(true, Keire::ManagedBuildState::Cancelled, Keire::ManagedReloadState::Active)
              .find("Build Scripts") != std::string_view::npos);
}

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
