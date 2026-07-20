#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
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
        bool EditEntity(std::string_view, Keire::EntityId& value) override
        {
            value = Keire::EntityId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000011"));
            return true;
        }

        double Scalar = 3.0;
        std::optional<Keire::AssetTypeId> ExpectedAssetType;
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
}

TEST_CASE("scene document owns selection and deterministic close state")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000001"),
                                                Keire::SceneAsset::EmptyDefinition("Document test"));
    auto entity = scene->CreateEntity("Selected");
    document.SceneStorage() = scene;
    document.AssetStorage() = scene->Asset();
    document.Select(entity.Id().Value());
    CHECK(document.Selection() == entity.Id().Value());
    document.Select(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000099"));
    CHECK_FALSE(document.Selection());
    document.RecoveryAvailableStorage() = true;

    document.Close();
    CHECK_FALSE(document.Scene());
    CHECK_FALSE(document.Asset());
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK_FALSE(scene->IsOpen());
}

TEST_CASE("input actions document owns authoring state and dirty lifecycle")
{
    KeireEditor::InputActionsDocument document;
    document.AssetStorage() = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000002");
    document.DefinitionStorage() = Keire::InputActionAsset::DefaultDefinition();
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
