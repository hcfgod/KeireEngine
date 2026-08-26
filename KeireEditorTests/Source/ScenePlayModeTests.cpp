#include "KeireClient/Editor/ManagedRuntimeSessionResolver.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Scenes/SceneSystem.h"

#include <doctest/doctest.h>

TEST_CASE("scene document activates its runtime session before world adoption")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000061");
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("World play document"));
    document.Open(scene, asset);

    REQUIRE_NOTHROW(document.BeginPlay({}, assets, {}, {}, {}, world));
    REQUIRE(document.PlaySession());
    REQUIRE(world->Active());
    CHECK(world->Session(world->Active()) == document.PlaySession());
    CHECK(world->Find(world->Active()) == document.ActiveScene());

    document.EndPlay();
    world->Close();
    scenes->Close();
    assets->Close();
    document.Close();
}

TEST_CASE("managed runtime services resolve the primary Play session before world adoption")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000062");
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Managed Play services"));
    const auto entity = scene->CreateEntity("Service consumer").Id();
    document.Open(scene, asset);

    REQUIRE_NOTHROW(document.BeginPlay({}, assets));
    const auto primary = document.PlaySession();
    REQUIRE(primary);
    REQUIRE(primary->RuntimeScene());
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, primary) == primary);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, primary, entity.Value()) == primary);

    const auto adopted = world->Adopt(primary);
    REQUIRE(adopted);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, {}) == primary);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, {}, entity.Value()) == primary);

    document.EndPlay();
    world->Close();
    scenes->Close();
    assets->Close();
    document.Close();
}
