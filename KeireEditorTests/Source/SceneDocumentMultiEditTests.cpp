#include "KeireClient/Editor/DroppedVfxCreation.h"
#include "KeireClient/Editor/ImportedModelCreation.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/ECS/Components/JointComponents.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

TEST_CASE("scene document rolls back entity creation when component setup fails")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Creation rollback"));
    document.Open(scene);
    const auto parent = document.CreateEntity("Existing parent");
    document.Select(parent.Value());
    const auto missing = Keire::ComponentTypeId(Keire::AssetId::Generate());
    CHECK_THROWS((void)document.CreateEntity("Rejected child", parent, missing));
    CHECK(scene->Entities().size() == 1);
    CHECK(scene->FindEntity(parent).Children().empty());
    CHECK(document.Selection() == parent.Value());
    const auto valid = document.CreateEntity("Valid child", parent, Keire::PointLightComponent::StaticType());
    CHECK(scene->FindEntity(valid).GetComponent<Keire::PointLightComponent>());
    CHECK(scene->FindEntity(parent).Children().size() == 1);
    document.Close();
}

TEST_CASE("imported model placement rolls back transform and animation failures")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Model rollback"));
    document.Open(scene);
    const auto existing = document.CreateEntity("Keep me");
    document.Select(existing.Value());
    const auto mesh = Keire::AssetId::Generate();
    CHECK_THROWS_WITH((void)KeireEditor::CreateImportedModel(document, "Invalid position", mesh,
                                                             {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}),
                      "Transform position must be finite.");
    CHECK(scene->Entities().size() == 1);
    const KeireEditor::ImportedModelAnimation incomplete{.Skin = Keire::AssetId::Generate()};
    CHECK_THROWS_WITH((void)KeireEditor::CreateImportedModel(document, "Invalid rig", mesh, {}, incomplete),
                      "An imported skin requires a skeleton.");
    CHECK(scene->Entities().size() == 1);
    CHECK(document.Selection() == existing.Value());
    CHECK(scene->FindEntity(existing).Name() == "Keep me");
    const KeireEditor::ImportedModelAnimation complete{
        .Skeleton = Keire::AssetId::Generate(), .Skin = Keire::AssetId::Generate(), .Rig = Keire::AssetId::Generate()};
    const auto created = KeireEditor::CreateImportedModel(document, "Valid model", mesh, {2, 3, 4}, complete);
    CHECK(scene->Entities().size() == 2);
    CHECK(scene->FindEntity(created).GetComponent<Keire::AnimatorComponent>()->Skeleton() == complete.Skeleton);
    CHECK(scene->FindEntity(created).GetComponent<Keire::MeshRendererComponent>()->Mesh() == mesh);
    CHECK(scene->FindEntity(created).GetComponent<Keire::MeshRendererComponent>()->Tint() ==
          Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
    auto restored = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), scene->Snapshot());
    CHECK(restored->FindEntity(created).GetComponent<Keire::MeshRendererComponent>()->Tint() ==
          Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
    restored->Close();
    document.Close();
}

TEST_CASE("dropped VFX placement assigns a positioned emitter and rolls back failures")
{
    KeireEditor::SceneDocument document;
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("VFX placement"));
    document.Open(scene);
    const auto existing = document.CreateEntity("Keep me");
    document.Select(existing.Value());
    const auto effect = Keire::AssetId::Generate();

    CHECK_THROWS_WITH((void)KeireEditor::CreateDroppedVfxEmitter(document, "Invalid position", effect,
                                                                 {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}),
                      "Transform position must be finite.");
    CHECK(scene->Entities().size() == 1);
    CHECK(document.Selection() == existing.Value());
    CHECK_THROWS_WITH((void)KeireEditor::CreateDroppedVfxEmitter(document, "Invalid effect", {}, {}),
                      "A dropped VFX effect requires a valid asset ID.");
    CHECK(scene->Entities().size() == 1);

    const auto created = KeireEditor::CreateDroppedVfxEmitter(document, "Fire", effect, {2.0F, 3.0F, 4.0F});
    const auto entity = scene->FindEntity(created);
    REQUIRE(entity);
    CHECK(entity.Name() == "Fire");
    REQUIRE(entity.GetComponent<Keire::TransformComponent>());
    CHECK(entity.GetComponent<Keire::TransformComponent>()->LocalPosition() == (Keire::Vector3{2.0F, 3.0F, 4.0F}));
    REQUIRE(entity.GetComponent<Keire::VfxEmitterComponent>());
    CHECK(entity.GetComponent<Keire::VfxEmitterComponent>()->Effect() == effect);
    CHECK(entity.GetComponent<Keire::VfxEmitterComponent>()->PlayOnAwake());
    CHECK_FALSE(entity.GetComponent<Keire::VfxEmitterComponent>()->EditModePreview());
    CHECK(document.Dirty());
    document.Close();
}

TEST_CASE("failed model placement cancels its history transaction without losing redo")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto history = service->CreateContext({.Name = "Placement"});
    int previousEdit = 0;
    history->Execute(Keire::CreateUndoCommand("Previous edit", [&] { previousEdit = 1; }, [&] { previousEdit = 0; }));
    REQUIRE(history->Undo());
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Generate();
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Failed placement"));
    document.Open(scene, asset, {}, history);
    const auto existing = document.CreateEntity("Keep me");
    const auto before = scene->Snapshot();
    const auto place = [&]
    {
        auto transaction = history->BeginTransaction("Create Mesh Entity");
        history->RecordApplied(Keire::CreateUndoCommand(
            "Placement snapshot", [] {},
            [&] { document.ReplaceEditingScene(Keire::CreateRef<Keire::Scene>(asset, before), true); }));
        (void)KeireEditor::CreateImportedModel(document, "Invalid rig", Keire::AssetId::Generate(), {},
                                               {.Skin = Keire::AssetId::Generate()});
        transaction->Commit();
    };
    CHECK_THROWS_WITH(place(), "An imported skin requires a skeleton.");
    CHECK(history->UndoCount() == 0);
    CHECK(history->RedoCount() == 1);
    CHECK(document.ActiveScene()->Entities().size() == 1);
    CHECK(document.ActiveScene()->FindEntity(existing).Name() == "Keep me");
    CHECK(history->Redo());
    CHECK(previousEdit == 1);
    document.Close();
    service->Close();
}

TEST_CASE("scene document multi-edit applies common component changes atomically")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Inspector multi-edit"));
    document.Open(scene);
    const auto first = document.CreateEntity("First", {}, Keire::PointLightComponent::StaticType());
    const auto second = document.CreateEntity("Second", {}, Keire::PointLightComponent::StaticType());
    const auto withoutLight = document.CreateEntity("Without light");
    const std::array selection{first.Value(), second.Value()};

    document.SetComponentsProperty(selection, Keire::PointLightComponent::StaticType(), "intensity", 17.0);
    document.SetComponentsProperties(selection, Keire::PointLightComponent::StaticType(),
                                     {{"intensity", 19.0}, {"range", 23.0}});
    document.SetComponentsEnabled(selection, Keire::PointLightComponent::StaticType(), false);
    document.SetTransforms(selection, {.Position = Keire::Vector3{4.0F, 5.0F, 6.0F}});
    document.SetEntitiesActive(selection, false);

    for (const auto id : selection)
    {
        const auto entity = scene->FindEntity(Keire::EntityId(id));
        REQUIRE(entity);
        const auto light = entity.GetComponent<Keire::PointLightComponent>();
        REQUIRE(light);
        CHECK(light->Intensity() == doctest::Approx(19.0F));
        CHECK(light->Range() == doctest::Approx(23.0F));
        CHECK_FALSE(light->Enabled());
        CHECK_FALSE(entity.ActiveSelf());
        CHECK(entity.GetComponent<Keire::TransformComponent>()->LocalPosition() == Keire::Vector3{4.0F, 5.0F, 6.0F});
    }

    CHECK_THROWS_AS(document.SetComponentsProperty(std::array{first.Value(), withoutLight.Value()},
                                                   Keire::PointLightComponent::StaticType(), "intensity", 3.0),
                    std::invalid_argument);
    CHECK(scene->FindEntity(first).GetComponent<Keire::PointLightComponent>()->Intensity() == doctest::Approx(19.0F));
    CHECK_THROWS_AS(document.SetComponentsProperty(selection, Keire::PointLightComponent::StaticType(), "intensity",
                                                   std::string("invalid")),
                    std::exception);
    for (const auto id : selection)
    {
        CHECK(scene->FindEntity(Keire::EntityId(id)).GetComponent<Keire::PointLightComponent>()->Intensity() ==
              doctest::Approx(19.0F));
    }

    const auto registration = scene->Components()->Find(Keire::PointLightComponent::StaticType());
    REQUIRE(registration);
    const auto reference = scene->FindEntity(first).GetComponent<Keire::PointLightComponent>();
    CHECK(KeireEditor::HaveUniformComponentValues(scene, selection, *registration, reference, 0));
    document.SetComponentEnabled(first, Keire::PointLightComponent::StaticType(), true);
    CHECK_FALSE(KeireEditor::HaveUniformComponentValues(scene, selection, *registration, reference, 0));
    document.SetComponentsEnabled(selection, Keire::PointLightComponent::StaticType(), false);
    document.SetComponentProperty(first, Keire::PointLightComponent::StaticType(), "intensity", 7.0);
    CHECK_FALSE(KeireEditor::HaveUniformComponentValues(scene, selection, *registration, reference, 0));
    document.SetComponentsProperty(selection, Keire::PointLightComponent::StaticType(), "intensity", 19.0);

    CHECK_THROWS_AS(document.SetComponentsProperties(selection, Keire::PointLightComponent::StaticType(),
                                                     {{"intensity", 5.0}, {"unknown", 8.0}}),
                    std::invalid_argument);
    for (const auto id : selection)
        CHECK(scene->FindEntity(Keire::EntityId(id)).GetComponent<Keire::PointLightComponent>()->Intensity() ==
              doctest::Approx(19.0F));

    const auto firstRenderer = document.CreateEntity("First renderer", {}, Keire::MeshRendererComponent::StaticType());
    const auto secondRenderer =
        document.CreateEntity("Second renderer", {}, Keire::MeshRendererComponent::StaticType());
    const std::array rendererSelection{firstRenderer.Value(), secondRenderer.Value()};
    const auto originalMaterial = Keire::AssetId::Generate();
    document.SetMeshRenderersMaterial(rendererSelection, 0, originalMaterial);
    const auto firstRendererComponent = scene->FindEntity(firstRenderer).GetComponent<Keire::MeshRendererComponent>();
    const auto secondRendererComponent = scene->FindEntity(secondRenderer).GetComponent<Keire::MeshRendererComponent>();
    REQUIRE(firstRendererComponent);
    REQUIRE(secondRendererComponent);
    CHECK(KeireEditor::HaveCommonMeshMaterialLayout(scene, rendererSelection, firstRendererComponent));
    secondRendererComponent->SetMesh(Keire::AssetId::Generate());
    CHECK_FALSE(KeireEditor::HaveCommonMeshMaterialLayout(scene, rendererSelection, firstRendererComponent));
    CHECK_THROWS_AS(document.SetMeshRenderersMaterial(rendererSelection, 256, Keire::AssetId::Generate()),
                    std::out_of_range);
    for (const auto id : rendererSelection)
        CHECK(scene->FindEntity(Keire::EntityId(id)).GetComponent<Keire::MeshRendererComponent>()->Material(0) ==
              originalMaterial);

    const auto firstJointEntity = document.CreateEntity("First joints");
    const auto secondJointEntity = document.CreateEntity("Second joints");
    for (const auto id : std::array{firstJointEntity, secondJointEntity})
    {
        (void)document.AddComponent(id, Keire::FixedJointComponent::StaticType());
        (void)document.AddComponent(id, Keire::FixedJointComponent::StaticType());
    }
    const std::array jointSelection{firstJointEntity.Value(), secondJointEntity.Value()};
    document.SetComponentsProperty(jointSelection, Keire::FixedJointComponent::StaticType(), "breakForce", 42.0, 1);
    for (const auto id : jointSelection)
    {
        const auto components = scene->FindEntity(Keire::EntityId(id)).GetComponents();
        std::vector<Keire::Ref<Keire::FixedJointComponent>> joints;
        for (const auto& component : components)
            if (const auto joint = Keire::DynamicRefCast<Keire::FixedJointComponent>(component))
                joints.push_back(joint);
        REQUIRE(joints.size() == 2);
        CHECK(joints[0]->BreakForce() == doctest::Approx(0.0F));
        CHECK(joints[1]->BreakForce() == doctest::Approx(42.0F));
    }

    document.ResetComponents(selection, Keire::PointLightComponent::StaticType());
    for (const auto id : selection)
    {
        CHECK(scene->FindEntity(Keire::EntityId(id)).GetComponent<Keire::PointLightComponent>()->Intensity() ==
              doctest::Approx(1.0F));
    }
    document.Close();
}
