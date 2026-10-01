#include "Keire/Core.h"
#include "KeireInternal/Scripting/ManagedBuiltinComponents.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <variant>

namespace
{
    Keire::Detail::NativeBuiltinProperty Sentinel()
    {
        Keire::Detail::NativeBuiltinProperty value;
        value.Kind = Keire::Detail::NativeBuiltinPropertyKind::Entity;
        value.Integer = 73;
        value.Scalar = 19.5;
        value.Vector = {1, 2, 3, 4};
        value.High = 101;
        value.Low = 103;
        return value;
    }
    void CheckEqual(const Keire::Detail::NativeBuiltinProperty& a, const Keire::Detail::NativeBuiltinProperty& b)
    {
        CHECK(a.Kind == b.Kind);
        CHECK(a.Integer == b.Integer);
        CHECK(a.Scalar == b.Scalar);
        CHECK(a.Vector == b.Vector);
        CHECK(a.High == b.High);
        CHECK(a.Low == b.Low);
    }
} // namespace
TEST_CASE("Managed collider hot getters exactly match serialized values and untouched destination fields")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Collider getters");
    auto collider = entity.AddComponent<Keire::ColliderComponent>();
    collider->SetCenter({1.25F, -2.5F, 3.75F});
    collider->SetHalfExtent({.25F, .5F, .75F});
    collider->SetRadius(.7F);
    collider->SetHeight(2.2F);
    const auto registration = Keire::CreateColliderComponentRegistration();
    for (const auto shape : {Keire::ColliderShape::Box, Keire::ColliderShape::Sphere, Keire::ColliderShape::Capsule,
                             Keire::ColliderShape::ConvexMesh, Keire::ColliderShape::TriangleMesh})
    {
        collider->SetShape(shape);
        for (const bool trigger : {false, true})
        {
            collider->SetTrigger(trigger);
            const auto serialized = registration.Serialize(*collider);
            for (const std::string_view key : {"shape", "center", "halfExtent", "trigger"})
            {
                auto actual = Sentinel(), expected = Sentinel();
                const auto& property = serialized.at(std::string(key));
                if (key == "shape")
                {
                    expected.Kind = Keire::Detail::NativeBuiltinPropertyKind::Integer;
                    expected.Integer = std::get<std::int64_t>(property);
                }
                else if (key == "trigger")
                {
                    expected.Kind = Keire::Detail::NativeBuiltinPropertyKind::Boolean;
                    expected.Integer = std::get<bool>(property) ? 1 : 0;
                }
                else
                {
                    const auto v = std::get<Keire::Vector3>(property);
                    expected.Kind = Keire::Detail::NativeBuiltinPropertyKind::Vector3;
                    expected.Vector = {v.X, v.Y, v.Z, 0};
                }
                REQUIRE(Keire::Detail::GetManagedBuiltinComponentProperty(entity, collider->Type(), key, actual));
                CheckEqual(actual, expected);
            }
        }
    }
    auto radius = Sentinel();
    REQUIRE(Keire::Detail::GetManagedBuiltinComponentProperty(entity, collider->Type(), "radius", radius));
    CHECK(radius.Kind == Keire::Detail::NativeBuiltinPropertyKind::Scalar);
    CHECK(radius.Scalar == static_cast<double>(collider->Radius()));
    scene->Close();
}
TEST_CASE("Managed collider getters reject stale identities without writing destination")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Removed collider");
    auto collider = entity.AddComponent<Keire::ColliderComponent>();
    const auto reject = [&](const Keire::Entity& target, Keire::ComponentTypeId type, std::string_view key)
    {
        auto value = Sentinel();
        CHECK_FALSE(Keire::Detail::GetManagedBuiltinComponentProperty(target, type, key, value));
        CheckEqual(value, Sentinel());
    };
    reject({}, collider->Type(), "shape");
    reject(entity, collider->Type(), "unknown");
    reject(entity, Keire::TransformComponent::StaticType(), "center");
    REQUIRE(entity.RemoveComponent<Keire::ColliderComponent>());
    scene->Update(0);
    for (const std::string_view key : {"shape", "center", "halfExtent", "trigger"})
        reject(entity, Keire::ColliderComponent::StaticType(), key);
    auto replacement = entity.AddComponent<Keire::ColliderComponent>();
    replacement->SetCenter({9, 8, 7});
    auto value = Sentinel();
    REQUIRE(Keire::Detail::GetManagedBuiltinComponentProperty(entity, replacement->Type(), "center", value));
    CHECK((value.Vector == Keire::Vector4{9, 8, 7, 0}));
    scene->Close();
    reject(entity, Keire::ColliderComponent::StaticType(), "center");
}
