#include "KeireClient/Editor/InspectorTransformUndo.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ScenePlayChanges.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <variant>

TEST_CASE("continuous Inspector Transform undo stores one compact command and the final drag sample")
{
    const auto service = Keire::CreateRef<Keire::UndoService>();
    const auto context = service->CreateContext({.Name = "Inspector Transform"});
    const auto scene = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071");
    const auto entity = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000072");
    Keire::Vector3 position{};
    std::size_t applyCount = 0;
    const KeireEditor::InspectorTransformApply apply = [&](const Keire::EntityId target,
                                                           const KeireEditor::InspectorTransformProperty property,
                                                           const KeireEditor::InspectorTransformValue& value)
    {
        CHECK(target == entity);
        CHECK(property == KeireEditor::InspectorTransformProperty::Position);
        position = std::get<Keire::Vector3>(value);
        ++applyCount;
    };

    std::size_t compactBytes = 0;
    for (int sample = 1; sample <= 64; ++sample)
    {
        const auto before = position;
        const Keire::Vector3 after{static_cast<float>(sample), 2.0F, 3.0F};
        auto edit = KeireEditor::MakeInspectorTransformEdit(entity, KeireEditor::InspectorTransformProperty::Position,
                                                            before, after, 9);
        edit.Scope = context;
        context->Execute(KeireEditor::CreateInspectorTransformUndoCommand(scene, true, std::move(edit), apply));
        if (sample == 1)
            compactBytes = context->EstimatedBytes();
    }

    CHECK(applyCount == 64);
    CHECK(context->UndoCount() == 1);
    CHECK(context->EstimatedBytes() == compactBytes);
    CHECK(position.X == doctest::Approx(64.0F));
    REQUIRE(context->Undo());
    CHECK(position == Keire::Vector3{});
    REQUIRE(context->Redo());
    CHECK(position.X == doctest::Approx(64.0F));

    auto separateEdit = KeireEditor::MakeInspectorTransformEdit(
        entity, KeireEditor::InspectorTransformProperty::Position, position, Keire::Vector3{80.0F, 2.0F, 3.0F}, 10);
    separateEdit.Scope = context;
    context->Execute(KeireEditor::CreateInspectorTransformUndoCommand(scene, true, std::move(separateEdit), apply));
    CHECK(context->UndoCount() == 2);
}

TEST_CASE("targeted Play Transform tracking preserves editor and mixed origin without scene mutation snapshots")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000073");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Targeted Play"), registry);
    const auto authored = editing->CreateEntity("Authored camera");
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();

    const auto runtimeEntity = session->RuntimeScene()->FindEntity(authored.Id());
    const auto runtimeTransform = runtimeEntity.GetComponent<Keire::TransformComponent>();
    const auto registration = registry->Find(Keire::TransformComponent::StaticType());
    REQUIRE(runtimeTransform);
    REQUIRE(registration);
    runtimeTransform->SetLocalEulerAngles({0.0F, 15.0F, 0.0F});
    runtimeTransform->SetLocalPosition({4.0F, 5.0F, 6.0F});

    KeireEditor::ScenePlayChangeTracker tracker;
    const auto authoredValues = registration->Serialize(*runtimeTransform);
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "position",
                                            authoredValues.at("position"));
    const KeireEditor::ScenePlayChangeSet editorChanges(editing, session->RuntimeScene(), tracker);
    const auto editorPosition =
        std::ranges::find_if(editorChanges.Changes(),
                             [&](const KeireEditor::ScenePlayChange& change)
                             {
                                 return change.Entity == authored.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                        change.Property == "position";
                             });
    REQUIRE(editorPosition != editorChanges.Changes().end());
    CHECK(editorPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);
    CHECK(editorPosition->Selected);

    const auto editorRotation =
        std::ranges::find_if(editorChanges.Changes(),
                             [&](const KeireEditor::ScenePlayChange& change)
                             {
                                 return change.Entity == authored.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                        change.Property == "rotation";
                             });
    REQUIRE(editorRotation != editorChanges.Changes().end());
    CHECK(editorRotation->Origin == KeireEditor::ScenePlayChangeOrigin::Runtime);
    CHECK_FALSE(editorRotation->Selected);

    runtimeTransform->SetLocalEulerAngles({0.0F, 30.0F, 0.0F});
    const KeireEditor::ScenePlayChangeSet siblingRuntimeChanges(editing, session->RuntimeScene(), tracker);
    const auto unaffectedPosition =
        std::ranges::find_if(siblingRuntimeChanges.Changes(),
                             [&](const KeireEditor::ScenePlayChange& change)
                             {
                                 return change.Entity == authored.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                        change.Property == "position";
                             });
    REQUIRE(unaffectedPosition != siblingRuntimeChanges.Changes().end());
    CHECK(unaffectedPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);
    CHECK(unaffectedPosition->Selected);

    runtimeTransform->SetLocalPosition({7.0F, 8.0F, 9.0F});
    const KeireEditor::ScenePlayChangeSet mixedChanges(editing, session->RuntimeScene(), tracker);
    const auto mixedPosition =
        std::ranges::find_if(mixedChanges.Changes(),
                             [&](const KeireEditor::ScenePlayChange& change)
                             {
                                 return change.Entity == authored.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                        change.Property == "position";
                             });
    REQUIRE(mixedPosition != mixedChanges.Changes().end());
    CHECK(mixedPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Mixed);
    CHECK(mixedPosition->Selected);
}

TEST_CASE("targeted and snapshot Play Transform tracking preserve per-property ordering")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000074");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Ordered Play"), registry);
    const auto authored = editing->CreateEntity("Authored camera");
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    const auto runtimeTransform =
        session->RuntimeScene()->FindEntity(authored.Id()).GetComponent<Keire::TransformComponent>();
    const auto registration = registry->Find(Keire::TransformComponent::StaticType());
    REQUIRE(runtimeTransform);
    REQUIRE(registration);

    KeireEditor::ScenePlayChangeTracker tracker;
    auto before = session->RuntimeScene()->Snapshot();
    runtimeTransform->SetLocalPosition({1.0F, 2.0F, 3.0F});
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot(), registry);
    runtimeTransform->SetLocalEulerAngles({0.0F, 20.0F, 0.0F});
    auto values = registration->Serialize(*runtimeTransform);
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "rotation",
                                            values.at("rotation"));

    const KeireEditor::ScenePlayChangeSet broadThenTargeted(editing, session->RuntimeScene(), tracker);
    const auto findProperty = [&](const KeireEditor::ScenePlayChangeSet& changes, const std::string_view property)
    {
        return std::ranges::find_if(changes.Changes(),
                                    [&](const KeireEditor::ScenePlayChange& change)
                                    {
                                        return change.Entity == authored.Id().Value() &&
                                               change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                               change.Property == property;
                                    });
    };
    const auto position = findProperty(broadThenTargeted, "position");
    const auto rotation = findProperty(broadThenTargeted, "rotation");
    REQUIRE(position != broadThenTargeted.Changes().end());
    REQUIRE(rotation != broadThenTargeted.Changes().end());
    CHECK(position->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);
    CHECK(rotation->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);

    runtimeTransform->SetLocalPosition({4.0F, 5.0F, 6.0F});
    values = registration->Serialize(*runtimeTransform);
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "position",
                                            values.at("position"));
    before = session->RuntimeScene()->Snapshot();
    runtimeTransform->SetLocalPosition({7.0F, 8.0F, 9.0F});
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot(), registry);

    const KeireEditor::ScenePlayChangeSet targetedThenBroad(editing, session->RuntimeScene(), tracker);
    const auto finalPosition = findProperty(targetedThenBroad, "position");
    REQUIRE(finalPosition != targetedThenBroad.Changes().end());
    CHECK(finalPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);
    CHECK(finalPosition->Selected);
}

TEST_CASE("Play Transform tracking honors wildcard ordering and clears across runtime sessions")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000079");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Wildcard Play"), registry);
    const auto authored = editing->CreateEntity("Authored camera");
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    const auto runtimeTransform =
        session->RuntimeScene()->FindEntity(authored.Id()).GetComponent<Keire::TransformComponent>();
    const auto registration = registry->Find(Keire::TransformComponent::StaticType());
    REQUIRE(runtimeTransform);
    REQUIRE(registration);

    const auto positionChange = [&](const KeireEditor::ScenePlayChangeSet& changes)
    {
        return std::ranges::find_if(changes.Changes(),
                                    [&](const KeireEditor::ScenePlayChange& change)
                                    {
                                        return change.Entity == authored.Id().Value() &&
                                               change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty &&
                                               change.Property == "position";
                                    });
    };

    KeireEditor::ScenePlayChangeTracker tracker;
    tracker.BindSession(session);
    runtimeTransform->SetLocalPosition({4.0F, 0.0F, 0.0F});
    auto values = registration->Serialize(*runtimeTransform);
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "position",
                                            values.at("position"));
    auto before = session->RuntimeScene()->Snapshot();
    runtimeTransform->SetLocalPosition({7.0F, 0.0F, 0.0F});
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());
    const KeireEditor::ScenePlayChangeSet targetedThenWildcard(editing, session->RuntimeScene(), tracker);
    const auto wildcardPosition = positionChange(targetedThenWildcard);
    REQUIRE(wildcardPosition != targetedThenWildcard.Changes().end());
    CHECK(wildcardPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);

    tracker.Clear();
    before = session->RuntimeScene()->Snapshot();
    runtimeTransform->SetLocalPosition({8.0F, 0.0F, 0.0F});
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());
    runtimeTransform->SetLocalPosition({9.0F, 0.0F, 0.0F});
    values = registration->Serialize(*runtimeTransform);
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "position",
                                            values.at("position"));
    const KeireEditor::ScenePlayChangeSet wildcardThenTargeted(editing, session->RuntimeScene(), tracker);
    const auto targetedPosition = positionChange(wildcardThenTargeted);
    REQUIRE(targetedPosition != wildcardThenTargeted.Changes().end());
    CHECK(targetedPosition->Origin == KeireEditor::ScenePlayChangeOrigin::Editor);

    auto otherEditing =
        Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Other runtime"), registry);
    auto otherSession = Keire::CreateRef<Keire::SceneRuntimeSession>(otherEditing);
    otherSession->Play();
    tracker.BindSession(otherSession);
    CHECK(tracker.Empty());
    tracker.RecordComponentPropertyMutation(authored.Id().Value(), Keire::TransformComponent::StaticType(), "position",
                                            values.at("position"));
    CHECK_FALSE(tracker.Empty());
    tracker.BindSession(session);
    CHECK(tracker.Empty());
}

TEST_CASE("Inspector Transform edit scopes support no-history documents and reject reopen generations")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000080");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("No history"), registry);
    const auto entity = editing->CreateEntity("Transform target");
    KeireEditor::SceneDocument document;
    document.Open(editing, asset);

    const auto noHistoryScope = KeireEditor::CaptureInspectorTransformSceneScope(document);
    REQUIRE(noHistoryScope.Identity());
    CHECK(KeireEditor::ResolveInspectorTransformScene(document, noHistoryScope) == editing);
    auto edit = KeireEditor::MakeInspectorTransformEdit(entity.Id(), KeireEditor::InspectorTransformProperty::Position,
                                                        Keire::Vector3{}, Keire::Vector3{2.0F, 0.0F, 0.0F}, 1);
    edit.Scope = noHistoryScope.Identity();
    auto command = KeireEditor::CreateInspectorTransformUndoCommand(
        asset, false, std::move(edit),
        [&](const Keire::EntityId target, const KeireEditor::InspectorTransformProperty,
            const KeireEditor::InspectorTransformValue& value)
        {
            const auto scene = KeireEditor::ResolveInspectorTransformScene(document, noHistoryScope);
            REQUIRE(scene);
            scene->FindEntity(target).GetComponent<Keire::TransformComponent>()->SetLocalPosition(
                std::get<Keire::Vector3>(value));
        });
    command->Redo();
    CHECK(editing->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalPosition().X ==
          doctest::Approx(2.0F));

    const auto generationDefinition = editing->Snapshot();
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto reusedHistory = undoService->CreateContext({.Name = "Reused history"});
    auto firstGeneration = Keire::CreateRef<Keire::Scene>(asset, generationDefinition, registry);
    auto secondGeneration = Keire::CreateRef<Keire::Scene>(asset, generationDefinition, registry);
    document.Open(firstGeneration, asset, {}, reusedHistory);
    const auto oldGeneration = KeireEditor::CaptureInspectorTransformSceneScope(document);
    document.Open(secondGeneration, asset, {}, reusedHistory);
    CHECK_FALSE(KeireEditor::ResolveInspectorTransformScene(document, oldGeneration));
}

TEST_CASE("Inspector Transform undo rejects invalid property enumerators")
{
    const auto scene = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000081");
    const auto entity = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000082");
    auto edit = KeireEditor::MakeInspectorTransformEdit(entity, KeireEditor::InspectorTransformProperty::Position,
                                                        Keire::Vector3{}, Keire::Vector3{1.0F, 0.0F, 0.0F}, 1);
    edit.Property = static_cast<KeireEditor::InspectorTransformProperty>(255);
    CHECK_THROWS_AS((void)KeireEditor::CreateInspectorTransformUndoCommand(
                        scene, false, std::move(edit),
                        [](const Keire::EntityId, const KeireEditor::InspectorTransformProperty,
                           const KeireEditor::InspectorTransformValue&) {}),
                    std::invalid_argument);
}

TEST_CASE("Inspector Transform undo remains bound to its concrete scene scope")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000075");
    const auto firstScope =
        Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("First scope"), registry);
    const auto secondScope =
        Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Second scope"), registry);
    Keire::Ref<Keire::Scene> activeScope = firstScope;
    const auto service = Keire::CreateRef<Keire::UndoService>();
    const auto context = service->CreateContext({.Name = "Scoped Transform"});
    const auto entity = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000076");
    Keire::Vector3 firstPosition{};
    Keire::Vector3 secondPosition{};

    auto firstEdit = KeireEditor::MakeInspectorTransformEdit(entity, KeireEditor::InspectorTransformProperty::Position,
                                                             firstPosition, Keire::Vector3{1.0F, 0.0F, 0.0F}, 4);
    firstEdit.Scope = firstScope;
    context->Execute(KeireEditor::CreateInspectorTransformUndoCommand(
        asset, true, std::move(firstEdit),
        [&](const Keire::EntityId, const KeireEditor::InspectorTransformProperty,
            const KeireEditor::InspectorTransformValue& value) { firstPosition = std::get<Keire::Vector3>(value); },
        [&] { return activeScope == firstScope; }));
    CHECK(firstPosition.X == doctest::Approx(1.0F));

    activeScope = secondScope;
    CHECK_FALSE(context->Undo());
    CHECK(firstPosition.X == doctest::Approx(1.0F));

    auto secondEdit = KeireEditor::MakeInspectorTransformEdit(entity, KeireEditor::InspectorTransformProperty::Position,
                                                              secondPosition, Keire::Vector3{2.0F, 0.0F, 0.0F}, 4);
    secondEdit.Scope = secondScope;
    context->Execute(KeireEditor::CreateInspectorTransformUndoCommand(
        asset, true, std::move(secondEdit),
        [&](const Keire::EntityId, const KeireEditor::InspectorTransformProperty,
            const KeireEditor::InspectorTransformValue& value) { secondPosition = std::get<Keire::Vector3>(value); },
        [&] { return activeScope == secondScope; }));
    CHECK(context->UndoCount() == 2);
    CHECK(secondPosition.X == doctest::Approx(2.0F));
    REQUIRE(context->Undo());
    CHECK(secondPosition == Keire::Vector3{});
}

TEST_CASE("Inspector Transform scene scope survives scene replacement but rejects session replacement")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000077");
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto editHistory = undoService->CreateContext({.Name = "Edit history"});
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Original"), registry);
    const auto entity = editing->CreateEntity("Scoped transform");
    KeireEditor::SceneDocument document;
    document.Open(editing, asset, {}, editHistory);

    const auto editScope = KeireEditor::CaptureInspectorTransformSceneScope(document);
    CHECK(KeireEditor::ResolveInspectorTransformScene(document, editScope) == editing);
    const KeireEditor::InspectorTransformApply apply = [&](const Keire::EntityId target,
                                                           const KeireEditor::InspectorTransformProperty,
                                                           const KeireEditor::InspectorTransformValue& value)
    {
        const auto scene = KeireEditor::ResolveInspectorTransformScene(document, editScope);
        REQUIRE(scene);
        scene->FindEntity(target).GetComponent<Keire::TransformComponent>()->SetLocalPosition(
            std::get<Keire::Vector3>(value));
    };
    auto edit = KeireEditor::MakeInspectorTransformEdit(entity.Id(), KeireEditor::InspectorTransformProperty::Position,
                                                        Keire::Vector3{}, Keire::Vector3{3.0F, 0.0F, 0.0F}, 12);
    edit.Scope = editScope.Identity();
    editHistory->Execute(KeireEditor::CreateInspectorTransformUndoCommand(
        asset, false, std::move(edit), apply,
        [&]
        {
            const auto scene = KeireEditor::ResolveInspectorTransformScene(document, editScope);
            return scene && static_cast<bool>(scene->FindEntity(entity.Id()));
        }));
    CHECK(editing->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalPosition().X ==
          doctest::Approx(3.0F));

    auto replacement = Keire::CreateRef<Keire::Scene>(asset, editing->Snapshot(), registry);
    document.ReplaceEditingScene(replacement);
    CHECK(KeireEditor::ResolveInspectorTransformScene(document, editScope) == replacement);
    REQUIRE(editHistory->Undo());
    CHECK(replacement->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalPosition() ==
          Keire::Vector3{});
    REQUIRE(editHistory->Redo());
    CHECK(replacement->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalPosition().X ==
          doctest::Approx(3.0F));

    const auto playHistory = undoService->CreateContext({.Name = "Play history"});
    document.BeginPlay(playHistory);
    const auto playScope = KeireEditor::CaptureInspectorTransformSceneScope(document);
    const auto originalSession = document.PlaySession();
    REQUIRE(originalSession);
    CHECK(KeireEditor::ResolveInspectorTransformScene(document, playScope) == originalSession->RuntimeScene());
    const KeireEditor::InspectorTransformApply playApply = [&](const Keire::EntityId target,
                                                               const KeireEditor::InspectorTransformProperty,
                                                               const KeireEditor::InspectorTransformValue& value)
    {
        const auto scene = KeireEditor::ResolveInspectorTransformScene(document, playScope);
        REQUIRE(scene);
        scene->FindEntity(target).GetComponent<Keire::TransformComponent>()->SetLocalPosition(
            std::get<Keire::Vector3>(value));
    };
    auto playEdit =
        KeireEditor::MakeInspectorTransformEdit(entity.Id(), KeireEditor::InspectorTransformProperty::Position,
                                                Keire::Vector3{3.0F, 0.0F, 0.0F}, Keire::Vector3{4.0F, 0.0F, 0.0F}, 13);
    playEdit.Scope = playScope.Identity();
    playHistory->Execute(KeireEditor::CreateInspectorTransformUndoCommand(
        asset, true, std::move(playEdit), playApply,
        [&]
        {
            const auto scene = KeireEditor::ResolveInspectorTransformScene(document, playScope);
            return scene && static_cast<bool>(scene->FindEntity(entity.Id()));
        }));
    const auto replacementDefinition = originalSession->RuntimeScene()->Snapshot();
    originalSession->ReplaceRuntime(replacementDefinition);
    CHECK(KeireEditor::ResolveInspectorTransformScene(document, playScope) == originalSession->RuntimeScene());
    REQUIRE(playHistory->Undo());
    CHECK(originalSession->RuntimeScene()
              ->FindEntity(entity.Id())
              .GetComponent<Keire::TransformComponent>()
              ->LocalPosition()
              .X == doctest::Approx(3.0F));
    REQUIRE(playHistory->Redo());

    auto otherSession = Keire::CreateRef<Keire::SceneRuntimeSession>(replacement);
    otherSession->Play();
    document.SetPlaySession(otherSession);
    CHECK_FALSE(KeireEditor::ResolveInspectorTransformScene(document, playScope));
    CHECK_FALSE(playHistory->Undo());
    originalSession->Stop();
    document.EndPlay();
}
