#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace Keire
{
    class JobSystem;

    enum class PhysicsMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    struct PhysicsSystemSpecification
    {
        PhysicsMode Mode = PhysicsMode::Disabled;
        std::uint32_t MaximumWorlds = 64;
        std::array<std::uint32_t, 32> CollisionMatrix = []
        {
            std::array<std::uint32_t, 32> result;
            result.fill(~0U);
            return result;
        }();
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

    enum class PhysicsMaterialCombineMode : std::uint8_t
    {
        Average,
        Minimum,
        Multiply,
        Maximum
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
        bool UseGravity = true;
        float Friction = 0.5F;
        float Restitution = 0.0F;
        PhysicsMaterialCombineMode FrictionCombine = PhysicsMaterialCombineMode::Average;
        PhysicsMaterialCombineMode RestitutionCombine = PhysicsMaterialCombineMode::Average;
    };

    struct PhysicsBodyState
    {
        PhysicsBodyId Body;
        Vector3 Position;
        Quaternion Rotation;
        Vector3 LinearVelocity;
        Vector3 AngularVelocity;
        bool Sleeping = false;
    };

    struct PhysicsRayQuery
    {
        Vector3 Origin;
        Vector3 Direction{0.0F, 0.0F, -1.0F};
        float MaximumDistance = 1000.0F;
        std::uint32_t Mask = ~0U;
        bool IncludeTriggers = true;
        std::uint32_t Layer = 1;
    };

    struct PhysicsCapsuleCastQuery
    {
        Vector3 Origin;
        Quaternion Rotation;
        float Radius = 0.5F;
        float Height = 1.0F;
        Vector3 Displacement;
        std::uint32_t Mask = ~0U;
        bool IncludeTriggers = false;
        std::uint32_t Layer = 1;
        PhysicsBodyId IgnoreBody;
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
        Vector3 Point;
        Vector3 Normal;
        float Impulse = 0.0F;
    };

    enum class PhysicsDebugQueryKind : std::uint8_t
    {
        RayCast,
        SphereOverlap,
        CapsuleCast
    };

    struct PhysicsDebugBody
    {
        PhysicsBodyId Body;
        PhysicsMotionType Motion = PhysicsMotionType::Static;
        ColliderShape Shape = ColliderShape::Box;
        Vector3 Position;
        Quaternion Rotation;
        Vector3 HalfExtent;
        float Radius = 0.0F;
        float Height = 0.0F;
        std::uint32_t Layer = 0;
        std::uint32_t Mask = 0;
        bool Trigger = false;
        bool Sleeping = false;
        bool UseGravity = true;
    };

    struct PhysicsDebugQueryTrace
    {
        std::uint64_t Sequence = 0;
        PhysicsDebugQueryKind Kind = PhysicsDebugQueryKind::RayCast;
        PhysicsRayQuery Ray;
        PhysicsCapsuleCastQuery Capsule;
        Vector3 SphereCenter;
        float SphereRadius = 0.0F;
        std::uint32_t Mask = ~0U;
        std::uint32_t Layer = 1;
        std::vector<PhysicsQueryHit> Hits;
        std::vector<PhysicsBodyId> Overlaps;
        std::size_t DroppedHits = 0;
    };

    struct PhysicsDebugSnapshot
    {
        std::uint64_t Revision = 0;
        std::vector<PhysicsDebugBody> Bodies;
        std::vector<PhysicsContactEvent> Contacts;
        std::vector<PhysicsDebugQueryTrace> Queries;
        std::size_t DroppedBodies = 0;
        std::size_t DroppedContacts = 0;
        std::size_t DroppedQueries = 0;
    };

    struct PhysicsDebugCaptureConfiguration
    {
        bool Enabled = false;
        std::uint32_t MaximumBodies = 4096;
        std::uint32_t MaximumContacts = 4096;
        std::uint32_t MaximumQueryTraces = 128;
        std::uint32_t MaximumHitsPerQuery = 256;
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
        void SetBodyState(PhysicsBodyId body, const PhysicsBodyState& state);
        void SetGravityEnabled(PhysicsBodyId body, bool enabled);
        [[nodiscard]] std::optional<PhysicsBodyState> TryGetBody(PhysicsBodyId body) const;
        [[nodiscard]] std::vector<PhysicsQueryHit> RayCast(const PhysicsRayQuery& query) const;
        [[nodiscard]] std::optional<PhysicsQueryHit> CastCapsule(const PhysicsCapsuleCastQuery& query) const;
        [[nodiscard]] std::vector<PhysicsBodyId> OverlapSphere(Vector3 center, float radius, std::uint32_t mask = ~0U,
                                                               std::uint32_t layer = 1) const;
        void Step(float deltaSeconds);
        [[nodiscard]] std::vector<PhysicsContactEvent> DrainContactEvents();
        void ConfigureDebugCapture(PhysicsDebugCaptureConfiguration configuration);
        [[nodiscard]] std::shared_ptr<const PhysicsDebugSnapshot> CaptureDebugSnapshot() const;
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
        explicit PhysicsSystem(PhysicsSystemSpecification specification = {}, Ref<JobSystem> jobs = {});
        ~PhysicsSystem() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] Ref<PhysicsWorld> CreateWorld();
        void ConfigureCollisionMatrix(std::array<std::uint32_t, 32> matrix);
        [[nodiscard]] std::array<std::uint32_t, 32> CollisionMatrix() const;
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
