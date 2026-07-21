#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    class TestPropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        bool EditBoolean(std::string_view, bool& value) override
        {
            value = !value;
            return true;
        }
        bool EditInteger(std::string_view, std::int64_t& value, double, std::optional<double>,
                         std::optional<double>) override
        {
            ++value;
            return true;
        }
        bool EditChoice(std::string_view, std::int64_t& value, std::span<const std::string_view>) override
        {
            ++value;
            return true;
        }
        bool EditScalar(std::string_view, double& value, double, std::optional<double>, std::optional<double>) override
        {
            value = Scalar;
            return true;
        }
        bool EditText(std::string_view, std::string& value) override
        {
            value = "edited";
            return true;
        }
        bool EditVector2(std::string_view, Keire::Vector2& value, double) override
        {
            value.X += 1.0F;
            return true;
        }
        bool EditVector3(std::string_view, Keire::Vector3& value, double) override
        {
            value.X += 1.0F;
            return true;
        }
        bool EditVector4(std::string_view, Keire::Vector4& value, double) override
        {
            value.X += 1.0F;
            return true;
        }
        bool EditQuaternion(std::string_view, Keire::Quaternion& value, double) override
        {
            value = {};
            return true;
        }
        bool EditColor(std::string_view, Keire::Color& value) override
        {
            value.Red = 0.5F;
            return true;
        }
        bool EditAsset(std::string_view, Keire::AssetId& value, std::optional<Keire::AssetTypeId> expectedType) override
        {
            ExpectedAssetType = expectedType;
            value = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000010");
            return true;
        }
        bool EditTextureAsset(std::string_view label, Keire::AssetId& value,
                              const Keire::ShaderTextureSemantic semantic) override
        {
            TextureSemantics.push_back(semantic);
            return EditAsset(label, value, Keire::Texture2DAsset::StaticType());
        }
        bool EditEntity(std::string_view, Keire::EntityId& value) override
        {
            value = Keire::EntityId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000011"));
            return true;
        }

        double Scalar = 3.0;
        std::optional<Keire::AssetTypeId> ExpectedAssetType;
        std::vector<Keire::ShaderTextureSemantic> TextureSemantics;
    };

    class CustomComponent final : public Keire::Component
    {
      public:
        CustomComponent() : Component(StaticType()) {}
        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000012ULL));
        }

        double Value = 1.0;
    };

    Keire::ComponentRegistration CustomRegistration()
    {
        Keire::ComponentRegistration result;
        result.Type = CustomComponent::StaticType();
        result.Name = "Custom";
        result.Properties = {
            {"value", "Value", "Custom", Keire::ComponentPropertyKind::Scalar, false, 0.0, 10.0, 0.25}};
        result.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<CustomComponent>()); };
        result.Serialize = [](const Keire::Component& component)
        { return Keire::ComponentPropertyBag{{"value", dynamic_cast<const CustomComponent&>(component).Value}}; };
        result.Deserialize =
            [](Keire::Component& component, const Keire::ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported custom component version.");
            const auto value = std::get<double>(values.at("value"));
            if (value < 0.0 || value > 10.0)
                throw std::invalid_argument("Custom value is outside its supported range.");
            dynamic_cast<CustomComponent&>(component).Value = value;
        };
        return result;
    }
} // namespace

TEST_CASE("editor command router centralizes availability and execution")
{
    KeireEditor::EditorCommandRouter router;
    bool available = false;
    unsigned int executions = 0;
    router.Bind(
        KeireEditor::EditorCommand::SaveScene, [&executions] { ++executions; }, [&available] { return available; });

    CHECK_FALSE(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 0);
    available = true;
    CHECK(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 1);
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::Exit));
    CHECK_THROWS_AS(router.Bind(KeireEditor::EditorCommand::Exit, {}), std::invalid_argument);

    constexpr std::array routedCommands{
        KeireEditor::EditorCommand::CreateEntity, KeireEditor::EditorCommand::DeleteSelection,
        KeireEditor::EditorCommand::SelectAll,    KeireEditor::EditorCommand::ClearSelection,
        KeireEditor::EditorCommand::Play,         KeireEditor::EditorCommand::Pause,
        KeireEditor::EditorCommand::Stop,         KeireEditor::EditorCommand::Undo,
        KeireEditor::EditorCommand::Redo,
    };
    for (const auto command : routedCommands)
        router.Bind(command, [&executions] { ++executions; });
    for (const auto command : routedCommands)
        CHECK(router.Execute(command));
    CHECK(executions == 1 + routedCommands.size());
}

TEST_CASE("scene document owns selection and deterministic close state")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000001"),
                                                Keire::SceneAsset::EmptyDefinition("Document test"));
    auto entity = scene->CreateEntity("Selected");
    auto second = scene->CreateEntity("Also selected");
    document.Open(scene);
    document.Select(entity.Id().Value());
    CHECK(document.Selection() == entity.Id().Value());
    document.Select(second.Id().Value(), true);
    CHECK(document.Selection() == second.Id().Value());
    CHECK(document.Selections().size() == 2);
    CHECK(document.IsSelected(entity.Id().Value()));
    document.Select(entity.Id().Value(), true);
    CHECK_FALSE(document.IsSelected(entity.Id().Value()));
    CHECK(document.Selection() == second.Id().Value());
    document.Select(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000099"));
    CHECK_FALSE(document.Selection());
    document.SetRecoveryAvailable(true);

    document.Close();
    CHECK_FALSE(document.Scene());
    CHECK_FALSE(document.Asset());
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK_FALSE(scene->IsOpen());
}

TEST_CASE("scene rectangle selection returns every active projected entity in the marquee")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000031"),
                                                Keire::SceneAsset::EmptyDefinition("Rectangle picking"));
    auto left = scene->CreateEntity("Left");
    auto right = scene->CreateEntity("Right");
    right.GetComponent<Keire::TransformComponent>()->SetLocalPosition({100.0F, 0.0F, 0.0F});
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};

    auto selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected == std::vector<Keire::EntityId>{left.Id()});
    right.GetComponent<Keire::TransformComponent>()->SetLocalPosition({1.0F, 0.0F, 0.0F});
    selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected.size() == 2);
    right.SetActive(false);
    selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected == std::vector<Keire::EntityId>{left.Id()});
}

TEST_CASE("scene gizmo controller owns tool shortcuts and visualization settings")
{
    KeireEditor::SceneGizmoController controller;
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::Q));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::View);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::W));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Translate);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::E));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Rotate);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::R));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Scale);
    CHECK_FALSE(controller.ApplyToolShortcut(Keire::UiKey::F));

    controller.SetSnapping(true);
    controller.SetShowCameraFrustums(false);
    controller.SetShowLightDirections(false);
    CHECK(controller.Settings().Snapping);
    CHECK_FALSE(controller.Settings().ShowCameraFrustums);
    CHECK_FALSE(controller.Settings().ShowLightDirections);
}

TEST_CASE("scene transform groups move every selected root once and restore the drag baseline")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000032"),
                                                Keire::SceneAsset::EmptyDefinition("Group transform"));
    auto parent = scene->CreateEntity("Parent");
    auto child = scene->CreateEntity("Selected child");
    auto independent = scene->CreateEntity("Independent");
    child.SetParent(parent, false);
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({1.0F, 0.0F, 0.0F});
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({2.0F, 0.0F, 0.0F});
    independent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({5.0F, 0.0F, 0.0F});

    const std::array selections{parent.Id().Value(), child.Id().Value(), independent.Id().Value()};
    const auto targets = KeireEditor::SceneTransformGroup::Capture(scene, selections, independent.Id());
    REQUIRE(targets.size() == 2);
    KeireEditor::SceneTransformGroup::Apply(targets, KeireEditor::SceneTool::Translate,
                                            KeireEditor::SceneTransformAxis::X, 3.0F, {1.0F, 0.0F, 0.0F},
                                            independent.GetComponent<Keire::TransformComponent>()->WorldPosition(), {});
    CHECK(parent.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(4.0F));
    CHECK(child.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(6.0F));
    CHECK(independent.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(8.0F));

    KeireEditor::SceneTransformGroup::Restore(targets);
    CHECK(parent.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(1.0F));
    CHECK(child.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(2.0F));
    CHECK(independent.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(5.0F));
}

TEST_CASE("scene document targets the isolated runtime scene while playing")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000060"),
                                                Keire::SceneAsset::EmptyDefinition("Play document"));
    auto authored = scene->CreateEntity("Authored");
    document.Open(scene);
    document.BeginPlay();
    REQUIRE(document.ActiveScene());
    CHECK(document.ActiveScene() != document.EditingScene());
    document.ActiveScene()->FindEntity(authored.Id()).SetName("Runtime");
    CHECK(scene->FindEntity(authored.Id()).Name() == "Authored");
    CHECK(document.ActiveScene()->FindEntity(authored.Id()).Name() == "Runtime");
    document.Close();
}

TEST_CASE("play changes apply selected property and structural edits transactionally")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(CustomRegistration());
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000061");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Play changes"), registry);
    auto original = editing->CreateEntity("Original");
    auto custom = Keire::DynamicRefCast<CustomComponent>(original.AddComponent(CustomComponent::StaticType()));
    REQUIRE(custom);
    editing->MarkSaved();
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    auto runtimeEntity = session->RuntimeScene()->FindEntity(original.Id());
    runtimeEntity.SetName("Edited in Play");
    Keire::DynamicRefCast<CustomComponent>(runtimeEntity.GetComponent(CustomComponent::StaticType()))->Value = 4.0;
    const auto created = session->RuntimeScene()->CreateEntity("Created in Play");

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(),
                                            {original.Id().Value(), created.Id().Value()});
    CHECK_FALSE(changes.Empty());
    CHECK(changes.HasSelectedChanges());
    const auto applied = changes.BuildAppliedDefinition();
    auto restored = Keire::CreateRef<Keire::Scene>(asset, applied, registry);
    CHECK(restored->FindEntity(original.Id()).Name() == "Edited in Play");
    const auto restoredCustom = Keire::DynamicRefCast<CustomComponent>(
        restored->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()));
    REQUIRE(restoredCustom);
    CHECK(restoredCustom->Value == doctest::Approx(4.0));
    CHECK(restored->FindEntity(created.Id()));

    changes.SetAllSelected(false);
    const auto discarded = changes.BuildAppliedDefinition();
    CHECK(Keire::SceneAsset::Encode(discarded) == Keire::SceneAsset::Encode(editing->Snapshot()));
}

TEST_CASE("play change tracker distinguishes mixed values and enforces created-parent dependencies")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(CustomRegistration());
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000062");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Tracked Play"), registry);
    auto original = editing->CreateEntity("Original");
    REQUIRE(original.AddComponent(CustomComponent::StaticType()));
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    KeireEditor::ScenePlayChangeTracker tracker;

    auto before = session->RuntimeScene()->Snapshot();
    auto runtimeCustom = Keire::DynamicRefCast<CustomComponent>(
        session->RuntimeScene()->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()));
    REQUIRE(runtimeCustom);
    runtimeCustom->Value = 4.0;
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());
    runtimeCustom->Value = 6.0;

    before = session->RuntimeScene()->Snapshot();
    auto parent = session->RuntimeScene()->CreateEntity("Created Parent");
    auto child = session->RuntimeScene()->CreateEntity("Created Child");
    child.SetParent(parent, false);
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(), tracker);
    const auto all = changes.Changes();
    const auto property =
        std::ranges::find_if(all,
                             [&](const auto& change)
                             {
                                 return change.Entity == original.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty;
                             });
    REQUIRE(property != all.end());
    CHECK(property->Origin == KeireEditor::ScenePlayChangeOrigin::Mixed);
    CHECK(property->Selected);
    auto parentChange = std::ranges::find_if(all,
                                             [&](const auto& change)
                                             {
                                                 return change.Entity == parent.Id().Value() &&
                                                        change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
                                             });
    const auto childChange = std::ranges::find_if(
        all,
        [&](const auto& change)
        {
            return change.Entity == child.Id().Value() && change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
        });
    REQUIRE(parentChange != all.end());
    REQUIRE(childChange != all.end());
    CHECK(parentChange->Locked);
    CHECK_FALSE(parentChange->LockReason.empty());

    changes.KeepCreatedEntityAtRoot(child.Id().Value(), true);
    parentChange = std::ranges::find_if(changes.Changes(),
                                        [&](const auto& change)
                                        {
                                            return change.Entity == parent.Id().Value() &&
                                                   change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
                                        });
    REQUIRE(parentChange != changes.Changes().end());
    changes.SetSelected(parentChange->Id, false);
    const auto applied = changes.BuildAppliedDefinition();
    auto restored = Keire::CreateRef<Keire::Scene>(asset, applied, registry);
    CHECK_FALSE(restored->FindEntity(parent.Id()));
    REQUIRE(restored->FindEntity(child.Id()));
    CHECK_FALSE(restored->FindEntity(child.Id()).Parent());
    CHECK(Keire::DynamicRefCast<CustomComponent>(
              restored->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()))
              ->Value == doctest::Approx(6.0));
}

TEST_CASE("play changes preserve selected unavailable component replacements")
{
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000063");
    auto definition = Keire::SceneAsset::EmptyDefinition("Unknown component");
    const auto entityId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000064");
    const auto unknownType = Keire::ComponentTypeId::Parse("ed170000-0000-4000-8000-000000000065");
    definition.Objects.push_back(
        {.Id = entityId,
         .Name = "Unknown",
         .Components = {{.Type = unknownType, .SchemaVersion = 7, .Enabled = true, .Data = "{\"value\":1}"}}});
    auto editing = Keire::CreateRef<Keire::Scene>(asset, definition);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    KeireEditor::ScenePlayChangeTracker tracker;
    const auto before = session->RuntimeScene()->Snapshot();
    auto after = before;
    after.Objects.front().Components.back().Data = "{\"value\":2}";
    session->ReplaceRuntime(after);
    tracker.RecordMutation(before, after);

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(), tracker);
    REQUIRE(changes.HasSelectedChanges());
    const auto applied = changes.BuildAppliedDefinition();
    const auto object = std::ranges::find(applied.Objects, entityId, &Keire::SceneObjectDefinition::Id);
    REQUIRE(object != applied.Objects.end());
    const auto component = std::ranges::find(object->Components, unknownType, &Keire::SceneComponentDefinition::Type);
    REQUIRE(component != object->Components.end());
    CHECK(component->SchemaVersion == 7);
    CHECK(component->Data == "{\"value\":2}");
}

TEST_CASE("input actions document owns authoring state and dirty lifecycle")
{
    KeireEditor::InputActionsDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000002"),
                  Keire::InputActionAsset::DefaultDefinition());
    document.MarkDirty();
    CHECK(document.Dirty());
    CHECK(document.Definition().ActionMaps.size() > 0);
    document.MarkSaved();
    CHECK_FALSE(document.Dirty());
    document.Close();
    CHECK_FALSE(document.Asset());
    CHECK(document.Definition().ActionMaps.empty());
}

TEST_CASE("custom registered component properties edit transactionally and restore")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    const auto components = Keire::ComponentRegistry::CreateDefault();
    components->Register(CustomRegistration());
    const auto registration = components->Find(CustomComponent::StaticType());
    REQUIRE(registration);
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000013"),
                                                Keire::SceneAsset::EmptyDefinition("Custom Inspector"), components);
    auto entity = scene->CreateEntity("Custom");
    auto component = entity.AddComponent(CustomComponent::StaticType());
    REQUIRE(component);
    const auto original = registration->Serialize(*component);
    unsigned int commits = 0;

    CHECK(drawers.EditComponent(editor, *registration, *component, registration->Properties.front(),
                                [&commits] { ++commits; }));
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(3.0));
    CHECK(commits == 1);

    registration->Deserialize(*component, original, registration->SchemaVersion);
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(1.0));

    editor.Scalar = 20.0;
    CHECK_THROWS_AS((void)drawers.EditComponent(editor, *registration, *component, registration->Properties.front()),
                    std::invalid_argument);
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(1.0));
}

TEST_CASE("component and property-specific drawers override generic kinds")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    auto registration = CustomRegistration();
    auto component = registration.Factory();
    drawers.RegisterOverride(
        registration.Type, "value",
        [](KeireEditor::IPropertyEditor&, const Keire::ComponentProperty&, Keire::ComponentPropertyValue& value)
        {
            value = 7.0;
            return true;
        });

    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front()));
    CHECK(std::get<double>(registration.Serialize(*component).at("value")) == doctest::Approx(7.0));
}

TEST_CASE("generic property drawers cover every component property kind")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    const auto assetType = Keire::MeshAsset::StaticType();
    const std::vector<std::pair<Keire::ComponentProperty, Keire::ComponentPropertyValue>> properties = {
        {{"boolean", "Boolean", {}, Keire::ComponentPropertyKind::Boolean}, false},
        {{"integer", "Integer", {}, Keire::ComponentPropertyKind::Integer}, std::int64_t{1}},
        {{"scalar", "Scalar", {}, Keire::ComponentPropertyKind::Scalar}, 1.0},
        {{"text", "Text", {}, Keire::ComponentPropertyKind::Text}, std::string("original")},
        {{"vector2", "Vector2", {}, Keire::ComponentPropertyKind::Vector2}, Keire::Vector2{}},
        {{"vector3", "Vector3", {}, Keire::ComponentPropertyKind::Vector3}, Keire::Vector3{}},
        {{"vector4", "Vector4", {}, Keire::ComponentPropertyKind::Vector4}, Keire::Vector4{}},
        {{"quaternion", "Quaternion", {}, Keire::ComponentPropertyKind::Quaternion}, Keire::Quaternion{}},
        {{"color", "Color", {}, Keire::ComponentPropertyKind::Color}, Keire::Color{}},
        {{"asset", "Asset", {}, Keire::ComponentPropertyKind::Asset, false, {}, {}, 0.1, assetType}, Keire::AssetId{}},
        {{"entity", "Entity", {}, Keire::ComponentPropertyKind::Entity}, Keire::EntityId{}}};

    for (auto [property, value] : properties)
        CHECK(drawers.Draw(editor, CustomComponent::StaticType(), property, value));
    CHECK(editor.ExpectedAssetType == assetType);
}

TEST_CASE("material documents expose every shader texture property without hardcoded slots")
{
    const auto shader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000020");
    const auto baseColor = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000021");
    const auto normal = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000022");
    Keire::ShaderAssetDefinition shaderDefinition;
    shaderDefinition.Source = "Assets/Shaders/Material.hlsl";
    shaderDefinition.Properties = {{"Tint", Keire::ShaderPropertyType::Color, {1.0F, 1.0F, 1.0F, 1.0F}},
                                   {"Roughness", Keire::ShaderPropertyType::Scalar, {1.0F, 0.0F, 0.0F, 0.0F}},
                                   {"BaseColorTexture", Keire::ShaderPropertyType::Texture2D, {}, baseColor},
                                   {"NormalTexture", Keire::ShaderPropertyType::Texture2D},
                                   {"EmissiveTexture", Keire::ShaderPropertyType::Texture2D},
                                   {"MaskTexture", Keire::ShaderPropertyType::Texture2D}};
    shaderDefinition.Properties[2].TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    shaderDefinition.Properties[3].TextureSemantic = Keire::ShaderTextureSemantic::Normal;
    shaderDefinition.Properties[4].TextureSemantic = Keire::ShaderTextureSemantic::Emissive;
    shaderDefinition.Properties[5].TextureSemantic = Keire::ShaderTextureSemantic::MetallicRoughness;
    const auto resolveShader = [&](const Keire::AssetId id) -> std::optional<Keire::ShaderAssetDefinition>
    { return id == shader ? std::optional(shaderDefinition) : std::nullopt; };

    Keire::MaterialAssetDefinition sourceDefinition;
    sourceDefinition.Shader = shader;
    const auto source = Keire::MaterialAsset::EncodeSource(sourceDefinition);
    KeireEditor::MaterialDocument document;
    document.Open(source, resolveShader);
    REQUIRE(document.TextureProperties().size() == 4);
    CHECK(document.Texture("BaseColorTexture") == baseColor);
    CHECK(document.SetTexture("NormalTexture", normal));
    CHECK(document.LastChangedProperty() == "NormalTexture");
    CHECK_FALSE(document.SetTexture("NormalTexture", normal));
    CHECK_THROWS_AS((void)document.SetTexture("NotDeclared", normal), std::invalid_argument);
    CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(1.0F));
    CHECK(document.SetProperty("Roughness", 0.35F));
    CHECK(document.LastChangedProperty() == "Roughness");
    CHECK_FALSE(document.SetProperty("Roughness", 0.35F));
    CHECK_THROWS_AS((void)document.SetProperty("Roughness", Keire::Color{}), std::invalid_argument);

    KeireEditor::MaterialDocument restored;
    restored.Open(document.SaveSource(), resolveShader);
    CHECK(restored.Texture("NormalTexture") == normal);
    CHECK(restored.Definition().Texture("NormalTexture") == normal);
    CHECK(std::get<float>(restored.Property("Roughness")) == doctest::Approx(0.35F));
    TestPropertyEditor editor;
    editor.Scalar = 0.6;
    CHECK(KeireEditor::MaterialInspectorPanel{}.Draw(editor, restored));
    CHECK(std::get<float>(restored.Property("Roughness")) == doctest::Approx(0.6F));
    CHECK(editor.ExpectedAssetType == Keire::Texture2DAsset::StaticType());
    CHECK(editor.TextureSemantics == std::vector<Keire::ShaderTextureSemantic>{
                                         Keire::ShaderTextureSemantic::BaseColor, Keire::ShaderTextureSemantic::Normal,
                                         Keire::ShaderTextureSemantic::Emissive,
                                         Keire::ShaderTextureSemantic::MetallicRoughness});

    Keire::AssetSourceRecord textureRecord;
    textureRecord.Type = Keire::Texture2DAsset::StaticType();
    textureRecord.ImportSettings = {{"semantic", std::string("color")}, {"colorSpace", std::string("srgb")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::BaseColor));
    CHECK_FALSE(
        KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Normal));
    textureRecord.ImportSettings = {{"semantic", std::string("normal")}, {"colorSpace", std::string("linear")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Normal));
    textureRecord.ImportSettings = {{"semantic", std::string("data")}, {"colorSpace", std::string("linear")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Roughness));
    CHECK_FALSE(
        KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::BaseColor));
}

TEST_CASE("material document owns draft and committed source state")
{
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000023");
    const auto shader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000024");
    Keire::ShaderAssetDefinition shaderDefinition;
    shaderDefinition.Source = "Assets/Shaders/Material.hlsl";
    shaderDefinition.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {1.0F, 0.0F, 0.0F, 0.0F}}};
    const auto resolver = [&](const Keire::AssetId id) -> std::optional<Keire::ShaderAssetDefinition>
    { return id == shader ? std::optional(shaderDefinition) : std::nullopt; };
    Keire::MaterialAssetDefinition definition;
    definition.Shader = shader;
    const auto source = Keire::MaterialAsset::EncodeSource(definition);

    KeireEditor::MaterialDocument document;
    document.OpenAsset(asset, "Assets/Materials/Test.keirematerial", source, resolver);
    CHECK(document.IsOpen(asset));
    CHECK_FALSE(document.Dirty());
    CHECK(document.SetProperty("Roughness", 0.25F));
    document.CaptureDraft();
    CHECK(document.Dirty());
    CHECK_FALSE(std::ranges::equal(document.DraftSource(), document.BaselineSource()));
    const std::vector<std::byte> committed(document.DraftSource().begin(), document.DraftSource().end());
    document.AcceptSavedSource(committed);
    CHECK_FALSE(document.Dirty());
    CHECK(std::ranges::equal(document.DraftSource(), document.BaselineSource()));
}

TEST_CASE("scene picker selects transform-only and rendered entities by nearest viewport hit")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000030"),
                                                Keire::SceneAsset::EmptyDefinition("Picking"));
    auto transformOnly = scene->CreateEntity("Transform only");
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};

    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == transformOnly.Id());
    CHECK_FALSE(KeireEditor::PickSceneEntity(scene, viewport, {250.0F, 100.0F}, camera));

    auto rendered = scene->CreateEntity("Rendered");
    REQUIRE(rendered.AddComponent<Keire::MeshRendererComponent>());
    rendered.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, 2.0F});
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == rendered.Id());
    rendered.SetActive(false);
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == transformOnly.Id());
}

TEST_CASE("viewport asset drops dispatch through narrow typed commands")
{
    class Commands final : public KeireEditor::IViewportAssetDropCommands
    {
      public:
        void OpenDroppedScene(const Keire::AssetId asset) override { Scene = asset; }
        void OpenDroppedInputActions(const Keire::AssetId asset) override { Input = asset; }
        void CreateDroppedMeshEntity(const Keire::AssetId asset) override { Mesh = asset; }
        void AssignDroppedMaterial(const Keire::EntityId entity, const Keire::AssetId asset) override
        {
            Target = entity;
            Material = asset;
        }

        Keire::AssetId Scene;
        Keire::AssetId Input;
        Keire::AssetId Mesh;
        Keire::AssetId Material;
        Keire::EntityId Target;
    } commands;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000040");
    const auto target = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000041");
    KeireEditor::ViewportAssetDropRouter router;
    router.Route(Keire::SceneAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Scene == asset);
    router.Route(Keire::InputActionAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Input == asset);
    router.Route(Keire::MeshAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Mesh == asset);
    router.Route(Keire::MaterialAsset::StaticType(), asset, target, commands);
    CHECK(commands.Material == asset);
    CHECK(commands.Target == target);
    CHECK_THROWS_AS(router.Route(Keire::MaterialAsset::StaticType(), asset, {}, commands), std::invalid_argument);
    CHECK_THROWS_AS(
        router.Route(Keire::AssetTypeId::Parse("ed170000-0000-4000-8000-000000000042"), asset, {}, commands),
        std::invalid_argument);
}

TEST_CASE("scene camera state and entity locking persist without a workspace layer")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-SceneCameraController-Test";
    const auto path = root / "SceneCamera.state";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    KeireEditor::SceneCameraController camera;
    auto state = camera.State();
    state.Focus = {4.0F, 5.0F, 6.0F};
    state.YawDegrees = 35.0F;
    state.PitchDegrees = -15.0F;
    state.Distance = 12.0F;
    state.OrthographicSize = 7.0F;
    state.MoveSpeed = 3.0F;
    state.Projection = Keire::Detail::EditorCameraProjection::Orthographic;
    camera.SetState(state);
    camera.SetLockedEntity(Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000050"));
    camera.MarkDirty();
    REQUIRE(camera.Save(path));

    KeireEditor::SceneCameraController restored;
    REQUIRE(restored.Load(path));
    CHECK(restored.State().Focus == state.Focus);
    CHECK(restored.State().YawDegrees == doctest::Approx(state.YawDegrees));
    CHECK(restored.State().Projection == Keire::Detail::EditorCameraProjection::Orthographic);
    CHECK(restored.LockedEntity() == camera.LockedEntity());
    std::filesystem::remove_all(root, error);
}

TEST_CASE("content previews use immutable loaded assets without blocking shutdown")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-ThumbnailService-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    KeireEditor::ThumbnailService thumbnails(root);
    const auto await = [&thumbnails]
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            auto completed = thumbnails.DrainCompleted();
            if (!completed.empty())
                return std::move(completed.front());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return KeireEditor::ThumbnailResult{};
    };

    Keire::TextureImportSettings textureSettings;
    textureSettings.Mips = Keire::TextureMipPolicy::None;
    Keire::TextureMipLevel mip;
    mip.Width = 2;
    mip.Height = 1;
    mip.Pixels = {std::byte{255}, std::byte{0},   std::byte{0}, std::byte{255},
                  std::byte{0},   std::byte{255}, std::byte{0}, std::byte{0}};
    auto texture = Keire::CreateRef<Keire::Texture2DAsset>(textureSettings, std::vector{mip});
    const auto textureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000070");
    REQUIRE(thumbnails.Request({.Asset = textureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Preview.png",
                                .Digest = "texture-preview"}));
    const auto textureResult = await();
    REQUIRE(textureResult.Pixels.size() == 96U * 96U * 4U);
    const auto redOffset = (48U * 96U + 24U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[redOffset]) >
          std::to_integer<unsigned>(textureResult.Pixels[redOffset + 1]));
    const auto alphaOffset = (48U * 96U + 72U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[alphaOffset]) ==
          std::to_integer<unsigned>(textureResult.Pixels[alphaOffset + 1]));

    const auto recoveredTextureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000073");
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview",
                                .Missing = true}));
    REQUIRE(await().Pixels.size() == 96U * 96U * 4U);
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview"}));
    const auto recoveredTexture = await();
    REQUIRE(recoveredTexture.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset]) >
          std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset + 1]));

    Keire::MaterialAssetDefinition materialDefinition;
    materialDefinition.Properties.emplace("Tint", Keire::Color{1.0F, 0.05F, 0.05F, 1.0F});
    auto material = Keire::CreateRef<Keire::MaterialAsset>(materialDefinition);
    const auto materialId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071");
    REQUIRE(thumbnails.Request({.Asset = materialId,
                                .Type = Keire::MaterialAsset::StaticType(),
                                .PreviewAsset = material,
                                .RelativePath = "Preview.keirematerial",
                                .Digest = "material-preview"}));
    const auto materialResult = await();
    REQUIRE(materialResult.Pixels.size() == 96U * 96U * 4U);
    const auto center = (48U * 96U + 48U) * 4U;
    CHECK(std::to_integer<unsigned>(materialResult.Pixels[center]) >
          std::to_integer<unsigned>(materialResult.Pixels[center + 1]));

    const auto meshId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000072");
    REQUIRE(thumbnails.Request({.Asset = meshId,
                                .Type = Keire::MeshAsset::StaticType(),
                                .PreviewAsset = Keire::MeshAsset::Cube(),
                                .RelativePath = "Preview.obj",
                                .Digest = "mesh-preview"}));
    const auto meshResult = await();
    REQUIRE(meshResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::ranges::any_of(meshResult.Pixels,
                              [](const std::byte value) { return std::to_integer<unsigned>(value) > 210U; }));
    thumbnails.CancelAll();
    std::filesystem::remove_all(root, error);
}
