#include "Keire/ECS/Components/AnimatorComponent.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>

namespace
{
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

TEST_CASE("selection component additions preserve existing instances and reject stale targets before mutation")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId{}, Keire::SceneAsset::EmptyDefinition("Batch"));
    document.Open(scene);
    const auto first = document.CreateEntity("First");
    const auto second = document.CreateEntity("Second");
    const auto type = Keire::AnimatorComponent::StaticType();
    const auto existing = document.AddComponent(first, type);
    const std::array selection{first.Value(), second.Value(), second.Value()};
    CHECK(document.AddComponents(selection, type) == 1);
    CHECK(scene->FindEntity(first).GetComponent(type) == existing);
    CHECK(document.AddComponents(selection, type) == 0);
    document.RemoveComponents(selection, type);
    CHECK_FALSE(scene->FindEntity(first).HasComponent(type));
    CHECK_FALSE(scene->FindEntity(second).HasComponent(type));
    const std::array invalid{first.Value(), Keire::AssetId{}};
    CHECK_THROWS_AS((void)document.AddComponents(invalid, type), std::invalid_argument);
    CHECK_FALSE(scene->FindEntity(first).HasComponent(type));
    (void)document.AddComponent(first, type);
    CHECK_THROWS_AS(document.RemoveComponents(selection, type), std::invalid_argument);
    CHECK(scene->FindEntity(first).HasComponent(type));
    CHECK_THROWS_AS(document.RemoveComponents(selection, Keire::TransformComponent::StaticType()),
                    std::invalid_argument);
}

TEST_CASE("selection component factory failures roll back successful additions")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    auto registration = CustomRegistration();
    int created = 0;
    registration.AllowMultiple = true;
    registration.RequiredComponents = {Keire::AnimatorComponent::StaticType()};
    registration.Factory = [&created]() -> Keire::Ref<Keire::Component>
    {
        if (++created == 2)
            throw std::runtime_error("Expected fixture factory failure");
        return Keire::CreateRef<CustomComponent>();
    };
    registry->Register(std::move(registration));
    KeireEditor::SceneDocument document;
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId{}, Keire::SceneAsset::EmptyDefinition("Batch"), registry);
    document.Open(scene);
    const auto first = document.CreateEntity("First");
    const auto second = document.CreateEntity("Second");
    const std::array selection{first.Value(), second.Value()};
    CHECK_THROWS_AS((void)document.AddComponents(selection, CustomComponent::StaticType()), std::runtime_error);
    CHECK_FALSE(scene->FindEntity(first).HasComponent(CustomComponent::StaticType()));
    CHECK_FALSE(scene->FindEntity(second).HasComponent(CustomComponent::StaticType()));
    CHECK_FALSE(scene->FindEntity(first).HasComponent(Keire::AnimatorComponent::StaticType()));
    CHECK_FALSE(scene->FindEntity(second).HasComponent(Keire::AnimatorComponent::StaticType()));
    CHECK(document.AddComponents(selection, CustomComponent::StaticType()) == 2);
    CHECK_THROWS_AS(document.RemoveComponents(selection, Keire::AnimatorComponent::StaticType()),
                    std::invalid_argument);
    CHECK(document.AddComponents(selection, CustomComponent::StaticType()) == 2);
    const auto retained = scene->FindEntity(first).GetComponents(CustomComponent::StaticType()).front();
    document.RemoveComponents(selection, CustomComponent::StaticType(), 1);
    CHECK(scene->FindEntity(first).GetComponents(CustomComponent::StaticType()).size() == 1);
    CHECK(scene->FindEntity(first).GetComponent(CustomComponent::StaticType()) == retained);
}
