#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneTransitionCoordinator.h"

#include "Keire/Assets/Asset.h"
#include "Keire/Ref.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"

#include <doctest/doctest.h>

TEST_CASE("Scene transition coordinator serializes requests and retains failure diagnostics")
{
    KeireEditor::SceneTransitionCoordinator transitions;
    const auto scene = Keire::AssetId::Generate();
    CHECK(transitions.Request({KeireEditor::SceneTransitionKind::Open, scene}));
    CHECK(transitions.Pending());
    CHECK_FALSE(transitions.Request({KeireEditor::SceneTransitionKind::Close, {}}));

    const auto request = transitions.BeginCommit();
    REQUIRE(request);
    CHECK(request->Kind == KeireEditor::SceneTransitionKind::Open);
    CHECK(request->Asset == scene);
    CHECK_FALSE(transitions.BeginCommit());
    transitions.Fail("decode failed");
    CHECK_FALSE(transitions.Pending());
    CHECK(transitions.State() == KeireEditor::SceneTransitionState::Failed);
    CHECK(transitions.Diagnostic() == "decode failed");

    CHECK(transitions.Request({KeireEditor::SceneTransitionKind::Create, {}}));
    CHECK(transitions.BeginCommit());
    transitions.Complete();
    CHECK(transitions.State() == KeireEditor::SceneTransitionState::Idle);
}

TEST_CASE("Scene document views become inert after the underlying scene closes")
{
    KeireEditor::SceneDocument document;
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Transient"));
    (void)scene->CreateEntity("Transient");
    document.Open(scene);
    CHECK(document.ActiveScene());
    scene->Close();
    CHECK_FALSE(document.ActiveScene());
    CHECK_FALSE(document.EditingScene());
    CHECK_FALSE(document.Dirty());
    CHECK_NOTHROW(document.SynchronizeSelection());
}

TEST_CASE("Source scene asset readiness cannot reopen or complete a superseded scene")
{
    KeireEditor::SceneTransitionCoordinator transitions;
    const auto initial = Keire::AssetId::Generate();
    const auto replacement = Keire::AssetId::Generate();
    transitions.RequireStartupRefresh(initial);
    transitions.AwaitSceneAssets(initial);
    transitions.CompleteStartupRefresh(replacement);
    CHECK(transitions.StartupRefresh() == initial);
    CHECK_FALSE(transitions.CompleteSceneAssets(replacement));
    REQUIRE(transitions.Request({KeireEditor::SceneTransitionKind::Open, replacement}));
    CHECK_FALSE(transitions.AwaitingSceneAssets());
    CHECK_FALSE(transitions.CompleteSceneAssets(initial));
    transitions.AwaitSceneAssets(replacement);
    transitions.CompleteStartupRefresh(initial);
    CHECK_FALSE(transitions.StartupRefresh());
    CHECK(transitions.AwaitingSceneAssets() == replacement);
    CHECK(transitions.CompleteSceneAssets(replacement));
    CHECK_FALSE(transitions.CompleteSceneAssets(replacement));
}
