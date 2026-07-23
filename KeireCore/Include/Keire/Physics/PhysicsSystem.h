#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace Keire
{
    enum class PhysicsMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    struct PhysicsSystemSpecification
    {
        PhysicsMode Mode = PhysicsMode::Disabled;
        std::uint32_t MaximumWorlds = 64;
    };

    enum class PhysicsMotionType : std::uint8_t
    {
        Static,
        Dynamic,
        Kinematic
    };

    enum class ColliderShape : std::uint8_t
    {
        Box,
        Sphere,
        Capsule,
        ConvexMesh,
        TriangleMesh
    };

    enum class CollisionMeshKind : std::uint8_t
    {
        Convex,
        Triangle
    };

    struct CollisionCookInput
    {
        CollisionMeshKind Kind = CollisionMeshKind::Convex;
        std::vector<Vector3> Vertices;
        std::vector<std::uint32_t> Indices;
    };

    struct CookedCollisionMesh
    {
        CollisionMeshKind Kind = CollisionMeshKind::Convex;
        std::uint64_t ContentHash = 0;
        std::vector<Vector3> Vertices;
        std::vector<std::uint32_t> Indices;
    };

    [[nodiscard]] KEIRE_API std::shared_ptr<const CookedCollisionMesh>
    CookCollisionMesh(CollisionCookInput input, std::stop_token cancellation = {});

    class KEIRE_API PhysicsBodyId final
    {
      public:
        constexpr PhysicsBodyId() noexcept = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const PhysicsBodyId&) const noexcept = default;

      private:
        friend class PhysicsWorld;
        explicit constexpr PhysicsBodyId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    struct PhysicsBodyDefinition
    {
        PhysicsMotionType Motion = PhysicsMotionType::Static;
        ColliderShape Shape = ColliderShape::Box;
        Vector3 Position;
        Quaternion Rotation;
        Vector3 LinearVelocity;
        Vector3 HalfExtent{0.5F, 0.5F, 0.5F};
        float Radius = 0.5F;
        float Height = 1.0F;
        float Mass = 1.0F;
        std::uint32_t Layer = 1;
        std::uint32_t Mask = ~0U;
        bool Trigger = false;
        bool Continuous = false;
        std::shared_ptr<const CookedCollisionMesh> Collision;
    };

    struct PhysicsBodyState
    {
        PhysicsBodyId Body;
        Vector3 Position;
        Quaternion Rotation;
        Vector3 LinearVelocity;
        bool Sleeping = false;
    };

    struct PhysicsRayQuery
    {
        Vector3 Origin;
        Vector3 Direction{0.0F, 0.0F, -1.0F};
        float MaximumDistance = 1000.0F;
        std::uint32_t Mask = ~0U;
        bool IncludeTriggers = true;
    };

    struct PhysicsQueryHit
    {
        PhysicsBodyId Body;
        Vector3 Position;
        Vector3 Normal;
        float Distance = 0.0F;
    };

    enum class ContactPhase : std::uint8_t
    {
        Enter,
        Stay,
        Exit
    };

    struct PhysicsContactEvent
    {
        PhysicsBodyId First;
        PhysicsBodyId Second;
        ContactPhase Phase = ContactPhase::Enter;
        bool Trigger = false;
    };

    class KEIRE_API PhysicsWorld final : public RefCounted
    {
      public:
        class Impl;
        ~PhysicsWorld() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] PhysicsBodyId CreateBody(const PhysicsBodyDefinition& definition);
        void DestroyBody(PhysicsBodyId body);
        void SetKinematicTarget(PhysicsBodyId body, Vector3 position, Quaternion rotation);
        [[nodiscard]] std::optional<PhysicsBodyState> TryGetBody(PhysicsBodyId body) const;
        [[nodiscard]] std::vector<PhysicsQueryHit> RayCast(const PhysicsRayQuery& query) const;
        [[nodiscard]] std::vector<PhysicsBodyId> OverlapSphere(Vector3 center, float radius,
                                                               std::uint32_t mask = ~0U) const;
        void Step(float deltaSeconds);
        [[nodiscard]] std::vector<PhysicsContactEvent> DrainContactEvents();
        void Close();

      private:
        friend class PhysicsSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit PhysicsWorld(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API PhysicsSystem final : public RefCounted
    {
      public:
        explicit PhysicsSystem(PhysicsSystemSpecification specification = {});
        ~PhysicsSystem() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] Ref<PhysicsWorld> CreateWorld();
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
