#include "Keire/Physics/PhysicsSystem.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr JPH::ObjectLayer NonMovingLayer = 0;
        constexpr JPH::ObjectLayer MovingLayer = 1;
        constexpr JPH::uint BroadPhaseLayerCount = 2;

        class JoltRuntimeLease final
        {
          public:
            JoltRuntimeLease()
            {
                std::scoped_lock lock(s_Mutex);
                if (s_References++ != 0)
                    return;
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }

            ~JoltRuntimeLease()
            {
                std::scoped_lock lock(s_Mutex);
                if (--s_References != 0)
                    return;
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }

            JoltRuntimeLease(const JoltRuntimeLease&) = delete;
            JoltRuntimeLease& operator=(const JoltRuntimeLease&) = delete;

          private:
            static inline std::mutex s_Mutex;
            static inline std::uint32_t s_References = 0;
        };

        struct PhysicsServiceState final
        {
            std::atomic<bool> Open{true};
            std::atomic<std::uint32_t> Worlds{0};
            std::uint32_t MaximumWorlds = 0;
            std::shared_ptr<JoltRuntimeLease> Runtime = std::make_shared<JoltRuntimeLease>();
        };

        struct Bounds final
        {
            Vector3 Minimum;
            Vector3 Maximum;
        };

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] Vector3 Multiply(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] float LengthSquared(const Vector3 value) noexcept { return Dot(value, value); }

        [[nodiscard]] Vector3 Extent(const PhysicsBodyDefinition& body) noexcept
        {
            switch (body.Shape)
            {
            case ColliderShape::Box:
                return body.HalfExtent;
            case ColliderShape::Sphere:
                return {body.Radius, body.Radius, body.Radius};
            case ColliderShape::Capsule:
                return {body.Radius, body.Height * 0.5F + body.Radius, body.Radius};
            case ColliderShape::ConvexMesh:
            case ColliderShape::TriangleMesh:
            {
                Vector3 result{};
                if (!body.Collision)
                    return result;
                for (const auto vertex : body.Collision->Vertices)
                {
                    result.X = std::max(result.X, std::abs(vertex.X));
                    result.Y = std::max(result.Y, std::abs(vertex.Y));
                    result.Z = std::max(result.Z, std::abs(vertex.Z));
                }
                return result;
            }
            }
            return {};
        }

        [[nodiscard]] Bounds BodyBounds(const PhysicsBodyDefinition& body) noexcept
        {
            const auto extent = Extent(body);
            return {Subtract(body.Position, extent), Add(body.Position, extent)};
        }

        [[nodiscard]] bool Intersects(const Bounds& left, const Bounds& right) noexcept
        {
            return left.Minimum.X <= right.Maximum.X && left.Maximum.X >= right.Minimum.X &&
                   left.Minimum.Y <= right.Maximum.Y && left.Maximum.Y >= right.Minimum.Y &&
                   left.Minimum.Z <= right.Maximum.Z && left.Maximum.Z >= right.Minimum.Z;
        }

        [[nodiscard]] bool ValidBody(const PhysicsBodyDefinition& body) noexcept
        {
            if (!Math::IsFinite(body.Position) || !Math::IsFinite(body.Rotation) ||
                !Math::IsFinite(body.LinearVelocity) || !Math::IsFinite(body.HalfExtent) ||
                !std::isfinite(body.Radius) || !std::isfinite(body.Height) || !std::isfinite(body.Mass) ||
                body.Layer == 0)
                return false;
            if (body.Shape == ColliderShape::Box &&
                (body.HalfExtent.X <= 0.0F || body.HalfExtent.Y <= 0.0F || body.HalfExtent.Z <= 0.0F))
                return false;
            if ((body.Shape == ColliderShape::Sphere || body.Shape == ColliderShape::Capsule) && body.Radius <= 0.0F)
                return false;
            if (body.Shape == ColliderShape::Capsule && body.Height < 0.0F)
                return false;
            if (body.Shape == ColliderShape::ConvexMesh)
                return body.Collision && body.Collision->Kind == CollisionMeshKind::Convex;
            if (body.Shape == ColliderShape::TriangleMesh)
                return body.Collision && body.Collision->Kind == CollisionMeshKind::Triangle;
            return true;
        }

        [[nodiscard]] JPH::RVec3 ToJolt(const Vector3 value) noexcept { return {value.X, value.Y, value.Z}; }

        [[nodiscard]] JPH::Quat ToJolt(const Quaternion value) noexcept { return {value.X, value.Y, value.Z, value.W}; }

        [[nodiscard]] Vector3 FromJoltPosition(const JPH::RVec3Arg value) noexcept
        {
            return {static_cast<float>(value.GetX()), static_cast<float>(value.GetY()),
                    static_cast<float>(value.GetZ())};
        }

        [[nodiscard]] Vector3 FromJoltVector(const JPH::Vec3Arg value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ()};
        }

        [[nodiscard]] Quaternion FromJoltRotation(const JPH::QuatArg value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
        {
          public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayerCount; }

            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
            }
        };

        class ObjectBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            [[nodiscard]] bool ShouldCollide(const JPH::ObjectLayer layer,
                                             const JPH::BroadPhaseLayer broadPhase) const override
            {
                return layer == MovingLayer || broadPhase == JPH::BroadPhaseLayer(MovingLayer);
            }
        };

        class ObjectLayerFilter final : public JPH::ObjectLayerPairFilter
        {
          public:
            [[nodiscard]] bool ShouldCollide(const JPH::ObjectLayer first, const JPH::ObjectLayer second) const override
            {
                return first == MovingLayer || second == MovingLayer;
            }
        };

        template <typename Value> void HashValue(std::uint64_t& hash, const Value value) noexcept
        {
            const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
            for (const auto byte : bytes)
            {
                hash ^= std::to_integer<std::uint8_t>(byte);
                hash *= 1099511628211ULL;
            }
        }

        [[nodiscard]] JPH::ShapeRefC CreateCollisionShape(const CookedCollisionMesh& collision)
        {
            if (collision.Kind == CollisionMeshKind::Convex)
            {
                JPH::Array<JPH::Vec3> points;
                points.reserve(collision.Vertices.size());
                for (const auto vertex : collision.Vertices)
                    points.emplace_back(vertex.X, vertex.Y, vertex.Z);
                JPH::ConvexHullShapeSettings settings(points);
                auto result = settings.Create();
                if (result.HasError())
                {
                    const auto error = result.GetError();
                    throw std::invalid_argument("Jolt rejected cooked convex collision: " +
                                                std::string(error.data(), error.size()));
                }
                return result.Get();
            }

            JPH::VertexList vertices;
            vertices.reserve(collision.Vertices.size());
            for (const auto vertex : collision.Vertices)
                vertices.emplace_back(vertex.X, vertex.Y, vertex.Z);
            JPH::IndexedTriangleList triangles;
            triangles.reserve(collision.Indices.size() / 3U);
            for (std::size_t index = 0; index < collision.Indices.size(); index += 3U)
                triangles.emplace_back(collision.Indices[index], collision.Indices[index + 1U],
                                       collision.Indices[index + 2U]);
            JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
            auto result = settings.Create();
            if (result.HasError())
            {
                const auto error = result.GetError();
                throw std::invalid_argument("Jolt rejected cooked triangle collision: " +
                                            std::string(error.data(), error.size()));
            }
            return result.Get();
        }
    } // namespace

    std::shared_ptr<const CookedCollisionMesh> CookCollisionMesh(CollisionCookInput input,
                                                                 const std::stop_token cancellation)
    {
        if (input.Vertices.size() < 3U || input.Vertices.size() > 16U * 1024U * 1024U ||
            input.Indices.size() > 48U * 1024U * 1024U)
            throw std::invalid_argument("Collision cook vertex or index count is invalid.");
        for (const auto vertex : input.Vertices)
        {
            if (cancellation.stop_requested())
                throw std::runtime_error("Collision cooking was cancelled.");
            if (!Math::IsFinite(vertex))
                throw std::invalid_argument("Collision cook contains a non-finite vertex.");
        }
        if (input.Kind == CollisionMeshKind::Convex)
        {
            if (!input.Indices.empty())
                throw std::invalid_argument("Convex collision cooking does not accept triangle indices.");
            std::ranges::sort(input.Vertices,
                              [](const Vector3 left, const Vector3 right)
                              {
                                  if (left.X != right.X)
                                      return left.X < right.X;
                                  if (left.Y != right.Y)
                                      return left.Y < right.Y;
                                  return left.Z < right.Z;
                              });
            input.Vertices.erase(std::unique(input.Vertices.begin(), input.Vertices.end(),
                                             [](const Vector3 left, const Vector3 right)
                                             { return left.X == right.X && left.Y == right.Y && left.Z == right.Z; }),
                                 input.Vertices.end());
            if (input.Vertices.size() < 4U)
                throw std::invalid_argument("Convex collision cooking requires four unique non-coplanar points.");
        }
        else
        {
            if (input.Indices.empty() || input.Indices.size() % 3U != 0)
                throw std::invalid_argument("Triangle collision cooking requires indexed triangles.");
            for (const auto index : input.Indices)
                if (index >= input.Vertices.size())
                    throw std::invalid_argument("Collision cook contains an out-of-range index.");
        }

        auto result = std::make_shared<CookedCollisionMesh>();
        result->Kind = input.Kind;
        result->Vertices = std::move(input.Vertices);
        result->Indices = std::move(input.Indices);
        std::uint64_t hash = 14695981039346656037ULL;
        HashValue(hash, static_cast<std::uint8_t>(result->Kind));
        for (const auto vertex : result->Vertices)
        {
            HashValue(hash, std::bit_cast<std::uint32_t>(vertex.X));
            HashValue(hash, std::bit_cast<std::uint32_t>(vertex.Y));
            HashValue(hash, std::bit_cast<std::uint32_t>(vertex.Z));
        }
        for (const auto index : result->Indices)
            HashValue(hash, index);
        result->ContentHash = hash;

        const auto runtime = std::make_shared<JoltRuntimeLease>();
        (void)CreateCollisionShape(*result);
        return result;
    }

    class PhysicsWorld::Impl final
    {
      public:
        struct Body final
        {
            PhysicsBodyDefinition Definition;
            JPH::BodyID Native;
        };

        explicit Impl(std::shared_ptr<PhysicsServiceState> service)
            : Service(std::move(service)), Owner(std::this_thread::get_id()),
              Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                   static_cast<int>(std::max(1U, std::thread::hardware_concurrency()) - 1U))
        {
            Native.Init(65'536, 0, 65'536, 10'240, BroadPhaseLayers, BroadPhaseFilter, LayerFilter);
        }

        ~Impl()
        {
            CloseNative();
            Service->Worlds.fetch_sub(1, std::memory_order_relaxed);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("PhysicsWorld::") + operation + " must run on the owner thread.");
            if (!Open || !Service->Open.load(std::memory_order_acquire))
                throw std::logic_error("PhysicsWorld is closed or its PhysicsSystem is unavailable.");
        }

        void CloseNative() noexcept
        {
            if (!std::exchange(NativeOpen, false))
                return;
            auto& bodyInterface = Native.GetBodyInterface();
            for (const auto& [value, body] : Bodies)
            {
                (void)value;
                bodyInterface.RemoveBody(body.Native);
                bodyInterface.DestroyBody(body.Native);
            }
            Bodies.clear();
        }

        std::shared_ptr<PhysicsServiceState> Service;
        std::thread::id Owner;
        bool Open = true;
        bool NativeOpen = true;
        std::uint64_t NextBody = 1;
        std::map<std::uint64_t, Body> Bodies;
        std::set<std::pair<std::uint64_t, std::uint64_t>> Contacts;
        std::vector<PhysicsContactEvent> Events;
        BroadPhaseLayerInterface BroadPhaseLayers;
        ObjectBroadPhaseFilter BroadPhaseFilter;
        ObjectLayerFilter LayerFilter;
        JPH::PhysicsSystem Native;
        JPH::TempAllocatorImpl Temporary{16U * 1024U * 1024U};
        JPH::JobSystemThreadPool Jobs;
    };

    PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    PhysicsWorld::~PhysicsWorld() = default;

    bool PhysicsWorld::IsOpen() const noexcept
    {
        return m_Impl->Open && m_Impl->Service->Open.load(std::memory_order_acquire);
    }

    PhysicsBodyId PhysicsWorld::CreateBody(const PhysicsBodyDefinition& definition)
    {
        m_Impl->RequireOwner("CreateBody");
        if (!ValidBody(definition) || (definition.Motion == PhysicsMotionType::Dynamic && definition.Mass <= 0.0F))
            throw std::invalid_argument("Physics body definition contains invalid or non-finite values.");
        if (definition.Motion == PhysicsMotionType::Dynamic && definition.Shape == ColliderShape::TriangleMesh)
            throw std::invalid_argument("Dynamic triangle-mesh collision is unsupported; use a convex collider.");

        JPH::ShapeRefC shape;
        switch (definition.Shape)
        {
        case ColliderShape::Box:
            shape =
                new JPH::BoxShape(JPH::Vec3(definition.HalfExtent.X, definition.HalfExtent.Y, definition.HalfExtent.Z));
            break;
        case ColliderShape::Sphere:
            shape = new JPH::SphereShape(definition.Radius);
            break;
        case ColliderShape::Capsule:
            shape = new JPH::CapsuleShape(definition.Height * 0.5F, definition.Radius);
            break;
        case ColliderShape::ConvexMesh:
        case ColliderShape::TriangleMesh:
            shape = CreateCollisionShape(*definition.Collision);
            break;
        }

        auto motion = JPH::EMotionType::Static;
        if (definition.Motion == PhysicsMotionType::Dynamic)
            motion = JPH::EMotionType::Dynamic;
        else if (definition.Motion == PhysicsMotionType::Kinematic || definition.Trigger)
            motion = JPH::EMotionType::Kinematic;
        const auto objectLayer = motion == JPH::EMotionType::Static ? NonMovingLayer : MovingLayer;
        const PhysicsBodyId id(m_Impl->NextBody++);
        JPH::BodyCreationSettings settings(shape, ToJolt(definition.Position),
                                           ToJolt(Math::Normalize(definition.Rotation)), motion, objectLayer);
        settings.mIsSensor = definition.Trigger;
        settings.mMotionQuality =
            definition.Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        settings.mUserData = id.m_Value;
        if (definition.Motion == PhysicsMotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = definition.Mass;
        }

        auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        const auto native = bodyInterface.CreateAndAddBody(settings, definition.Motion == PhysicsMotionType::Dynamic
                                                                         ? JPH::EActivation::Activate
                                                                         : JPH::EActivation::DontActivate);
        if (native.IsInvalid())
            throw std::runtime_error("Jolt body capacity was exhausted.");
        bodyInterface.SetLinearVelocity(
            native, JPH::Vec3(definition.LinearVelocity.X, definition.LinearVelocity.Y, definition.LinearVelocity.Z));
        try
        {
            m_Impl->Bodies.emplace(id.m_Value, Impl::Body{definition, native});
        }
        catch (...)
        {
            bodyInterface.RemoveBody(native);
            bodyInterface.DestroyBody(native);
            throw;
        }
        return id;
    }

    void PhysicsWorld::DestroyBody(const PhysicsBodyId body)
    {
        m_Impl->RequireOwner("DestroyBody");
        const auto found = m_Impl->Bodies.find(body.m_Value);
        if (!body || found == m_Impl->Bodies.end())
            throw std::invalid_argument("Physics body is unavailable.");
        auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        bodyInterface.RemoveBody(found->second.Native);
        bodyInterface.DestroyBody(found->second.Native);
        m_Impl->Bodies.erase(found);
        std::erase_if(m_Impl->Contacts,
                      [body](const auto& pair) { return pair.first == body.m_Value || pair.second == body.m_Value; });
    }

    void PhysicsWorld::SetKinematicTarget(const PhysicsBodyId body, const Vector3 position, const Quaternion rotation)
    {
        m_Impl->RequireOwner("SetKinematicTarget");
        const auto found = m_Impl->Bodies.find(body.m_Value);
        if (found == m_Impl->Bodies.end() || found->second.Definition.Motion != PhysicsMotionType::Kinematic ||
            !Math::IsFinite(position) || !Math::IsFinite(rotation))
            throw std::invalid_argument("Kinematic target is invalid or targets a non-kinematic body.");
        found->second.Definition.Position = position;
        found->second.Definition.Rotation = Math::Normalize(rotation);
        m_Impl->Native.GetBodyInterface().SetPositionAndRotation(found->second.Native, ToJolt(position),
                                                                 ToJolt(found->second.Definition.Rotation),
                                                                 JPH::EActivation::Activate);
    }

    std::optional<PhysicsBodyState> PhysicsWorld::TryGetBody(const PhysicsBodyId body) const
    {
        m_Impl->RequireOwner("TryGetBody");
        const auto found = m_Impl->Bodies.find(body.m_Value);
        if (found == m_Impl->Bodies.end())
            return std::nullopt;
        JPH::RVec3 position;
        JPH::Quat rotation;
        const auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        bodyInterface.GetPositionAndRotation(found->second.Native, position, rotation);
        return PhysicsBodyState{body, FromJoltPosition(position), FromJoltRotation(rotation),
                                FromJoltVector(bodyInterface.GetLinearVelocity(found->second.Native)),
                                !bodyInterface.IsActive(found->second.Native)};
    }

    std::vector<PhysicsQueryHit> PhysicsWorld::RayCast(const PhysicsRayQuery& query) const
    {
        m_Impl->RequireOwner("RayCast");
        if (!Math::IsFinite(query.Origin) || !Math::IsFinite(query.Direction) ||
            !std::isfinite(query.MaximumDistance) || query.MaximumDistance <= 0.0F ||
            LengthSquared(query.Direction) <= std::numeric_limits<float>::epsilon())
            throw std::invalid_argument("Physics ray query is invalid.");
        const auto direction = Multiply(query.Direction, 1.0F / std::sqrt(LengthSquared(query.Direction)));
        std::vector<PhysicsQueryHit> result;
        for (const auto& [value, body] : m_Impl->Bodies)
        {
            if ((body.Definition.Layer & query.Mask) == 0 || (body.Definition.Trigger && !query.IncludeTriggers))
                continue;
            const auto bounds = BodyBounds(body.Definition);
            float nearValue = 0.0F;
            float farValue = query.MaximumDistance;
            Vector3 normal{};
            bool hit = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto origin = axis == 0 ? query.Origin.X : axis == 1 ? query.Origin.Y : query.Origin.Z;
                const auto component = axis == 0 ? direction.X : axis == 1 ? direction.Y : direction.Z;
                const auto minimum = axis == 0 ? bounds.Minimum.X : axis == 1 ? bounds.Minimum.Y : bounds.Minimum.Z;
                const auto maximum = axis == 0 ? bounds.Maximum.X : axis == 1 ? bounds.Maximum.Y : bounds.Maximum.Z;
                if (std::abs(component) <= std::numeric_limits<float>::epsilon())
                {
                    if (origin < minimum || origin > maximum)
                        hit = false;
                    continue;
                }
                float first = (minimum - origin) / component;
                float second = (maximum - origin) / component;
                float sign = -1.0F;
                if (first > second)
                {
                    std::swap(first, second);
                    sign = 1.0F;
                }
                if (first > nearValue)
                {
                    nearValue = first;
                    normal = axis == 0   ? Vector3{sign, 0.0F, 0.0F}
                             : axis == 1 ? Vector3{0.0F, sign, 0.0F}
                                         : Vector3{0.0F, 0.0F, sign};
                }
                farValue = std::min(farValue, second);
                if (nearValue > farValue)
                    hit = false;
            }
            if (hit && nearValue <= query.MaximumDistance)
                result.push_back(
                    {PhysicsBodyId(value), Add(query.Origin, Multiply(direction, nearValue)), normal, nearValue});
        }
        std::ranges::sort(
            result, [](const PhysicsQueryHit& left, const PhysicsQueryHit& right)
            { return left.Distance < right.Distance || (left.Distance == right.Distance && left.Body < right.Body); });
        return result;
    }

    std::vector<PhysicsBodyId> PhysicsWorld::OverlapSphere(const Vector3 center, const float radius,
                                                           const std::uint32_t mask) const
    {
        m_Impl->RequireOwner("OverlapSphere");
        if (!Math::IsFinite(center) || !std::isfinite(radius) || radius <= 0.0F)
            throw std::invalid_argument("Physics sphere overlap is invalid.");
        const Bounds query{Subtract(center, {radius, radius, radius}), Add(center, {radius, radius, radius})};
        std::vector<PhysicsBodyId> result;
        for (const auto& [value, body] : m_Impl->Bodies)
            if ((body.Definition.Layer & mask) != 0 && Intersects(query, BodyBounds(body.Definition)))
                result.emplace_back(PhysicsBodyId(value));
        return result;
    }

    void PhysicsWorld::Step(const float deltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || deltaSeconds > 1.0F)
            throw std::invalid_argument("Physics step delta is invalid.");
        const auto collisionSteps = std::max(1, static_cast<int>(std::ceil(deltaSeconds * 60.0F)));
        const auto error = m_Impl->Native.Update(deltaSeconds, collisionSteps, &m_Impl->Temporary, &m_Impl->Jobs);
        if (error != JPH::EPhysicsUpdateError::None)
            throw std::runtime_error("Jolt physics update capacity was exhausted.");

        auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        for (auto& [value, body] : m_Impl->Bodies)
        {
            (void)value;
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(body.Native, position, rotation);
            body.Definition.Position = FromJoltPosition(position);
            body.Definition.Rotation = FromJoltRotation(rotation);
            body.Definition.LinearVelocity = FromJoltVector(bodyInterface.GetLinearVelocity(body.Native));
        }

        std::set<std::pair<std::uint64_t, std::uint64_t>> current;
        for (auto first = m_Impl->Bodies.begin(); first != m_Impl->Bodies.end(); ++first)
        {
            for (auto second = std::next(first); second != m_Impl->Bodies.end(); ++second)
            {
                if ((first->second.Definition.Layer & second->second.Definition.Mask) == 0 ||
                    (second->second.Definition.Layer & first->second.Definition.Mask) == 0 ||
                    !Intersects(BodyBounds(first->second.Definition), BodyBounds(second->second.Definition)))
                    continue;
                const auto pair = std::pair{first->first, second->first};
                current.insert(pair);
                m_Impl->Events.push_back({PhysicsBodyId(pair.first), PhysicsBodyId(pair.second),
                                          m_Impl->Contacts.contains(pair) ? ContactPhase::Stay : ContactPhase::Enter,
                                          first->second.Definition.Trigger || second->second.Definition.Trigger});
            }
        }
        for (const auto& pair : m_Impl->Contacts)
            if (!current.contains(pair))
                m_Impl->Events.push_back(
                    {PhysicsBodyId(pair.first), PhysicsBodyId(pair.second), ContactPhase::Exit, false});
        m_Impl->Contacts = std::move(current);
    }

    std::vector<PhysicsContactEvent> PhysicsWorld::DrainContactEvents()
    {
        m_Impl->RequireOwner("DrainContactEvents");
        return std::exchange(m_Impl->Events, {});
    }

    void PhysicsWorld::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsWorld::Close must run on the owner thread.");
        if (!std::exchange(m_Impl->Open, false))
            return;
        m_Impl->CloseNative();
        m_Impl->Contacts.clear();
        m_Impl->Events.clear();
    }

    class PhysicsSystem::Impl final
    {
      public:
        explicit Impl(const std::uint32_t maximumWorlds) : Owner(std::this_thread::get_id())
        {
            Service->MaximumWorlds = maximumWorlds;
        }

        std::thread::id Owner;
        std::shared_ptr<PhysicsServiceState> Service = std::make_shared<PhysicsServiceState>();
    };

    PhysicsSystem::PhysicsSystem(const PhysicsSystemSpecification specification)
        : m_Impl(std::make_unique<Impl>(specification.MaximumWorlds))
    {
        if (specification.Mode == PhysicsMode::Disabled || specification.MaximumWorlds == 0 ||
            specification.MaximumWorlds > 4096)
            throw std::invalid_argument("PhysicsSystem specification is invalid.");
    }

    PhysicsSystem::~PhysicsSystem() = default;
    bool PhysicsSystem::IsOpen() const noexcept { return m_Impl->Service->Open.load(std::memory_order_acquire); }

    Ref<PhysicsWorld> PhysicsSystem::CreateWorld()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsSystem::CreateWorld must run on the owner thread.");
        if (!IsOpen())
            throw std::logic_error("PhysicsSystem is closed.");
        const auto count = m_Impl->Service->Worlds.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count > m_Impl->Service->MaximumWorlds)
        {
            m_Impl->Service->Worlds.fetch_sub(1, std::memory_order_relaxed);
            throw std::runtime_error("PhysicsSystem world capacity was exhausted.");
        }
        try
        {
            return CreateRef<PhysicsWorld>(std::make_unique<PhysicsWorld::Impl>(m_Impl->Service));
        }
        catch (...)
        {
            m_Impl->Service->Worlds.fetch_sub(1, std::memory_order_relaxed);
            throw;
        }
    }

    void PhysicsSystem::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsSystem::Close must run on the owner thread.");
        m_Impl->Service->Open.store(false, std::memory_order_release);
    }
} // namespace Keire
