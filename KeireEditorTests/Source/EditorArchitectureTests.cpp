#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <doctest/doctest.h>

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
