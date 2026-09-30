#include "KeireClient/Editor/EditorPanelMenuPolicy.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClientInternal/Editor/PhysicsPropertyDrawers.h"
#include "KeireClientInternal/Editor/VfxPropertyDrawers.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class ChoicePropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        bool EditBoolean(std::string_view, bool&) override { return false; }
        bool EditInteger(std::string_view, std::int64_t&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditChoice(const std::string_view label, std::int64_t& value,
                        const std::span<const std::string_view> choices) override
        {
            Label = label;
            Choices.assign(choices.begin(), choices.end());
            ValueBeforeEdit = value;
            ++ChoiceEdits;
            value = Selection.value_or(value + 1);
            return true;
        }
        bool EditScalar(std::string_view, double&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditText(std::string_view, std::string&) override { return false; }
        bool EditVector2(std::string_view, Keire::Vector2&, double) override { return false; }
        bool EditVector3(std::string_view, Keire::Vector3&, double) override { return false; }
        bool EditVector4(std::string_view, Keire::Vector4&, double) override { return false; }
        bool EditQuaternion(std::string_view, Keire::Quaternion&, double) override { return false; }
        bool EditColor(std::string_view, Keire::Color&) override { return false; }
        bool EditAsset(std::string_view, Keire::AssetId&, std::optional<Keire::AssetTypeId>, std::string_view) override
        {
            return false;
        }
        bool EditEntity(std::string_view, Keire::EntityId&) override { return false; }
        bool EditEvent(std::string_view, Keire::ComponentEventValue&, std::size_t) override { return false; }

        std::string Label;
        std::vector<std::string_view> Choices;
        std::optional<std::int64_t> Selection;
        std::optional<std::int64_t> ValueBeforeEdit;
        std::size_t ChoiceEdits = 0;
    };

    constexpr Keire::ComponentTypeId ChoiceComponentType()
    {
        return Keire::ComponentTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000099ULL));
    }
} // namespace

TEST_CASE("window menu hides visible panels and focuses hidden panels when reopening")
{
    using enum KeireEditor::EditorPanelMenuAction;

    CHECK(KeireEditor::EditorPanelMenuActionFor(true) == Hide);
    CHECK(KeireEditor::EditorPanelMenuActionFor(false) == ShowAndFocus);
}

TEST_CASE("integer choice overrides expose readable labels")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    drawers.RegisterIntegerChoices(ChoiceComponentType(), "mode", {"Realtime", "Mixed", "Baked"});
    ChoicePropertyEditor editor;
    Keire::ComponentProperty property{"mode", "Bake Mode", "Lighting", Keire::ComponentPropertyKind::Integer};
    Keire::ComponentPropertyValue value = std::int64_t{1};

    CHECK(drawers.Draw(editor, ChoiceComponentType(), property, value));
    CHECK(std::get<std::int64_t>(value) == 2);
    CHECK(editor.Label == "Bake Mode##mode");
    CHECK(editor.Choices == std::vector<std::string_view>{"Realtime", "Mixed", "Baked"});
    CHECK(editor.ChoiceEdits == 1);

    CHECK_THROWS_AS(drawers.RegisterIntegerChoices(ChoiceComponentType(), "empty", {}), std::invalid_argument);
}

TEST_CASE("physics property drawers expose and recover rigid body motion choices")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    KeireEditor::Detail::RegisterPhysicsPropertyDrawers(drawers);
    ChoicePropertyEditor editor;
    editor.Selection = static_cast<std::int64_t>(Keire::PhysicsMotionType::Kinematic);
    Keire::ComponentProperty property{"motion", "Motion", "Body", Keire::ComponentPropertyKind::Integer};
    Keire::ComponentPropertyValue value = std::int64_t{99};

    CHECK(drawers.Draw(editor, Keire::RigidBodyComponent::StaticType(), property, value));
    REQUIRE(editor.ValueBeforeEdit);
    CHECK(*editor.ValueBeforeEdit == 99);
    CHECK(std::get<std::int64_t>(value) == static_cast<std::int64_t>(Keire::PhysicsMotionType::Kinematic));
    CHECK(editor.Label == "Motion##motion");
    CHECK(editor.Choices == std::vector<std::string_view>{"Static", "Dynamic", "Kinematic"});
    CHECK(editor.ChoiceEdits == 1);
}

TEST_CASE("VFX property drawers expose and recover quality and culling choices")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    KeireEditor::Detail::RegisterVfxPropertyDrawers(drawers);
    const auto registration = Keire::CreateVfxEmitterComponentRegistration();
    const auto quality =
        std::ranges::find(registration.Properties, std::string_view("quality"), &Keire::ComponentProperty::Key);
    const auto culling =
        std::ranges::find(registration.Properties, std::string_view("culling"), &Keire::ComponentProperty::Key);
    REQUIRE(quality != registration.Properties.end());
    REQUIRE(culling != registration.Properties.end());

    ChoicePropertyEditor editor;
    editor.Selection = static_cast<std::int64_t>(Keire::VfxQualityTier::Cinematic);
    Keire::ComponentPropertyValue qualityValue = std::int64_t{99};
    CHECK(drawers.Draw(editor, registration.Type, *quality, qualityValue));
    REQUIRE(editor.ValueBeforeEdit);
    CHECK(*editor.ValueBeforeEdit == 99);
    CHECK(std::get<std::int64_t>(qualityValue) == static_cast<std::int64_t>(Keire::VfxQualityTier::Cinematic));
    CHECK(editor.Label == "Quality Tier##quality");
    CHECK(editor.Choices == std::vector<std::string_view>{"Low", "Medium", "High", "Cinematic"});

    editor.Selection = static_cast<std::int64_t>(Keire::VfxCullingMode::AlwaysSimulate);
    Keire::ComponentPropertyValue cullingValue = std::int64_t{-1};
    CHECK(drawers.Draw(editor, registration.Type, *culling, cullingValue));
    REQUIRE(editor.ValueBeforeEdit);
    CHECK(*editor.ValueBeforeEdit == -1);
    CHECK(std::get<std::int64_t>(cullingValue) == static_cast<std::int64_t>(Keire::VfxCullingMode::AlwaysSimulate));
    CHECK(editor.Label == "Culling Mode##culling");
    CHECK(editor.Choices == std::vector<std::string_view>{"Automatic", "Fixed Bounds", "Always Simulate"});
    CHECK(editor.ChoiceEdits == 2);
}
