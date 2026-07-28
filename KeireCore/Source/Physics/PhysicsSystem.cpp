#include "Keire/Physics/PhysicsSystem.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
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
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <ranges>
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
            std::array<std::uint32_t, 32> CollisionMatrix;
            std::shared_ptr<JoltRuntimeLease> Runtime = std::make_shared<JoltRuntimeLease>();
        };

        void ValidateCollisionMatrix(const std::array<std::uint32_t, 32>& matrix)
        {
            for (std::size_t first = 0; first < matrix.size(); ++first)
            {
                for (std::size_t second = first + 1; second < matrix.size(); ++second)
                {
                    if (((matrix[first] >> second) & 1U) != ((matrix[second] >> first) & 1U))
                        throw std::invalid_argument("Physics collision matrix must be symmetric.");
                }
            }
        }

        [[nodiscard]] std::uint32_t EffectiveMask(const std::uint32_t layer, const std::uint32_t authoredMask,
                                                  const std::array<std::uint32_t, 32>& matrix) noexcept
        {
            return authoredMask & matrix[std::countr_zero(layer)];
        }

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

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const auto lengthSquared = LengthSquared(value);
            if (lengthSquared <= std::numeric_limits<float>::epsilon())
                return {};
            return Multiply(value, 1.0F / std::sqrt(lengthSquared));
        }

        [[nodiscard]] bool ValidBody(const PhysicsBodyDefinition& body) noexcept
        {
            if (!Math::IsFinite(body.Position) || !Math::IsFinite(body.Rotation) ||
                !Math::IsFinite(body.LinearVelocity) || !Math::IsFinite(body.HalfExtent) ||
                !std::isfinite(body.Radius) || !std::isfinite(body.Height) || !std::isfinite(body.Mass) ||
                !std::isfinite(body.Friction) || !std::isfinite(body.Restitution) || !std::has_single_bit(body.Layer) ||
                body.Friction < 0.0F || body.Friction > 100.0F || body.Restitution < 0.0F || body.Restitution > 1.0F ||
                body.FrictionCombine > PhysicsMaterialCombineMode::Maximum ||
                body.RestitutionCombine > PhysicsMaterialCombineMode::Maximum)
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

        [[nodiscard]] float CombineMaterialValues(const float first, const PhysicsMaterialCombineMode firstMode,
                                                  const float second,
                                                  const PhysicsMaterialCombineMode secondMode) noexcept
        {
            const auto mode = std::max(firstMode, secondMode);
            switch (mode)
            {
            case PhysicsMaterialCombineMode::Average:
                return (first + second) * 0.5F;
            case PhysicsMaterialCombineMode::Minimum:
                return std::min(first, second);
            case PhysicsMaterialCombineMode::Multiply:
                return first * second;
            case PhysicsMaterialCombineMode::Maximum:
                return std::max(first, second);
            }
            return (first + second) * 0.5F;
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

        class QueryBodyFilter final : public JPH::BodyFilter
        {
          public:
            QueryBodyFilter(const std::uint32_t mask, const std::uint32_t layer, const bool includeTriggers) noexcept
                : m_Mask(mask), m_Layer(layer), m_IncludeTriggers(includeTriggers)
            {
            }

            [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                const auto& group = body.GetCollisionGroup();
                return (group.GetGroupID() & m_Mask) != 0 && (group.GetSubGroupID() & m_Layer) != 0 &&
                       (m_IncludeTriggers || !body.IsSensor());
            }

          private:
            std::uint32_t m_Mask;
            std::uint32_t m_Layer;
            bool m_IncludeTriggers;
        };

        class SpecificBodyFilter final : public JPH::BodyFilter
        {
          public:
            explicit SpecificBodyFilter(const JPH::BodyID body) noexcept : m_Body(body) {}

            [[nodiscard]] bool ShouldCollide(const JPH::BodyID& body) const override { return body == m_Body; }

          private:
            JPH::BodyID m_Body;
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

        struct ContactData final
        {
            Vector3 Point;
            Vector3 Normal;
            float Impulse = 0.0F;
            float Penetration = 0.0F;
            bool Trigger = false;
        };

        using BodyPair = std::pair<std::uint64_t, std::uint64_t>;

        class WorldContactListener final : public JPH::ContactListener
        {
          public:
            explicit WorldContactListener(const Impl& owner) noexcept : m_Owner(owner) {}

            [[nodiscard]] JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second,
                                                                JPH::RVec3Arg, const JPH::CollideShapeResult&) override
            {
                const auto firstBody = m_Owner.Bodies.find(first.GetUserData());
                const auto secondBody = m_Owner.Bodies.find(second.GetUserData());
                if (firstBody == m_Owner.Bodies.end() || secondBody == m_Owner.Bodies.end())
                    return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                const auto& firstDefinition = firstBody->second.Definition;
                const auto& secondDefinition = secondBody->second.Definition;
                if ((firstDefinition.Layer & secondDefinition.Mask) == 0 ||
                    (secondDefinition.Layer & firstDefinition.Mask) == 0)
                {
                    return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                }
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                                JPH::ContactSettings& settings) override
            {
                ApplyMaterial(first, second, settings);
                Capture(first, second, manifold);
            }

            void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                                    const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override
            {
                ApplyMaterial(first, second, settings);
                Capture(first, second, manifold);
            }

            void Clear()
            {
                std::scoped_lock lock(m_Mutex);
                m_Samples.clear();
            }

            [[nodiscard]] std::map<BodyPair, ContactData> TakeSamples()
            {
                std::scoped_lock lock(m_Mutex);
                return std::exchange(m_Samples, {});
            }

          private:
            void ApplyMaterial(const JPH::Body& first, const JPH::Body& second, JPH::ContactSettings& settings) const
            {
                const auto firstBody = m_Owner.Bodies.find(first.GetUserData());
                const auto secondBody = m_Owner.Bodies.find(second.GetUserData());
                if (firstBody == m_Owner.Bodies.end() || secondBody == m_Owner.Bodies.end())
                    return;
                const auto& firstDefinition = firstBody->second.Definition;
                const auto& secondDefinition = secondBody->second.Definition;
                settings.mCombinedFriction =
                    CombineMaterialValues(firstDefinition.Friction, firstDefinition.FrictionCombine,
                                          secondDefinition.Friction, secondDefinition.FrictionCombine);
                settings.mCombinedRestitution =
                    CombineMaterialValues(firstDefinition.Restitution, firstDefinition.RestitutionCombine,
                                          secondDefinition.Restitution, secondDefinition.RestitutionCombine);
                settings.mIsSensor = firstDefinition.Trigger || secondDefinition.Trigger;
            }

            void Capture(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold)
            {
                auto firstValue = first.GetUserData();
                auto secondValue = second.GetUserData();
                if (firstValue == 0 || secondValue == 0)
                    return;

                ContactData sample;
                if (!manifold.mRelativeContactPointsOn1.empty() && !manifold.mRelativeContactPointsOn2.empty())
                {
                    sample.Point = Multiply(Add(FromJoltPosition(manifold.GetWorldSpaceContactPointOn1(0)),
                                                FromJoltPosition(manifold.GetWorldSpaceContactPointOn2(0))),
                                            0.5F);
                }
                sample.Normal = Normalize(FromJoltVector(manifold.mWorldSpaceNormal));
                sample.Penetration = manifold.mPenetrationDepth;
                sample.Trigger = first.IsSensor() || second.IsSensor();

                const auto relativeVelocity = FromJoltVector(second.GetLinearVelocity() - first.GetLinearVelocity());
                const auto closingSpeed = std::max(0.0F, -Dot(relativeVelocity, sample.Normal));
                const auto firstInverseMass =
                    first.IsDynamic() ? first.GetMotionProperties()->GetInverseMassUnchecked() : 0.0F;
                const auto secondInverseMass =
                    second.IsDynamic() ? second.GetMotionProperties()->GetInverseMassUnchecked() : 0.0F;
                const auto inverseMass = firstInverseMass + secondInverseMass;
                if (!sample.Trigger && inverseMass > std::numeric_limits<float>::epsilon())
                    sample.Impulse = closingSpeed / inverseMass;

                if (firstValue > secondValue)
                {
                    std::swap(firstValue, secondValue);
                    sample.Normal = Multiply(sample.Normal, -1.0F);
                }
                const BodyPair pair{firstValue, secondValue};
                std::scoped_lock lock(m_Mutex);
                const auto found = m_Samples.find(pair);
                if (found == m_Samples.end() || Prefer(sample, found->second))
                    m_Samples[pair] = sample;
            }

            [[nodiscard]] static bool Prefer(const ContactData& candidate, const ContactData& current) noexcept
            {
                if (candidate.Penetration != current.Penetration)
                    return candidate.Penetration > current.Penetration;
                if (candidate.Point.X != current.Point.X)
                    return candidate.Point.X < current.Point.X;
                if (candidate.Point.Y != current.Point.Y)
                    return candidate.Point.Y < current.Point.Y;
                return candidate.Point.Z < current.Point.Z;
            }

            const Impl& m_Owner;
            std::mutex m_Mutex;
            std::map<BodyPair, ContactData> m_Samples;
        };

        explicit Impl(std::shared_ptr<PhysicsServiceState> service)
            : Service(std::move(service)), Owner(std::this_thread::get_id()), ContactListener(*this),
              Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                   static_cast<int>(std::max(1U, std::thread::hardware_concurrency()) - 1U))
        {
            Native.Init(65'536, 0, 65'536, 10'240, BroadPhaseLayers, BroadPhaseFilter, LayerFilter);
            Native.SetContactListener(&ContactListener);
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
            ActiveContacts.clear();
            ContactListener.Clear();
            QueryTraces.clear();
        }

        [[nodiscard]] std::optional<ContactData> QueryContact(const Body& first, const Body& second)
        {
            const auto& bodyInterface = Native.GetBodyInterface();
            const auto shape = bodyInterface.GetShape(first.Native);
            JPH::CollideShapeSettings settings;
            settings.mMaxSeparationDistance = 0.001F;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            const SpecificBodyFilter bodyFilter(second.Native);
            Native.GetNarrowPhaseQuery().CollideShape(shape.GetPtr(), JPH::Vec3::sOne(),
                                                      bodyInterface.GetCenterOfMassTransform(first.Native), settings,
                                                      JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);
            if (!collector.HadHit())
                return std::nullopt;

            const auto best = std::ranges::max_element(
                collector.mHits, [](const JPH::CollideShapeResult& left, const JPH::CollideShapeResult& right)
                { return left.mPenetrationDepth < right.mPenetrationDepth; });
            ContactData result;
            result.Point =
                Multiply(Add(FromJoltVector(best->mContactPointOn1), FromJoltVector(best->mContactPointOn2)), 0.5F);
            result.Normal = Normalize(Multiply(FromJoltVector(best->mPenetrationAxis), -1.0F));
            result.Penetration = best->mPenetrationDepth;
            result.Trigger = first.Definition.Trigger || second.Definition.Trigger;
            if (!result.Trigger)
            {
                const auto relativeVelocity =
                    Subtract(second.Definition.LinearVelocity, first.Definition.LinearVelocity);
                const auto closingSpeed = std::max(0.0F, -Dot(relativeVelocity, result.Normal));
                const auto firstInverseMass =
                    first.Definition.Motion == PhysicsMotionType::Dynamic ? 1.0F / first.Definition.Mass : 0.0F;
                const auto secondInverseMass =
                    second.Definition.Motion == PhysicsMotionType::Dynamic ? 1.0F / second.Definition.Mass : 0.0F;
                if (firstInverseMass + secondInverseMass > std::numeric_limits<float>::epsilon())
                    result.Impulse = closingSpeed / (firstInverseMass + secondInverseMass);
            }
            return result;
        }

        [[nodiscard]] std::vector<std::uint64_t> QueryPotentialContacts(const std::uint64_t value, const Body& body)
        {
            const auto& bodyInterface = Native.GetBodyInterface();
            const auto shape = bodyInterface.GetShape(body.Native);
            JPH::CollideShapeSettings settings;
            settings.mMaxSeparationDistance = 0.001F;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            const QueryBodyFilter bodyFilter(body.Definition.Mask, body.Definition.Layer, true);
            Native.GetNarrowPhaseQuery().CollideShape(shape.GetPtr(), JPH::Vec3::sOne(),
                                                      bodyInterface.GetCenterOfMassTransform(body.Native), settings,
                                                      JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

            std::set<std::uint64_t> unique;
            for (const auto& hit : collector.mHits)
            {
                JPH::BodyLockRead lock(Native.GetBodyLockInterface(), hit.mBodyID2);
                if (lock.Succeeded() && lock.GetBody().GetUserData() != value)
                    unique.insert(lock.GetBody().GetUserData());
            }
            return {unique.begin(), unique.end()};
        }

        void RecordRayQuery(const PhysicsRayQuery& query, const std::vector<PhysicsQueryHit>& hits) const
        {
            if (!DebugCapture.Enabled)
                return;
            PhysicsDebugQueryTrace trace;
            trace.Sequence = NextQuerySequence++;
            trace.Kind = PhysicsDebugQueryKind::RayCast;
            trace.Ray = query;
            const auto count = std::min<std::size_t>(hits.size(), DebugCapture.MaximumHitsPerQuery);
            trace.Hits.assign(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(count));
            trace.DroppedHits = hits.size() - count;
            PushQueryTrace(std::move(trace));
        }

        void RecordOverlapQuery(const Vector3 center, const float radius, const std::uint32_t mask,
                                const std::uint32_t layer, const std::vector<PhysicsBodyId>& overlaps) const
        {
            if (!DebugCapture.Enabled)
                return;
            PhysicsDebugQueryTrace trace;
            trace.Sequence = NextQuerySequence++;
            trace.Kind = PhysicsDebugQueryKind::SphereOverlap;
            trace.SphereCenter = center;
            trace.SphereRadius = radius;
            trace.Mask = mask;
            trace.Layer = layer;
            const auto count = std::min<std::size_t>(overlaps.size(), DebugCapture.MaximumHitsPerQuery);
            trace.Overlaps.assign(overlaps.begin(), overlaps.begin() + static_cast<std::ptrdiff_t>(count));
            trace.DroppedHits = overlaps.size() - count;
            PushQueryTrace(std::move(trace));
        }

        void PushQueryTrace(PhysicsDebugQueryTrace trace) const
        {
            if (QueryTraces.size() == DebugCapture.MaximumQueryTraces)
            {
                QueryTraces.pop_front();
                ++DroppedQueryTraces;
            }
            QueryTraces.push_back(std::move(trace));
        }

        std::shared_ptr<PhysicsServiceState> Service;
        std::thread::id Owner;
        bool Open = true;
        bool NativeOpen = true;
        std::uint64_t NextBody = 1;
        std::map<std::uint64_t, Body> Bodies;
        std::map<BodyPair, ContactData> ActiveContacts;
        std::vector<PhysicsContactEvent> Events;
        PhysicsDebugCaptureConfiguration DebugCapture;
        mutable std::deque<PhysicsDebugQueryTrace> QueryTraces;
        mutable std::uint64_t NextQuerySequence = 1;
        mutable std::size_t DroppedQueryTraces = 0;
        std::uint64_t Revision = 0;
        WorldContactListener ContactListener;
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
        auto runtimeDefinition = definition;
        runtimeDefinition.Mask = EffectiveMask(definition.Layer, definition.Mask, m_Impl->Service->CollisionMatrix);

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
        settings.mCollisionGroup = JPH::CollisionGroup(nullptr, runtimeDefinition.Layer, runtimeDefinition.Mask);
        settings.mFriction = definition.Friction;
        settings.mRestitution = definition.Restitution;
        settings.mGravityFactor = definition.UseGravity ? 1.0F : 0.0F;
        if (definition.Motion == PhysicsMotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = definition.Mass;
        }

        auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        const auto native = bodyInterface.CreateAndAddBody(
            settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (native.IsInvalid())
            throw std::runtime_error("Jolt body capacity was exhausted.");
        bodyInterface.SetLinearVelocity(
            native, JPH::Vec3(definition.LinearVelocity.X, definition.LinearVelocity.Y, definition.LinearVelocity.Z));
        try
        {
            m_Impl->Bodies.emplace(id.m_Value, Impl::Body{std::move(runtimeDefinition), native});
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
        std::erase_if(m_Impl->ActiveContacts, [body](const auto& entry)
                      { return entry.first.first == body.m_Value || entry.first.second == body.m_Value; });
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

    void PhysicsWorld::SetGravityEnabled(const PhysicsBodyId body, const bool enabled)
    {
        m_Impl->RequireOwner("SetGravityEnabled");
        const auto found = m_Impl->Bodies.find(body.m_Value);
        if (!body || found == m_Impl->Bodies.end())
            throw std::invalid_argument("Physics body is unavailable.");
        found->second.Definition.UseGravity = enabled;
        if (found->second.Definition.Motion == PhysicsMotionType::Dynamic)
        {
            auto& bodyInterface = m_Impl->Native.GetBodyInterface();
            bodyInterface.SetGravityFactor(found->second.Native, enabled ? 1.0F : 0.0F);
            if (enabled)
                bodyInterface.ActivateBody(found->second.Native);
        }
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

        const JPH::RRayCast ray(ToJolt(query.Origin),
                                JPH::Vec3(direction.X, direction.Y, direction.Z) * query.MaximumDistance);
        JPH::RayCastSettings settings;
        JPH::ClosestHitPerBodyCollisionCollector<JPH::CastRayCollector> collector;
        if (!std::has_single_bit(query.Layer))
            throw std::invalid_argument("Physics ray query layer must select one collision layer.");
        const QueryBodyFilter bodyFilter(query.Mask, query.Layer, query.IncludeTriggers);
        m_Impl->Native.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, {}, bodyFilter);
        collector.Sort();

        std::vector<PhysicsQueryHit> result;
        result.reserve(collector.mHits.size());
        for (const auto& hit : collector.mHits)
        {
            JPH::BodyLockRead lock(m_Impl->Native.GetBodyLockInterface(), hit.mBodyID);
            if (!lock.Succeeded())
                continue;
            const auto& nativeBody = lock.GetBody();
            const auto position = ray.GetPointOnRay(hit.mFraction);
            result.push_back({PhysicsBodyId(nativeBody.GetUserData()), FromJoltPosition(position),
                              FromJoltVector(nativeBody.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, position)),
                              hit.mFraction * query.MaximumDistance});
        }
        std::ranges::sort(
            result, [](const PhysicsQueryHit& left, const PhysicsQueryHit& right)
            { return left.Distance < right.Distance || (left.Distance == right.Distance && left.Body < right.Body); });
        m_Impl->RecordRayQuery(query, result);
        return result;
    }

    std::vector<PhysicsBodyId> PhysicsWorld::OverlapSphere(const Vector3 center, const float radius,
                                                           const std::uint32_t mask, const std::uint32_t layer) const
    {
        m_Impl->RequireOwner("OverlapSphere");
        if (!Math::IsFinite(center) || !std::isfinite(radius) || radius <= 0.0F || !std::has_single_bit(layer))
            throw std::invalid_argument("Physics sphere overlap is invalid.");

        const JPH::SphereShape queryShape(radius);
        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const QueryBodyFilter bodyFilter(mask, layer, true);
        m_Impl->Native.GetNarrowPhaseQuery().CollideShape(&queryShape, JPH::Vec3::sOne(),
                                                          JPH::RMat44::sTranslation(ToJolt(center)), settings,
                                                          JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

        std::set<PhysicsBodyId> unique;
        for (const auto& hit : collector.mHits)
        {
            JPH::BodyLockRead lock(m_Impl->Native.GetBodyLockInterface(), hit.mBodyID2);
            if (lock.Succeeded())
                unique.emplace(PhysicsBodyId(lock.GetBody().GetUserData()));
        }
        std::vector<PhysicsBodyId> result;
        result.assign(unique.begin(), unique.end());
        m_Impl->RecordOverlapQuery(center, radius, mask, layer, result);
        return result;
    }

    void PhysicsWorld::Step(const float deltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || deltaSeconds > 1.0F)
            throw std::invalid_argument("Physics step delta is invalid.");
        if (m_Impl->Bodies.empty())
        {
            m_Impl->ActiveContacts.clear();
            m_Impl->Events.clear();
            return;
        }
        m_Impl->ContactListener.Clear();
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

        const auto samples = m_Impl->ContactListener.TakeSamples();
        std::set<Impl::BodyPair> candidates;
        for (const auto& [pair, data] : samples)
        {
            (void)data;
            candidates.insert(pair);
        }
        for (const auto& [pair, data] : m_Impl->ActiveContacts)
        {
            (void)data;
            candidates.insert(pair);
        }
        for (const auto& [value, body] : m_Impl->Bodies)
        {
            if (!body.Definition.Trigger)
                continue;
            for (const auto other : m_Impl->QueryPotentialContacts(value, body))
                candidates.emplace(std::min(value, other), std::max(value, other));
        }

        std::map<Impl::BodyPair, Impl::ContactData> current;
        for (const auto& pair : candidates)
        {
            const auto first = m_Impl->Bodies.find(pair.first);
            const auto second = m_Impl->Bodies.find(pair.second);
            if (first == m_Impl->Bodies.end() || second == m_Impl->Bodies.end() ||
                (first->second.Definition.Layer & second->second.Definition.Mask) == 0 ||
                (second->second.Definition.Layer & first->second.Definition.Mask) == 0)
                continue;
            auto queried = m_Impl->QueryContact(first->second, second->second);
            if (!queried)
                continue;
            auto data = *queried;
            if (const auto sample = samples.find(pair); sample != samples.end())
                data = sample->second;
            current.emplace(pair, data);
            m_Impl->Events.push_back({PhysicsBodyId(pair.first), PhysicsBodyId(pair.second),
                                      m_Impl->ActiveContacts.contains(pair) ? ContactPhase::Stay : ContactPhase::Enter,
                                      data.Trigger, data.Point, data.Normal, data.Impulse});
        }
        for (const auto& [pair, data] : m_Impl->ActiveContacts)
        {
            if (!current.contains(pair))
            {
                m_Impl->Events.push_back({PhysicsBodyId(pair.first), PhysicsBodyId(pair.second), ContactPhase::Exit,
                                          data.Trigger, data.Point, data.Normal, data.Impulse});
            }
        }
        m_Impl->ActiveContacts = std::move(current);
        ++m_Impl->Revision;
    }

    std::vector<PhysicsContactEvent> PhysicsWorld::DrainContactEvents()
    {
        m_Impl->RequireOwner("DrainContactEvents");
        return std::exchange(m_Impl->Events, {});
    }

    void PhysicsWorld::ConfigureDebugCapture(const PhysicsDebugCaptureConfiguration configuration)
    {
        m_Impl->RequireOwner("ConfigureDebugCapture");
        if (configuration.Enabled &&
            (configuration.MaximumBodies == 0 || configuration.MaximumBodies > 65'536 ||
             configuration.MaximumContacts == 0 || configuration.MaximumContacts > 262'144 ||
             configuration.MaximumQueryTraces == 0 || configuration.MaximumQueryTraces > 4096 ||
             configuration.MaximumHitsPerQuery == 0 || configuration.MaximumHitsPerQuery > 65'536))
        {
            throw std::invalid_argument("Physics debug capture configuration exceeds its bounded limits.");
        }
        m_Impl->DebugCapture = configuration;
        m_Impl->QueryTraces.clear();
        m_Impl->NextQuerySequence = 1;
        m_Impl->DroppedQueryTraces = 0;
    }

    std::shared_ptr<const PhysicsDebugSnapshot> PhysicsWorld::CaptureDebugSnapshot() const
    {
        m_Impl->RequireOwner("CaptureDebugSnapshot");
        if (!m_Impl->DebugCapture.Enabled)
            return {};

        auto result = std::make_shared<PhysicsDebugSnapshot>();
        result->Revision = m_Impl->Revision;
        result->DroppedBodies = m_Impl->Bodies.size() > m_Impl->DebugCapture.MaximumBodies
                                    ? m_Impl->Bodies.size() - m_Impl->DebugCapture.MaximumBodies
                                    : 0;
        result->Bodies.reserve(std::min<std::size_t>(m_Impl->Bodies.size(), m_Impl->DebugCapture.MaximumBodies));
        const auto& bodyInterface = m_Impl->Native.GetBodyInterface();
        for (const auto& [value, body] : m_Impl->Bodies)
        {
            if (result->Bodies.size() == m_Impl->DebugCapture.MaximumBodies)
                break;
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(body.Native, position, rotation);
            result->Bodies.push_back({PhysicsBodyId(value), body.Definition.Motion, body.Definition.Shape,
                                      FromJoltPosition(position), FromJoltRotation(rotation),
                                      body.Definition.HalfExtent, body.Definition.Radius, body.Definition.Height,
                                      body.Definition.Layer, body.Definition.Mask, body.Definition.Trigger,
                                      !bodyInterface.IsActive(body.Native), body.Definition.UseGravity});
        }

        result->DroppedContacts = m_Impl->ActiveContacts.size() > m_Impl->DebugCapture.MaximumContacts
                                      ? m_Impl->ActiveContacts.size() - m_Impl->DebugCapture.MaximumContacts
                                      : 0;
        result->Contacts.reserve(
            std::min<std::size_t>(m_Impl->ActiveContacts.size(), m_Impl->DebugCapture.MaximumContacts));
        for (const auto& [pair, data] : m_Impl->ActiveContacts)
        {
            if (result->Contacts.size() == m_Impl->DebugCapture.MaximumContacts)
                break;
            result->Contacts.push_back({PhysicsBodyId(pair.first), PhysicsBodyId(pair.second), ContactPhase::Stay,
                                        data.Trigger, data.Point, data.Normal, data.Impulse});
        }

        result->Queries.assign(m_Impl->QueryTraces.begin(), m_Impl->QueryTraces.end());
        result->DroppedQueries = m_Impl->DroppedQueryTraces;
        return result;
    }

    void PhysicsWorld::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsWorld::Close must run on the owner thread.");
        if (!std::exchange(m_Impl->Open, false))
            return;
        m_Impl->CloseNative();
        m_Impl->ActiveContacts.clear();
        m_Impl->Events.clear();
    }

    class PhysicsSystem::Impl final
    {
      public:
        Impl(const std::uint32_t maximumWorlds, std::array<std::uint32_t, 32> collisionMatrix)
            : Owner(std::this_thread::get_id())
        {
            Service->MaximumWorlds = maximumWorlds;
            Service->CollisionMatrix = std::move(collisionMatrix);
        }

        std::thread::id Owner;
        std::shared_ptr<PhysicsServiceState> Service = std::make_shared<PhysicsServiceState>();
    };

    PhysicsSystem::PhysicsSystem(const PhysicsSystemSpecification specification)
        : m_Impl(std::make_unique<Impl>(specification.MaximumWorlds, specification.CollisionMatrix))
    {
        ValidateCollisionMatrix(specification.CollisionMatrix);
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

    void PhysicsSystem::ConfigureCollisionMatrix(std::array<std::uint32_t, 32> matrix)
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsSystem::ConfigureCollisionMatrix must run on the owner thread.");
        if (!IsOpen())
            throw std::logic_error("PhysicsSystem is closed.");
        if (m_Impl->Service->Worlds.load(std::memory_order_acquire) != 0)
            throw std::logic_error("Physics collision matrix cannot change while a physics world is active.");
        ValidateCollisionMatrix(matrix);
        m_Impl->Service->CollisionMatrix = std::move(matrix);
    }

    std::array<std::uint32_t, 32> PhysicsSystem::CollisionMatrix() const
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsSystem::CollisionMatrix must run on the owner thread.");
        return m_Impl->Service->CollisionMatrix;
    }

    void PhysicsSystem::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("PhysicsSystem::Close must run on the owner thread.");
        m_Impl->Service->Open.store(false, std::memory_order_release);
    }
} // namespace Keire
